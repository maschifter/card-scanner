#pragma once
#include <string_view>

namespace cardscanner {
namespace util {

/// One timestamped, tag-prefixed line on stderr: "12:34:56.789 [frame] ...".
/// Thread-safe; exists so card-scanner-server.log can be read after the fact.
/// Server-side only - the OBS module logs through blog().
void logLine(std::string_view tag, std::string_view message);

} // namespace util
} // namespace cardscanner
