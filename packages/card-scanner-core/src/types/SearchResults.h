#pragma once

#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace cardscanner {

struct CardLanguageInfo {
  std::string code; // "de", "en", ..., "zhs", "zht"; empty when unreadable
  float similarity = 0.0f;

  bool isEmpty() const { return code.empty(); }
};

/**
 * @struct CtcRead
 * @brief A greedy CTC read: the text and the per-step distribution behind it,
 * so a caller can score known candidates against it instead of trusting the
 * text.
 */
struct CtcRead {
  std::string text;
  /// Row-major [steps x classes] probabilities; class 0 is the CTC blank.
  std::vector<float> probs;
  int steps = 0;
  int classes = 0;
  /// What each class reads as, in class order: the CTC blank first.
  std::vector<std::string> classNames;
  /// The first and last step whose best class is not the blank; -1 when the
  /// read is empty.
  int firstStep = -1;
  int lastStep = -1;
  /// Geometric mean of the per-step best probability, 0-1.
  float confidence = 0.f;
};

/**
 * @struct SetCodeInfo
 * @brief A printed set code (Yu-Gi-Oh!: YGLD-ENA22), unlike the MTG set in
 * SetSymbolInfo, which comes from the symbol. Empty when the reader did not
 * run.
 *
 * Classes are in setCodeReading.classNames order: the CTC blank, then one
 * character each.
 */
struct SetCodeInfo {
  CtcRead read;
  /// The search box that gave the read: "regular", "anchored", "link" or
  /// "pendulum".
  std::string layout;
  /// The search box, as fractions of the card.
  cv::Rect2f region;

  bool isEmpty() const { return read.steps == 0; }
};

/**
 * @struct CardSearchResult
 * @brief One card from a similarity search, with its score.
 *
 * The score is a plain dot product against the query embedding. Both sides are
 * L2-normalised by the model, so it reads as a cosine similarity in [-1, 1].
 */
struct CardSearchResult {
  std::string cardId;
  std::string gameName; ///< Which game database this result is from
  double score = 0.0;
  /// Consecutive detections of this card; only ScanSession counts them.
  int detections = 0;
  CardLanguageInfo language;
  /// The printed set code; only ScanSession carries it to the candidate.
  SetCodeInfo printedSetCode;
};

/// The top two matches score within `margin` of each other, so the top pick
/// alone does not settle which card it is.
inline bool isAmbiguous(const std::vector<CardSearchResult> &matches,
                        double margin) {
  return matches.size() > 1 && matches[0].score - matches[1].score <= margin;
}

/**
 * @struct SetSymbolInfo
 * @brief An MTG set symbol match. Empty when nothing matched.
 */
struct SetSymbolInfo {
  std::string setCode; // e.g., "BRO" (Brother's War)
  float similarity;    // Confidence score [0.0, 1.0]

  // Default constructor for empty results
  SetSymbolInfo() : setCode(""), similarity(0.0f) {}

  SetSymbolInfo(const std::string &code, float sim)
      : setCode(code), similarity(sim) {}

  bool isEmpty() const { return setCode.empty(); }
};

} // namespace cardscanner
