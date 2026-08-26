import type { HybridObject } from 'react-native-nitro-modules';
import type { Frame } from 'react-native-vision-camera';

// Nitro structs: source of truth for what native emits - index.ts derives the
// public types. Optionals are `T | undefined` (= std::optional), never `?:`.
export interface NitroBoundingBox {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  conf: number;
}

export interface NitroAlternativeMatch {
  cardId: string;
  confidence: number;
}

export interface NitroCapturedImage {
  uri: string;
  width: number;
  height: number;
  size: number;
}

export interface NitroSetSymbol {
  setCode: string;
  similarity: number;
}

export interface NitroFabColor {
  color: string;
  similarity: number;
}

export interface NitroDetectedCard {
  cardId: string | undefined;
  gameName: string | undefined;
  predictedGameName: string | undefined;
  predictedGameConfidence: number | undefined;
  confidenceScore: number | undefined;
  boundingBox: NitroBoundingBox;
  alternativeCards: NitroAlternativeMatch[];
  capturedImage: NitroCapturedImage | undefined;
  setSymbol: NitroSetSymbol | undefined;
  fabColor: NitroFabColor | undefined;
}

export interface NitroDetection {
  success: boolean;
  cards: NitroDetectedCard[];
  processingTime: number;
  error: string | undefined;
}

/** Scan result (`scanFrame` -> listener). The Frame is disposed by then, so
 *  buffer dims and the coordinateSnapshot ride along for box mapping. */
export interface NitroAsyncScanResult {
  detection: NitroDetection;
  frameWidth: number;
  frameHeight: number;
  coordinateSnapshot: number[];
}

/** The native frame plugin, replacing v4's `global.scanFramePlugin` - v5
 *  Frames are Nitro HybridObjects, unreadable from raw JSI. */
export interface CardScannerPlugin
  extends HybridObject<{ ios: 'c++'; android: 'c++' }> {
  /** Sync scan on the calling thread (an AsyncRunner task). Takes ownership
   *  of the Frame. Never call `frame.dispose()` after this. */
  scanFrame(frame: Frame, coordinateSnapshot: number[]): void;

  /** Registers the async result listener - from the JS thread; results go
   *  back to the registering runtime. Replaces any previous listener. */
  setDetectionListener(listener: (result: NitroAsyncScanResult) => void): void;

  /** Unregisters the async result listener. Pending results are dropped. */
  clearDetectionListener(): void;
}
