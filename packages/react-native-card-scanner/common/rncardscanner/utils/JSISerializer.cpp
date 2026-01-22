#include "JSISerializer.h"
#include "../Constants.h"
#include <iostream>

namespace rncardscanner {
namespace utils {

using namespace rncardscanner::constants;

jsi::Object JSISerializer::serializeScanResult(jsi::Runtime &runtime,
                                               const dto::ScanResult &result) {
  jsi::Object jsResult(runtime);

  // Success flag
  jsResult.setProperty(runtime, "success", jsi::Value(true));

  // Processing time
  jsResult.setProperty(runtime, "processingTime",
                       jsi::Value(result.processingTimeMs));

  // Cards array (all detected cards)
  jsi::Array cards(runtime, result.cards.size());
  size_t cardIndex = 0;

  for (const auto &card : result.cards) {
    jsi::Object jsCard = serializeDetectedCard(runtime, card);
    cards.setValueAtIndex(runtime, cardIndex++, jsCard);
  }

  jsResult.setProperty(runtime, "cards", cards);

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

  // Parse disambiguation threshold (optional, defaults to 2%)
  auto disambiguationThresholdProp =
      configObj.getProperty(runtime, "disambiguationThreshold");
  config.disambiguationThreshold =
      disambiguationThresholdProp.isNumber()
          ? static_cast<float>(disambiguationThresholdProp.asNumber())
          : dto::ScannerConfig::DEFAULT_DISAMBIGUATION_THRESHOLD;

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

  // 4b. Parse blur threshold (optional, defaults to 100)
  auto blurThresholdProp = configObj.getProperty(runtime, "blurThreshold");
  config.blurThreshold = blurThresholdProp.isNumber()
                             ? static_cast<double>(blurThresholdProp.asNumber())
                             : dto::ScannerConfig::DEFAULT_BLUR_THRESHOLD;

  // 4c. Parse low light threshold (optional, defaults to 65)
  auto lowLightThresholdProp =
      configObj.getProperty(runtime, "lowLightThreshold");
  config.lowLightThreshold =
      lowLightThresholdProp.isNumber()
          ? static_cast<double>(lowLightThresholdProp.asNumber())
          : dto::ScannerConfig::DEFAULT_LOW_LIGHT_THRESHOLD;

  // 4d. Parse low light gamma (optional, defaults to 2.0)
  auto lowLightGammaProp = configObj.getProperty(runtime, "lowLightGamma");
  config.lowLightGamma = lowLightGammaProp.isNumber()
                             ? static_cast<double>(lowLightGammaProp.asNumber())
                             : dto::ScannerConfig::DEFAULT_LOW_LIGHT_GAMMA;

  // 4e. Parse max frame rate (optional, defaults to 5)
  auto maxFrameRateProp = configObj.getProperty(runtime, "maxFrameRate");
  config.maxFrameRate = maxFrameRateProp.isNumber()
                            ? static_cast<int>(maxFrameRateProp.asNumber())
                            : dto::ScannerConfig::DEFAULT_MAX_FRAME_RATE;

  // 4f. Parse game class mapping (optional)
  auto gameClassMappingProp =
      configObj.getProperty(runtime, "gameClassMapping");
  if (!gameClassMappingProp.isUndefined() &&
      gameClassMappingProp.isObject()) {
    jsi::Object mappingObj = gameClassMappingProp.asObject(runtime);
    auto propertyNames = mappingObj.getPropertyNames(runtime);
    size_t count = propertyNames.size(runtime);

    for (size_t i = 0; i < count; i++) {
      jsi::String propName =
          propertyNames.getValueAtIndex(runtime, i).asString(runtime);
      std::string keyStr = propName.utf8(runtime);

      try {
        int classId = std::stoi(keyStr);
        jsi::Value value = mappingObj.getProperty(runtime, propName);
        if (value.isString()) {
          std::string gameName = value.asString(runtime).utf8(runtime);
          config.gameClassMapping[classId] = gameName;
        }
      } catch (const std::exception &e) {
        // Skip invalid entries
      }
    }
  }

  // 5. Parse gameSpecificConfig map
  // Structure: { gameSpecificConfig: { [gameName]: { ... } } }
  auto gameSpecificProp = configObj.getProperty(runtime, "gameSpecificConfig");

  if (!gameSpecificProp.isUndefined() && gameSpecificProp.isObject()) {
    jsi::Object gameSpecificObj = gameSpecificProp.asObject(runtime);
    auto gameNames = gameSpecificObj.getPropertyNames(runtime);
    size_t gameCount = gameNames.size(runtime);

    for (size_t i = 0; i < gameCount; i++) {
      jsi::String gameNameProp =
          gameNames.getValueAtIndex(runtime, i).asString(runtime);
      std::string gameName = gameNameProp.utf8(runtime);

      auto gameConfigProp = gameSpecificObj.getProperty(runtime, gameNameProp);
      if (!gameConfigProp.isUndefined() && gameConfigProp.isObject()) {
        jsi::Object gameConfigObj = gameConfigProp.asObject(runtime);
        dto::ScannerConfig::GameConfig gameConfig;

        // Parse game-specific embedding model (optional)
        auto embeddingModelPathProp =
            gameConfigObj.getProperty(runtime, "embeddingModelPath");
        if (embeddingModelPathProp.isString()) {
          gameConfig.embeddingModelPath =
              embeddingModelPathProp.asString(runtime).utf8(runtime);
        }

        // Parse setSymbolDetection config (MTG-specific, optional)
        auto setSymbolProp =
            gameConfigObj.getProperty(runtime, "setSymbolDetection");
        if (!setSymbolProp.isUndefined() && setSymbolProp.isObject()) {
          jsi::Object setSymbolObj = setSymbolProp.asObject(runtime);

          auto detPath =
              setSymbolObj.getProperty(runtime, "detectionModelPath");
          if (detPath.isString()) {
            gameConfig.setSymbolDetectionModelPath =
                detPath.asString(runtime).utf8(runtime);
          }

          auto embPath =
              setSymbolObj.getProperty(runtime, "embeddingModelPath");
          if (embPath.isString()) {
            gameConfig.setSymbolEmbedderModelPath =
                embPath.asString(runtime).utf8(runtime);
          }

          auto detThresh =
              setSymbolObj.getProperty(runtime, "detectionThreshold");
          if (detThresh.isNumber()) {
            gameConfig.setSymbolDetectionThreshold =
                static_cast<float>(detThresh.asNumber());
          }

          auto confThresh =
              setSymbolObj.getProperty(runtime, "confidenceThreshold");
          if (confThresh.isNumber()) {
            gameConfig.setSymbolConfidenceThreshold =
                static_cast<float>(confThresh.asNumber());
          }
        }

        // Parse colorDetection config (FAB-specific, optional)
        auto colorDetectionProp =
            gameConfigObj.getProperty(runtime, "colorDetection");
        if (!colorDetectionProp.isUndefined() &&
            colorDetectionProp.isObject()) {
          jsi::Object colorDetectionObj = colorDetectionProp.asObject(runtime);

          auto modelPath = colorDetectionObj.getProperty(runtime, "modelPath");
          if (modelPath.isString()) {
            gameConfig.colorDetectionModelPath =
                modelPath.asString(runtime).utf8(runtime);
          }

          auto dotsRegionRatioProp =
              colorDetectionObj.getProperty(runtime, "dotsRegionRatio");
          if (dotsRegionRatioProp.isNumber()) {
            gameConfig.dotsRegionRatio =
                static_cast<double>(dotsRegionRatioProp.asNumber());
          }

          auto minDotsRegionSizeProp =
              colorDetectionObj.getProperty(runtime, "minDotsRegionSize");
          if (minDotsRegionSizeProp.isNumber()) {
            gameConfig.minDotsRegionSize =
                static_cast<int>(minDotsRegionSizeProp.asNumber());
          }
        }

        // Add to config map
        config.gameSpecificConfig[gameName] = gameConfig;
      }
    }
  }
  return config;
}

jsi::Object
JSISerializer::serializeDetectedCard(jsi::Runtime &runtime,
                                     const dto::ProcessedCard &card) {
  jsi::Object jsCard(runtime);

  // Primary match fields (flattened from matches[0])
  if (!card.matches.empty()) {
    const auto &primaryMatch = card.matches[0];
    jsCard.setProperty(
        runtime, "cardId",
        jsi::String::createFromUtf8(runtime, primaryMatch.cardId));
    jsCard.setProperty(
        runtime, "gameName",
        jsi::String::createFromUtf8(runtime, primaryMatch.gameName));
    jsCard.setProperty(runtime, "confidenceScore",
                       jsi::Value(primaryMatch.score));
  }

  // Always include predicted game name if available
  if (!card.predictedGameName.empty()) {
    jsCard.setProperty(
        runtime, "predictedGameName",
        jsi::String::createFromUtf8(runtime, card.predictedGameName));
  }

  // Bounding box
  jsi::Object box =
      serializeBoundingBox(runtime, card.boundingBox, card.detectionConfidence);
  jsCard.setProperty(runtime, "boundingBox", box);

  // Alternative cards (matches[1:])
  if (card.matches.size() > 1) {
    jsi::Array altCards(runtime, card.matches.size() - 1);
    for (size_t i = 1; i < card.matches.size(); i++) {
      jsi::Object altCard(runtime);
      const auto &match = card.matches[i];
      altCard.setProperty(runtime, "cardId",
                          jsi::String::createFromUtf8(runtime, match.cardId));
      altCard.setProperty(runtime, "confidence", jsi::Value(match.score));
      altCards.setValueAtIndex(runtime, i - 1, altCard);
    }
    jsCard.setProperty(runtime, "alternativeCards", altCards);
  } else {
    // Empty array if no alternatives
    jsi::Array emptyArray(runtime, 0);
    jsCard.setProperty(runtime, "alternativeCards", emptyArray);
  }

  // Captured image (optional)
  if (!card.savedImagePath.empty()) {
    jsi::Object capturedImage(runtime);
    capturedImage.setProperty(
        runtime, "uri",
        jsi::String::createFromUtf8(runtime, card.savedImagePath));
    capturedImage.setProperty(runtime, "width", jsi::Value(card.imageWidth));
    capturedImage.setProperty(runtime, "height", jsi::Value(card.imageHeight));
    capturedImage.setProperty(
        runtime, "size", jsi::Value(static_cast<double>(card.imageFileSize)));
    jsCard.setProperty(runtime, "capturedImage", capturedImage);
  }

  // Set symbol (optional, MTG only)
  if (!card.setSymbol.isEmpty()) {
    jsi::Object setSymbol = serializeSetSymbol(runtime, card.setSymbol);
    jsCard.setProperty(runtime, "setSymbol", setSymbol);
  }

  // FAB color (optional, FAB only)
  if (!card.fabColor.isEmpty()) {
    jsi::Object fabColor = serializeFABColor(runtime, card.fabColor);
    jsCard.setProperty(runtime, "fabColor", fabColor);
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

jsi::Object
JSISerializer::serializeSetSymbol(jsi::Runtime &runtime,
                                  const dto::SetSymbolInfo &setSymbol) {
  jsi::Object jsSetSymbol(runtime);
  jsSetSymbol.setProperty(
      runtime, "setCode",
      jsi::String::createFromUtf8(runtime, setSymbol.setCode));
  jsSetSymbol.setProperty(runtime, "similarity",
                          jsi::Value(setSymbol.similarity));
  return jsSetSymbol;
}

jsi::Object
JSISerializer::serializeFABColor(jsi::Runtime &runtime,
                                 const dto::FABColorInfo &fabColor) {
  jsi::Object jsFABColor(runtime);
  jsFABColor.setProperty(runtime, "color",
                         jsi::String::createFromUtf8(runtime, fabColor.color));
  jsFABColor.setProperty(runtime, "similarity",
                         jsi::Value(fabColor.similarity));
  return jsFABColor;
}

jsi::Object JSISerializer::serializeDatabaseInfo(
    jsi::Runtime &runtime, const std::string &gameName, const std::string &path,
    uint64_t cardCount, const std::string &creationTimestamp, long fileSize) {
  jsi::Object dbInfo(runtime);
  dbInfo.setProperty(runtime, "gameName",
                     jsi::String::createFromUtf8(runtime, gameName));
  dbInfo.setProperty(runtime, "path",
                     jsi::String::createFromUtf8(runtime, path));
  dbInfo.setProperty(runtime, "cardCount",
                     jsi::Value(static_cast<double>(cardCount)));
  dbInfo.setProperty(runtime, "creationTimestamp",
                     jsi::String::createFromUtf8(runtime, creationTimestamp));
  if (fileSize > 0) {
    dbInfo.setProperty(runtime, "fileSize",
                       jsi::Value(static_cast<double>(fileSize)));
  }
  return dbInfo;
}

jsi::Object JSISerializer::serializeOperationResult(jsi::Runtime &runtime,
                                                    bool success,
                                                    const std::string &error) {
  jsi::Object result(runtime);
  result.setProperty(runtime, "success", jsi::Value(success));
  if (!success && !error.empty()) {
    result.setProperty(runtime, "error",
                       jsi::String::createFromUtf8(runtime, error));
  }
  return result;
}

} // namespace utils
} // namespace rncardscanner
