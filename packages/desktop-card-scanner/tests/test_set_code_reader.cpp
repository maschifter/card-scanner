// Checks the set-code reader end to end on straightened cards: the model reads
// the code in each layout's search box, and processSetCode() reads only when
// the top matches are ambiguous. Also checks which read the cascade and
// ScanSession keep.
//
// The end-to-end checks need apps/desktop-example/assets/models/
// SetCodeReader.onnx (Git LFS) and skip when it is missing.

#include <core/ScanSession.h>
#include <core/SetCodeProcessor.h>
#include <models/ygo/SetCodeReader.h>
#include <types/ScannerConfig.h>
#include <utils/ImageUtils.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using cardscanner::CardSearchResult;
using cardscanner::core::SetCodeProcessor;

int failures = 0;

/// Not assert(), which compiles out under NDEBUG.
void check(bool condition, const std::string &what) {
  if (condition) {
    return;
  }
  std::printf("  FAIL: %s\n", what.c_str());
  failures++;
}

cv::Mat loadRgb(const std::string &path) {
  return cardscanner::utils::ImageUtils::loadImageRGB(path);
}

/// Files are named <layout>-<printed code>.jpg.
void testReadsEveryLayout(cardscanner::SetCodeReader &reader) {
  for (const char *name :
       {"regular-RDS-EN034", "link-BLCR-EN045", "pendulum-DOCS-EN026"}) {
    const std::string file(name);
    const cv::Mat card =
        loadRgb(std::string(TEST_FIXTURE_DIR) + "/set_code/" + file + ".jpg");
    const auto dash = file.find('-');
    const std::string layout = file.substr(0, dash);
    const std::string code = file.substr(dash + 1);

    const auto info = SetCodeProcessor::read(card, reader);
    const auto &read = info.read;
    check(info.read.text == code, file + ": read " + info.read.text);
    check(info.layout == layout, file + ": layout " + info.layout);
    check(info.read.confidence > 0.5f, file + ": confident");
    check(read.steps == 64 && read.classes == 38 &&
              read.probs.size() == size_t(64 * 38),
          file + ": keeps the distribution");
  }
}

void testReadsOnlyWhenAmbiguous(cardscanner::SetCodeReader &reader) {
  const cv::Mat card = loadRgb(std::string(TEST_FIXTURE_DIR) +
                               "/set_code/regular-RDS-EN034.jpg");
  CardSearchResult first, second;
  first.score = 0.95f;
  second.score = 0.94f;
  check(!SetCodeProcessor::processSetCode(card, {first, second}, 0.05f, &reader)
             .isEmpty(),
        "reads when the top two are within the threshold");
  check(SetCodeProcessor::processSetCode(card, {first, second}, 0.005f, &reader)
            .isEmpty(),
        "skips when the top match is clear");
  check(SetCodeProcessor::processSetCode(card, {first}, 0.05f, &reader).isEmpty(),
        "skips a card with one match");
  check(SetCodeProcessor::processSetCode(card, {first, second}, 0.05f, nullptr)
            .isEmpty(),
        "skips without a reader");
}

cardscanner::CtcRead ctcRead(const std::string &text, float confidence) {
  cardscanner::CtcRead read;
  read.text = text;
  read.confidence = confidence;
  read.steps = 64;
  read.classes = 38;
  return read;
}

void testIsBetter() {
  const auto code = ctcRead("YGLD-ENA22", 0.6f);
  check(SetCodeProcessor::isBetter(code, ctcRead("YGLD", 0.99f)),
        "a code-shaped read beats a more confident one that is not");
  check(SetCodeProcessor::isBetter(ctcRead("YGLD-ENA22", 0.9f), code),
        "between code-shaped reads, the more confident wins");
  check(!SetCodeProcessor::isBetter(ctcRead("", 0.3f), code),
        "an empty read does not beat a code");
  // An empty read is confident (every step is a sure blank), so confidence
  // alone would let it beat a partial read.
  check(SetCodeProcessor::isBetter(ctcRead("-EN0986", 0.88f), ctcRead("", 0.99f)),
        "a partial read beats a more confident empty one");
  check(!SetCodeProcessor::isBetter(ctcRead("", 0.99f), ctcRead("S4-E", 0.84f)),
        "an empty read does not beat a partial one");
}

/// Review case: an ambiguous card needs 4 frames; the last one is blurry and
/// reads nothing. The session must keep the good read.
void testSessionKeepsTheBestRead() {
  cardscanner::core::ScanSession session;
  session.setMode(cardscanner::core::ScanSession::Mode::Manual);
  CardSearchResult top, runnerUp;
  top.cardId = "a";
  top.gameName = "ygo";
  top.score = 0.95;
  runnerUp.cardId = "b";
  runnerUp.gameName = "ygo";
  runnerUp.score = 0.945;
  for (const auto &[text, confidence] :
       std::vector<std::pair<std::string, float>>{{"YGLD-ENA22", 0.95f},
                                                  {"YGLD-ENA22", 0.94f},
                                                  {"YGLD-ENA22", 0.96f},
                                                  {"", 0.3f}}) {
    cardscanner::ProcessedCard card;
    card.matches = {top, runnerUp};
    card.printedSetCode.read = ctcRead(text, confidence);
    cardscanner::ScanResult result;
    result.cards.push_back(card);
    session.onScanResult(result);
  }
  const auto snapshot = session.snapshot();
  check(snapshot.candidate.has_value(), "the card is the candidate");
  check(snapshot.candidate &&
            snapshot.candidate->printedSetCode.read.text == "YGLD-ENA22",
        "a blurry last frame does not replace the read");
}

} // namespace

int main() {
  testIsBetter();
  testSessionKeepsTheBestRead();

  const std::string model = SET_CODE_MODEL;
  // An LFS pointer is a few hundred bytes of text.
  if (std::filesystem::exists(model) &&
      std::filesystem::file_size(model) >= 10000) {
    cardscanner::SetCodeReader reader(
        model, cardscanner::ScannerConfig::GameConfig{}.setCodeClassNames);
    testReadsEveryLayout(reader);
    testReadsOnlyWhenAmbiguous(reader);
  } else {
    std::printf("set code reader: end-to-end checks skipped, %s missing "
                "(git lfs pull)\n",
                model.c_str());
  }

  if (failures > 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("set code reader: all checks passed\n");
  return 0;
}
