#pragma once

#include <core/MultiScanSession.h>
#include <types/ScanResults.h>
#include "NitroDetection.hpp"
#include <string>
#include <vector>

namespace cardscanner {
namespace utils {

/**
 * @class NitroSerializer
 * @brief Converts ScanResult DTOs to the generated Nitro structs
 *
 * Single dto used for detection mapping. The frame-processor plugin returns the
 * structs directly; JSISerializer converts them to JSI with the generated
 * JSIConverter, so both paths share this serialization.
 */
class NitroSerializer {
public:
  /**
   * @brief Convert a ScanResult DTO to a NitroDetection
   *
   * @param result Scan result DTO
   * @return The populated struct
   */
  static margelo::nitro::cardscanner::NitroDetection
  serializeScanResult(const ScanResult &result);

  /// One processed card as its own detection, for the `multiCard` event.
  static margelo::nitro::cardscanner::NitroDetection
  serializeCard(const ProcessedCard &card);

  /// The frozen frame's cards before recognition (box, quad and predicted
  /// game only), for the `multiStart` event.
  static margelo::nitro::cardscanner::NitroDetection serializePlaceholders(
      const std::vector<core::MultiScanSession::Slot> &slots);

  /**
   * @brief Build a failed NitroDetection
   *
   * @param error Error message
   * @return A struct with success = false and no cards
   */
  static margelo::nitro::cardscanner::NitroDetection
  serializeError(const std::string &error);
};

} // namespace utils
} // namespace cardscanner
