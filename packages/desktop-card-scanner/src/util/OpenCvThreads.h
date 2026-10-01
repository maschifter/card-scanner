#pragma once

namespace cardscanner {
namespace util {

/**
 * @brief Sizes OpenCV's parallel_for pool for the scanner process.
 *
 * The pool is deliberately small. It only serves the CPU stages around
 * inference (mask upsampling, dewarp, letterbox), which are memory-bound and
 * stop scaling after a few threads: on a 12-core machine 4 threads take the
 * YOLO stage on 12 Mpx stills from 9.1 ms to 6.7 ms and 12 threads gain
 * nothing more. Staying small also keeps the pool from competing with the
 * inference runtime's own threads when a model runs on CPU.
 */
void configureOpenCvThreads();

} // namespace util
} // namespace cardscanner
