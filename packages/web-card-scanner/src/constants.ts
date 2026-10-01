/** Every tuned number in one place — the Constants.h of the web pipeline.
 *  Model contracts come from the deployed bundle; thresholds come from the
 *  training repo (card_nexus/config.py), which is the source of truth where
 *  it and the native app disagree. */

// --- Model input contracts (deployed-models bundle README) ---
/** Trained input size; segmentation may use another (segmentationInputSize). */
export const YOLO_INPUT_SIZE = 384;
export const YOLO_STRIDES = [8, 16, 32] as const;
export const NUM_GAME_CLASSES = 17;
export const CARD_EMBED_SIZE = 224;
export const SYMBOL_EMBED_SIZE = 96;
export const COLOR_BAR_SIZE = 100;
/** Letterbox pad gray, 0–255 (ultralytics 114). */
export const LETTERBOX_PAD = 114;
export const IMAGENET_MEAN = [0.485, 0.456, 0.406] as const;
export const IMAGENET_STD = [0.229, 0.224, 0.225] as const;

// --- Slow GPUs ---
export const SLOW_GPU_INPUT_SIZE = 288;
/** A load test above this moves to SLOW_GPU_INPUT_SIZE. */
export const SLOW_GPU_MS = 40;
export const SLOW_GPU_TEST_BUDGET_MS = 1000;

// --- Frame capture ---
/** Videos and images are downscaled to this width; results use that space. */
export const FRAME_MAX_WIDTH = 1280;

// --- Dewarp geometry (Constants.h DEWARP_HEIGHT / ASPECT_RATIO) ---
export const DEWARP_HEIGHT = 640;
export const DEWARP_WIDTH = Math.round(DEWARP_HEIGHT * 0.63);

// --- Detection / search defaults ---
export const DEFAULT_SEGMENTATION_THRESHOLD = 0.6;
/** Decode cap per frame (the native core has no cap). */
export const MAX_DETECTIONS = 32;
/** Mask crop padding around the box, in frame pixels (native BBOX_PADDING). */
export const MASK_BOX_PADDING_PX = 10;
/** card_nexus MIN_GAME_CONF. The native reference app passes 1e-5, which
 *  disables the floor and leaves only the class/database caps. */
export const DEFAULT_MIN_GAME_CONFIDENCE = 0.1;
export const MAX_CANDIDATE_CLASSES = 3;
export const MAX_CANDIDATE_GAMES = 4;

// --- Disambiguation (card_nexus DISAMBIGUATION_MARGIN / SYMBOL_CONF) ---
export const DEFAULT_DISAMBIGUATION_MARGIN = 0.05;
export const DEFAULT_SYMBOL_DETECTION_THRESHOLD = 0.3;
/** Grow the detected set-symbol box by 10% before cropping
 *  (card_nexus/inference/stages.py crop_symbol). */
export const SYMBOL_CROP_PADDING = 0.1;

/** FaB pitch-indicator crop: the top-left 100×100 px of a 745×1040 scan, as
 *  fractions of the dewarped card (card_nexus/inference/disambiguate.py). */
export const FAB_INDICATOR_W = 100 / 745;
export const FAB_INDICATOR_H = 100 / 1040;
/** Reject a low-confidence colour read (card_nexus FAB_COLOUR_MIN_CONF). */
export const FAB_COLOR_MIN_CONF = 0.7;

// --- Quad extraction (Constants.h constants::yolo) ---
/** approxPolyDP epsilon ladder as fractions of the hull perimeter, tried in
 *  this order. */
export const QUAD_EPSILON_FRACS = [
  0.02, 0.03, 0.015, 0.04, 0.01, 0.05, 0.06, 0.08, 0.1, 0.12, 0.15,
] as const;
export const MIN_POINT_DISTANCE = 1;
export const MIN_QUAD_AREA = 5;
/** Pre-filter for the top-edge pick; ORIENT_Y_TOLERANCE decides the tie. */
export const TOPMOST_TIE_TOLERANCE = 2;
export const ORIENT_Y_TOLERANCE = 0.01;

// --- Cross-frame stability (Constants.h constants::selection / card) ---
export const SAME_CARD_MIN_IOU = 0.3;
export const RIVAL_TAKEOVER_DIST_FRAC = 0.55;
export const TRACKING_LOST_AFTER_MS = 500;
export const DEAD_CENTER_RADIUS_FRAC = 0.1;
/** Group-box drop: a container box covering ≥2 cards (each ≥80% inside), or
 *  one card ≥1.5× smaller with a ≥1.4× aspect mismatch, is a scene box. */
export const CONTAINED_MIN_AREA_FRAC = 0.8;
export const GROUP_BOX_MIN_AREA_RATIO = 1.5;
export const GROUP_BOX_MIN_ASPECT_MISMATCH = 1.4;
export const GROUP_BOX_MIN_CONTAINED_CARDS = 2;
export const SIDEWAYS_FLIP_CACHE_TTL_MS = 3000;

// --- Search reuse (the backend is shared and remote) ---
/** Reuse a search answer for embeddings at least this similar (cosine). Two
 *  frames of one card sit above 0.97; different printings sit near 0.7. */
export const SEARCH_REUSE_MIN_COS = 0.97;
export const SEARCH_REUSE_TTL_MS = 2000;
export const SEARCH_REUSE_CAPACITY = 16;
