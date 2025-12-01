import React, { useState, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  ActivityIndicator,
  Dimensions,
  Image,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import {
  Camera,
  runAtTargetFps,
  useCameraDevice,
  useCameraFormat,
  useCameraPermission,
  useFrameProcessor,
} from 'react-native-vision-camera';
import {
  startScanning,
  initializeScanner,
  releaseScanner,
  createDetectionResult,
  switchGame,
  getSupportedGames,
  type Detection,
  type DetectedCard,
  type Game,
} from 'react-native-card-scanner';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import Svg, { Rect } from 'react-native-svg';
import { useRunOnJS } from 'react-native-worklets-core';

const screenWidth = Dimensions.get('window').width;
const screenHeight = Dimensions.get('window').height;

export default function VisionCameraScanner() {
  const insets = useSafeAreaInsets();
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');
  const format = useCameraFormat(device, [
    { videoResolution: { width: 1080, height: 1920 } },
  ]);
  const [isScanning, setIsScanning] = useState(false);
  const [isLoadingModels, setIsLoadingModels] = useState(true);
  const [modelError, setModelError] = useState<string | null>(null);
  const [modelPath, setModelPath] = useState<string | null>(null);
  const [embeddingModelPath, setEmbeddingModelPath] = useState<string | null>(
    null,
  );
  const [availableGames, setAvailableGames] = useState<Game[]>([]);
  const [currentGame, setCurrentGame] = useState<string>('lorcana');
  // Use Detection type from createDetectionResult
  const [detection, setDetection] = useState<Detection | null>(null);
  // Coordinates are from rotated frame (portrait 1080x1920)
  const [frameSize, setFrameSize] = useState({ width: 1080, height: 1920 });
  const [cardWithConfidence, setCardWithConfidence] = useState<string | null>(
    null,
  );
  const [croppedImagePath, setCroppedImagePath] = useState<string | null>(null);
  const [timingStats, setTimingStats] = useState<any>(null);
  const [cameraLayout, setCameraLayout] = useState({ width: 0, height: 0 });

  // Load models on mount
  useEffect(() => {
    loadModels();

    return () => {
      releaseScanner();
    };
  }, []);

  const loadModels = async () => {
    try {
      setIsLoadingModels(true);

      // 1. Load ML models
      console.log('📦 Loading ML models...');
      const yoloAsset = Asset.fromModule(require('../assets/yolo11n-seg.pte'));
      await yoloAsset.downloadAsync();

      if (!yoloAsset.localUri) {
        throw new Error('Failed to load YOLO model');
      }

      const yoloLocalPath = `${cacheDirectory}yolo11n-seg.pte`;
      await copyAsync({
        from: yoloAsset.localUri,
        to: yoloLocalPath,
      });
      setModelPath(yoloLocalPath);
      console.log('✅ YOLO model loaded');

      const embeddingAsset = Asset.fromModule(
        require('../assets/embedding_model.pte'),
      );
      await embeddingAsset.downloadAsync();

      if (!embeddingAsset.localUri) {
        throw new Error('Failed to load embedding model');
      }

      const embeddingLocalPath = `${cacheDirectory}embedding_model.pte`;
      await copyAsync({
        from: embeddingAsset.localUri,
        to: embeddingLocalPath,
      });
      setEmbeddingModelPath(embeddingLocalPath);
      console.log('✅ Embedding model loaded');

      // 3. Initialize scanner with configuration
      console.log('🚀 Initializing scanner...');
      const result = await initializeScanner({
        segmentationModelPath: yoloLocalPath,
        embeddingModelPath: embeddingLocalPath,
        gameName: currentGame,
        scanMode: 'single',
        segmentationThreshold: 0.7,
        iouThreshold: 0.7,
        confidenceThreshold: 0.6,
        maxMatches: 5,
        searchCandidates: 100,
        captureImage: false, // Enable image capture
      });

      if (!result.success) {
        throw new Error(result.error || 'Failed to initialize scanner');
      }

      console.log('✅ Scanner initialized successfully');

      // Load available games after initialization
      const games = await getSupportedGames();
      console.log('🎮 Available games:', games);
      setAvailableGames(games);

      // Set first game as current if available
      if (games.length > 0) {
        setCurrentGame(games[0].name);
      }

      setIsLoadingModels(false);
    } catch (error) {
      console.error('❌ Failed to initialize:', error);
      setModelError(error instanceof Error ? error.message : String(error));
      setIsLoadingModels(false);
    }
  };

  // Process raw scan result and transform to rich Detection type
  const processDetectionCallback = useRunOnJS(
    (rawResult: any, width: number, height: number) => {
      // Use createDetectionResult to transform raw → rich types
      const detection = createDetectionResult(rawResult);

      setDetection(detection);
      setFrameSize({ width, height });

      // Update UI with first card's info
      if (detection.success && detection.cards.length > 0) {
        const firstCard = detection.cards[0];
        setCardWithConfidence(
          `${firstCard.cardId} (${(firstCard.confidenceScore * 100).toFixed(1)}%)`,
        );

        // Set cropped image if available
        if (firstCard.capturedImage) {
          setCroppedImagePath(firstCard.capturedImage);
        }
      }
    },
    [],
  );

  // Frame processor (runs on separate thread)
  const frameProcessor = useFrameProcessor(
    (frame) => {
      'worklet';
      runAtTargetFps(5, () => {
        if (!isScanning || !modelPath) {
          return;
        }

        // Call startScanning (returns RawScanResult - worklet safe)
        const rawResult = startScanning(frame);

        // Pass raw result to JS thread for transformation using createDetectionResult
        if (rawResult.cardCount > 0) {
          processDetectionCallback(
            rawResult,
            rawResult.frameWidth,
            rawResult.frameHeight,
          );
        }

        // Log detailed timing breakdown
        const yoloTotal =
          (rawResult.yoloPreprocessMs ?? 0) +
          (rawResult.yoloInferenceMs ?? 0) +
          (rawResult.yoloPostprocessMs ?? 0);
        const embeddingTotal =
          (rawResult.embeddingPreprocessMs ?? 0) +
          (rawResult.embeddingInferenceMs ?? 0);

        console.log(
          `📊 Frame: ${rawResult.frameWidth}x${rawResult.frameHeight} | Total: ${(rawResult.processingTime ?? 0).toFixed(2)}ms\n` +
            `  Extract: ${(rawResult.frameExtractionMs ?? 0).toFixed(2)}ms\n` +
            `  YOLO: ${yoloTotal.toFixed(2)}ms (pre:${(rawResult.yoloPreprocessMs ?? 0).toFixed(1)} + inf:${(rawResult.yoloInferenceMs ?? 0).toFixed(1)} + post:${(rawResult.yoloPostprocessMs ?? 0).toFixed(1)})\n` +
            `  Embedding: ${embeddingTotal.toFixed(2)}ms (pre:${(rawResult.embeddingPreprocessMs ?? 0).toFixed(1)} + inf:${(rawResult.embeddingInferenceMs ?? 0).toFixed(1)})\n` +
            `  DB Search: ${(rawResult.dbSearchMs ?? 0).toFixed(2)}ms | Cards: ${rawResult.cardCount}`,
        );
      });
    },
    [isScanning, processDetectionCallback],
  );

  const toggleScanning = () => {
    setIsScanning(!isScanning);
    if (isScanning) {
    }
  };

  const handleGameSwitch = (gameName: string) => {
    // Stop scanning during switch
    const wasScanning = isScanning;
    if (wasScanning) {
      setIsScanning(false);
    }

    // Switch game
    console.log(`🎮 Switching to ${gameName}...`);
    switchGame(gameName);
    setCurrentGame(gameName);

    // Clear previous detection
    setDetection(null);
    setCardWithConfidence(null);
    setCroppedImagePath(null);

    // Resume scanning if it was active
    if (wasScanning) {
      setTimeout(() => setIsScanning(true), 100);
    }
  };

  if (!hasPermission) {
    return (
      <View style={styles.container}>
        <Text style={styles.message}>Camera permission is required</Text>
        <TouchableOpacity style={styles.button} onPress={requestPermission}>
          <Text style={styles.buttonText}>Grant Permission</Text>
        </TouchableOpacity>
      </View>
    );
  }

  if (isLoadingModels) {
    return (
      <View style={styles.container}>
        <ActivityIndicator size="large" color="#4CAF50" />
        <Text style={styles.message}>Loading models...</Text>
      </View>
    );
  }

  if (modelError) {
    return (
      <View style={styles.container}>
        <Text style={styles.errorText}>Failed to load models:</Text>
        <Text style={styles.errorText}>{modelError}</Text>
      </View>
    );
  }

  if (!device) {
    return (
      <View style={styles.container}>
        <Text style={styles.errorText}>No camera device found</Text>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <Camera
        style={StyleSheet.absoluteFill}
        device={device}
        format={format}
        isActive={true}
        frameProcessor={frameProcessor}
        pixelFormat="rgb"
        onLayout={(event) => {
          const { width, height } = event.nativeEvent.layout;
          setCameraLayout({ width, height });
        }}
      />

      {/* Bounding box overlay */}
      {detection && detection.cards.length > 0 && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {detection.cards.map((card, index) => {
              const box = card.boundingBox;

              // Use actual measured camera layout dimensions
              const cameraWidth = cameraLayout.width || screenWidth;
              const cameraHeight = cameraLayout.height || screenHeight;

              // Calculate camera preview dimensions with proper aspect ratio
              const frameAspectRatio = frameSize.width / frameSize.height;
              const cameraAspectRatio = cameraWidth / cameraHeight;

              let previewWidth, previewHeight, offsetX, offsetY;

              if (cameraAspectRatio > frameAspectRatio) {
                // Camera view is wider - pillarboxed (black bars on sides)
                previewHeight = cameraHeight;
                previewWidth = cameraHeight * frameAspectRatio;
                offsetX = (cameraWidth - previewWidth) / 2;
                offsetY = 0;
              } else {
                // Camera view is taller - letterboxed (black bars on top/bottom)
                previewWidth = cameraWidth;
                previewHeight = cameraWidth / frameAspectRatio;
                offsetX = 0;
                offsetY = (cameraHeight - previewHeight) / 2;
              }

              const scaleX = previewWidth / frameSize.width;
              const scaleY = previewHeight / frameSize.height;

              const x = box.x1 * scaleX + offsetX;
              const y = box.y1 * scaleY + offsetY;
              const width = (box.x2 - box.x1) * scaleX;
              const height = (box.y2 - box.y1) * scaleY;

              return (
                <Rect
                  key={index}
                  x={x}
                  y={y}
                  width={width}
                  height={height}
                  stroke="#00ff00"
                  strokeWidth="4"
                  fill="none"
                />
              );
            })}
          </Svg>
          {/* Debug info */}
          <View
            style={{
              position: 'absolute',
              top: 100,
              left: 20,
              backgroundColor: 'rgba(0,0,0,0.7)',
              padding: 10,
              maxWidth: 200,
            }}
          >
            <Text style={{ color: 'white', fontSize: 11, lineHeight: 16 }}>
              {/* Card info with confidence */}
              {cardWithConfidence && (
                <>
                  🎴 {cardWithConfidence}
                  {'\n\n'}
                </>
              )}
              {/* Timing breakdown */}
              {detection?.timings && (
                <>
                  ⏱️ Total: {detection.processingTime.toFixed(1)}ms{'\n'}
                  {'  '}Extract:{' '}
                  {(detection.timings.frameExtraction ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'  '}YOLO:{' '}
                  {(
                    (detection.timings.yoloPreprocess ?? 0) +
                    (detection.timings.yoloInference ?? 0) +
                    (detection.timings.yoloPostprocess ?? 0)
                  ).toFixed(1)}
                  ms{'\n'}
                  {'    '}Pre:{' '}
                  {(detection.timings.yoloPreprocess ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'    '}Inf:{' '}
                  {(detection.timings.yoloInference ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'    '}Post:{' '}
                  {(detection.timings.yoloPostprocess ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'  '}Emb:{' '}
                  {(
                    (detection.timings.embeddingPreprocess ?? 0) +
                    (detection.timings.embeddingInference ?? 0)
                  ).toFixed(1)}
                  ms{'\n'}
                  {'    '}Pre:{' '}
                  {(detection.timings.embeddingPreprocess ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'    '}Inf:{' '}
                  {(detection.timings.embeddingInference ?? 0).toFixed(1)}ms
                  {'\n'}
                  {'  '}DB: {(detection.timings.dbSearch ?? 0).toFixed(1)}ms
                  {'\n\n'}
                </>
              )}
              Detections: {detection?.cards.length ?? 0}
            </Text>
            {croppedImagePath && (
              <Image
                source={{ uri: croppedImagePath }}
                style={{
                  width: 150,
                  height: 200,
                  marginTop: 10,
                  borderRadius: 4,
                }}
                resizeMode="contain"
              />
            )}
          </View>
        </View>
      )}

      {/* Game Switcher */}
      {availableGames.length > 0 && (
        <View style={[styles.gameSwitcher, { top: 10 }]}>
          <Text style={styles.gameSwitcherLabel}>
            Game: {currentGame.toUpperCase()}
          </Text>
          <View style={styles.gameButtons}>
            {availableGames.map((game) => (
              <TouchableOpacity
                key={game.name}
                style={[
                  styles.gameButton,
                  currentGame === game.name && styles.gameButtonActive,
                ]}
                onPress={() => handleGameSwitch(game.name)}
              >
                <Text
                  style={[
                    styles.gameButtonText,
                    currentGame === game.name && styles.gameButtonTextActive,
                  ]}
                >
                  {game.name.toUpperCase()}
                </Text>
              </TouchableOpacity>
            ))}
          </View>
        </View>
      )}

      {/* Controls */}
      <View style={[styles.controls, { paddingBottom: insets.bottom + 20 }]}>
        <TouchableOpacity
          style={[styles.scanButton, isScanning && styles.scanButtonActive]}
          onPress={toggleScanning}
        >
          <Text style={styles.scanButtonText}>
            {isScanning ? 'Stop Scanning' : 'Start Scanning'}
          </Text>
        </TouchableOpacity>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#000',
    overflow: 'hidden',
  },
  message: {
    textAlign: 'center',
    paddingBottom: 10,
    color: '#fff',
    fontSize: 16,
  },
  camera: {
    flex: 1,
  },
  statsOverlay: {
    position: 'absolute',
    top: 60,
    left: 20,
    right: 20,
    backgroundColor: 'rgba(0,0,0,0.7)',
    paddingHorizontal: 16,
    paddingVertical: 8,
    borderRadius: 4,
  },
  statsText: {
    color: '#4CAF50',
    fontSize: 14,
    fontWeight: 'bold',
  },
  controls: {
    position: 'absolute',
    bottom: 0,
    left: 0,
    right: 0,
    alignItems: 'center',
  },
  scanButton: {
    backgroundColor: '#4CAF50',
    paddingHorizontal: 32,
    paddingVertical: 16,
    borderRadius: 8,
  },
  scanButtonActive: {
    backgroundColor: '#F44336',
  },
  scanButtonText: {
    color: '#fff',
    fontSize: 18,
    fontWeight: 'bold',
  },
  button: {
    backgroundColor: '#2196F3',
    padding: 16,
    borderRadius: 8,
    margin: 20,
  },
  buttonText: {
    color: '#fff',
    textAlign: 'center',
    fontSize: 16,
    fontWeight: 'bold',
  },
  errorText: {
    color: '#F44336',
    textAlign: 'center',
    padding: 20,
    fontSize: 16,
  },
  gameSwitcher: {
    position: 'absolute',
    left: 0,
    right: 0,
    paddingHorizontal: 10,
    paddingVertical: 8,
    backgroundColor: 'rgba(0,0,0,0.7)',
    zIndex: 100,
  },
  gameSwitcherLabel: {
    color: '#fff',
    fontSize: 12,
    fontWeight: 'bold',
    marginBottom: 4,
    textAlign: 'center',
  },
  gameButtons: {
    flexDirection: 'row',
    flexWrap: 'wrap',
    justifyContent: 'center',
    gap: 6,
  },
  gameButton: {
    backgroundColor: 'rgba(255,255,255,0.2)',
    paddingHorizontal: 10,
    paddingVertical: 6,
    borderRadius: 4,
    borderWidth: 1,
    borderColor: 'rgba(255,255,255,0.3)',
  },
  gameButtonActive: {
    backgroundColor: '#4CAF50',
    borderColor: '#4CAF50',
  },
  gameButtonText: {
    color: 'rgba(255,255,255,0.7)',
    fontSize: 10,
    fontWeight: '600',
  },
  gameButtonTextActive: {
    color: '#fff',
  },
});
