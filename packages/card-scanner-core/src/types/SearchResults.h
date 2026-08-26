#pragma once

#include <string>

namespace cardscanner {

/**
 * @struct CardSearchResult
 * @brief One row returned by a card similarity search, with its score.
 *
 * The score is a plain dot product against the query embedding. Both sides are
 * L2-normalised by the model, so it reads as a cosine similarity in [-1, 1].
 */
struct CardSearchResult {
  std::string card_id;
  std::string gameName; ///< Which game database this result is from
  double score;
};

/**
 * @struct SetSymbolMatch
 * @brief One row returned by a set-symbol similarity search.
 */
struct SetSymbolMatch {
  std::string setCode;
  float similarity;
};

} // namespace cardscanner
