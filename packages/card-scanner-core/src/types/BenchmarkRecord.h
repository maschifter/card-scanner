#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cardscanner {

/**
 * @enum ScanOutcome
 * @brief Outcome of a single scan for benchmark.
 */
enum class ScanOutcome {
  Correct,        
  WrongMatch,     // Match met the threshold, but it is the wrong card
  BelowThreshold, // Best score lost to confidenceThreshold
  NoDetection,    // YOLO found no card
  NoGroundTruth,  // CardID not provided
};

inline const char *toString(ScanOutcome outcome) {
  switch (outcome) {
  case ScanOutcome::Correct:
    return "correct";
  case ScanOutcome::WrongMatch:
    return "wrong_match";
  case ScanOutcome::BelowThreshold:
    return "below_threshold";
  case ScanOutcome::NoDetection:
    return "no_detection";
  case ScanOutcome::NoGroundTruth:
    return "no_ground_truth";
  }
  return "unknown";
}

/**
 * @struct BenchmarkRecord
 * @brief One entry of the benchmark JSON: a single scan of one still image. 
 * A false *Ran flag means skipped, not 0ms.
 */
struct BenchmarkRecord {
  // Identity of this scan
  std::string game;      
  std::string cardId;   
  int iteration = 0;    
  bool isWarmup = false; 

  // Core pipeline stage durations (ms)
  double yoloMs = 0.0;
  float yoloConfidence = 0.0f;
  int detectionCount = 0;     
  double preprocMs = 0.0;     
  double saveMs = 0.0;        
  double embedMs = 0.0;        
  double dbSearchMs = 0.0;     
  int gamesSearched = 0;

  // Which databases were searched; differing from `game` means the expected card was never a candidate.
  std::string yoloPredictedGames;

  // MTG-specific extra model, split per step.
  bool setSymbolRan = false;
  double setSymbolYoloMs = 0.0;
  double setSymbolPreprocMs = 0.0;
  double setSymbolEmbedMs = 0.0;
  double setSymbolDbSearchMs = 0.0;

  // FAB-specific extra model; a classifier, so it has no detect/search step.
  bool fabColorRan = false;
  double fabColorPreprocMs = 0.0;
  double fabColorClassifyMs = 0.0;

  double totalMs = 0.0; // whole processFrame call

  // The match as the scanner would report it; blank/0 when nothing met confidenceThreshold.
  std::string topMatchCardId;
  float topScore = 0.0f;
  bool matchCorrect = false;

  float rawTopScore = 0.0f;
  std::string rawTopCardId;

  ScanOutcome outcome = ScanOutcome::NoDetection;
};

/**
 * @struct BenchmarkImageInput
 * @brief One still image to be scanned repeatedly during benchmarking.
 */
struct BenchmarkImageInput {
  std::string imagePath; ///< file:// prefix stripped internally
  std::string game;      ///< expected game name
  std::vector<std::string> cardIds; ///< Every id that counts as a hit
};

/**
 * @struct BenchmarkRunResult
 * @brief Outcome of a run: the records as JSON.
 */
struct BenchmarkRunResult {
  size_t recordCount = 0;
  std::string json;
};

} // namespace cardscanner
