#pragma once

#include <string>

namespace cardscanner {

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
};

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
