#pragma once

#include "../dto/ScanResults.h"
#include "../dto/ScannerConfig.h"
#include <jsi/jsi.h>

namespace rncardscanner {
namespace utils {

using namespace facebook;

/**
 * @class JSISerializer
 * @brief Converts pure C++ DTOs to JSI objects
 *
 * This is the ONLY layer that should contain JSI code.
 * All business logic stays in core/ and dto/ layers.
 */
class JSISerializer {
public:
  /**
   * @brief Convert ScanResult DTO to JSI object
   *
   * Matches the RawScanResult TypeScript interface:
   * {
   *   cardCount: number,
   *   segmentationCount: number,
   *   detections: RawDetection[],
   *   processingTime: number
   * }
   *
   * @param runtime JSI runtime
   * @param result Scan result DTO
   * @return JSI object
   */
  static jsi::Object serializeScanResult(jsi::Runtime &runtime,
                                         const dto::ScanResult &result);

  static dto::ScannerConfig parseScannerConfig(jsi::Runtime &runtime,
                                               const jsi::Object &configObj);

private:
  /**
   * @brief Serialize single ProcessedCard to JSI object
   *
   * Matches RawDetection interface:
   * {
   *   box: BoundingBox,
   *   matches: RawMatch[],
   *   croppedImagePath?: string,
   *   setSymbol?: SetSymbol
   * }
   *
   * @param runtime JSI runtime
   * @param card Processed card DTO
   * @return JSI object
   */
  static jsi::Object serializeProcessedCard(jsi::Runtime &runtime,
                                            const dto::ProcessedCard &card);

  /**
   * @brief Serialize bounding box to JSI object
   *
   * {x1, y1, x2, y2, conf}
   *
   * @param runtime JSI runtime
   * @param box Bounding box rectangle
   * @param confidence Detection confidence
   * @return JSI object
   */
  static jsi::Object serializeBoundingBox(jsi::Runtime &runtime,
                                          const cv::Rect &box,
                                          float confidence);

  /**
   * @brief Serialize card match to JSI object
   *
   * {cardId, name, gameName, score}
   *
   * @param runtime JSI runtime
   * @param match Card match DTO
   * @return JSI object
   */
  static jsi::Object serializeCardMatch(jsi::Runtime &runtime,
                                        const dto::CardMatch &match);

  /**
   * @brief Serialize set symbol info to JSI object
   *
   * {setCode, setName, variant, similarity}
   *
   * @param runtime JSI runtime
   * @param setSymbol Set symbol DTO
   * @return JSI object
   */
  static jsi::Object serializeSetSymbol(jsi::Runtime &runtime,
                                        const dto::SetSymbolInfo &setSymbol);
};

} // namespace utils
} // namespace rncardscanner
