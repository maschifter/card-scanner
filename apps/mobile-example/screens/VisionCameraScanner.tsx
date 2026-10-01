import React, {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from 'react';
import {
  View,
  Text,
  StyleSheet,
  TouchableOpacity,
  ActivityIndicator,
  Image,
  ScrollView,
} from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';
import { useFocusEffect, useIsFocused } from '@react-navigation/native';
import {
  Camera,
  CommonResolutions,
  useAsyncRunner,
  useCameraDevice,
  useCameraPermission,
  useFrameOutput,
  type CameraRef,
} from 'react-native-vision-camera';
import {
  cardScannerPlugin,
  setScanMode,
  type AsyncScanResult,
  type DetectedCard,
  type Detection,
  type ScanMode,
} from '@cardnexus/card-scanner';
import Animated, {
  FadeIn,
  FadeOut,
  useSharedValue,
} from 'react-native-reanimated';
import { createSynchronizable } from 'react-native-worklets';
import { BoundingBox } from '../components/BoundingBox';
import { FrozenMultiView } from '../components/FrozenMultiView';
import { useCardConfirmation } from '../hooks/useCardConfirmation';
import { useDetectionListener } from '../hooks/useDetectionListener';
import { useMultiScan } from '../hooks/useMultiScan';
import { useScanMode } from '../hooks/useScanMode';
import { useScannerLoader } from '../hooks/useScannerLoader';
import {
  snapshotBoxToCameraSpace,
  cameraSpaceBoxToViewBox,
  type CameraSpaceBox,
  type ViewBox,
} from '../utils/cameraCoords';
import { createScanOnFrame } from '../utils/scanOnFrame';
import { cardLabel } from '../utils/cardNames';
import { fileUri, pct } from '../utils/format';

// Whether frame worklet should scan. Thread-safe, only mutated via setBlocking() from the JS thread.
// Read via getDirty() inside `onFrame` instead of capturing React state.
const isScanningSync = createSynchronizable(false);
const BOX_CLEAR_GRACE_MS = 400;
// Multi-card freeze: how long after a rescan native accepts frames again, and
// how long a still stays up before it rescans on its own (0 = only on tap).
const RESCAN_COOLDOWN_MS = 1500;
const AUTO_RESUME_MS = 0;
// "multiple" stays a native mode: the shutter scans its one frame with it.
const SCAN_MODES: ScanMode[] = ['single', 'auto'];
const NOTICE_MS = 1500;

export default function VisionCameraScanner() {
  const insets = useSafeAreaInsets();
  const isFocused = useIsFocused();
  const { hasPermission, requestPermission } = useCameraPermission();
  const device = useCameraDevice('back');

  const [isScanning, setIsScanning] = useState(false);
  const cameraRef = useRef<CameraRef>(null);
  const box = useSharedValue<ViewBox | null>(null);

  const [torchEnabled, setTorchEnabled] = useState(false);

  useEffect(() => {
    isScanningSync.setBlocking(isScanning);
    if (!isScanning) {
      box.value = null;
    }
  }, [isScanning, box]);

  const [isCameraRunning, setIsCameraRunning] = useState(false);
  const {
    scannedCardsHistory,
    confirmDetection,
    removeCard,
    addCard,
    replaceCard: replaceHistoryCard,
    confirmCard: confirmHistoryCard,
  } = useCardConfirmation();

  const { isLoading, error, retry } = useScannerLoader();

  // Switched in place on the native side; the models stay loaded.
  const [scanMode, setMode] = useState<ScanMode>('auto');
  useScanMode(scanMode, !isLoading);
  // The card inspector wants the height the carousel would take.
  const [inspecting, setInspecting] = useState(false);

  // JS thread (via the Nitro listener). Finishes the box mapping - camera->
  // view needs the PreviewView ref - then feeds the confirmation bookkeeping.
  const lastDetectionAtRef = useRef(0);
  const processDetection = (
    result: Detection,
    cameraBox: CameraSpaceBox | null,
  ) => {
    if (cameraBox != null) {
      box.value = cameraSpaceBoxToViewBox(cameraRef.current, cameraBox);
      lastDetectionAtRef.current = Date.now();
    } else if (Date.now() - lastDetectionAtRef.current > BOX_CLEAR_GRACE_MS) {
      box.value = null;
    }
    // A detection with no cardId means the card was found but not recognised —
    // the bounding box still shows, but it is not tracked or added to the
    // carousel.
    const identified = result.cards.filter((c) => c.cardId);
    if (identified.length > 0) {
      confirmDetection({ ...result, cards: identified });
    }
  };

  // The freeze owns the listener: it keeps its own stream and hands live
  // frames back only while no still is up.
  const multi = useMultiScan({
    cooldownMs: RESCAN_COOLDOWN_MS,
    autoResumeMs: AUTO_RESUME_MS,
    onCardResolved: addCard,
    onFrame: (res) => {
      const cameraBox =
        res.detection.success && res.detection.cards.length > 0
          ? snapshotBoxToCameraSpace(
              res.detection.cards[0].boundingBox,
              res.frameWidth,
              res.frameHeight,
              res.coordinateSnapshot,
            )
          : null;
      processDetection(res.detection, cameraBox);
    },
  });

  // The shutter: in single mode, one frame scanned as a page and frozen on
  // whatever cards it holds. Pending until that frame's result comes back.
  const shutterPending = useRef(false);
  const [notice, setNotice] = useState<string | null>(null);
  useEffect(() => {
    if (notice === null) {
      return;
    }
    const timer = setTimeout(() => setNotice(null), NOTICE_MS);
    return () => clearTimeout(timer);
  }, [notice]);

  const cancelShutter = useCallback(() => {
    shutterPending.current = false;
    cardScannerPlugin.requestShutter(false);
  }, []);

  const pressShutter = () => {
    shutterPending.current = true;
    setIsScanning(true);
    cardScannerPlugin.requestShutter(true);
  };

  // A request must not outlive the screen: Debug View would freeze on it.
  useFocusEffect(useCallback(() => cancelShutter, [cancelShutter]));

  // Scan results arrive through the plugin's single native listener slot.
  // Single mode reports no multi verdict, so one here answers the shutter.
  useDetectionListener((res: AsyncScanResult) => {
    if (shutterPending.current) {
      if (res.type === 'multiStart') {
        shutterPending.current = false;
      } else if (res.multiRejectReason) {
        shutterPending.current = false;
        setNotice('No cards found');
      }
    }
    multi.onEvent(res);
  });

  const selectMode = (mode: ScanMode) => {
    cancelShutter();
    setMode(mode);
    setScanMode(mode);
    multi.rescan();
  };

  const pickAlternative = (index: number, card: DetectedCard) => {
    const slots = multi.frozen?.cards ?? [];
    const previous = slots[index]?.card?.cardId;
    // Another slot still showing the old card keeps its history entry.
    const stillShown = slots.some(
      (slot, i) => i !== index && slot.card?.cardId === previous,
    );
    multi.replaceCard(index, card, true);
    if (previous && !stillShown) {
      replaceHistoryCard(previous, card);
    } else {
      confirmHistoryCard(card);
    }
  };

  const confirmSlot = (index: number) => {
    const card = multi.frozen?.cards[index]?.card;
    if (!card?.cardId) {
      return;
    }
    multi.replaceCard(index, card, true);
    confirmHistoryCard(card);
  };

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

  const toggleTorch = () => {
    setTorchEnabled(!torchEnabled);
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
        {isFocused && (
          <Camera
            ref={cameraRef}
            style={styles.camera}
            device={device}
            // Frames oriented to the portrait UI, not the phone: a frozen
            // still then matches the preview however the phone is held.
            orientationSource="interface"
            // Off behind a still: nothing live under it, and no frames
            // streaming into a paused pipeline.
            isActive={!multi.frozen}
            outputs={[frameOutput]}
            constraints={[{ fps: 30 }, { videoStabilizationMode: 'off' }]}
            torchMode={
              isCameraRunning ? (torchEnabled ? 'on' : 'off') : undefined
            }
            onStarted={() => setIsCameraRunning(true)}
            onStopped={() => setIsCameraRunning(false)}
          />
        )}

        {/* Bounding box overlay - positioned within camera. */}
        <View style={styles.boundingBoxOverlay} pointerEvents="none">
          <BoundingBox box={box} />
        </View>

        {/* Multi-card freeze: the still with the crops popping in over it. */}
        {multi.frozen && (
          <FrozenMultiView
            frame={multi.frozen}
            onReplaceCard={pickAlternative}
            onConfirmCard={confirmSlot}
            onInspecting={setInspecting}
          />
        )}

        {notice !== null && (
          <Animated.View
            entering={FadeIn}
            exiting={FadeOut}
            style={styles.notice}
            pointerEvents="none"
          >
            <Text style={styles.noticeText}>{notice}</Text>
          </Animated.View>
        )}

        <View style={styles.modePicker}>
          {SCAN_MODES.map((mode) => (
            <TouchableOpacity
              key={mode}
              style={[
                styles.modeButton,
                mode === scanMode && styles.modeActive,
              ]}
              onPress={() => selectMode(mode)}
            >
              <Text style={styles.modeText}>{mode}</Text>
            </TouchableOpacity>
          ))}
        </View>
      </View>

      {/* Bottom Overlay Container */}
      <View style={[styles.bottomOverlay, { paddingBottom: insets.bottom }]}>
        {/* Rescan while a still is up, pause/play otherwise */}
        <View style={styles.bottomControlsContainer}>
          {scanMode === 'single' && !multi.frozen && (
            <TouchableOpacity
              style={styles.shutterButton}
              onPress={pressShutter}
              accessibilityLabel="Scan every card in view"
            >
              <View style={styles.shutterInner} />
            </TouchableOpacity>
          )}
          <TouchableOpacity
            style={styles.bottomIconButton}
            onPress={multi.frozen ? multi.rescan : toggleScanning}
          >
            {multi.frozen ? (
              <Text style={styles.rescanIcon}>↻</Text>
            ) : (
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
            )}
          </TouchableOpacity>
        </View>

        {/* Bottom Card Carousel */}
        {scannedCardsHistory.length > 0 && !inspecting && (
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
                        source={{ uri: fileUri(card.capturedImage.uri) }}
                        style={styles.cardImage}
                        resizeMode="cover"
                      />
                    ) : (
                      <Text style={styles.cardPlaceholderText}>
                        {card.cardId?.substring(0, 3).toUpperCase()}
                      </Text>
                    )}
                  </View>

                  {/* Card Name */}
                  <View style={styles.cardNameContainer}>
                    <Text style={styles.cardNameText} numberOfLines={2}>
                      {card.confirmed ? '✓ ' : ''}
                      {cardLabel(card.cardId)} {pct(card.confidenceScore)}
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
                        card.gameName?.toUpperCase()}
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
  modePicker: {
    position: 'absolute',
    top: 10,
    alignSelf: 'center',
    flexDirection: 'row',
    backgroundColor: 'rgba(0,0,0,0.5)',
    borderRadius: 20,
    padding: 3,
  },
  modeButton: {
    paddingVertical: 6,
    paddingHorizontal: 14,
    borderRadius: 17,
  },
  modeActive: {
    backgroundColor: 'rgba(255,255,255,0.3)',
  },
  modeText: {
    color: '#fff',
    fontSize: 13,
    fontWeight: '600',
  },
  rescanIcon: {
    color: '#fff',
    fontSize: 28,
    lineHeight: 32,
  },
  bottomOverlay: {
    backgroundColor: 'rgba(0,0,0,0.9)',
    paddingTop: 10,
    zIndex: 10,
  },
  bottomControlsContainer: {
    flexDirection: 'row',
    justifyContent: 'center',
    alignItems: 'center',
    gap: 24,
    paddingVertical: 10,
  },
  shutterButton: {
    width: 56,
    height: 56,
    borderRadius: 28,
    borderWidth: 3,
    borderColor: '#fff',
    justifyContent: 'center',
    alignItems: 'center',
  },
  shutterInner: {
    width: 42,
    height: 42,
    borderRadius: 21,
    backgroundColor: '#4CAF50',
  },
  notice: {
    position: 'absolute',
    alignSelf: 'center',
    top: '45%',
    backgroundColor: 'rgba(0,0,0,0.75)',
    borderRadius: 12,
    paddingVertical: 10,
    paddingHorizontal: 18,
  },
  noticeText: { color: '#fff', fontSize: 15, fontWeight: '600' },
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
