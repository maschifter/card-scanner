#pragma once

#include "../types/ScanResults.h"
#include <opencv2/core.hpp>
#include <vector>

namespace cardscanner {

class SetCodeReader;

namespace core {

/**
 * @class SetCodeProcessor
 * @brief Reads the printed set code (Yu-Gi-Oh!: YGLD-ENA22) that tells apart
 * printings of the same artwork.
 *
 * Tries the search boxes where card layouts print the code, in order: regular
 * (95% of cards), the regular box re-centered on the text box's top border
 * (corrects a card straightened a few percent too high or low), Link, then
 * Pendulum. Stops at the first read shaped like a set code that stays clear of
 * the box's edges; a read touching one is a code the box clipped. When no read
 * qualifies, it keeps the best: code-shaped first, then any text, then the
 * most confident.
 */
class SetCodeProcessor {
public:
  /**
   * @brief Reads the set code when the top matches are ambiguous.
   *
   * @param cardImage The straightened card (RGB), in the orientation that
   *   matched; scaled to card::DEWARP_HEIGHT internally
   * @param cardMatches Recognition results, to check ambiguity
   * @param disambiguationThreshold The top-two score gap that still counts as
   *   ambiguous
   * @param reader The top match's game's reader (nullable)
   * @return SetCodeInfo, empty when not ambiguous or the reader is missing
   */
  static SetCodeInfo
  processSetCode(const cv::Mat &cardImage,
                 const std::vector<CardSearchResult> &cardMatches,
                 float disambiguationThreshold, SetCodeReader *reader);

  /**
   * @brief Whether `read` beats `than`: a code-shaped read beats any other
   * text, any text beats an empty read, and within that the more confident
   * one wins. Picks the read to keep across search boxes and across frames.
   */
  static bool isBetter(const CtcRead &read, const CtcRead &than);

  /// Reads the set code unconditionally. @see processSetCode
  static SetCodeInfo read(const cv::Mat &rgbCard, SetCodeReader &reader);

  /**
   * @brief The text box's top border: the strongest long horizontal line in
   * the band where it sits.
   * @param gray 8-bit gray card, any size
   * @return Its height as a fraction of the card, or a negative value when
   *   no such line was found
   */
  static float textBoxTop(const cv::Mat &gray);
};

} // namespace core
} // namespace cardscanner
