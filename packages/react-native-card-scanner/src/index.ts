import { CardScannerInstallerNativeModule } from './RnCardScannerModules';

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

export interface CardRecognitionResult {
  cards: RecognizedCard[];
  yoloTimeMs: number; // YOLO inference time only
  totalTimeMs: number; // Total pipeline time
}

export interface TimingBreakdown {
  yoloInferenceMs: number;
  yoloTotalMs: number;
  yoloPostprocessingMs: number;
  dbOpenMs: number;
  perCardMs: number[];
  totalPipelineMs: number;
}

// eslint-disable-next-line no-var
declare global {
  var runInference: (modelPath: string) => InferenceResult;
  var runInferenceOnImage: (
    modelPath: string,
    imagePath: string
  ) => InferenceResult;
  var loadCardEmbeddings: (
    dbPath: string,
    jsonPath: string
  ) => LoadEmbeddingsResult;
  var searchSimilarCards: (
    dbPath: string,
    embedding: number[],
    limit: number
  ) => CardSearchResult[];
  var runYoloSegmentation: (
    modelPath: string,
    imagePath: string,
    conf: number,
    iou: number,
    outputDir: string
  ) => YoloSegmentationResult;
  var recognizeCards: (
    imagePath: string,
    yoloModelPath: string,
    embeddingModelPath: string,
    dbPath: string,
    yoloConf?: number,
    yoloIou?: number,
    topK?: number
  ) => CardRecognitionResult;
  var getCardCount: (dbPath: string) => number;
}

if (global.runInference == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.runInference == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'runInference' function was not found after installation.`
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
