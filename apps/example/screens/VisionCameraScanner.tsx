import React, { useState, useEffect, useCallback } from 'react';
import { View, Text, StyleSheet, TouchableOpacity, ActivityIndicator, Dimensions } from 'react-native';
import { Camera, useCameraDevice, useCameraPermission, useFrameProcessor } from 'react-native-vision-camera';
import { scanFaces, type VCDetection } from 'react-native-card-scanner';
import { Asset } from 'expo-asset';
import { cacheDirectory, copyAsync } from 'expo-file-system/legacy';
import Svg, { Rect } from 'react-native-svg';
import { useRunOnJS } from 'react-native-worklets-core';
import { useSharedValue } from 'react-native-reanimated';

const screenWidth = Dimensions.get('window').width;
const screenHeight = Dimensions.get('window').height;

export default function VisionCameraScanner() {
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');
  const [isScanning, setIsScanning] = useState(false);
  const [isLoadingModels, setIsLoadingModels] = useState(true);
  const [modelError, setModelError] = useState<string | null>(null);
  const [modelPath, setModelPath] = useState<string | null>(null);
  const [detections, setDetections] = useState<VCDetection[]>([]);
  const [frameSize, setFrameSize] = useState({ width: 1920, height: 1080 });
  const lastProcessedTime = useSharedValue(0);

  // Load YOLO model on mount
  useEffect(() => {
    loadModel();
  }, []);

  const loadModel = async () => {
    try {
      setIsLoadingModels(true);

      // Load the YOLO segmentation model
      const yoloAsset = Asset.fromModule(require('../assets/yolo11n-seg.pte'));
      await yoloAsset.downloadAsync();

      if (!yoloAsset.localUri) {
        throw new Error('Failed to load YOLO model');
      }

      // Copy to cache directory
      const localPath = `${cacheDirectory}yolo11n-seg.pte`;
      await copyAsync({
        from: yoloAsset.localUri,
        to: localPath,
      });

      setModelPath(localPath);
      console.log('✅ YOLO model loaded:', localPath);
      setIsLoadingModels(false);
    } catch (error) {
      console.error('Failed to load model:', error);
      setModelError(error instanceof Error ? error.message : String(error));
      setIsLoadingModels(false);
    }
  };

  const updateDetectionsCallback = useRunOnJS((count: number, x1s: number[], y1s: number[], x2s: number[], y2s: number[], confs: number[], width: number, height: number) => {
    const dets: VCDetection[] = [];
    for (let i = 0; i < count; i++) {
      dets.push({
        box: {
          x1: x1s[i],
          y1: y1s[i],
          x2: x2s[i],
          y2: y2s[i],
          conf: confs[i],
        }
      });
    }
    console.log(dets);
    setDetections(dets);
    setFrameSize({ width, height });
  }, []);

  // Frame processor (runs on separate thread)
  const frameProcessor = useFrameProcessor((frame) => {
    'worklet';

    if (!isScanning || !modelPath) {
      return;
    }

    // Throttle to 5 FPS (200ms between frames)
    const now = Date.now();
    const timeSinceLastProcess = now - lastProcessedTime.value;
    if (timeSinceLastProcess < 200) {
      return; // Skip this frame
    }
    lastProcessedTime.value = now;

    const result = scanFaces(frame, modelPath);

    // Extract detection data into separate arrays
    if (result.cardCount > 0) {
      const x1s: number[] = [];
      const y1s: number[] = [];
      const x2s: number[] = [];
      const y2s: number[] = [];
      const confs: number[] = [];

      for (let i = 0; i < result.detections.length; i++) {
        const box = result.detections[i].box;
        x1s.push(box.x1);
        y1s.push(box.y1);
        x2s.push(box.x2);
        y2s.push(box.y2);
        confs.push(box.conf);
      }

      updateDetectionsCallback(result.cardCount, x1s, y1s, x2s, y2s, confs, result.frameWidth, result.frameHeight);
    } else {
      updateDetectionsCallback(0, [], [], [], [], [], result.frameWidth, result.frameHeight);
    }
    // Log detailed timing breakdown
    console.log(
      `📊 Frame: ${result.frameWidth}x${result.frameHeight} | ` +
      `Extract: ${result.frameExtractionMs.toFixed(2)}ms | ` +
      `YOLO: ${result.inferenceTimeMs.toFixed(2)}ms | ` +
      `Total: ${result.totalMs.toFixed(2)}ms | ` +
      `Cards: ${result.cardCount}`
    );

    // Log frame properties (first time only)
    if (result.debug) {
      console.log(`🔍 ${result.debug}`);
    }
  }, [isScanning, modelPath, updateDetectionsCallback]);

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
        style={styles.camera}
        device={device}
        isActive={true}
        frameProcessor={frameProcessor}
        pixelFormat="yuv"
      />

      {/* Bounding box overlay */}
      {detections.length > 0 && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {detections.map((detection, index) => {
              const box = detection.box;

              // Coordinates are already in original frame space (1920x1080)
              // Just map directly to screen coordinates
              const scaleX = screenWidth / frameSize.width;
              const scaleY = screenHeight / frameSize.height;

              const x = box.x1 * scaleX;
              const y = box.y1 * scaleY;
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
          <View style={{ position: 'absolute', top: 100, left: 20, backgroundColor: 'rgba(0,0,0,0.7)', padding: 10 }}>
            <Text style={{ color: 'white', fontSize: 12 }}>
              Frame: {frameSize.width}x{frameSize.height}{'\n'}
              Screen: {screenWidth.toFixed(0)}x{screenHeight.toFixed(0)}{'\n'}
              Detections: {detections.length}
            </Text>
          </View>
        </View>
      )}

      {/* Controls */}
      <View style={styles.controls}>
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
    bottom: 40,
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
