#include "JSISerializer.h"
#include "../Constants.h"

namespace rncardscanner {
namespace utils {

using namespace cardscanner::constants;

jsi::Object JSISerializer::serializeScanResult(jsi::Runtime &runtime,
                                               const dto::ScanResult &result) {
  jsi::Object jsResult(runtime);

  // Metadata
  jsResult.setProperty(runtime, "cardCount",
                       jsi::Value(result.getIdentifiedCount()));
  jsResult.setProperty(runtime, "segmentationCount",
                       jsi::Value(result.getTotalDetectionCount()));
  jsResult.setProperty(runtime, "processingTime",
                       jsi::Value(result.processingTimeMs));

  // Detections array (only identified cards)
  jsi::Array detections(runtime, result.getIdentifiedCount());
  size_t detectionIndex = 0;

  for (const auto &card : result.cards) {
    if (card.isIdentified()) {
      jsi::Object jsCard = serializeProcessedCard(runtime, card);
      detections.setValueAtIndex(runtime, detectionIndex++, jsCard);
    }
  }

  jsResult.setProperty(runtime, "detections", detections);

  return jsResult;
}

dto::ScannerConfig
JSISerializer::parseScannerConfig(jsi::Runtime &runtime,
                                  const jsi::Object &configObj) {
  dto::ScannerConfig config;

  // 1. Parse Top-Level Strings
  // We use .utf8(runtime) to convert JSI String to std::string
  config.segmentationModelPath =
      configObj.getProperty(runtime, "segmentationModelPath")
          .asString(runtime)
          .utf8(runtime);

  config.embeddingModelPath =
      configObj.getProperty(runtime, "embeddingModelPath")
          .asString(runtime)
          .utf8(runtime);

  config.scanMode = configObj.getProperty(runtime, "scanMode")
                        .asString(runtime)
                        .utf8(runtime);

  // 2. Parse Thresholds (Floats)
  config.segmentationThreshold = static_cast<float>(
      configObj.getProperty(runtime, "segmentationThreshold").asNumber());

  config.iouThreshold = static_cast<float>(
      configObj.getProperty(runtime, "iouThreshold").asNumber());

  config.confidenceThreshold = static_cast<float>(
      configObj.getProperty(runtime, "confidenceThreshold").asNumber());

  // 3. Parse Search Parameters (Ints)
  config.maxMatches =
      static_cast<int>(configObj.getProperty(runtime, "maxMatches").asNumber());

  config.searchCandidates = static_cast<int>(
      configObj.getProperty(runtime, "searchCandidates").asNumber());

  // 4. Parse Optional Booleans
  // safe check: if property doesn't exist, default to false
  auto captureImageProp = configObj.getProperty(runtime, "captureImage");
  config.captureImage =
      captureImageProp.isBool() ? captureImageProp.asBool() : false;

  // 5. Parse Nested MTG Config
  // Structure: { gameSpecificConfig: { mtg: { setSymbolDetection: { ... } } } }
  auto gameSpecificProp = configObj.getProperty(runtime, "gameSpecificConfig");

  if (!gameSpecificProp.isUndefined() && gameSpecificProp.isObject()) {
    jsi::Object gameSpecificObj = gameSpecificProp.asObject(runtime);

    auto mtgProp = gameSpecificObj.getProperty(runtime, "mtg");
    if (!mtgProp.isUndefined() && mtgProp.isObject()) {
      jsi::Object mtgObj = mtgProp.asObject(runtime);

      auto setSymbolProp = mtgObj.getProperty(runtime, "setSymbolDetection");
      if (!setSymbolProp.isUndefined() && setSymbolProp.isObject()) {
        jsi::Object setSymbolObj = setSymbolProp.asObject(runtime);

        // Create the optional struct
        dto::ScannerConfig::MTGConfig mtgConfig;

        // Extract paths
        auto detPath = setSymbolObj.getProperty(runtime, "detectionModelPath");
        if (detPath.isString()) {
          mtgConfig.setSymbolDetectionModelPath =
              detPath.asString(runtime).utf8(runtime);
        }

        auto embPath = setSymbolObj.getProperty(runtime, "embeddingModelPath");
        if (embPath.isString()) {
          mtgConfig.setSymbolEmbedderModelPath =
              embPath.asString(runtime).utf8(runtime);
        }

        // Extract thresholds (with safety checks if optional in JS)
        auto detThresh =
            setSymbolObj.getProperty(runtime, "detectionThreshold");
        mtgConfig.detectionThreshold =
            detThresh.isNumber()
                ? static_cast<float>(detThresh.asNumber())
                : mtg::DEFAULT_DETECTION_THRESHOLD; // Default if missing in JS

        auto confThresh =
            setSymbolObj.getProperty(runtime, "confidenceThreshold");
        mtgConfig.confidenceThreshold =
            confThresh.isNumber()
                ? static_cast<float>(confThresh.asNumber())
                : mtg::DEFAULT_CONFIDENCE_THRESHOLD; // Default if missing in JS

        // Assign to the main config (activates the std::optional)
        config.mtgConfig = mtgConfig;
      }
    }
  }

  return config;
}

jsi::Object
JSISerializer::serializeProcessedCard(jsi::Runtime &runtime,
                                      const dto::ProcessedCard &card) {
  jsi::Object jsCard(runtime);

  // Bounding box
  jsi::Object box =
      serializeBoundingBox(runtime, card.boundingBox, card.detectionConfidence);
  jsCard.setProperty(runtime, "box", box);

  // Matches array
  if (!card.matches.empty()) {
    jsi::Array matches(runtime, card.matches.size());
    for (size_t i = 0; i < card.matches.size(); i++) {
      jsi::Object match = serializeCardMatch(runtime, card.matches[i]);
      matches.setValueAtIndex(runtime, i, match);
    }
    jsCard.setProperty(runtime, "matches", matches);
  }

  // Cropped image path (optional)
  if (!card.savedImagePath.empty()) {
    jsCard.setProperty(
        runtime, "croppedImagePath",
        jsi::String::createFromUtf8(runtime, card.savedImagePath));
  }

  // Set symbol (optional)
  if (!card.setSymbol.isEmpty()) {
    jsi::Object setSymbol = serializeSetSymbol(runtime, card.setSymbol);
    jsCard.setProperty(runtime, "setSymbol", setSymbol);
  }

  return jsCard;
}

jsi::Object JSISerializer::serializeBoundingBox(jsi::Runtime &runtime,
                                                const cv::Rect &box,
                                                float confidence) {
  jsi::Object jsBox(runtime);
  jsBox.setProperty(runtime, "x1", jsi::Value(box.x));
  jsBox.setProperty(runtime, "y1", jsi::Value(box.y));
  jsBox.setProperty(runtime, "x2", jsi::Value(box.x + box.width));
  jsBox.setProperty(runtime, "y2", jsi::Value(box.y + box.height));
  jsBox.setProperty(runtime, "conf", jsi::Value(confidence));
  return jsBox;
}

jsi::Object JSISerializer::serializeCardMatch(jsi::Runtime &runtime,
                                              const dto::CardMatch &match) {
  jsi::Object jsMatch(runtime);
  jsMatch.setProperty(runtime, "cardId",
                      jsi::String::createFromUtf8(runtime, match.cardId));
  jsMatch.setProperty(runtime, "name",
                      jsi::String::createFromUtf8(runtime, match.name));
  jsMatch.setProperty(runtime, "gameName",
                      jsi::String::createFromUtf8(runtime, match.gameName));
  jsMatch.setProperty(runtime, "score", jsi::Value(match.score));
  return jsMatch;
}

jsi::Object
JSISerializer::serializeSetSymbol(jsi::Runtime &runtime,
                                  const dto::SetSymbolInfo &setSymbol) {
  jsi::Object jsSetSymbol(runtime);
  jsSetSymbol.setProperty(
      runtime, "setCode",
      jsi::String::createFromUtf8(runtime, setSymbol.setCode));
  jsSetSymbol.setProperty(
      runtime, "setName",
      jsi::String::createFromUtf8(runtime, setSymbol.setName));
  jsSetSymbol.setProperty(
      runtime, "variant",
      jsi::String::createFromUtf8(runtime, setSymbol.variant));
  jsSetSymbol.setProperty(runtime, "similarity",
                          jsi::Value(setSymbol.similarity));
  return jsSetSymbol;
}

} // namespace utils
} // namespace rncardscanner
