export { WebCardScanner } from './core/pipeline.ts';
export type { KernelProfile, ProfiledModel } from './core/profile.ts';
export { httpSearchClient } from './core/httpSearchClient.ts';
export { cachedSearchClient } from './core/cachedSearchClient.ts';
export { captureSize, sourceSize } from './utils/frame.ts';
export { FRAME_MAX_WIDTH } from './constants.ts';
export type {
  Box,
  CardMatch,
  FabColor,
  FrameSource,
  GameClassEntry,
  Pt,
  Quad,
  ScanMode,
  ScannedCard,
  ScannerOptions,
  ScanResult,
  ScanTimings,
  SearchClient,
  SetSymbolMatch,
} from './types.ts';
export { iou } from './utils/box.ts';
// For the parity harness.
export { l2Normalize } from './utils/vector.ts';
export { CardRecognitionModel } from './models/cardRecognition.ts';
export { SetSymbolRecognitionModel } from './models/setSymbolRecognition.ts';
export { ColorBarModel } from './models/colorBar.ts';
