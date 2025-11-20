import React, { useState, useRef, useEffect } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  Platform,
  ActivityIndicator,
} from 'react-native';
import { CameraView, useCameraPermissions } from 'expo-camera';
import { recognizeCards, RecognizedCard } from 'react-native-card-scanner';
import {
  cacheDirectory,
  documentDirectory,
  copyAsync,
} from 'expo-file-system/legacy';
import { Asset } from 'expo-asset';
import CardOverlay from '../components/CardOverlay';

export default function CameraScanner() {
  const [permission, requestPermission] = useCameraPermissions();
  const [isScanning, setIsScanning] = useState(false);
  const [detectedCards, setDetectedCards] = useState<RecognizedCard[]>([]);
  const [isProcessing, setIsProcessing] = useState(false);
  const [isLoadingModels, setIsLoadingModels] = useState(true);
  const [modelError, setModelError] = useState<string | null>(null);
  const [capturedImageSize, setCapturedImageSize] = useState<{
    width: number;
    height: number;
  } | null>(null);
  const cameraRef = useRef<CameraView>(null);
  const scanIntervalRef = useRef<NodeJS.Timeout | null>(null);
  const isScanningRef = useRef(false); // Use ref to avoid closure issues
  const lastCaptureTimeRef = useRef(0); // Track last capture time to prevent overlapping
  const modelPathsRef = useRef<{
    yolo: string;
    embedding: string;
    db: string;
  } | null>(null);

  // Load models on mount
  useEffect(() => {
    loadModels();
  }, []);

  const loadModels = async () => {
    try {
      setIsLoadingModels(true);
      setModelError(null);

      console.log('Loading models from assets...');

      // Load assets
      const yoloAsset = await Asset.loadAsync(
        require('../assets/yolo11n-seg.pte'),
      );
      const embeddingAsset = await Asset.loadAsync(
        require('../assets/embedding_model.pte'),
      );

      let yoloModelPath: string;
      let embeddingModelPath: string;

      if (Platform.OS === 'android') {
        yoloModelPath = yoloAsset[0].localUri || yoloAsset[0].uri;
        embeddingModelPath =
          embeddingAsset[0].localUri || embeddingAsset[0].uri;
      } else {
        // iOS: Copy to cache
        const yoloCachePath = `${cacheDirectory}yolo11n-seg.pte`;
        const embeddingCachePath = `${cacheDirectory}embedding_model.pte`;

        await copyAsync({
          from: yoloAsset[0].localUri || yoloAsset[0].uri,
          to: yoloCachePath,
        });

        await copyAsync({
          from: embeddingAsset[0].localUri || embeddingAsset[0].uri,
          to: embeddingCachePath,
        });

        yoloModelPath = yoloCachePath;
        embeddingModelPath = embeddingCachePath;
      }

      modelPathsRef.current = {
        yolo: yoloModelPath,
        embedding: embeddingModelPath,
        db: dbPath,
      };

      console.log('Models loaded successfully');
      console.log('YOLO:', yoloModelPath);
      console.log('Embedding:', embeddingModelPath);

      setIsLoadingModels(false);
    } catch (error) {
      const errorMsg = error instanceof Error ? error.message : String(error);
      console.error('Failed to load models:', errorMsg);
      setModelError(errorMsg);
      setIsLoadingModels(false);
    }
  };

  // Start/stop scanning
  const toggleScanning = () => {
    if (isScanning) {
      stopScanning();
    } else {
      startScanning();
    }
  };

  const startScanning = () => {
    console.log('Starting scanning...');
    setIsScanning(true);
    isScanningRef.current = true; // Update ref
    // Capture and process frames every 1000ms (1 FPS)
    scanIntervalRef.current = setInterval(() => {
      console.log('Interval tick');
      captureAndProcessFrame();
    }, 1000);
    console.log('Scanning started, interval ID:', scanIntervalRef.current);
  };

  const stopScanning = () => {
    console.log('Stopping scanning...');
    setIsScanning(false);
    isScanningRef.current = false; // Update ref
    if (scanIntervalRef.current) {
      clearInterval(scanIntervalRef.current);
      scanIntervalRef.current = null;
    }
    setDetectedCards([]);
    console.log('Scanning stopped');
  };

  const captureAndProcessFrame = async () => {
    console.log('captureAndProcessFrame called');

    // Enforce minimum 1 second between captures to prevent camera unmounting
    const now = Date.now();
    const timeSinceLastCapture = now - lastCaptureTimeRef.current;
    if (timeSinceLastCapture < 1000) {
      console.log(
        'Skipping - too soon since last capture:',
        timeSinceLastCapture,
        'ms',
      );
      return;
    }

    console.log('  isProcessing:', isProcessing);
    console.log('  cameraRef.current:', !!cameraRef.current);
    console.log('  isScanningRef.current:', isScanningRef.current);
    console.log('  modelPathsRef.current:', !!modelPathsRef.current);

    // Skip if already processing or camera not ready or not scanning or models not loaded
    if (
      isProcessing ||
      !cameraRef.current ||
      !isScanningRef.current ||
      !modelPathsRef.current
    ) {
      console.log('Skipping frame capture');
      return;
    }

    lastCaptureTimeRef.current = now;

    try {
      setIsProcessing(true);
      console.log('Taking picture...');

      // Capture photo from camera
      const photo = await cameraRef.current.takePictureAsync({
        quality: 0.3, // Lower quality for faster processing
        skipProcessing: true,
      });

      console.log('Picture taken:', photo?.uri);

      if (!photo?.uri) {
        console.warn('No photo URI received');
        setIsProcessing(false);
        return;
      }

      // Store captured image dimensions for bounding box scaling
      if (photo.width && photo.height) {
        setCapturedImageSize({ width: photo.width, height: photo.height });
        console.log('Captured image size:', photo.width, 'x', photo.height);
      }

      const { yolo, embedding, db } = modelPathsRef.current;

      console.log('Running recognition...');

      // Run recognition pipeline
      const result = recognizeCards(
        photo.uri,
        yolo,
        embedding,
        db,
        0.5, // yoloConf
        0.0, // yoloIou
        1, // topK - only get top 1 match per card
      );

      console.log('Recognition complete:', result.cards.length, 'cards found');

      // Update detected cards only if still scanning
      if (isScanningRef.current) {
        try {
          if (result.cards && result.cards.length > 0) {
            console.log(
              'Detected cards:',
              result.cards.map((c) => c.matches[0]?.name),
            );
            console.log('Updating state with new cards...');
            setDetectedCards([...result.cards]); // Create new array to ensure re-render
            console.log('State updated');
          } else {
            console.log('No cards detected');
            setDetectedCards([]);
          }
        } catch (stateError) {
          console.error('Error updating state:', stateError);
        }
      }
    } catch (error) {
      console.error('Error processing frame:', error);
      // Continue processing, don't crash
    } finally {
      setIsProcessing(false);
      console.log('Frame processing complete');
    }
  };

  // Cleanup on unmount
  useEffect(() => {
    return () => {
      if (scanIntervalRef.current) {
        clearInterval(scanIntervalRef.current);
      }
    };
  }, []);

  if (!permission) {
    return (
      <View style={styles.container}>
        <Text>Loading...</Text>
      </View>
    );
  }

  if (!permission.granted) {
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
        <TouchableOpacity style={styles.button} onPress={loadModels}>
          <Text style={styles.buttonText}>Retry</Text>
        </TouchableOpacity>
      </View>
    );
  }

  return (
    <View style={styles.container}>
      <CameraView ref={cameraRef} style={styles.camera} facing="back">
        {/* Processing indicator */}
        {isProcessing && (
          <View style={styles.processingIndicator}>
            <Text style={styles.processingText}>Processing...</Text>
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
      </CameraView>

      {/* Overlay detected cards - rendered OUTSIDE camera view to prevent unmounting issues */}
      {isScanning && detectedCards.length > 0 && (
        <CardOverlay cards={detectedCards} />
      )}
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
  processingIndicator: {
    position: 'absolute',
    top: 60,
    right: 20,
    backgroundColor: 'rgba(0,0,0,0.7)',
    paddingHorizontal: 16,
    paddingVertical: 8,
    borderRadius: 4,
  },
  processingText: {
    color: '#fff',
    fontSize: 14,
  },
  errorText: {
    color: '#F44336',
    textAlign: 'center',
    padding: 20,
    fontSize: 16,
  },
});
