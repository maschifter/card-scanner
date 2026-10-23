import type { Quad } from './utils/geometry.ts';
import type { Box } from './utils/box.ts';

export type { Pt, Quad } from './utils/geometry.ts';
export type { Box } from './utils/box.ts';

export interface CardMatch {
  cardId: string;
  gameName: string;
  score: number;
  /** Display name when the backend resolves one (the reference server does). */
  name?: string | null;
}

export interface SetSymbolMatch {
  setCode: string;
  score: number;
}

export interface FabColor {
  color: string;
  score: number;
}

export interface CardLanguageInfo {
  /** "de", "en", ..., "zhs", "zht". */
  code: string;
  score: number;
}

/** The backend boundary: embeddings go out, matches come back. The server
 *  implements the pooled search + accept gate + filter-to-best-game logic. */
export interface SearchClient {
  searchCards(embedding: number[], games: string[]): Promise<CardMatch[]>;
  searchSetSymbol(embedding: number[]): Promise<SetSymbolMatch | null>;
}

export interface ScannedCard {
  /** Frame-pixel detection box. */
  box: Box;
  detectionConfidence: number;
  /** Frame-pixel card quad (TL,TR,BR,BL) when mask extraction succeeded. */
  quad: Quad | null;
  /** Dewarped upright card, 403×640 — the recognition input. */
  dewarp: ImageData;
  embedding: Float32Array;
  candidateGames: string[];
  matches: CardMatch[];
  /** The best match's game, else the segmentation model's top game. */
  predictedGame: string | null;
  setSymbol: SetSymbolMatch | null;
  fabColor: FabColor | null;
  /** Print language of a matched card; null when unreadable. */
  language: CardLanguageInfo | null;
}

/** Video/image (downscaled to FRAME_MAX_WIDTH) or ImageData as is. */
export type FrameSource = ImageData | HTMLVideoElement | HTMLImageElement;

/** Wall-clock breakdown of one scan, ms. `segMs` is the GPU segmentation
 *  pass incl. readback; the per-card stages are summed over all cards. */
export interface ScanTimings {
  captureMs: number;
  segMs: number;
  decodeMs: number;
  /** Includes the dewarp, which runs on the GPU ahead of the embedder. */
  embedMs: number;
  searchMs: number;
  stagesMs: number;
  totalMs: number;
}

export interface ScanResult {
  cards: ScannedCard[];
  /** Size of the frame the boxes and quads are in (see FrameSource). */
  frameWidth: number;
  frameHeight: number;
  timings: ScanTimings;
}

/** 'single' follows one centre-most card across frames (the native
 *  ScannerConfig.scanMode "single" with sticky selection); 'multiple'
 *  returns every detection. */
export type ScanMode = 'single' | 'multiple';

export interface ScannerOptions {
  /** Base URL serving the export-web-models.py output (per-model dirs).
   *  Weights are cached by URL: serve a new model set under a new URL. */
  modelsBaseUrl: string;
  search: SearchClient;
  /** Default 'multiple'. */
  scanMode?: ScanMode;
  /** Scans per second at most, 0 = unthrottled. Default 30. */
  maxFrameRate?: number;
  /** Default true. */
  useSidewaysFlipCache?: boolean;
  /** Reuse a backend answer while the same card stays in view instead of
   *  searching every frame. Default true. */
  reuseSearchResults?: boolean;
  /** Read the set symbol when an MTG top match nearly ties. Default true. */
  matchSetSymbol?: boolean;
  /** Read the pitch colour when a FaB top match nearly ties. Default true. */
  classifyFabColor?: boolean;
  /** Read the print language of matched cards. Default true. */
  classifyLanguage?: boolean;
  segmentationThreshold?: number;
  minGameConfidence?: number;
  disambiguationThreshold?: number;
  setSymbolDetectionThreshold?: number;
  /** Counts of models loaded ('models'), then compiled ('shaders'). */
  onProgress?: (
    stage: 'models' | 'shaders',
    done: number,
    total: number,
  ) => void;
  /** Uncaptured WebGPU errors (shader compile / validation failures) are
   *  asynchronous and never reject a scan; they surface here instead. */
  onGpuError?: (message: string) => void;
  /** Default 384; create() rejects anything but a positive multiple of 32. */
  segmentationInputSize?: number;
  /** Used when the load test finds the GPU slow, default 288; null skips it. */
  slowGpuInputSize?: number | null;
}

/** One row of gameClassMapping.json — YOLO class index → game database slugs. */
export interface GameClassEntry {
  labelIndex: number;
  label: string;
  gameSlugs: string[];
}
