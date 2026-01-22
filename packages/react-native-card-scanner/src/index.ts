import { CardScannerInstallerNativeModule } from './RnCardScannerModules';
import type { Frame } from 'react-native-vision-camera';

// ============================================================================
// Configuration Types
// ============================================================================

export interface ScannerConfig {
  segmentationModelPath: string;
  embeddingModelPath: string;
  scanMode: 'single' | 'multiple'; // Single: highest confidence only
  segmentationThreshold: number; // YOLO confidence threshold (default: 0.7)
  iouThreshold: number; // NMS IOU threshold (default: 0.7)
  confidenceThreshold: number; // Min similarity score (default: 0.6)
  disambiguationThreshold?: number; // Min score difference to skip game-specific detection (default: 0.02 = 2%)
  maxMatches: number; // Max matches to return per detection
  searchCandidates: number; // DB fetch size for approximate search (recommended: 100)
  captureImage?: boolean; // Save cropped card images
  blurThreshold?: number; // Min blur score (higher = sharper, 0 = disabled, default: 100)
  lowLightThreshold?: number; // Min brightness for gamma correction (0-255, 0 = disabled, default: 65)
  lowLightGamma?: number; // Gamma correction value for low-light enhancement (default: 2.0)
  maxFrameRate?: number; // Maximum frame rate for ML pipeline in FPS (default: 5)
  gameClassMapping: Record<number, string>; // YOLO class ID to game name mapping (required)

  // Game-specific detection configs (extensible per-game features)
  gameSpecificConfig?: Record<
    string,
    {
      // Optional: Game-specific embedding model for improved accuracy
      embeddingModelPath?: string;

      // MTG-specific: Set symbol detection configuration
      setSymbolDetection?: {
        detectionModelPath: string;
        embeddingModelPath: string;
        detectionThreshold: number;
        confidenceThreshold: number;
      };

      // FAB-specific: Color variant detection configuration
      colorDetection?: {
        modelPath: string;
        dotsRegionRatio?: number; // Ratio of card size to extract for dots region (default: 0.20)
        minDotsRegionSize?: number; // Minimum region size in pixels (default: 50)
      };
    }
  >;
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
// Scan Result Types (Worklet-safe)
// ============================================================================

export interface Detection {
  success: boolean;
  cards: DetectedCard[];
  processingTime: number;
  error?: string;
}

export interface CapturedImage {
  uri: string;
  width: number;
  height: number;
  size: number; // in bytes
}

export interface DetectedCard {
  cardId?: string;
  gameName?: string; // From best match (multi-game search)
  predictedGameName?: string; // From YOLO model, even if no DB match
  confidenceScore?: number;
  boundingBox: BoundingBox;
  capturedImage?: CapturedImage;
  alternativeCards: AlternativeMatch[];
  setSymbol?: {
    // MTG set symbol info
    setCode: string;
    similarity: number;
  };
  fabColor?: {
    // FAB color variant info
    color: string; // "red", "yellow", "blue"
    similarity: number;
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

// ============================================================================
// Database Types
// ============================================================================

export interface DatabaseInfo {
  gameName: string;
  path: string;
  cardCount: number;
  creationTimestamp: string;
  fileSize?: number; // File size in bytes
}

export interface DeleteResult {
  success: boolean;
  error?: string;
}

// ============================================================================
// JSI Global Function Declarations
// ============================================================================

declare global {
  // Core scanner functions
  var initializeScanner: (config: ScannerConfig) => InitializationResult;
  var releaseScanner: () => void;
  var scanFramePlugin: (frame: Frame) => Detection;

  // Database management (new CRUD-like interface)
  var swapDatabase: (sourcePath: string, gameName: string) => SwapResult;
  var listDatabases: () => Promise<DatabaseInfo[]>;
  var getDatabaseInfo: (gameName: string) => Promise<DatabaseInfo>;
  var deleteDatabase: (gameName: string) => Promise<DeleteResult>;

  // Image scanning
  var scanImage: (imagePath: string) => Promise<Detection>;
}

// ============================================================================
// Install JSI Bindings
// ============================================================================

if (global.initializeScanner == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      `Failed to install react-native-card-scanner: The native module could not be found.`,
    );
  }
  CardScannerInstallerNativeModule.install();

  if (global.scanFramePlugin == null) {
    throw new Error(
      `Failed to install react-native-card-scanner: The global 'scanFramePlugin' function was not found after installation.`,
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
    const result = await global.initializeScanner(config);
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
 * @returns Detection result with identified cards (worklet-safe)
 */
export function scanFrame(frame: Frame): Detection {
  'worklet';

  if (typeof scanFramePlugin !== 'function') {
    throw new Error('scanFramePlugin is not available in worklet runtime');
  }

  return scanFramePlugin(frame);
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
    const result = await global.swapDatabase(newDatabasePath, gameName);
    return result;
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

/**
 * List all available game databases with full metadata
 * @returns Promise resolving to array of database info
 */
export async function listDatabases(): Promise<DatabaseInfo[]> {
  try {
    return await global.listDatabases();
  } catch (error) {
    console.error('Failed to list databases:', error);
    return [];
  }
}

/**
 * Get detailed information about a specific game database
 * @param gameName - Game identifier (e.g., 'mtg', 'lorcana')
 * @returns Promise resolving to database info
 */
export async function getDatabaseInfo(
  gameName: string,
): Promise<DatabaseInfo | null> {
  try {
    return await global.getDatabaseInfo(gameName);
  } catch (error) {
    return null;
  }
}

/**
 * Delete a game database and all its contents
 * @param gameName - Game identifier
 * @returns Promise resolving to delete result
 */
export async function deleteDatabase(gameName: string): Promise<DeleteResult> {
  try {
    return await global.deleteDatabase(gameName);
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

// ============================================================================
// Image Scanning
// ============================================================================

/**
 * Scan a static image file for cards
 * @param imagePath - Path to image file
 * @returns Promise resolving to Detection result with identified cards
 */
export async function scanImage(imagePath: string): Promise<Detection> {
  return await global.scanImage(imagePath);
}
