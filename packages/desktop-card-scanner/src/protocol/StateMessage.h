#pragma once

#include <string>

namespace cardscanner {
namespace desktop {

class ScannerService;
class ProductClient;

/**
 * @brief The whole scanner state as one JSON message, so a client that reloads
 * is current from the first message it receives.
 */
std::string stateMessage(ScannerService &service, ProductClient &products);

} // namespace desktop
} // namespace cardscanner
