import { CardScannerInstallerNativeModule } from './RnCardScannerModules';
import type { Frame } from 'react-native-vision-camera';

// ============================================================================
// Configuration Types
// ============================================================================

export interface ScannerConfig {
  segmentationModelPath: string;
  embeddingModelPath: string;
  gameName: string; // Default game for initialization
  scanMode: 'single' | 'multiple'; // Single: highest confidence only
  segmentationThreshold: number; // YOLO confidence threshold (default: 0.7)
  iouThreshold: number; // NMS IOU threshold (default: 0.7)
  confidenceThreshold: number; // Min similarity score (default: 0.6)
  maxMatches: number; // Max matches to return per detection
  searchCandidates: number; // DB fetch size for approximate search
  captureImage?: boolean; // Save cropped card images

  // Game-specific detection configs (extensible per-game features)
  gameSpecificConfig?: {
    mtg?: {
      setSymbolDetection?: {
        detectionModelPath: string;
        embeddingModelPath: string;
        detectionThreshold: number;
        confidenceThreshold: number;
      };
    };
  };

  enableLanguageDetection?: boolean;
  enableFoilDetection?: boolean;
}

// ============================================================================
// Result Types
// ============================================================================

export interface InitializationResult {
  success: boolean;
  error?: string;
}

export interface SwapResult {
  success: boolean;
  error?: string;
}

// ============================================================================
// Raw Types (Worklet-safe - primitives only)
// ============================================================================

export interface RawScanResult {
  cardCount: number;
  frameWidth: number;
  frameHeight: number;
  detections: RawDetection[];
  processingTime: number;
  // Timing breakdown
  frameExtractionMs?: number;
  yoloPreprocessMs?: number;
  yoloInferenceMs?: number;
  yoloPostprocessMs?: number;
  embeddingPreprocessMs?: number;
  embeddingInferenceMs?: number;
  dbSearchMs?: number;
  setSymbolDetectionMs?: number;
  error?: string;
}

export interface RawDetection {
  box: BoundingBox;
  matches: RawMatch[];
  croppedImagePath?: string;
  predictedGame?: string; // YOLO's top game prediction
  topGamePredictions?: GamePrediction[]; // Top 3 game predictions from YOLO
  setSymbol?: {
    // MTG set symbol info
    setCode: string;
    setName: string;
    variant: string;
    similarity: number;
    croppedImagePath?: string;
  };
}

export interface GamePrediction {
  game: string;
  confidence: number;
}

export interface RawMatch {
  cardId: string;
  name: string;
  gameName: string;
  score: number;
}

// ============================================================================
// Rich Types (JS thread - complex objects allowed)
// ============================================================================

export interface Detection {
  success: boolean;
  cards: DetectedCard[];
  processingTime: number;
  timings?: {
    frameExtraction?: number;
    yoloPreprocess?: number;
    yoloInference?: number;
    yoloPostprocess?: number;
    embeddingPreprocess?: number;
    embeddingInference?: number;
    dbSearch?: number;
    setSymbolDetection?: number;
  };
  error?: string;
}

interface CapturedImage {
  uri: string;
  width: number;
  height: number;
  format: 'jpeg' | 'png' | 'webp';
  size: number; // in bytes
}

export interface DetectedCard {
  cardId: string;
  name: string;
  gameName: string; // From best match (multi-game search)
  confidenceScore: number;
  boundingBox: BoundingBox;
  capturedImage?: CapturedImage;
  alternativeCards: AlternativeMatch[];
  predictedGame?: string; // YOLO's prediction
  topGamePredictions?: GamePrediction[]; // YOLO's top 3
  setSymbol?: {
    // MTG set symbol info
    setCode: string;
    setName: string;
    variant: string;
    similarity: number;
    croppedImagePath?: string;
  };
  language?: {
    code: string;
    confidence: number;
  };
  foiling?: {
    isFoil: boolean;
    foilType?: string;
    confidence: number;
  };
}

export interface AlternativeMatch {
  cardId: string;
  name: string;
  confidence: number;
}

export interface BoundingBox {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  conf?: number; // Confidence score from detection
}

export interface Game {
  name: string;
  version: string;
  lastUpdated: string;
  totalCards: number;
}

// ============================================================================
// Database Types (for backwards compatibility)
// ============================================================================

export interface DatabaseInfo {
  gameName: string;
  path: string;
}

// ============================================================================
// JSI Global Function Declarations
// ============================================================================

declare global {
  // Core scanner functions
  var initializeScannerNative: (config: ScannerConfig) => InitializationResult;
  var releaseScanner: () => void;
  var startScanningPlugin: (frame: Frame) => RawScanResult;

  // Database management
  var swapDatabaseNative: (sourcePath: string, gameName: string) => SwapResult;
  var getCardCount: (gameName: string) => number;
  var listAvailableGames: () => DatabaseInfo[];
  var closeGameStore: (gameName: string) => void;

  // Debug functions
  var runSegmentationDebug: (
    imagePath: string,
    outputDir: string,
  ) => RawScanResult;

  // Set symbol detection
  var detectSetSymbol: (imagePath: string) => SetSymbolDetectionResult;
}

// Set symbol detection result type
export interface SetSymbolDetectionResult {
  success: boolean;
  error?: string;
  setCode?: string;
  setName?: string;
  variant?: string;
  confidence?: number;
  croppedImagePath?: string;
  embedding?: number[];
  bbox?: {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    confidence: number;
  };
  topMatches?: Array<{
    setCode: string;
    setName: string;
    variant: string;
    similarity: number;
  }>;
  performance?: {
    detectionMs: number;
    embeddingMs: number;
  };
}

// ============================================================================
// Install JSI Bindings
// ============================================================================

if (global.initializeScannerNative == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`,
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.startScanningPlugin == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'startScanningPlugin' function was not found after installation.`,
    );
  }
}

// ============================================================================
// Public API Functions
// ============================================================================

/**
 * Initialize the card scanner with configuration
 * @param config - Scanner configuration object
 * @returns Promise resolving to initialization result
 */
export async function initializeScanner(
  config: ScannerConfig,
): Promise<InitializationResult> {
  try {
    // Native function returns a Promise that runs on background thread
    const result = await global.initializeScannerNative(config);
    return result;
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

/**
 * Release scanner resources and cleanup
 */
export function releaseScanner(): void {
  global.releaseScanner();
}

/**
 * Scan a frame for cards (use in frameProcessor worklet)
 * @param frame - Vision Camera frame
 * @returns Raw scan result (worklet-safe)
 */
export function startScanning(frame: Frame): RawScanResult {
  'worklet';

  if (typeof startScanningPlugin !== 'function') {
    throw new Error('startScanningPlugin is not available in worklet runtime');
  }

  return startScanningPlugin(frame);
}

/**
 * Transform raw scan result to rich Detection object
 * @param raw - Raw scan result from native code
 * @returns Rich detection object with full card information
 */
export function createDetectionResult(raw: RawScanResult): Detection {
  if (raw.error) {
    return {
      success: false,
      cards: [],
      processingTime: raw.processingTime,
      error: raw.error,
    };
  }

  const cards: DetectedCard[] = raw.detections
    .filter((det) => det.matches && det.matches.length > 0)
    .map((det) => {
      const primaryMatch = det.matches[0];
      const alternativeCards = det.matches.slice(1).map((match) => ({
        cardId: match.cardId,
        name: match.name,
        confidence: match.score,
      }));

      return {
        cardId: primaryMatch.cardId,
        name: primaryMatch.name,
        gameName: primaryMatch.gameName,
        confidenceScore: primaryMatch.score,
        boundingBox: det.box,
        capturedImage: det.croppedImagePath
          ? {
              uri: det.croppedImagePath,
              width: 0, // TODO: Add actual dimensions if available
              height: 0,
              format: 'jpeg' as const,
              size: 0,
            }
          : undefined,
        alternativeCards,
        predictedGame: det.predictedGame,
        topGamePredictions: det.topGamePredictions,
        setSymbol: det.setSymbol,
      };
    });

  return {
    success: true,
    cards,
    processingTime: raw.processingTime,
    timings: {
      frameExtraction: raw.frameExtractionMs,
      yoloPreprocess: raw.yoloPreprocessMs,
      yoloInference: raw.yoloInferenceMs,
      yoloPostprocess: raw.yoloPostprocessMs,
      embeddingPreprocess: raw.embeddingPreprocessMs,
      embeddingInference: raw.embeddingInferenceMs,
      dbSearch: raw.dbSearchMs,
      setSymbolDetection: raw.setSymbolDetectionMs,
    },
  };
}

/**
 * Get list of supported/available games
 * @returns Promise resolving to array of games
 */
export async function getSupportedGames(): Promise<Game[]> {
  try {
    // Native function returns a Promise that scans on background thread
    const databases = await global.listAvailableGames();
    return databases.map((db) => ({
      name: db.gameName,
      version: '1.0.0', // TODO: Add version tracking
      lastUpdated: new Date().toISOString(), // TODO: Add actual update date
      totalCards: global.getCardCount(db.gameName),
    }));
  } catch (error) {
    console.error('Failed to get supported games:', error);
    return [];
  }
}

/**
 * Swap/update game database
 * @param gameName - Game identifier
 * @param newDatabasePath - Path to new database file
 * @returns Promise resolving to swap result
 */
export async function swapDatabase(
  gameName: string,
  newDatabasePath: string,
): Promise<SwapResult> {
  try {
    // Native function returns a Promise that runs on background thread
    const result = await global.swapDatabaseNative(newDatabasePath, gameName);
    return result;
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

/**
 * Get card count for a specific game
 * @param gameName - Game identifier
 * @returns Number of cards in database
 */
export function getCardCount(gameName: string): number {
  return global.getCardCount(gameName);
}

/**
 * Close a game store/database
 * @param gameName - Game identifier to close
 */
export function closeGameStore(gameName: string): void {
  global.closeGameStore(gameName);
}

// ============================================================================
// Debug Functions
// ============================================================================

/**
 * Run segmentation on an image file (for debugging)
 * @param imagePath - Path to image file
 * @param outputDir - Output directory for visualization
 * @returns Scan result with detections
 */
export function runSegmentationDebug(
  imagePath: string,
  outputDir: string,
): RawScanResult {
  return global.runSegmentationDebug(imagePath, outputDir);
}

// ============================================================================
// Re-export database utilities for backwards compatibility
// ============================================================================

export {
  downloadDatabase,
  loadDatabaseFromAsset,
  useDatabaseManager,
  listDatabases,
} from './DatabaseDownloader';
