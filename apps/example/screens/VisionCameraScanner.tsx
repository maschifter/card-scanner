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

  // Card history tracking
  const [lastScannedCardId, setLastScannedCardId] = useState<string | null>(
    null,
  );
  const [consecutiveDetections, setConsecutiveDetections] = useState<number>(0);
  const [scannedCardsHistory, setScannedCardsHistory] = useState<
    DetectedCard[]
  >([]);

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
      const yoloAsset = Asset.fromModule(
        require('../assets/yolo11n-seg-cls.pte'),
      );
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

      // 2. Load set symbol detection models (MTG)
      console.log('📦 Loading set symbol detection models...');
      const setSymbolYoloAsset = Asset.fromModule(
        require('../assets/mtg/set_symbol_detection.pte'),
      );
      await setSymbolYoloAsset.downloadAsync();
      if (!setSymbolYoloAsset.localUri) {
        throw new Error('Failed to load set symbol YOLO model');
      }
      const setSymbolYoloPath = `${cacheDirectory}set_symbol_detection.pte`;
      await copyAsync({
        from: setSymbolYoloAsset.localUri,
        to: setSymbolYoloPath,
      });
      console.log('✅ Set symbol YOLO loaded');

      const setSymbolEmbedderAsset = Asset.fromModule(
        require('../assets/mtg/set_symbol_embedder.pte'),
      );
      await setSymbolEmbedderAsset.downloadAsync();
      if (!setSymbolEmbedderAsset.localUri) {
        throw new Error('Failed to load set symbol embedder');
      }
      const setSymbolEmbedderPath = `${cacheDirectory}set_symbol_embedder.pte`;
      await copyAsync({
        from: setSymbolEmbedderAsset.localUri,
        to: setSymbolEmbedderPath,
      });
      console.log('✅ Set symbol embedder loaded');

      // Note: Set symbol database is loaded at app start via loadAllDatabases()
      // It's accessible via DatabaseManager at 'set-symbols'

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
        captureImage: true,
        gameSpecificConfig: {
          mtg: {
            setSymbolDetection: {
              detectionModelPath: setSymbolYoloPath,
              embeddingModelPath: setSymbolEmbedderPath,
              detectionThreshold: 0.3,
              confidenceThreshold: 0.6,
            },
          },
        },
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
    (
      rawResult: any,
      width: number,
      height: number,
      lastCardId: string | null,
      currentCount: number,
    ) => {
      // Use createDetectionResult to transform raw → rich types
      const detection = createDetectionResult(rawResult);

      setFrameSize({ width, height });

      // Update UI with first card's info
      if (detection.success && detection.cards.length > 0) {
        const firstCard = detection.cards[0];

        // Check if this is a new card (different from last scanned)
        if (firstCard.cardId !== lastCardId) {
          // Different card - reset counter and start tracking
          setLastScannedCardId(firstCard.cardId);
          setConsecutiveDetections(1); // First detection of this card

          // Don't add to history yet - wait for confirmation (2nd detection)
          setDetection(null); // Hide UI until confirmed
        } else {
          // Same card as before - increment counter
          const newCount = currentCount + 1;
          setConsecutiveDetections(newCount);

          if (newCount === 2) {
            // Second consecutive detection - now confirm and add to history!
            setScannedCardsHistory((prev) => {
              const newHistory = [firstCard, ...prev];
              return newHistory.slice(0, 10); // Keep only last 10
            });

            setCardWithConfidence(
              `${firstCard.name} [${firstCard.gameName}] (${(firstCard.confidenceScore * 100).toFixed(1)}%)`,
            );

            // Set cropped image if available
            console.log(firstCard.capturedImage);
            if (firstCard.capturedImage) {
              setCroppedImagePath(firstCard.capturedImage.uri);
            }

            // Show detection UI now that it's confirmed
            setDetection(detection);

            // Hide detection overlay after 1 second
            setTimeout(() => {
              setDetection(null);
            }, 1000);
          } else if (newCount > 2) {
            // Already confirmed - UI already hidden by timeout
            // Do nothing
          }
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
            lastScannedCardId,
            consecutiveDetections,
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
    [
      isScanning,
      processDetectionCallback,
      lastScannedCardId,
      consecutiveDetections,
    ],
  );

  const toggleScanning = () => {
    setIsScanning(!isScanning);
    if (isScanning) {
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
      />

      {/* Bounding box overlay */}
      {detection && detection.cards.length > 0 && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {detection.cards.map((card, index) => {
              const box = card.boundingBox;

              // Use uniform X scaling (works on iOS)
              const scale = screenWidth / frameSize.width;

              const x = box.x1 * scale;
              const y = box.y1 * scale;
              const width = (box.x2 - box.x1) * scale;
              const height = (box.y2 - box.y1) * scale;

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
              top: 50,
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
              {/* MTG Set Symbol */}
              {detection?.cards[0]?.setSymbol && (
                <>
                  ⚡ Set: {detection.cards[0].setSymbol.setCode.toUpperCase()} -{' '}
                  {detection.cards[0].setSymbol.setName}
                  {'\n'}
                  {'  '}Variant: {detection.cards[0].setSymbol.variant}
                  {'\n'}
                  {'  '}Match:{' '}
                  {(detection.cards[0].setSymbol.similarity * 100).toFixed(1)}%
                  {'\n'}
                </>
              )}
              {/* YOLO game predictions */}
              {detection?.cards[0]?.predictedGame && (
                <>
                  🎮 YOLO: {detection.cards[0].predictedGame.toUpperCase()}
                  {'\n'}
                  {detection.cards[0].topGamePredictions
                    ?.map(
                      (pred, idx) =>
                        `  ${idx + 1}. ${pred.game}: ${(pred.confidence * 100).toFixed(1)}%\n`,
                    )
                    .join('')}
                  {'\n'}
                </>
              )}
              {/* Timing breakdown */}
              {detection?.timings && (
                <>
                  ⏱️ Total: {detection.processingTime.toFixed(1)}ms{'\n'}
                  {/* {'  '}Extract:{' '}
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
                  {'\n'}
                  {detection.timings.setSymbolDetection &&
                    detection.timings.setSymbolDetection > 0 && (
                      <>
                        {'  '}SetSym:{' '}
                        {(detection.timings.setSymbolDetection ?? 0).toFixed(1)}
                        ms
                        {'\n'}
                      </>
                    )}
                  {'\n'}
                </> */}
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
            {/* Set Symbol Image */}
            {detection?.cards[0]?.setSymbol?.croppedImagePath && (
              <View style={{ marginTop: 10 }}>
                <Text style={{ color: 'white', fontSize: 10, marginBottom: 4 }}>
                  Set Symbol:
                </Text>
                <Image
                  source={{
                    uri: detection.cards[0].setSymbol.croppedImagePath,
                  }}
                  style={{
                    width: 60,
                    height: 60,
                    borderRadius: 4,
                    backgroundColor: 'rgba(255,255,255,0.1)',
                  }}
                  resizeMode="contain"
                />
              </View>
            )}
          </View>
        </View>
      )}

      {/* Card History */}
      {scannedCardsHistory.length > 0 && (
        <View style={styles.historyContainer}>
          <Text style={styles.historyTitle}>
            📜 Scanned Cards ({scannedCardsHistory.length})
          </Text>
          {scannedCardsHistory.map((card, index) => (
            <View key={`${card.cardId}-${index}`} style={styles.historyItem}>
              <Text style={styles.historyCardName} numberOfLines={1}>
                {card.name}
              </Text>
              <Text style={styles.historyCardInfo}>
                {card.gameName.toUpperCase()} •{' '}
                {(card.confidenceScore * 100).toFixed(0)}%
                {card.setSymbol && ` • ${card.setSymbol.setCode.toUpperCase()}`}
              </Text>
            </View>
          ))}
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
  historyContainer: {
    position: 'absolute',
    top: 100,
    right: 10,
    maxWidth: 200,
    backgroundColor: 'rgba(0,0,0,0.8)',
    borderRadius: 8,
    padding: 10,
    maxHeight: 300,
  },
  historyTitle: {
    color: '#4CAF50',
    fontSize: 12,
    fontWeight: 'bold',
    marginBottom: 8,
  },
  historyItem: {
    borderBottomWidth: 1,
    borderBottomColor: 'rgba(255,255,255,0.1)',
    paddingVertical: 6,
  },
  historyCardName: {
    color: '#fff',
    fontSize: 11,
    fontWeight: '600',
  },
  historyCardInfo: {
    color: 'rgba(255,255,255,0.6)',
    fontSize: 9,
    marginTop: 2,
  },
});
