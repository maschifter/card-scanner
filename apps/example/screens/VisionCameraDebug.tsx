import React, { useEffect, useMemo, useRef, useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  ActivityIndicator,
  Image,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { useIsFocused } from '@react-navigation/native';
import {
  Camera,
  CommonResolutions,
  useAsyncRunner,
  useCameraDevice,
  useCameraPermission,
  useFrameOutput,
  type CameraRef,
} from 'react-native-vision-camera';
import { type Detection, type DetectedCard } from '@cardnexus/card-scanner';
import Svg, { Rect } from 'react-native-svg';
import { createSynchronizable } from 'react-native-worklets';
import { useDetectionListener } from '../hooks/useDetectionListener';
import { useScannerLoader } from '../hooks/useScannerLoader';
import { checkDatabaseStatus } from '../utils/database';
import {
  cameraSpaceBoxToViewBox,
  snapshotBoxToCameraSpace,
  type CameraSpaceBox,
  type ViewBox,
} from '../utils/cameraCoords';
import { createScanOnFrame } from '../utils/scanOnFrame';

const isScanningSync = createSynchronizable(false);

export default function VisionCameraDebug() {
  const insets = useSafeAreaInsets();
  const isFocused = useIsFocused();
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');
  const cameraRef = useRef<CameraRef>(null);
  const [isScanning, setIsScanning] = useState(false);
  const [detection, setDetection] = useState<Detection | null>(null);
  // Boxes already mapped into preview-view points (see utils/cameraCoords.ts).
  const [viewBoxes, setViewBoxes] = useState<ViewBox[]>([]);
  const [croppedImagePath, setCroppedImagePath] = useState<string | null>(null);

  const [currentCard, setCurrentCard] = useState<DetectedCard | null>(null);
  const [missingGameAlerts, setMissingGameAlerts] = useState<
    Record<string, number>
  >({});
  const [emptyDatabases, setEmptyDatabases] = useState<Set<string>>(new Set());

  const { isLoading, error, retry } = useScannerLoader();

  useEffect(() => {
    isScanningSync.setBlocking(isScanning);
  }, [isScanning]);

  // Runs on the JS thread (via the Nitro detection listener). Finishes the
  // bounding-box mapping here: the camera->view conversion needs the
  // PreviewView ref, which only exists on the JS thread.
  const processDetection = (
    result: Detection,
    cameraBoxes: CameraSpaceBox[],
  ) => {
    const rects: ViewBox[] = [];
    for (const box of cameraBoxes) {
      const rect = cameraSpaceBoxToViewBox(cameraRef.current, box);
      if (rect != null) rects.push(rect);
    }
    setViewBoxes(rects);
    setDetection(result);
    const firstCard = result.cards[0];
    setCurrentCard(firstCard);

    if (firstCard.capturedImage) {
      setCroppedImagePath(firstCard.capturedImage.uri);
    }

    // Track missing database detections with debouncing
    if (firstCard.predictedGameName && !firstCard.gameName) {
      // No match found - might be missing database
      setMissingGameAlerts((prev) => {
        const game = firstCard.predictedGameName!;
        const count = (prev[game] || 0) + 1;
        const confidence = firstCard.predictedGameConfidence || 0;

        // Alert if: 3+ consecutive frames AND confidence >= 60%
        if (count >= 3 && confidence >= 0.6) {
          // Verify database is actually missing before alerting
          checkDatabaseStatus(game).then((status) => {
            if (!status.isLoaded) {
              console.warn(
                `Missing database detected: ${game.toUpperCase()} ` +
                  `(confidence: ${(confidence * 100).toFixed(1)}%, frames: ${count})`,
              );
              // Mark this game as having empty database
              setEmptyDatabases((prev) => new Set(prev).add(game));
              // Could show Alert/Modal here: Alert.alert(...)
            }
          });
        }

        return { ...prev, [game]: count };
      });
    } else {
      // Reset counters on successful match
      setMissingGameAlerts({});
      setEmptyDatabases(new Set());
    }
  };

  useDetectionListener((res) => {
    const result = res.detection;
    if (!result.success || result.cards.length === 0) {
      return;
    }
    const cameraBoxes: CameraSpaceBox[] = [];
    for (const card of result.cards) {
      const box = snapshotBoxToCameraSpace(
        card.boundingBox,
        res.frameWidth,
        res.frameHeight,
        res.coordinateSnapshot,
      );
      if (box != null) cameraBoxes.push(box);
    }
    processDetection(result, cameraBoxes);
  });

  const asyncRunner = useAsyncRunner();

  const onFrame = useMemo(
    () => createScanOnFrame(asyncRunner, isScanningSync),
    [asyncRunner],
  );

  // The native pipeline consumes interleaved RGB; was the Camera's
  // pixelFormat prop in v4.
  const frameOutput = useFrameOutput({
    pixelFormat: 'rgb',
    // Only a target - session negotiates across outputs and
    // picks the closest natively supported sensor stream.
    //
    // Nothing is scaled, and the box mapping (coordinate-conversion from v5)
    // does not depend on this value. FHD matches what v4 requested.
    targetResolution: CommonResolutions.FHD_16_9,
    onFrame,
  });

  const toggleScanning = () => {
    setIsScanning((prev) => !prev);
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
        <TouchableOpacity style={styles.retryButton} onPress={retry}>
          <Text style={styles.retryButtonText}>Try Again</Text>
        </TouchableOpacity>
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
      {isFocused && (
        <Camera
          ref={cameraRef}
          style={StyleSheet.absoluteFill}
          device={device}
          isActive={true}
          outputs={[frameOutput]}
          constraints={[{ fps: 30 }, { videoStabilizationMode: 'off' }]}
        />
      )}

      {/* Bounding box overlay - rects already in view points */}
      {detection && (
        <View style={StyleSheet.absoluteFill} pointerEvents="none">
          <Svg style={StyleSheet.absoluteFill}>
            {viewBoxes.map((rect, index) => (
              <Rect
                key={`identified-${index}`}
                x={rect.x}
                y={rect.y}
                width={rect.width}
                height={rect.height}
                stroke="#00ff00"
                strokeWidth="4"
                fill="none"
              />
            ))}
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
                {currentCard.predictedGameConfidence !== undefined &&
                  ` (${(currentCard.predictedGameConfidence * 100).toFixed(1)}%)`}
              </Text>
            )}
            {/* Missing Database Alert */}
            {currentCard?.predictedGameName &&
              !currentCard.gameName &&
              emptyDatabases.has(currentCard.predictedGameName) && (
                <Text style={styles.warningText}>
                  Missing DB: {currentCard.predictedGameName.toUpperCase()}{' '}
                  (Database not loaded -{' '}
                  {missingGameAlerts[currentCard.predictedGameName] || 1}{' '}
                  frames)
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
  warningText: {
    color: '#FFA726',
    fontSize: 11,
    lineHeight: 16,
    fontWeight: 'bold',
    marginTop: 4,
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
  retryButton: {
    backgroundColor: '#4CAF50',
    paddingVertical: 14,
    paddingHorizontal: 32,
    borderRadius: 8,
    marginTop: 8,
  },
  retryButtonText: {
    color: '#fff',
    textAlign: 'center',
    fontSize: 16,
    fontWeight: 'bold',
  },
});
