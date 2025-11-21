import React, { useState, useEffect, useCallback } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  ActivityIndicator,
  Dimensions,
  Image,
  Platform,
  StatusBar,
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
import { startScanning, type VCDetection } from 'react-native-card-scanner';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import Svg, { Rect } from 'react-native-svg';
import { useRunOnJS } from 'react-native-worklets-core';
import { useSharedValue } from 'react-native-reanimated';

const screenWidth = Dimensions.get('window').width;
const screenHeight = Dimensions.get('window').height;

// Game name for card database lookup
const GAME_NAME = 'lorcana';

export default function VisionCameraScanner() {
  const insets = useSafeAreaInsets();
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');
  const format = useCameraFormat(device, [
    { videoResolution: { width: 1920, height: 1080 } },
  ]);
  const [isScanning, setIsScanning] = useState(false);
  const [isLoadingModels, setIsLoadingModels] = useState(true);
  const [modelError, setModelError] = useState<string | null>(null);
  const [modelPath, setModelPath] = useState<string | null>(null);
  const [embeddingModelPath, setEmbeddingModelPath] = useState<string | null>(
    null,
  );
  const [detections, setDetections] = useState<VCDetection[]>([]);
  // Coordinates are from rotated frame (portrait 1080x1920)
  const [frameSize, setFrameSize] = useState({ width: 1080, height: 1920 });
  const [recognizedCards, setRecognizedCards] = useState<string[]>([]);
  const [cardWithConfidence, setCardWithConfidence] = useState<string | null>(
    null,
  );
  const [croppedImagePath, setCroppedImagePath] = useState<string | null>(null);
  const [timingStats, setTimingStats] = useState<any>(null);
  const lastProcessedTime = useSharedValue(0);

  // Load models on mount
  useEffect(() => {
    loadModels();
  }, []);

  const loadModels = async () => {
    try {
      setIsLoadingModels(true);

      // Load the YOLO segmentation model
      const yoloAsset = Asset.fromModule(require('../assets/yolo11n-seg.pte'));
      await yoloAsset.downloadAsync();

      if (!yoloAsset.localUri) {
        throw new Error('Failed to load YOLO model');
      }

      // Copy YOLO model to cache directory
      const yoloLocalPath = `${cacheDirectory}yolo11n-seg.pte`;
      await copyAsync({
        from: yoloAsset.localUri,
        to: yoloLocalPath,
      });

      setModelPath(yoloLocalPath);
      console.log('✅ YOLO model loaded:', yoloLocalPath);

      // Load the embedding model for card recognition
      try {
        const embeddingAsset = Asset.fromModule(
          require('../assets/embedding_model.pte'),
        );
        await embeddingAsset.downloadAsync();

        if (embeddingAsset.localUri) {
          const embeddingLocalPath = `${cacheDirectory}embedding_model.pte`;
          await copyAsync({
            from: embeddingAsset.localUri,
            to: embeddingLocalPath,
          });
          setEmbeddingModelPath(embeddingLocalPath);
          console.log('✅ Embedding model loaded:', embeddingLocalPath);
        }
      } catch (embErr) {
        console.warn(
          '⚠️ Embedding model not found, card recognition will be disabled:',
          embErr,
        );
      }

      setIsLoadingModels(false);
    } catch (error) {
      console.error('Failed to load models:', error);
      setModelError(error instanceof Error ? error.message : String(error));
      setIsLoadingModels(false);
    }
  };

  const updateDetectionsCallback = useRunOnJS(
    (
      count: number,
      x1s: number[],
      y1s: number[],
      x2s: number[],
      y2s: number[],
      confs: number[],
      cardNames: string[],
      croppedPaths: string[],
      width: number,
      height: number,
    ) => {
      const dets: VCDetection[] = [];
      const names: string[] = [];

      for (let i = 0; i < count; i++) {
        dets.push({
          box: {
            x1: x1s[i],
            y1: y1s[i],
            x2: x2s[i],
            y2: y2s[i],
            conf: confs[i],
          },
        });

        // Log recognized card names
        if (cardNames[i]) {
          names.push(cardNames[i]);
        }
      }

      setDetections(dets);
      setRecognizedCards(names);
      setFrameSize({ width, height });

      // Set first cropped image path for display
      if (croppedPaths.length > 0 && croppedPaths[0]) {
        setCroppedImagePath(croppedPaths[0]);
      }
    },
    [],
  );

  const updateCardWithConfidenceCallback = useRunOnJS((cardInfo: string) => {
    setCardWithConfidence(cardInfo);
  }, []);

  const updateTimingStatsCallback = useRunOnJS((stats: any) => {
    setTimingStats(stats);
  }, []);

  // Frame processor (runs on separate thread)
  const frameProcessor = useFrameProcessor(
    (frame) => {
      'worklet';
      runAtTargetFps(5, () => {
        if (!isScanning || !modelPath) {
          return;
        }

        // Call startScanning with optional embedding model and game name for recognition
        const result = startScanning(
          frame,
          modelPath,
          embeddingModelPath ?? undefined,
          embeddingModelPath ? GAME_NAME : undefined,
        );

        // Extract detection data into separate arrays
        if (result.cardCount > 0) {
          const x1s: number[] = [];
          const y1s: number[] = [];
          const x2s: number[] = [];
          const y2s: number[] = [];
          const confs: number[] = [];
          const cardNames: string[] = [];

          const croppedPaths: string[] = [];

          for (let i = 0; i < result.detections.length; i++) {
            const det = result.detections[i];
            x1s.push(det.box.x1);
            y1s.push(det.box.y1);
            x2s.push(det.box.x2);
            y2s.push(det.box.y2);
            confs.push(det.box.conf);

            // Get card name from top match if available
            const topMatch = det.matches?.[0];
            cardNames.push(topMatch?.name ?? '');

            // Get cropped image path
            croppedPaths.push(det.croppedImagePath ?? '');
          }

          // Set card with confidence for first detection
          if (result.detections.length > 0) {
            const firstMatch = result.detections[0].matches?.[0];
            if (firstMatch) {
              updateCardWithConfidenceCallback(
                `${firstMatch.name} (${(firstMatch.score * 100).toFixed(1)}%)`,
              );
            }
          }

          // Set timing stats
          updateTimingStatsCallback({
            total: result.totalMs,
            frameExtraction: result.frameExtractionMs,
            yoloPreprocess: result.yoloPreprocessMs,
            yoloInference: result.yoloInferenceMs,
            yoloPostprocess: result.yoloPostprocessMs,
            embeddingPreprocess: result.embeddingPreprocessMs,
            embeddingInference: result.embeddingInferenceMs,
            dbSearch: result.dbSearchMs,
          });

          updateDetectionsCallback(
            result.cardCount,
            x1s,
            y1s,
            x2s,
            y2s,
            confs,
            cardNames,
            croppedPaths,
            result.frameWidth,
            result.frameHeight,
          );
        } else {
          updateDetectionsCallback(
            0,
            [],
            [],
            [],
            [],
            [],
            [],
            [],
            result.frameWidth,
            result.frameHeight,
          );
        }

        // Log detailed timing breakdown
        const yoloTotal =
          (result.yoloPreprocessMs ?? 0) +
          (result.yoloInferenceMs ?? 0) +
          (result.yoloPostprocessMs ?? 0);
        const embeddingTotal =
          (result.embeddingPreprocessMs ?? 0) +
          (result.embeddingInferenceMs ?? 0);

        console.log(
          `📊 Frame: ${result.frameWidth}x${result.frameHeight} | Total: ${result.totalMs.toFixed(2)}ms\n` +
            `  Extract: ${result.frameExtractionMs.toFixed(2)}ms\n` +
            `  YOLO: ${yoloTotal.toFixed(2)}ms (pre:${(result.yoloPreprocessMs ?? 0).toFixed(1)} + inf:${(result.yoloInferenceMs ?? 0).toFixed(1)} + post:${(result.yoloPostprocessMs ?? 0).toFixed(1)})\n` +
            `  Embedding: ${embeddingTotal.toFixed(2)}ms (pre:${(result.embeddingPreprocessMs ?? 0).toFixed(1)} + inf:${(result.embeddingInferenceMs ?? 0).toFixed(1)})\n` +
            `  DB Search: ${(result.dbSearchMs ?? 0).toFixed(2)}ms | Cards: ${result.cardCount}`,
        );

        // Log frame properties (first time only)
        if (result.debug) {
          console.log(`🔍 ${result.debug}`);
        }
      });
    },
    [isScanning, modelPath, embeddingModelPath, updateDetectionsCallback],
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
        pixelFormat={Platform.OS === 'ios' ? 'rgb' : 'yuv'}
      />

      {/* Bounding box overlay */}
      {detections.length > 0 && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {detections.map((detection, index) => {
              const box = detection.box;

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
              {timingStats && (
                <>
                  ⏱️ Total: {timingStats.total?.toFixed(1)}ms{'\n'}
                  {'  '}Extract: {timingStats.frameExtraction?.toFixed(1)}ms
                  {'\n'}
                  {'  '}YOLO:{' '}
                  {(
                    (timingStats.yoloPreprocess ?? 0) +
                    (timingStats.yoloInference ?? 0) +
                    (timingStats.yoloPostprocess ?? 0)
                  ).toFixed(1)}
                  ms{'\n'}
                  {'    '}Pre: {timingStats.yoloPreprocess?.toFixed(1)}ms{'\n'}
                  {'    '}Inf: {timingStats.yoloInference?.toFixed(1)}ms{'\n'}
                  {'    '}Post: {timingStats.yoloPostprocess?.toFixed(1)}ms
                  {'\n'}
                  {'  '}Emb:{' '}
                  {(
                    (timingStats.embeddingPreprocess ?? 0) +
                    (timingStats.embeddingInference ?? 0)
                  ).toFixed(1)}
                  ms{'\n'}
                  {'    '}Pre: {timingStats.embeddingPreprocess?.toFixed(1)}ms
                  {'\n'}
                  {'    '}Inf: {timingStats.embeddingInference?.toFixed(1)}ms
                  {'\n'}
                  {'  '}DB: {timingStats.dbSearch?.toFixed(1)}ms{'\n\n'}
                </>
              )}
              Detections: {detections.length}
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
});
