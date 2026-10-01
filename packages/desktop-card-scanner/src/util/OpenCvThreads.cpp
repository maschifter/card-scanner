#include "OpenCvThreads.h"

#include <opencv2/core/utility.hpp>

#include <algorithm>
#include <thread>

namespace cardscanner {
namespace util {

namespace {
constexpr unsigned kMaxOpenCvThreads = 4;
} // namespace

void configureOpenCvThreads() {
  // hardware_concurrency() may report 0 when it cannot tell.
  const unsigned cores = std::max(1u, std::thread::hardware_concurrency());
  cv::setNumThreads(static_cast<int>(std::min(kMaxOpenCvThreads, cores)));
}

} // namespace util
} // namespace cardscanner
