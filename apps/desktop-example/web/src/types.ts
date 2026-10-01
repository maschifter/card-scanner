/** Mirrors the `state` message the scanner server broadcasts. */

export type Status = 'idle' | 'detecting' | 'candidate_ready' | 'emitted';

export interface ScannedCard {
  cardId: string;
  game: string;
  score: number;
  detections: number;
  /** Present once the product lookup resolves. */
  name?: string;
  set?: string;
  imageUrl?: string;
}

/** What the scanner saw last frame, matched or not. */
export interface ScanDiagnostics {
  live: boolean;
  detections: number;
  detectionConfidence: number;
  game: string;
  gameConfidence: number;
  topScore: number;
  topCardId: string;
  ms: number;
  /** The OBS filter's "show timings" box: false means show no timings. */
  timings: boolean;
  /**
   * Stage timings are valid only when true: frames the throttle or blur gate
   * rejected never reach the timers. Hold the last measured values instead.
   */
  measured: boolean;
  yoloMs: number;
  preprocMs: number;
  embedMs: number;
  dbSearchMs: number;
}

export interface Settings {
  acceptScore: number;
  stableDetections: number;
  gracePeriodMs: number;
  emittedTimeoutMs: number;
}

export interface ScannerState {
  status: Status;
  mode: 'auto' | 'manual';
  history: ScannedCard[];
  settings: Settings;
  candidate: ScannedCard | null;
  emitted: ScannedCard | null;
  scan: ScanDiagnostics;
}

export interface StateMessage {
  type: 'state';
  payload: ScannerState;
}

export const emptyScan: ScanDiagnostics = {
  live: false,
  detections: 0,
  detectionConfidence: 0,
  game: '',
  gameConfidence: 0,
  topScore: 0,
  topCardId: '',
  ms: 0,
  timings: false,
  measured: false,
  yoloMs: 0,
  preprocMs: 0,
  embedMs: 0,
  dbSearchMs: 0,
};

export const initialState: ScannerState = {
  status: 'idle',
  mode: 'auto',
  history: [],
  settings: {
    acceptScore: 0.6,
    stableDetections: 2,
    gracePeriodMs: 400,
    emittedTimeoutMs: 1500,
  },
  candidate: null,
  emitted: null,
  scan: emptyScan,
};
