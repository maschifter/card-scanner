import { CardScannerInstallerNativeModule } from './RnCardScannerModules';
import {
  downloadDatabase,
  useDatabaseManager,
  DatabaseInfo,
  listDatabases,
} from './DatabaseDownloader';

export interface DatabaseInfo {
  gameName: string;
  path: string;
}
export interface InferenceResult {
  outputShape: number[];
  inferenceTimeMs: number;
  embedding: number[]; // 256-dimensional embedding vector
}

export interface CardSearchResult {
  cardId: string;
  name: string;
  score: number; // Cosine similarity score
}

export interface LoadEmbeddingsResult {
  loaded: number; // Number of cards loaded
  totalCards: number; // Total cards in database
}

export interface BoundingBox {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  conf: number;
  cls: number;
}

export interface Point {
  x: number;
  y: number;
}

export interface Detection {
  box: BoundingBox;
  mask: Point[][]; // Array of contours, each contour is an array of points
}

export interface YoloSegmentationResult {
  detections: Detection[];
  inferenceTimeMs: number;
  visualizedImagePath: string; // Path to image with drawn bounding boxes
  dewarpedCardPaths: (string | null)[]; // Paths to dewarped (perspective-corrected) card images
}

export interface RecognizedCard {
  box: {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
  };
  conf: number; // YOLO detection confidence
  embeddingTimeMs: number;
  matches: CardSearchResult[]; // Top similar cards from database
}

export interface TimingBreakdown {
  yoloPreprocessingMs: number;
  yoloInferenceMs: number;
  yoloPostprocessingMs: number;
  yoloTotalMs: number;
  embeddingPreprocessingMs: number;
  embeddingInferenceMs: number;
  embeddingTotalMs: number;
  databaseSearchMs: number;
  totalPipelineMs: number;
}

// Debug function result types
export interface SegmentationDebugResult {
  cardCount: number;
  totalMs: number;
  preprocessingMs: number;
  inferenceMs: number;
  postprocessingMs: number;
  detections: Array<{
    box: {
      x1: number;
      y1: number;
      x2: number;
      y2: number;
      conf: number;
    };
  }>;
}

export interface RecognitionDebugResult {
  cardCount: number;
  totalMs: number;
  preprocessingMs: number;
  inferenceMs: number;
  postprocessingMs: number;
  detections: Array<{
    box: {
      x1: number;
      y1: number;
      x2: number;
      y2: number;
      conf: number;
    };
    matches?: CardSearchResult[];
  }>;
}

export interface CardRecognitionResult {
  cards: RecognizedCard[];
  yoloTimeMs: number; // YOLO inference time only (deprecated - use timingBreakdown)
  totalTimeMs: number; // Total pipeline time (deprecated - use timingBreakdown)
  timingBreakdown: TimingBreakdown; // Detailed timing information
}

// JSI function signature:
// export const listAvailableGames: () => DatabaseInfo[];

// eslint-disable-next-line no-var
declare global {
  // Initialize scanner with ML models and optional default game
  var initializeScanner: {
    (yoloModelPath: string, embeddingModelPath: string): boolean;
    (
      yoloModelPath: string,
      embeddingModelPath: string,
      defaultGame: string,
    ): boolean;
  };
  var releaseScanner: () => boolean;

  // Database management
  var populateDatabase: (assetPath: string, gameName: string) => boolean;
  var switchGame: (gameName: string) => void;
  var swapDatabase: (sourcePath: string, gameName: string) => boolean;
  var getCardCount: (gameName: string) => number;
  var listAvailableGames: () => DatabaseInfo[];
  var closeGameStore: (gameName: string) => void;

  // Debug functions (for testing with image files)
  var runSegmentationDebug: (imagePath: string) => SegmentationDebugResult;
}

if (global.initializeScanner == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`,
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.initializeScanner == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'initializeScanner' function was not found after installation.`,
    );
  }
}

// Export scanner functions
export const initializeScanner = global.initializeScanner;
export const releaseScanner = global.releaseScanner;

// Export database management functions
export const populateDatabase = global.populateDatabase;
export const switchGame = global.switchGame;
export const swapDatabase = global.swapDatabase;
export const getCardCount = global.getCardCount;
export const listAvailableGames = global.listAvailableGames;
export const closeGameStore = global.closeGameStore;

// Export debug functions
export const runSegmentationDebug = global.runSegmentationDebug;

// Export database utilities
export {
  downloadDatabase,
  loadDatabaseFromAsset,
  useDatabaseManager,
  DatabaseInfo,
  listDatabases,
} from './DatabaseDownloader';
// Vision Camera Frame Processor

import type { Frame } from 'react-native-vision-camera';

export interface VCDetection {
  box: {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    conf: number;
  };
  matches?: CardSearchResult[];
  croppedImagePath?: string;
}

export interface SegmentationResult {
  inferenceTimeMs: number;
  cardCount: number;
  detections: VCDetection[];
  frameExtractionMs: number;
  recognitionTimeMs: number;
  totalMs: number;
  frameWidth: number;
  frameHeight: number;
  debug?: string;
}

/**
 * Vision Camera frame processor plugin for YOLO segmentation.
 * Calls the native myCppPlugin JSI function directly.
 *
 * @param frame - Vision Camera frame
 * @param yoloModelPath - Path to the YOLO segmentation model
 * @param embeddingModelPath - Optional: Path to the embedding model for card recognition
 * @param gameName - Optional: Game name for database lookup (required if embeddingModelPath is provided)
 */
export function startScanning(
  frame: Frame,
  gameName?: string,
): SegmentationResult {
  'worklet';

  // @ts-expect-error - startScanningPlugin is a global JSI function
  if (typeof startScanningPlugin !== 'function') {
    throw new Error('startScanningPlugin is not available in worklet runtime');
  }

  // @ts-expect-error - startScanningPlugin is a global JSI function
  return startScanningPlugin(frame, gameName);
}
