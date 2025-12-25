import React, { useState } from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  ActivityIndicator,
  Dimensions,
  Image,
  ScrollView,
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

export default function VisionCameraScanner() {
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
  const [cameraLayout, setCameraLayout] = useState({ width: 0, height: 0 });
  const [lastScannedCardId, setLastScannedCardId] = useState<string | null>(
    null,
  );
  const [consecutiveDetections, setConsecutiveDetections] = useState<number>(0);
  const [scannedCardsHistory, setScannedCardsHistory] = useState<
    DetectedCard[]
  >([]);

  const [pendingSetSymbols, setPendingSetSymbols] = useState<
    Array<{
      setCode: string;
      similarity: number;
    }>
  >([]);

  const [torchEnabled, setTorchEnabled] = useState(false);

  const headerHeight = 100 + insets.top;
  const bottomHeight = 190;

  const { isLoading, error } = useScannerLoader('single');

  // Process raw scan result and transform to rich Detection type
  const processDetectionCallback = useRunOnJS(
    (
      detection: Detection,
      lastCardId: string | null,
      currentCount: number,
      currentSetSymbols: Array<any>,
    ) => {
      setFrameSize({ width: 1080, height: 1920 });

      // Always show detection for real-time feedback
      setDetection(detection);

      // Update UI with first identified card's info
      if (detection.success && detection.cards.length > 0) {
        const firstCard = detection.cards[0];

        // Check if this is a new card (different from last scanned)
        if (firstCard.cardId !== lastCardId) {
          // Different card - reset counter and start tracking
          setLastScannedCardId(firstCard.cardId);
          setConsecutiveDetections(1); // First detection of this card

          // Reset set symbol tracking for new card
          setPendingSetSymbols(
            firstCard.setSymbol ? [firstCard.setSymbol] : [],
          );

          // Don't add to history yet - wait for confirmation (2nd detection)
        } else {
          // Same card as before - increment counter
          const newCount = currentCount + 1;
          setConsecutiveDetections(newCount);

          // Collect set symbol if detected
          if (firstCard.setSymbol) {
            setPendingSetSymbols((prev) => [...prev, firstCard.setSymbol!]);
          }

          // Confirmation logic:
          // - If top 2 matches are close (≤1% difference): confirm after 4 detections (need more certainty)
          // - If top match is clear winner (>1% difference): confirm after 2 detections
          const isMTG = firstCard.gameName === 'mtg';

          // Check if top 2 matches are close
          const hasCloseMatches =
            firstCard.alternativeCards &&
            firstCard.alternativeCards.length > 0 &&
            firstCard.confidenceScore -
              firstCard.alternativeCards[0].confidence <=
              0.01; // 1% difference

          // Pick the best set symbol from all detections (highest similarity)
          let bestSetSymbol = firstCard.setSymbol;
          if (currentSetSymbols.length > 0) {
            bestSetSymbol = currentSetSymbols.reduce((best, current) => {
              return current.similarity > best.similarity ? current : best;
            });
          }

          const hasSetSymbolMatch = bestSetSymbol?.setCode ? true : false;

          // Require 4 detections if:
          // - MTG card without set symbol OR
          // - Top 2 matches are very close (ambiguous)
          const requiredDetections =
            (isMTG && !hasSetSymbolMatch) || hasCloseMatches ? 4 : 2;

          if (newCount === requiredDetections) {
            // Required consecutive detections reached - confirm and add to history!

            // Create final card with best set symbol
            const finalCard = {
              ...firstCard,
              setSymbol: bestSetSymbol,
            };

            const reason = hasCloseMatches
              ? ' (close matches)'
              : isMTG && !hasSetSymbolMatch
                ? ' (no set symbol)'
                : '';
            console.log(`Card confirmed after ${newCount} detections${reason}`);

            // Card fully identified - add to history (check for duplicates)
            setScannedCardsHistory((prev) => {
              // Check if this card is already in the history (avoid duplicates)
              const isDuplicate = prev.some(
                (card) => card.cardId === finalCard.cardId,
              );
              if (isDuplicate) {
                console.log('Card already in history, skipping duplicate');
                return prev;
              }
              const newHistory = [finalCard, ...prev];
              return newHistory.slice(0, 10); // Keep only last 10
            });

            setPendingSetSymbols([]);

            setLastScannedCardId(null);
            setConsecutiveDetections(0);
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

      if (!isScanning) {
        return;
      }

      // Run the heavy processing asynchronously to avoid blocking the camera
      runAsync(frame, () => {
        'worklet';
        const detection = scanFrame(frame);

        // Only update UI when cards are detected (to avoid constant refreshing)
        if (detection.success && detection.cards.length > 0) {
          processDetectionCallback(
            detection,
            lastScannedCardId,
            consecutiveDetections,
            pendingSetSymbols,
          );
        }
      });
    },
    [
      isScanning,
      processDetectionCallback,
      lastScannedCardId,
      consecutiveDetections,
      pendingSetSymbols,
    ],
  );

  const toggleScanning = () => {
    setIsScanning(!isScanning);
    if (isScanning) {
    }
  };

  const toggleTorch = () => {
    setTorchEnabled(!torchEnabled);
  };

  const removeCard = (index: number) => {
    setScannedCardsHistory((prev) => prev.filter((_, i) => i !== index));
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
      {/* Header */}
      <View style={[styles.header, { paddingTop: insets.top }]}>
        <View style={styles.headerLeft}>
          <Text style={styles.headerTitle}>New Scan List</Text>
        </View>
        <View style={styles.headerRight}>
          <TouchableOpacity
            style={[
              styles.headerIconButton,
              torchEnabled && styles.torchActive,
            ]}
            onPress={toggleTorch}
          >
            <Text style={styles.torchIcon}>{torchEnabled ? '🔦' : '💡'}</Text>
          </TouchableOpacity>
          <View style={styles.cardCountBadge}>
            <Text style={styles.cardCountText}>
              {scannedCardsHistory.length}
            </Text>
          </View>
        </View>
      </View>

      {/* Camera View - fills remaining space with flexbox */}
      <View style={styles.cameraContainer}>
        <Camera
          style={styles.camera}
          device={device}
          format={format}
          isActive={isFocused}
          frameProcessor={frameProcessor}
          pixelFormat="rgb"
          fps={30}
          videoStabilizationMode="off"
          enableBufferCompression={false}
          preview={true}
          torch={torchEnabled ? 'on' : 'off'}
          onLayout={(event) => {
            const { width, height } = event.nativeEvent.layout;
            setCameraLayout({ width, height });
          }}
        />

        {/* Bounding box overlay - positioned within camera */}
        {detection?.cards && detection.cards.length > 0 && (
          <View style={styles.boundingBoxOverlay} pointerEvents="none">
            <Svg style={StyleSheet.absoluteFill}>
              {detection.cards.map((det: DetectedCard, index: number) => {
                const box = det.boundingBox;

                // Camera fills the screen width in portrait mode
                // The frame aspect ratio is 1080:1920 (9:16)
                const cameraWidth = screenWidth;

                // Calculate height based on frame aspect ratio
                // Camera should maintain the same aspect ratio as the frame
                const frameAspectRatio = frameSize.width / frameSize.height; // 1080/1920 = 0.5625
                const cameraHeight = cameraWidth / frameAspectRatio; // e.g., 393 / 0.5625 = 698

                // The camera container might be taller than the camera preview
                // Calculate the actual container height
                const containerHeight =
                  cameraLayout.height ||
                  screenHeight - headerHeight - bottomHeight;

                // Calculate vertical offset (letterboxing - black bars on top/bottom)
                const offsetY = (containerHeight - cameraHeight) / 2;

                // Simple direct scaling
                const scale = cameraWidth / frameSize.width;

                const x = box.x1 * scale;
                const y = box.y1 * scale + offsetY;
                const width = (box.x2 - box.x1) * scale;
                const height = (box.y2 - box.y1) * scale;

                return (
                  <Rect
                    key={`detection-${index}`}
                    x={x}
                    y={y}
                    width={width}
                    height={height}
                    stroke={'#00ff00'}
                    strokeWidth="4"
                    fill="none"
                  />
                );
              })}
            </Svg>
          </View>
        )}
      </View>

      {/* Bottom Overlay Container */}
      <View style={[styles.bottomOverlay, { paddingBottom: insets.bottom }]}>
        {/* Pause/Play Button - centered at top of bottom bar */}
        <View style={styles.bottomControlsContainer}>
          <TouchableOpacity
            style={styles.bottomIconButton}
            onPress={toggleScanning}
          >
            <View style={styles.pausePlayIcon}>
              {isScanning ? (
                <>
                  <View style={styles.pauseBar} />
                  <View style={styles.pauseBar} />
                </>
              ) : (
                <View style={styles.playTriangle} />
              )}
            </View>
          </TouchableOpacity>
        </View>

        {/* Bottom Card Carousel */}
        {scannedCardsHistory.length > 0 && (
          <View style={styles.bottomCarousel}>
            <ScrollView
              horizontal
              showsHorizontalScrollIndicator={false}
              contentContainerStyle={styles.carouselContent}
            >
              {scannedCardsHistory.map((card, index) => (
                <View
                  key={`${card.cardId}-${index}`}
                  style={styles.carouselCard}
                >
                  {/* Remove button */}
                  <TouchableOpacity
                    style={styles.removeButton}
                    onPress={() => removeCard(index)}
                  >
                    <View style={styles.removeIcon} />
                  </TouchableOpacity>

                  {/* Card Image */}
                  <View style={styles.cardImagePlaceholder}>
                    {card.capturedImage?.uri ? (
                      <Image
                        source={{
                          uri: card.capturedImage.uri.startsWith('file://')
                            ? card.capturedImage.uri
                            : `file://${card.capturedImage.uri}`,
                        }}
                        style={styles.cardImage}
                        resizeMode="cover"
                      />
                    ) : (
                      <Text style={styles.cardPlaceholderText}>
                        {card.name.substring(0, 3).toUpperCase()}
                      </Text>
                    )}
                  </View>

                  {/* Card Name */}
                  <View style={styles.cardNameContainer}>
                    <Text style={styles.cardNameText} numberOfLines={2}>
                      {card.name} {(card.confidenceScore * 100).toFixed(1)}%
                    </Text>
                    <Text style={styles.cardIdText} numberOfLines={1}>
                      {card.cardId}
                    </Text>
                  </View>

                  {/* Card Info */}
                  <View style={styles.cardInfoOverlay}>
                    <Text style={styles.cardSetText}>
                      #{index + 1}{' '}
                      {card.setSymbol?.setCode.toUpperCase() ||
                        card.gameName.toUpperCase()}
                    </Text>
                  </View>
                </View>
              ))}
            </ScrollView>
          </View>
        )}
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#000',
  },
  cameraContainer: {
    flex: 1,
    position: 'relative',
    zIndex: 1,
  },
  camera: {
    flex: 1,
  },
  message: {
    textAlign: 'center',
    paddingBottom: 10,
    color: '#fff',
    fontSize: 16,
  },
  header: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
    paddingHorizontal: 20,
    paddingBottom: 10,
    backgroundColor: 'rgba(0,0,0,0.9)',
    zIndex: 10,
  },
  headerLeft: {
    flex: 1,
    flexDirection: 'row',
    alignItems: 'center',
    gap: 12,
  },
  headerTitle: {
    color: '#fff',
    fontSize: 20,
    fontWeight: '600',
  },
  headerIconButton: {
    width: 40,
    height: 40,
    borderRadius: 20,
    backgroundColor: 'rgba(255,255,255,0.2)',
    justifyContent: 'center',
    alignItems: 'center',
  },
  torchActive: {
    backgroundColor: 'rgba(255,215,0,0.4)',
  },
  torchIcon: {
    fontSize: 20,
  },
  pausePlayIcon: {
    flexDirection: 'row',
    gap: 4,
    alignItems: 'center',
    justifyContent: 'center',
  },
  pauseBar: {
    width: 4,
    height: 16,
    backgroundColor: '#fff',
    borderRadius: 1,
  },
  playTriangle: {
    width: 0,
    height: 0,
    borderLeftWidth: 12,
    borderTopWidth: 8,
    borderBottomWidth: 8,
    borderLeftColor: '#fff',
    borderTopColor: 'transparent',
    borderBottomColor: 'transparent',
    marginLeft: 3,
  },
  headerRight: {
    flexDirection: 'row',
    alignItems: 'center',
    gap: 15,
  },
  cardCountBadge: {
    backgroundColor: 'rgba(255,255,255,0.2)',
    borderRadius: 20,
    paddingHorizontal: 12,
    paddingVertical: 6,
    minWidth: 40,
    alignItems: 'center',
  },
  cardCountText: {
    color: '#fff',
    fontSize: 18,
    fontWeight: 'bold',
  },
  boundingBoxOverlay: {
    ...StyleSheet.absoluteFillObject,
  },
  bottomOverlay: {
    backgroundColor: 'rgba(0,0,0,0.9)',
    paddingTop: 10,
    zIndex: 10,
  },
  bottomControlsContainer: {
    alignItems: 'center',
    paddingVertical: 10,
  },
  bottomIconButton: {
    width: 56,
    height: 56,
    borderRadius: 28,
    backgroundColor: 'rgba(255,255,255,0.2)',
    justifyContent: 'center',
    alignItems: 'center',
    borderWidth: 2,
    borderColor: 'rgba(255,255,255,0.3)',
  },
  // Bottom carousel
  bottomCarousel: {
    marginBottom: 10,
  },
  carouselContent: {
    paddingHorizontal: 10,
    gap: 10,
  },
  carouselCard: {
    width: 100,
    backgroundColor: 'rgba(30,30,30,0.95)',
    borderRadius: 8,
    overflow: 'hidden',
    marginRight: 10,
  },
  removeButton: {
    position: 'absolute',
    top: 4,
    right: 4,
    width: 24,
    height: 24,
    borderRadius: 12,
    backgroundColor: '#E53935',
    justifyContent: 'center',
    alignItems: 'center',
    zIndex: 10,
  },
  removeIcon: {
    width: 12,
    height: 2,
    backgroundColor: '#fff',
  },
  cardImagePlaceholder: {
    width: '100%',
    height: 130,
    backgroundColor: 'rgba(50,50,50,0.8)',
    justifyContent: 'center',
    alignItems: 'center',
  },
  cardImage: {
    width: '100%',
    height: '100%',
  },
  cardPlaceholderText: {
    color: 'rgba(255,255,255,0.3)',
    fontSize: 24,
    fontWeight: 'bold',
  },
  cardNameContainer: {
    position: 'absolute',
    top: 4,
    left: 4,
    right: 4,
    backgroundColor: 'rgba(0,0,0,0.8)',
    paddingVertical: 4,
    paddingHorizontal: 6,
    borderRadius: 4,
  },
  cardNameText: {
    color: '#fff',
    fontSize: 8,
    fontWeight: '600',
    lineHeight: 10,
  },
  cardIdText: {
    color: '#ccc',
    fontSize: 6,
    marginTop: 2,
    lineHeight: 8,
  },
  cardInfoOverlay: {
    position: 'absolute',
    bottom: 32,
    left: 0,
    right: 0,
    backgroundColor: 'rgba(0,0,0,0.75)',
    paddingVertical: 4,
    paddingHorizontal: 6,
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  cardSetText: {
    color: '#fff',
    fontSize: 9,
    fontWeight: '600',
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
