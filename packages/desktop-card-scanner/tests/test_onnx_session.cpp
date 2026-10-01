// Checks the inference seam: the ONNX backend loads a model, moves floats
// through it, and hands back correctly-shaped tensors.
//
// Asserts the InferenceSession contract, not recognition accuracy, so it runs
// without the card models or databases present.

#include <inference/InferenceSession.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

/// Not assert(), which compiles out under NDEBUG.
void check(bool condition, const std::string &what) {
  if (condition) {
    return;
  }
  std::printf("  FAIL: %s\n", what.c_str());
  failures++;
}

void checkClose(float actual, float expected, const std::string &what) {
  check(std::fabs(actual - expected) < 1e-5f,
        what + " (got " + std::to_string(actual) + ", expected " +
            std::to_string(expected) + ")");
}

void testRunsAndShapesOut() {
  auto session = cardscanner::inference::loadSession(
      std::string(TEST_FIXTURE_DIR) + "/two_output.onnx");
  check(session != nullptr, "loadSession returned a session");

  // 1..12, so a wrong stride or a transposed copy shows up as wrong values.
  std::vector<float> input(12);
  for (size_t i = 0; i < input.size(); i++) {
    input[i] = static_cast<float>(i + 1);
  }

  const auto outputs = session->run(input.data(), {1, 3, 2, 2});

  check(outputs.size() == 2, "model's two outputs both come back");
  if (outputs.size() != 2) {
    return;
  }

  // scaled = input * 2, shape preserved.
  check(outputs[0].shape == std::vector<int64_t>({1, 3, 2, 2}),
        "output 0 keeps its 4-D shape");
  check(outputs[0].data.size() == 12, "output 0 has 12 elements");
  for (size_t i = 0; i < outputs[0].data.size(); i++) {
    checkClose(outputs[0].data[i], input[i] * 2.0f,
               "output 0 element " + std::to_string(i));
  }

  // flat = reshape(input, [1,12]): a different rank from output 0, which
  // catches a backend that assumes every output matches the input.
  check(outputs[1].shape == std::vector<int64_t>({1, 12}),
        "output 1 has its own 2-D shape");
  check(outputs[1].data.size() == 12, "output 1 has 12 elements");
  for (size_t i = 0; i < outputs[1].data.size(); i++) {
    checkClose(outputs[1].data[i], input[i],
               "output 1 element " + std::to_string(i));
  }
}

void testInputBufferIsNotRetained() {
  // The contract says the input is borrowed, not retained. Run twice with the
  // buffer mutated in between; a backend that captured the pointer and lazily
  // re-read it would return the first result twice.
  auto session = cardscanner::inference::loadSession(
      std::string(TEST_FIXTURE_DIR) + "/two_output.onnx");

  std::vector<float> input(12, 1.0f);
  const auto first = session->run(input.data(), {1, 3, 2, 2});

  std::fill(input.begin(), input.end(), 3.0f);
  const auto second = session->run(input.data(), {1, 3, 2, 2});

  checkClose(first[0].data[0], 2.0f, "first run saw 1.0");
  checkClose(second[0].data[0], 6.0f, "second run saw the mutated 3.0");
}

void testMissingModelThrows() {
  bool threw = false;
  try {
    auto session = cardscanner::inference::loadSession(
        std::string(TEST_FIXTURE_DIR) + "/does-not-exist.onnx");
  } catch (const std::exception &e) {
    threw = true;
    // Five models load at init, so the message has to name the path.
    check(std::string(e.what()).find("does-not-exist.onnx") != std::string::npos,
          "load failure names the offending path");
  }
  check(threw, "loading a missing model throws");
}

void testFilePrefixIsStripped() {
  // Mobile's paths carry a file:// prefix and a desktop config may too.
  bool ok = true;
  try {
    auto session = cardscanner::inference::loadSession(
        "file://" + std::string(TEST_FIXTURE_DIR) + "/two_output.onnx");
  } catch (const std::exception &e) {
    ok = false;
    std::printf("  (%s)\n", e.what());
  }
  check(ok, "a file:// prefixed path loads");
}

} // namespace

int main() {
  std::printf("test_onnx_session\n");
  try {
    testRunsAndShapesOut();
    testInputBufferIsNotRetained();
    testMissingModelThrows();
    testFilePrefixIsStripped();
  } catch (const std::exception &e) {
    // Usually a missing fixture. Report it rather than aborting on SIGABRT.
    std::printf("  FAIL: unexpected exception: %s\n", e.what());
    return 1;
  }

  if (failures > 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
