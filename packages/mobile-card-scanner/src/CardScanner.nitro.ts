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
  /** The database the candidate came from; near misses can span games. */
  gameName: string | undefined;
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

export interface NitroCardLanguageInfo {
  code: string;
  similarity: number;
}

/** A printed set code (Yu-Gi-Oh!: YGLD-ENA22), read when the top matches
 *  are ambiguous. Unlike NitroSetSymbol.setCode, it comes from the printed
 *  text. */
export interface NitroSetCodeInfo {
  /** Greedy read: the first code-shaped one, else the most confident. */
  text: string;
  /** Geometric mean of the per-step best probability, 0-1. */
  confidence: number;
  /** Search box that gave the read: regular, anchored, link or pendulum. */
  layout: string;
  /** The search box, [x, y, width, height] as fractions of the card. */
  region: number[];
  /** Per-step probabilities, row-major [steps x classes], class 0 = CTC
   *  blank: score a card's printings against it. */
  probs: number[];
  steps: number;
  classes: number;
  /** What each class reads as, in class order: the CTC blank first, then
   *  one character each. */
  classNames: string[];
}

export interface NitroDetectedCard {
  cardId: string | undefined;
  gameName: string | undefined;
  predictedGameName: string | undefined;
  predictedGameConfidence: number | undefined;
  confidenceScore: number | undefined;
  boundingBox: NitroBoundingBox;
  /** Oriented quad [TL, TR, BR, BL] as 8 numbers (x, y pairs), same
   *  coordinate space as boundingBox. Undefined when the mask gave none. */
  quad: number[] | undefined;
  /** Runner-up matches. On a multi-card page a card with no cardId carries
   *  its best below-threshold candidates here instead, for confirmation. */
  alternativeCards: NitroAlternativeMatch[];
  capturedImage: NitroCapturedImage | undefined;
  setSymbol: NitroSetSymbol | undefined;
  fabColor: NitroFabColor | undefined;
  language: NitroCardLanguageInfo | undefined;
  printedSetCode: NitroSetCodeInfo | undefined;
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
  /** `multiStart`, `multiCard` or `multiEnd` on the multi-card freeze, in
   *  frozen-frame pixels; absent on a frame result. A string keeps nitrogen
   *  from generating an enum. */
  type: string | undefined;
  detection: NitroDetection;
  /** Raw buffer dims for `frame`; upright frame dims for `multi*`. */
  frameWidth: number;
  frameHeight: number;
  coordinateSnapshot: number[];
  /** Frozen frame JPEG (`multiStart` only). */
  frameUri: string | undefined;
  /** Position of this card in the frozen frame (`multiCard` only). */
  cardIndex: number | undefined;
  /** Cards in the frozen frame (`multiStart`, `multiCard`, `multiEnd`). */
  total: number | undefined;
  /** Why a frame in auto/multiple mode did not freeze (`frame` only). */
  multiRejectReason: string | undefined;
  /** The frame held a qualifying multi-card layout (`frame` only). With
   *  `freezeOnMulti` off, this is the only sign the layout qualified. */
  multi: boolean | undefined;
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

  /** The shutter: the next frame that reaches the pipeline scans as
   *  `multiple` and freezes on whatever cards it holds, skipping the layout
   *  checks. One frame only; `false` cancels a request not yet used. A frame
   *  with no card to freeze on comes back with `multiRejectReason` set. */
  requestShutter(requested: boolean): void;
}
