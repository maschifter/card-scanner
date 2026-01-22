import React, { useState } from 'react';
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
import { useIsFocused } from '@react-navigation/native';
import {
  Camera,
  runAsync,
  useCameraDevice,
  useCameraFormat,
  useCameraPermission,
  useFrameProcessor,
} from 'react-native-vision-camera';
import {
  scanFrame,
  type Detection,
  type DetectedCard,
} from '@cardnexus/card-scanner';
import Svg, { Rect } from 'react-native-svg';
import { useRunOnJS } from 'react-native-worklets-core';
import { useScannerLoader } from '../hooks/useScannerLoader';

const screenWidth = Dimensions.get('window').width;
const screenHeight = Dimensions.get('window').height;

export default function VisionCameraDebug() {
  const insets = useSafeAreaInsets();
  const isFocused = useIsFocused();
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');
  const format = useCameraFormat(device, [
    { fps: 30 },
    { videoResolution: { width: 1080, height: 1920 } },
    { videoStabilizationMode: 'off' },
  ]);

  const [isScanning, setIsScanning] = useState(false);
  const [detection, setDetection] = useState<Detection | null>(null);
  const [frameSize, setFrameSize] = useState({ width: 1080, height: 1920 });
  const [croppedImagePath, setCroppedImagePath] = useState<string | null>(null);
  const [cameraLayout, setCameraLayout] = useState({ width: 0, height: 0 });

  const [currentCard, setCurrentCard] = useState<DetectedCard | null>(null);

  const { isLoading, error } = useScannerLoader('single');

  const processDetectionCallback = useRunOnJS((detection: Detection) => {
    setFrameSize({ width: 1080, height: 1920 });
    setDetection(detection);
    if (detection.success && detection.cards.length > 0) {
      const firstCard = detection.cards[0];
      setCurrentCard(firstCard);

      if (firstCard.capturedImage) {
        setCroppedImagePath(firstCard.capturedImage.uri);
      }
    } else {
      setCurrentCard(null);
    }
  }, []);

  const frameProcessor = useFrameProcessor(
    (frame) => {
      'worklet';

      if (!isScanning) {
        return;
      }

      runAsync(frame, () => {
        'worklet';

        const detection = scanFrame(frame);
        if (detection.success && detection.cards.length > 0) {
          processDetectionCallback(detection);
        }
      });
    },
    [isScanning, processDetectionCallback],
  );

  const toggleScanning = () => {
    setIsScanning(!isScanning);
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

  if (isLoading) {
    return (
      <View style={styles.container}>
        <ActivityIndicator size="large" color="#4CAF50" />
        <Text style={styles.message}>Loading models...</Text>
      </View>
    );
  }

  if (error) {
    return (
      <View style={styles.container}>
        <Text style={styles.errorText}>Failed to load models:</Text>
        <Text style={styles.errorText}>{error}</Text>
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
        isActive={isFocused}
        frameProcessor={frameProcessor}
        fps={30}
        videoStabilizationMode="off"
        pixelFormat="rgb"
        enableBufferCompression={false}
        preview={true}
        onLayout={(event) => {
          const { width, height } = event.nativeEvent.layout;
          setCameraLayout({ width, height });
        }}
      />

      {/* Bounding box overlay */}
      {detection && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {detection.cards.map((card, index) => {
              const box = card.boundingBox;

              const cameraWidth = cameraLayout.width || screenWidth;
              const cameraHeight = cameraLayout.height || screenHeight;

              const frameAspectRatio = frameSize.width / frameSize.height;
              const cameraAspectRatio = cameraWidth / cameraHeight;

              let previewWidth, previewHeight, offsetX, offsetY;

              if (cameraAspectRatio > frameAspectRatio) {
                previewHeight = cameraHeight;
                previewWidth = cameraHeight * frameAspectRatio;
                offsetX = (cameraWidth - previewWidth) / 2;
                offsetY = 0;
              } else {
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
                  key={`identified-${index}`}
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
          <View style={styles.debugOverlay}>
            <Text style={styles.debugText}>
              Processing: {detection.processingTime.toFixed(1)} ms{'\n'}{' '}
            </Text>
            {/* Predicted Game Name */}
            {currentCard?.predictedGameName && (
              <Text style={styles.debugText}>
                Predicted Game: {currentCard.predictedGameName.toUpperCase()}
              </Text>
            )}
            {currentCard && (
              <Text style={styles.debugText}>
                Game: {currentCard.gameName ? currentCard.gameName.toUpperCase() : 'NOT POPULATED'}
              </Text>
            )}
            {/* Top 3 Matches */}
            {currentCard && (
              <View style={styles.matchesContainer}>
                <Text style={styles.matchesTitle}>Top Matches:</Text>

                {/* Match #1 (Primary) */}
                <View style={styles.matchItem}>
                  <Text style={styles.matchNumber}>1.</Text>
                  <View style={styles.matchDetails}>
                    <Text style={styles.matchName}>{currentCard.cardId}</Text>
                    {(currentCard.gameName || currentCard.confidenceScore) && (
                      <Text style={styles.matchInfo}>
                        {currentCard.gameName?.toUpperCase()}
                        {currentCard.gameName &&
                          currentCard.confidenceScore &&
                          ' • '}
                        {currentCard.confidenceScore &&
                          `${(currentCard.confidenceScore * 100).toFixed(1)}%`}
                        {currentCard.setSymbol?.setCode &&
                          ` • ${currentCard.setSymbol.setCode.toUpperCase()}`}
                      </Text>
                    )}
                  </View>
                </View>

                {/* Match #2 */}
                {currentCard.alternativeCards &&
                  currentCard.alternativeCards.length > 0 && (
                    <View style={styles.matchItem}>
                      <Text style={styles.matchNumber}>2.</Text>
                      <View style={styles.matchDetails}>
                        <Text style={styles.matchName}>
                          {currentCard.alternativeCards[0].cardId}
                        </Text>
                        <Text style={styles.matchInfo}>
                          {(
                            currentCard.alternativeCards[0].confidence * 100
                          ).toFixed(1)}
                          %
                        </Text>
                      </View>
                    </View>
                  )}

                {/* Match #3 */}
                {currentCard.alternativeCards &&
                  currentCard.alternativeCards.length > 1 && (
                    <View style={styles.matchItem}>
                      <Text style={styles.matchNumber}>3.</Text>
                      <View style={styles.matchDetails}>
                        <Text style={styles.matchName}>
                          {currentCard.alternativeCards[1].cardId}
                        </Text>
                        <Text style={styles.matchInfo}>
                          {(
                            currentCard.alternativeCards[1].confidence * 100
                          ).toFixed(1)}
                          %
                        </Text>
                      </View>
                    </View>
                  )}
              </View>
            )}

            <Text style={styles.debugText}>
              Detections: {detection?.cards.length ?? 0}
            </Text>
            {croppedImagePath && (
              <Image
                source={{
                  uri: croppedImagePath.startsWith('file://')
                    ? croppedImagePath
                    : `file://${croppedImagePath}`,
                }}
                style={styles.croppedImage}
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
    justifyContent: 'center',
    alignItems: 'center',
  },
  message: {
    textAlign: 'center',
    paddingBottom: 10,
    color: '#fff',
    fontSize: 16,
  },
  debugOverlay: {
    position: 'absolute',
    top: 60,
    left: 10,
    backgroundColor: 'rgba(0,0,0,0.8)',
    padding: 10,
    borderRadius: 8,
    maxWidth: 250,
  },
  debugText: {
    color: '#4CAF50',
    fontSize: 11,
    lineHeight: 16,
  },
  croppedImage: {
    width: 150,
    height: 200,
    marginTop: 10,
    borderRadius: 4,
  },
  matchesTitle: {
    color: '#4CAF50',
    fontSize: 12,
    fontWeight: 'bold',
    marginBottom: 8,
  },
  matchItem: {
    flexDirection: 'row',
    marginBottom: 8,
    alignItems: 'flex-start',
  },
  matchNumber: {
    color: '#4CAF50',
    fontSize: 12,
    fontWeight: 'bold',
    marginRight: 8,
    width: 20,
  },
  matchDetails: {
    flex: 1,
  },
  matchName: {
    color: '#fff',
    fontSize: 11,
    fontWeight: '600',
    marginBottom: 2,
  },
  matchesContainer: {
    marginTop: 10,
    marginBottom: 10,
    paddingTop: 10,
    borderTopWidth: 1,
    borderTopColor: 'rgba(76,175,80,0.3)',
  },
  matchInfo: {
    color: 'rgba(255,255,255,0.6)',
    fontSize: 10,
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
