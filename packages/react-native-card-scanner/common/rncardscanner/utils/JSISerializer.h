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
   * Matches the Detection TypeScript interface:
   * {
   *   success: boolean,
   *   cards: DetectedCard[],
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

  /**
   * @brief Serialize database info to JSI object
   *
   * Matches DatabaseInfo interface:
   * {
   *   gameName: string,
   *   path: string,
   *   cardCount: number,
   *   creationTimestamp: string,
   *   fileSize?: number
   * }
   *
   * @param runtime JSI runtime
   * @param gameName Game identifier
   * @param path Database file path
   * @param cardCount Number of cards
   * @param creationTimestamp Creation timestamp
   * @param fileSize File size in bytes (optional)
   * @return JSI object
   */
  static jsi::Object serializeDatabaseInfo(jsi::Runtime &runtime,
                                           const std::string &gameName,
                                           const std::string &path,
                                           uint64_t cardCount,
                                           const std::string &creationTimestamp,
                                           long fileSize = 0);

  /**
   * @brief Serialize operation result to JSI object
   *
   * Matches SwapResult/DeleteResult interface:
   * {
   *   success: boolean,
   *   error?: string
   * }
   *
   * @param runtime JSI runtime
   * @param success Whether operation succeeded
   * @param error Error message if failed
   * @return JSI object
   */
  static jsi::Object serializeOperationResult(jsi::Runtime &runtime,
                                              bool success,
                                              const std::string &error = "");

private:
  /**
   * @brief Serialize single ProcessedCard to JSI object
   *
   * Matches DetectedCard interface:
   * {
   *   cardId: string,
   *   name: string,
   *   gameName: string,
   *   confidenceScore: number,
   *   boundingBox: BoundingBox,
   *   alternativeCards: AlternativeMatch[],
   *   capturedImage?: CapturedImage,
   *   setSymbol?: SetSymbolInfo,
   *   fabColor?: FABColorInfo
   * }
   *
   * @param runtime JSI runtime
   * @param card Processed card DTO
   * @return JSI object
   */
  static jsi::Object serializeDetectedCard(jsi::Runtime &runtime,
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
   * @brief Serialize set symbol info to JSI object
   *
   * {setCode, similarity}
   *
   * @param runtime JSI runtime
   * @param setSymbol Set symbol DTO
   * @return JSI object
   */
  static jsi::Object serializeSetSymbol(jsi::Runtime &runtime,
                                        const dto::SetSymbolInfo &setSymbol);

  /**
   * @brief Serialize FAB color info to JSI object
   *
   * {color, similarity}
   *
   * @param runtime JSI runtime
   * @param fabColor FAB color DTO
   * @return JSI object
   */
  static jsi::Object serializeFABColor(jsi::Runtime &runtime,
                                       const dto::FABColorInfo &fabColor);
};

} // namespace utils
} // namespace rncardscanner
