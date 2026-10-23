// Checks core's config parser, the one both hosts feed: defaults survive
// missing keys, wrong types throw, and unread keys are reported by full path.
//
// Builds ConfigValue trees by hand, so it covers what the mobile JSI converter
// produces as much as what the desktop JSON converter does.

#include <config/ScannerConfigLoader.h>
#include <types/ScannerConfig.h>

#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using cardscanner::ConfigValue;
using cardscanner::parseScannerConfig;

int failures = 0;

/// Not assert(), which compiles out under NDEBUG.
void check(bool condition, const std::string &what) {
  if (condition) {
    return;
  }
  std::printf("  FAIL: %s\n", what.c_str());
  failures++;
}

ConfigValue object(ConfigValue::Object members) { return {std::move(members)}; }
ConfigValue text(const char *value) { return {std::string(value)}; }

/// The message parseScannerConfig throws, or "" when it does not throw.
std::string errorOf(const ConfigValue &root) {
  try {
    parseScannerConfig(root);
  } catch (const std::runtime_error &e) {
    return e.what();
  }
  return "";
}

void testMissingKeysKeepDefaults() {
  const auto parsed = parseScannerConfig(object({}));
  const cardscanner::ScannerConfig defaults;
  check(parsed.config.maxMatches == defaults.maxMatches, "maxMatches default");
  check(parsed.config.scanMode == defaults.scanMode, "scanMode default");
  check(parsed.config.useSidewaysFlipCache == defaults.useSidewaysFlipCache,
        "useSidewaysFlipCache default");
  check(parsed.unknownKeys.empty(), "an empty document reports nothing");
}

void testReadsEveryType() {
  const auto parsed = parseScannerConfig(object({
      {"scanMode", text("auto")},
      {"segmentationThreshold", {0.5}},
      {"maxMatches", {7.0}},
      {"captureImage", {true}},
      {"lowLightGamma", {1.5}},
      {"useDetectionSelection", {}}, // null keeps the default
      {"gameClassMapping",
       object({{"0", text("mtg")},
               {"1", {ConfigValue::Array{text("fab"), text("fab2")}}}})},
      {"gameSpecificConfig",
       object({{"mtg", object({{"confidenceThreshold", {0.7}},
                               {"setSymbolDetection",
                                object({{"imageSize", {320.0}}})}})},
               {"fab", object({{"colorDetection",
                                object({{"modelPath", text("c.onnx")}})}})}})},
  }));
  const auto &config = parsed.config;
  check(config.scanMode == "auto", "string field");
  check(config.segmentationThreshold == 0.5f, "float field");
  check(config.maxMatches == 7, "int field");
  check(config.captureImage, "bool field");
  check(config.lowLightGamma == 1.5, "double field");
  check(config.useDetectionSelection, "null keeps the default");
  check(config.gameClassMapping.at(0) == std::vector<std::string>{"mtg"},
        "class mapped to one game");
  check(config.gameClassMapping.at(1) ==
            std::vector<std::string>{"fab", "fab2"},
        "class mapped to several games");

  const auto &mtg = config.gameSpecificConfig.at("mtg");
  check(mtg.confidenceThreshold == 0.7f, "optional game field set");
  check(config.confidenceThresholdFor("mtg") == 0.7f,
        "game confidenceThreshold override");
  check(config.confidenceThresholdFor("fab") == config.confidenceThreshold,
        "game without a confidenceThreshold override uses the scanner-wide one");
  check(mtg.setSymbolImageSize == 320, "nested section field");
  const auto &fab = config.gameSpecificConfig.at("fab");
  check(!fab.confidenceThreshold.has_value(), "optional game field absent");
  check(fab.colorDetectionModelPath == "c.onnx", "second nested section");
  check(parsed.unknownKeys.empty(), "every key was read");
}

void testSetCodeReading() {
  const auto parsed = parseScannerConfig(object({
      {"disambiguationThreshold", {0.02}},
      {"gameSpecificConfig",
       object({{"//ygo", text("a comment, not a game")},
               {"ygo-tcg",
                object({{"disambiguationThreshold", {0.05}},
                        {"setCodeReading",
                         object({{"modelPath", text("SetCodeReader.onnx")}})}})},
               {"mtg", object({})}})},
  }));
  const auto &config = parsed.config;
  const auto &ygo = config.gameSpecificConfig.at("ygo-tcg");
  check(ygo.hasSetCodeReading(), "set-code reading configured");
  check(ygo.setCodeModelPath == "SetCodeReader.onnx", "set-code model path");
  check(config.disambiguationThresholdFor("ygo-tcg") == 0.05f,
        "game disambiguationThreshold override");
  check(config.disambiguationThresholdFor("mtg") == 0.02f,
        "game without an override uses the scanner-wide threshold");
  check(config.disambiguationThresholdFor("lorcana") == 0.02f,
        "game without a config uses the scanner-wide threshold");
  check(!config.gameSpecificConfig.at("mtg").hasSetCodeReading(),
        "set-code reading absent");
  check(!config.gameSpecificConfig.count("//ygo"),
        "a // key in gameSpecificConfig is a comment");
  check(parsed.unknownKeys.empty(), "every set-code key was read");
}

/// setCodeReading.classNames parses, defaults to the repo model's, and must
/// start with the CTC blank and list single, distinct characters after it.
void testSetCodeClassNames() {
  using cardscanner::ScannerConfig;
  ScannerConfig config;
  config.segmentationModelPath = "seg.onnx";
  config.embeddingModelPath = "embed.onnx";
  config.gameClassMapping = {{0, {"ygo"}}};
  auto &ygo = config.gameSpecificConfig["ygo"];
  ygo.setCodeModelPath = "SetCodeReader.onnx";
  check(ygo.setCodeClassNames.size() == 38 &&
            ygo.setCodeClassNames.front() == "blank",
        "set-code class names default to the repo model's");
  check(config.validate().empty(),
        "the default set-code class names are valid: " + config.validate());
  ygo.setCodeClassNames = {};
  check(!config.validate().empty(), "set-code class names must not be empty");

  ygo.setCodeClassNames = {"-", "0"};
  check(!config.validate().empty(), "set-code class names need the blank first");
  ygo.setCodeClassNames = {"blank", "AB"};
  check(!config.validate().empty(),
        "set-code class names after the blank are one character");
  ygo.setCodeClassNames = {"blank", "A", "A"};
  check(!config.validate().empty(), "set-code class names are distinct");

  const auto parsed = parseScannerConfig(object(
      {{"gameSpecificConfig",
        object({{"ygo",
                 object({{"setCodeReading",
                          object({{"modelPath", text("m.onnx")},
                                  {"classNames",
                                   {ConfigValue::Array{text("blank"),
                                                       text("A")}}}})}})}})}}));
  check(parsed.config.gameSpecificConfig.at("ygo").setCodeClassNames ==
            std::vector<std::string>{"blank", "A"},
        "set-code class names parse");
}

/// The desktop example ships its config; it must load and validate.
void testExampleConfigLoads() {
  try {
    const auto loaded = cardscanner::desktop::loadConfigFile(EXAMPLE_CONFIG);
    const std::string error = loaded.scanner.validate();
    check(error.empty(), "example config validates: " + error);
  } catch (const std::exception &e) {
    check(false, std::string("example config loads: ") + e.what());
  }
}

void testReportsUnknownKeys() {
  const auto parsed = parseScannerConfig(
      object({
          {"maxMatchs", {5.0}},
          {"// note", text("comments are skipped")},
          {"modelsDir", text("host key")},
          {"gameSpecificConfig",
           object({{"mtg", object({{"typo", {1.0}},
                                   {"setSymbolDetection",
                                    object({{"bogus", {1.0}}})}})}})},
      }),
      {"modelsDir"});
  check(parsed.unknownKeys ==
            std::vector<std::string>{
                "gameSpecificConfig.mtg.setSymbolDetection.bogus",
                "gameSpecificConfig.mtg.typo", "maxMatchs"},
        "unknown keys by full path, host keys and comments excluded");
}

void testWrongTypesThrow() {
  check(errorOf(object({{"maxMatches", text("five")}})) ==
            "config: 'maxMatches' type must be number, but is string",
        "number field given a string");
  check(errorOf(object({{"captureImage", {1.0}}})) ==
            "config: 'captureImage' type must be boolean, but is number",
        "bool field given a number");
  check(errorOf(object({{"gameSpecificConfig",
                         object({{"mtg", object({{"confidenceThreshold",
                                                  text("high")}})}})}})) ==
            "config: 'gameSpecificConfig.mtg.confidenceThreshold' type must "
            "be number, but is string",
        "nested error names the full path");
  check(errorOf(object({{"gameClassMapping", object({{"x", text("mtg")}})}})) ==
            "config: gameClassMapping key 'x' is not an integer class id",
        "non-integer class id");
  check(errorOf(object({{"gameClassMapping",
                         object({{"0", {ConfigValue::Array{{1.0}}}}})}})) ==
            "config: gameClassMapping['0'] must be a string or array of "
            "strings",
        "non-string game name");
  check(errorOf(object({{"gameSpecificConfig", object({{"mtg", {1.0}}})}})) ==
            "config: gameSpecificConfig['mtg'] must be an object",
        "game config that is not an object");
  check(!errorOf(text("not an object")).empty(), "root must be an object");
}

} // namespace

int main() {
  testMissingKeysKeepDefaults();
  testReadsEveryType();
  testSetCodeReading();
  testSetCodeClassNames();
  testExampleConfigLoads();
  testReportsUnknownKeys();
  testWrongTypesThrow();

  if (failures > 0) {
    std::printf("%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("config parse: all checks passed\n");
  return 0;
}
