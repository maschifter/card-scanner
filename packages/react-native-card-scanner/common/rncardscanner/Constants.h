#pragma once

#include <cstddef>
#include <cstdint>
#include <opencv2/core.hpp>

namespace rncardscanner {
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
// Search parameters
constexpr int MAX_SEARCH_RESULTS = 100;
// Minimum cosine similarity score for card matching (0.6 = 60% similarity)
constexpr float MIN_SIMILARITY_SCORE = 0.6f;
// Embedding vector dimension
constexpr size_t EMBEDDING_VECTOR_SIZE = 256;

// Database file names and paths
constexpr const char* SET_SYMBOL_DB_NAME = "set-symbols";
constexpr const char* DB_FILENAME = "data.mdb";
constexpr const char* TEMP_SWAP_PREFIX = "temp_swap_";

// Set symbol search configuration
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
constexpr int NUM_CLASSES = 8;
constexpr int MAX_TOP_PREDICTIONS = 3;
constexpr const char* UNKNOWN_CLASS_NAME = "unknown";

// Bounding box processing
// Padding pixels to add around bounding box for mask extraction
constexpr int BBOX_PADDING = 10;
// Bounding box features: [x, y, w, h]
constexpr int YOLO11_BOX_FEATURES = 4;

// Mask processing
// Threshold for converting probabilistic mask to binary (0.5 = 50%)
constexpr float MASK_THRESHOLD = 0.5f;
// Binary mask value for foreground pixels
constexpr uint8_t BINARY_MASK_VALUE = 255;
// Downsample scale for faster quad extraction from mask
constexpr float MASK_DOWNSAMPLE_SCALE = 0.25f;
// Mask coefficients count (prototypes)
constexpr int YOLO11_MASK_COEFFS = 32;

// IoU calculation
constexpr float IOU_EPSILON = 1e-6f;

// Set symbol detection (specific to SetSymbolYoloModel)
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
// Card Dewarping (Perspective Transform)
// ============================================================================
namespace card {
// Target height for dewarped card images
constexpr int DEWARP_HEIGHT = 640;
// Standard card aspect ratio (width/height) for trading cards
constexpr float ASPECT_RATIO = 0.63f;
} // namespace card

// ============================================================================
// Frame Extraction
// ============================================================================
namespace frame {
// YUV420 (NV21) format uses 1.5 bytes per pixel
constexpr float YUV420_SIZE_MULTIPLIER = 1.5f;
// RGB channels count
constexpr int RGB_CHANNELS = 3;
// RGBA channels count (with alpha)
constexpr int RGBA_CHANNELS = 4;
// Rotation angle for frame orientation correction
constexpr int ROTATION_90_CLOCKWISE = cv::ROTATE_90_CLOCKWISE;
} // namespace frame

// ============================================================================
// Performance Measurement
// ============================================================================
namespace perf {
// Conversion factor from microseconds to milliseconds
constexpr double MICROSECONDS_TO_MILLISECONDS = 1000.0;
} // namespace perf

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
// Visualization Constants
// ============================================================================
namespace viz {
// Colors (BGR format for OpenCV)
const cv::Scalar QUAD_COLOR_GREEN(0, 255, 0);
const cv::Scalar BBOX_COLOR_YELLOW(0, 255, 255);

// Line properties
constexpr int QUAD_LINE_THICKNESS = 3;
constexpr int BBOX_LINE_THICKNESS = 1;

// Label properties
constexpr float LABEL_FONT_SCALE = 0.5f;
constexpr int LABEL_OFFSET_Y = -5;
constexpr int PERCENT_MULTIPLIER = 100;
} // namespace viz

// ============================================================================
// File/Path Constants
// ============================================================================
namespace file {
constexpr const char* URI_PREFIX = "file://";
} // namespace file

} // namespace constants
} // namespace rncardscanner
