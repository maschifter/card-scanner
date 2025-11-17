#pragma once

#include "ObjectBoxDB.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

// Result for a single detected card
struct CardRecognitionResult {
  float x1, y1, x2, y2; // bounding box
  float conf;            // YOLO confidence
  std::vector<CardSearchResult> matches; // Top similar cards from database
  double embeddingTimeMs;                // Time to extract embedding
};

// Detailed timing breakdown for the pipeline
struct TimingBreakdown {
  double yoloPreprocessingMs;
  double yoloInferenceMs;
  double yoloPostprocessingMs;
  double yoloTotalMs;
  double embeddingPreprocessingMs;
  double embeddingInferenceMs;
  double embeddingTotalMs;
  double databaseSearchMs;
  double totalPipelineMs;
};

// Pipeline result containing all detected cards
struct PipelineResult {
  std::vector<CardRecognitionResult> cards;
  double yoloTimeMs;    // YOLO inference time (deprecated - use timingBreakdown)
  double totalTimeMs;   // Total pipeline time (deprecated - use timingBreakdown)
  TimingBreakdown timingBreakdown; // Detailed timing information
};

class CardRecognitionPipeline {
public:
  // Run full pipeline: YOLO detection -> crop cards -> extract embeddings -> search similar
  static PipelineResult recognize(const std::string &imagePath,
                                   const std::string &yoloModelPath,
                                   const std::string &embeddingModelPath,
                                   const std::string &dbPath,
                                   float yoloConf = 0.5f,
                                   float yoloIou = 0.7f,
                                   int topK = 10);

private:
  // Crop card from image using bounding box
  static cv::Mat cropCard(const cv::Mat &img, float x1, float y1, float x2,
                          float y2);
};

} // namespace cardscanner
