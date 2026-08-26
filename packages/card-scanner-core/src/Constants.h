#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <opencv2/core.hpp>

namespace cardscanner {
namespace constants {

// ============================================================================
// Model Configuration
// ============================================================================
namespace model {
// YOLO Segmentation Model
constexpr float DEFAULT_YOLO_CONF_THRESHOLD = 0.7f;
constexpr float DEFAULT_YOLO_IOU_THRESHOLD = 0.7f;
constexpr int DEFAULT_YOLO_IMAGE_SIZE = 384;

// Embedding Model (MobileNet)
constexpr int EMBEDDING_INPUT_SIZE = 224;
constexpr int EMBEDDING_CHANNELS = 3;
constexpr int EMBEDDING_DIMENSION = 256;
} // namespace model

// ============================================================================
// ImageNet Normalization Constants
// ============================================================================
namespace imagenet {
// RGB channel means for ImageNet normalization
constexpr float MEAN[3] = {0.485f, 0.456f, 0.406f};
// RGB channel standard deviations for ImageNet normalization
constexpr float STD[3] = {0.229f, 0.224f, 0.225f};
// Pixel value normalization scale (8-bit to [0, 1])
constexpr float PIXEL_SCALE = 255.0f;
} // namespace imagenet

// ============================================================================
// Database Configuration
// ============================================================================
namespace database {
// Default cap on candidates returned by a similarity search. Callers that have
// a configured value (ScannerConfig::searchCandidates) pass it explicitly.
constexpr int MAX_SEARCH_RESULTS = 100;
// Minimum cosine similarity score for card matching (0.6 = 60% similarity)
constexpr float MIN_SIMILARITY_SCORE = 0.6f;
// Embedding vector dimension
constexpr size_t EMBEDDING_VECTOR_SIZE = 256;

// Database file names and paths
constexpr const char *SET_SYMBOL_DB_NAME = "set-symbols";
constexpr const char *DB_FILENAME = "data.mdb";
constexpr const char *TEMP_SWAP_PREFIX = "temp_swap_";

// Set symbol matching takes the single best hit and compares it against a
// confidence threshold; ranking beyond the top one is never consulted.
constexpr int SET_SYMBOL_MAX_RESULTS = 1;
} // namespace database

// ============================================================================
// YOLO Segmentation Processing
// ============================================================================
namespace yolo {
// Letterbox preprocessing
// Gray padding color for letterbox (R=114, G=114, B=114)
const cv::Scalar LETTERBOX_PADDING_COLOR(114, 114, 114);

// Class detection
constexpr int MAX_TOP_PREDICTIONS = 3;
constexpr const char *UNKNOWN_CLASS_NAME = "unknown";

// Bounding box processing
// Padding pixels to add around bounding box for mask extraction
constexpr int BBOX_PADDING = 10;
// Bounding box features: [x, y, w, h]
constexpr int BOX_FEATURES = 4;

// Mask processing
// Threshold for converting probabilistic mask to binary (0.5 = 50%)
constexpr float MASK_THRESHOLD = 0.5f;
// Binary mask value for foreground pixels
constexpr uint8_t BINARY_MASK_VALUE = 255;
// Downsample scale for faster quad extraction from mask
constexpr float MASK_DOWNSAMPLE_SCALE = 0.25f;
// Mask coefficients count (prototypes)
constexpr int MASK_COEFFS = 32;

// Set symbol detection (specific to SetSymbolYoloModel)
// Predictions are [x, y, w, h, conf]
constexpr int SET_SYMBOL_PRED_SIZE = 5;

// Quad extraction
// Epsilon fractions for polygon approximation (in order of preference)
constexpr float QUAD_EPSILON_FRACS[] = {0.02f, 0.03f, 0.015f, 0.04f,
                                        0.01f, 0.05f, 0.06f,  0.08f,
                                        0.10f, 0.12f, 0.15f};
// Tolerance for comparing edge midpoint Y values
constexpr float TOPMOST_TIE_TOLERANCE = 2.0f;
// Minimum quad area to be considered valid
constexpr double MIN_QUAD_AREA = 5.0;
// Minimum distance between quad points to avoid degenerate cases
constexpr float MIN_POINT_DISTANCE = 1.0f;
// Orientation tolerance for Y-coordinate comparison
constexpr float ORIENT_Y_TOLERANCE = 0.01f;
} // namespace yolo

// ============================================================================
// Detection Selection (NMS/group-box filtering, single-mode card choice)
// ============================================================================
namespace selection {
// Fraction of a box's own area overlapped by another to count as contained
constexpr float CONTAINED_MIN_AREA_FRAC = 0.8f;
constexpr float GROUP_BOX_MIN_AREA_RATIO = 1.5f;
// How many times the group box and card aspect ratios must differ
constexpr float GROUP_BOX_MIN_ASPECT_MISMATCH = 1.4f;
constexpr int GROUP_BOX_MIN_CONTAINED_CARDS = 2;
// Minimum IoU with the previous pick to count as the same physical card
constexpr float SAME_CARD_MIN_IOU = 0.3f;
// Rival takes over below this fraction of the tracked card's center distance
constexpr float RIVAL_TAKEOVER_DIST_FRAC = 0.55f;
constexpr std::chrono::milliseconds TRACKING_LOST_AFTER{500};
// Radius as a fraction of the frame's smaller dimension
constexpr float DEAD_CENTER_RADIUS_FRAC = 0.1f;
} // namespace selection

// ============================================================================
// Card Dewarping (Perspective Transform)
// ============================================================================
namespace card {
// Target height for dewarped card images
constexpr int DEWARP_HEIGHT = 640;
// Standard card aspect ratio (width/height) for trading cards
constexpr float ASPECT_RATIO = 0.63f;
// Sideways dewarps are 180-deg ambiguous; how long a resolved flip stays valid
constexpr std::chrono::milliseconds SIDEWAYS_FLIP_CACHE_TTL{3000};
} // namespace card

// ============================================================================
// Frame Extraction
// ============================================================================
namespace frame {
// RGBA channels count (with alpha)
constexpr int RGBA_CHANNELS = 4;
} // namespace frame

// ============================================================================
// Letterbox Transform
// ============================================================================
namespace letterbox {
// Rounding adjustment for padding calculations
constexpr float PADDING_ADJUST_MINUS = 0.1f;
constexpr float PADDING_ADJUST_PLUS = 0.1f;
// Padding division factor
constexpr float PADDING_DIVISOR = 2.0f;
} // namespace letterbox

// ============================================================================
// Matrix Operations
// ============================================================================
namespace matrix {
// Alpha value for cv::gemm (matrix multiplication)
constexpr double GEMM_ALPHA = 1.0;
// Beta value for cv::gemm (no addition)
constexpr double GEMM_BETA = 0.0;
// Sigmoid constants
constexpr float SIGMOID_ONE = 1.0f;
// Matrix reshape single channel
constexpr int RESHAPE_SINGLE_CHANNEL = 1;
} // namespace matrix

// ============================================================================
// MTG-Specific Configuration
// ============================================================================
namespace mtg {
// Default thresholds for MTG card detection
constexpr float DEFAULT_DETECTION_THRESHOLD = 0.3f;
constexpr float DEFAULT_CONFIDENCE_THRESHOLD = 0.6f;
} // namespace mtg

// ============================================================================
// File/Path Constants
// ============================================================================
namespace file {
// Host file-system APIs hand out file:// URIs; core works in plain paths.
constexpr const char *URI_PREFIX = "file://";
} // namespace file

} // namespace constants
} // namespace cardscanner
