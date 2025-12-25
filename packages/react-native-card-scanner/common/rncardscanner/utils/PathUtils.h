#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <string>

namespace rncardscanner {
namespace utils {

/**
 * @class PathUtils
 * @brief Utility functions for file path manipulation and URI handling
 *
 * Provides static helper methods for common path operations, particularly
 * for handling file:// URI prefixes from React Native file system APIs.
 *
 * All methods are static and stateless - no instantiation required.
 */
class PathUtils {
public:
  /**
   * @brief Strips the "file://" prefix from a path if present
   *
   * @param path The path string that may contain the file:// prefix
   * @return std::string The path without the file:// prefix
   *
   * @example
   *   stripFilePrefix("file:///path/to/file") -> "/path/to/file"
   *   stripFilePrefix("/path/to/file") -> "/path/to/file"
   */
  static std::string stripFilePrefix(const std::string &path) {
    const std::string filePrefix = "file://";
    if (path.find(filePrefix) == 0) {
      return path.substr(filePrefix.length());
    }
    return path;
  }
};

} // namespace utils
} // namespace rncardscanner

#endif // PATH_UTILS_H
