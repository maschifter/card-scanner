#pragma once

#include <vector>

namespace cardscanner {

/**
 * @struct CardEmbeddingResult
 * @brief The vector representing a card's artwork.
 *
 * Named rather than a bare std::vector<float> so the dimension contract is
 * visible at the call site: this one is 256-wide and must match the width the
 * bundled card databases were built with.
 */
struct CardEmbeddingResult {
  std::vector<float> embedding; ///< 256-dim, L2-normalised by the model
};

/**
 * @struct SetSymbolEmbeddingResult
 * @brief The vector representing an MTG set symbol.
 *
 * Deliberately a separate type from CardEmbeddingResult despite the identical
 * shape - it is 128-wide, and the two must never be compared to each other or
 * searched against the wrong index.
 */
struct SetSymbolEmbeddingResult {
  std::vector<float> embedding; ///< 128-dim, L2-normalised by the model
};

} // namespace cardscanner
