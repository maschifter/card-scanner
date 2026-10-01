import { NitroModules } from 'react-native-nitro-modules';
import type { HybridObject } from 'react-native-nitro-modules';
import { CardScannerInstallerNativeModule } from './CardScannerModules';
import type {
  CardScannerPlugin,
  NitroAlternativeMatch,
  NitroAsyncScanResult,
  NitroBoundingBox,
  NitroCapturedImage,
  NitroDetectedCard,
  NitroDetection,
  NitroFabColor,
  NitroSetSymbol,
} from './CardScanner.nitro';

// ============================================================================
// Configuration Types
// ============================================================================

/**
 * `single`: the center-most card only. `multiple`: every card, live.
 * `auto`: single until a stable multi-card layout appears, then multi.
 * In `auto` and `multiple`, a qualifying frame freezes scanning and streams
 * its cards through the detection listener (see `AsyncScanResult.type`).
 */
export type ScanMode = 'single' | 'multiple' | 'auto';

export interface ScannerConfig {
  segmentationModelPath: string;
  embeddingModelPath: string;
  scanMode: ScanMode;
  segmentationThreshold: number; // YOLO confidence threshold (default: 0.7)
  iouThreshold: number; // NMS IOU threshold (default: 0.7)
  confidenceThreshold: number; // Min similarity score (default: 0.6)
  disambiguationThreshold?: number; // Min score difference to skip game-specific detection (default: 0.02 = 2%)
  minGameConfidence?: number; // Min YOLO class confidence for a game's database to be searched (default: 0.1)
  maxMatches: number; // Max matches to return per detection
  searchCandidates: number; // DB fetch size for approximate search (recommended: 100)
  captureImage?: boolean; // Save cropped card images
  blurThreshold?: number; // Min blur score (higher = sharper, 0 = disabled, default: 100)
  lowLightThreshold?: number; // Min brightness for gamma correction (0-255, 0 = disabled, default: 65)
  lowLightGamma?: number; // Gamma correction value for low-light enhancement (default: 2.0)
  maxFrameRate?: number; // Maximum frame rate for ML pipeline in FPS (default: 5)

  // Multi-card freeze (auto / multiple)
  minCardsForMulti?: number; // Min detections to consider multi (default: 6)
  multiStableFrames?: number; // Consecutive qualifying frames before freezing (default: 2)
  freezeOnMulti?: boolean; // Freeze and stream on a qualifying frame (default: true)

  gameClassMapping: Record<number, string | string[]>;

  // Game-specific detection configs (extensible per-game features)
  gameSpecificConfig?: Record<
    string,
    {
      // Optional: Game-specific embedding model for improved accuracy
      embeddingModelPath?: string;

      // Optional: Override the default confidence threshold for this game
      confidenceThreshold?: number;

      // MTG-specific: Set symbol detection configuration
      setSymbolDetection?: {
        detectionModelPath: string;
        embeddingModelPath: string;
        detectionThreshold: number;
        confidenceThreshold: number;
        imageSize?: number;
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

/** Maps nitrogen's `T | undefined` fields back to `?:` - public result types
 *  derive from the spec types in CardScanner.nitro.ts, not hand-mirrored. */
type Optionalize<T> = {
  [K in keyof T as undefined extends T[K] ? K : never]?: Exclude<
    T[K],
    undefined
  >;
} & {
  [K in keyof T as undefined extends T[K] ? never : K]: T[K];
};

export type BoundingBox = NitroBoundingBox;
export type CapturedImage = NitroCapturedImage;
export type SetSymbol = NitroSetSymbol;
export type FabColor = NitroFabColor;
export type AlternativeMatch = NitroAlternativeMatch;
export interface DetectedCard
  extends Optionalize<
    Omit<
      NitroDetectedCard,
      | 'boundingBox'
      | 'capturedImage'
      | 'alternativeCards'
      | 'setSymbol'
      | 'fabColor'
    >
  > {
  boundingBox: BoundingBox;
  capturedImage?: CapturedImage;
  alternativeCards: AlternativeMatch[];
  setSymbol?: SetSymbol;
  fabColor?: FabColor;
}

export interface Detection extends Optionalize<Omit<NitroDetection, 'cards'>> {
  cards: DetectedCard[];
}

/** Result of the async scan path (`scanFrame` -> detection listener). */
export interface AsyncScanResult
  extends Optionalize<Omit<NitroAsyncScanResult, 'detection'>> {
  detection: Detection;
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
// Benchmark Types
// ============================================================================

export interface BenchmarkImageInput {
  imagePath: string; // Path to a still image on disk
  game: string; // Expected game name
  cardIds: string[]; // Every id that counts as a hit
}

export interface BenchmarkResult {
  success: boolean;
  recordCount?: number;
  records?: BenchmarkRecord[];
  recordsJson?: string;
  error?: string;
}

export type ScanOutcome =
  | 'correct'
  | 'wrong_match'
  | 'below_threshold'
  | 'no_detection'
  | 'no_ground_truth' // cardIds not provided
  | 'unknown';

/**
 * One scan of one still image, as stored in the benchmark JSON.
 */
export interface BenchmarkRecord {
  // Identity of scan
  game: string;
  cardId: string;
  iteration: number;
  isWarmup: boolean;

  // Core pipeline stage durations
  yoloMs: number;
  yoloConfidence: number;
  detectionCount: number;
  preprocMs: number;
  saveMs: number;
  embedMs: number;
  dbSearchMs: number;
  gamesSearched: number;

  // Which databases were searched; differing from `game` means the expected
  // card was never a candidate.
  yoloPredictedGames: string;

  // MTG-specific extra model, split per step so its cost is attributable.
  setSymbolRan: boolean;
  setSymbolYoloMs: number;
  setSymbolPreprocMs: number;
  setSymbolEmbedMs: number;
  setSymbolDbSearchMs: number;

  // FAB-specific extra model; a classifier, so it has no detect/search step.
  fabColorRan: boolean;
  fabColorPreprocMs: number;
  fabColorClassifyMs: number;

  // Total time for the entire scan, including any overheads
  totalMs: number;

  // The match as the scanner would report it; blank/0 when nothing met
  // confidenceThreshold.
  topMatchCardId: string;
  topScore: number;
  matchCorrect: boolean;

  rawTopScore: number;
  rawTopCardId: string;

  outcome: ScanOutcome;
}

// ============================================================================
// JSI Global Function Declarations
// ============================================================================

declare global {
  // Core scanner functions
  var initializeScanner: (
    config: ScannerConfig,
  ) => Promise<InitializationResult>;
  var releaseScanner: () => Promise<boolean>;

  // Database management (new CRUD-like interface)
  var swapDatabase: (
    gameName: string,
    sourcePath: string,
  ) => Promise<SwapResult>;
  var listDatabases: () => Promise<DatabaseInfo[]>;
  var getDatabaseInfo: (gameName: string) => Promise<DatabaseInfo>;
  var deleteDatabase: (gameName: string) => Promise<DeleteResult>;
  var doesCardIdExist: (gameName: string, cardId: string) => Promise<boolean>;

  // Image scanning
  var scanImage: (imagePath: string, mode?: ScanMode) => Promise<Detection>;

  // Live scan control
  var pauseScanning: () => void;
  var resumeScanning: () => void;
  var setScanMode: (mode: ScanMode) => void;

  // Benchmarking. Resolves with the records as raw JSON.
  var runBenchmarkFromImages: (
    images: BenchmarkImageInput[],
    warmupIterations: number,
    benchmarkIterations: number,
  ) => Promise<{
    success: boolean;
    recordCount?: number;
    recordsJson?: string;
    error?: string;
  }>;
}

// ============================================================================
// Install JSI Bindings
// ============================================================================

if (global.initializeScanner == null) {
  if (!CardScannerInstallerNativeModule) {
    throw new Error(
      'Failed to install @cardnexus/card-scanner: The native module could not be found.',
    );
  }
  CardScannerInstallerNativeModule.install();
}

/** The plugin's public surface: the scan methods minus the HybridObject
 *  handling (`dispose`, `equals`, ...) - a supertype, so no cast below. */
export type CardScannerFramePlugin = Omit<
  CardScannerPlugin,
  keyof HybridObject<{ ios: 'c++'; android: 'c++' }>
>;

/** Autolinked Nitro HybridObject. Call scanFrame from an AsyncRunner task;
 *  setDetectionListener only from the JS thread. */
export const cardScannerPlugin: CardScannerFramePlugin =
  NitroModules.createHybridObject<CardScannerPlugin>('CardScannerPlugin');

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
export async function releaseScanner(): Promise<void> {
  try {
    const released = await global.releaseScanner();
    if (!released) {
      console.error('Scanner still busy after 1s; release skipped.');
    }
  } catch (error) {
    console.error('Failed to release scanner:', error);
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
    const result = await global.swapDatabase(gameName, newDatabasePath);
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

/**
 * Checks if card_id exists in selected DB.
 * @param gameName - Game identifier
 * @param cardId - Exact card_id string to check
 * @returns Promise resolving to true if the card_id exists, false otherwise
 * @throws If the game's database cannot be opened
 */
export async function doesCardIdExist(
  gameName: string,
  cardId: string,
): Promise<boolean> {
  return await global.doesCardIdExist(gameName, cardId);
}

// ============================================================================
// Image Scanning
// ============================================================================

/**
 * Scan a static image file for cards
 * @param imagePath - Path to image file
 * @param mode - Optional per-call override of the configured scan mode
 * @returns Promise resolving to Detection result with identified cards
 */
export async function scanImage(
  imagePath: string,
  mode?: ScanMode,
): Promise<Detection> {
  return await global.scanImage(imagePath, mode);
}

// ============================================================================
// Live Scan Control
// ============================================================================

/**
 * Stop accepting camera frames. The multi-card freeze calls this natively;
 * call it yourself to hold the pipeline without tearing down the camera.
 */
export function pauseScanning(): void {
  global.pauseScanning();
}

/**
 * Accept camera frames again after a freeze or a manual pause. Also forgets
 * the tracked card and restarts the multi-card stability window.
 */
export function resumeScanning(): void {
  global.resumeScanning();
}

/**
 * Switch the scan mode without reloading models. Takes effect on the next
 * frame.
 */
export function setScanMode(mode: ScanMode): void {
  global.setScanMode(mode);
}

// ============================================================================
// Benchmarking
// ============================================================================

/**
 * Run a native performance benchmark over a fixed set of still images.
 *
 * Each image is decoded once, then scanned `warmupIterations +
 * benchmarkIterations` times through the full pipeline.
 *
 * @param images - Still images to scan, each tagged with its expected game/card
 * @param warmupIterations - Iterations per image recorded but flagged
 * @param benchmarkIterations - Iterations per image recorded as real samples
 * @returns Promise resolving to the records and their count
 */
export async function runBenchmarkFromImages(
  images: BenchmarkImageInput[],
  warmupIterations: number,
  benchmarkIterations: number,
): Promise<BenchmarkResult> {
  try {
    const result = await global.runBenchmarkFromImages(
      images,
      warmupIterations,
      benchmarkIterations,
    );

    return {
      ...result,
      records: result.recordsJson
        ? (JSON.parse(result.recordsJson) as BenchmarkRecord[])
        : undefined,
    };
  } catch (error) {
    return {
      success: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}
