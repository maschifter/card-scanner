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
  var runInference: (modelPath: string) => InferenceResult;
  var runInferenceOnImage: (
    modelPath: string,
    imagePath: string,
  ) => InferenceResult;
  var loadCardEmbeddings: (
    gameName: string,
    jsonPath: string,
  ) => LoadEmbeddingsResult;
  var searchSimilarCards: (
    gameName: string,
    embedding: number[],
    limit: number,
  ) => CardSearchResult[];
  var runYoloSegmentation: (
    modelPath: string,
    imagePath: string,
    conf: number,
    iou: number,
    outputDir: string,
  ) => YoloSegmentationResult;
  var recognizeCards: (
    imagePath: string,
    yoloModelPath: string,
    embeddingModelPath: string,
    gameName: string,
    yoloConf?: number,
    yoloIou?: number,
    topK?: number,
  ) => CardRecognitionResult;
  var getCardCount: (gameName: string) => number;
  var swapDatabase: (sourcePath: string, gameName: string) => boolean;
  var listAvailableGames: () => DatabaseInfo[];
  var closeGameStore: (gameName: string) => void;
}

if (global.runInference == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`,
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.runInference == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'runInference' function was not found after installation.`,
    );
  }
}

export { runInference } from './CardScanner';
export const runInferenceOnImage = global.runInferenceOnImage;
export const loadCardEmbeddings = global.loadCardEmbeddings;
export const searchSimilarCards = global.searchSimilarCards;
export const runYoloSegmentation = global.runYoloSegmentation;
export const recognizeCards = global.recognizeCards;
export const getCardCount = global.getCardCount;
export const swapDatabase = global.swapDatabase;
export const listAvailableGames = global.listAvailableGames;
export const closeGameStore = global.closeGameStore;
export { downloadDatabase, useDatabaseManager, DatabaseInfo, listDatabases };
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
  yoloModelPath: string,
  embeddingModelPath?: string,
  gameName?: string,
): SegmentationResult {
  'worklet';

  // @ts-expect-error - myCppPlugin is a global JSI function
  if (typeof myCppPlugin !== 'function') {
    throw new Error('myCppPlugin is not available in worklet runtime');
  }

  // @ts-expect-error - myCppPlugin is a global JSI function
  return myCppPlugin(frame, yoloModelPath, embeddingModelPath, gameName);
}
