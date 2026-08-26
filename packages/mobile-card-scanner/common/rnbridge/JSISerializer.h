#pragma once

#include <types/ScanResults.h>
#include <types/ScannerConfig.h>
#include <jsi/jsi.h>

namespace cardscanner {
namespace utils {

// Alias, not `using namespace facebook` - a using-directive at namespace
// scope in a header leaks onto every includer.
namespace jsi = facebook::jsi;

/**
 * @class JSISerializer
 * @brief Converts pure C++ DTOs to JSI objects
 *
 * This is the ONLY layer that should contain JSI code.
 * All business logic stays in core/ and types/ layers.
 */
class JSISerializer {
public:
  /**
   * @brief Convert ScanResult DTO to JSI object
   *
   * Delegates to NitroSerializer + the nitrogen-generated JSIConverter,
   * so this emits the same Detection shape as the frame-processor plugin.
   *
   * @param runtime JSI runtime
   * @param result Scan result DTO
   * @return JSI object
   */
  static jsi::Object serializeScanResult(jsi::Runtime &runtime,
                                         const ScanResult &result);

  static ScannerConfig parseScannerConfig(jsi::Runtime &runtime,
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
};

} // namespace utils
} // namespace cardscanner
