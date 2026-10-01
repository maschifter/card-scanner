#include "JSISerializer.h"
#include <Constants.h>
#include "NitroSerializer.h"

namespace cardscanner {
namespace utils {

using namespace cardscanner::constants;

jsi::Object JSISerializer::serializeScanResult(jsi::Runtime &runtime,
                                               const ScanResult &result) {
  // Reuse the dto. Nitro struct mapping and the nitrogen-generated
  // struct -> JSI conversion so both paths share one serialization.
  auto detection = NitroSerializer::serializeScanResult(result);
  return margelo::nitro::JSIConverter<
             margelo::nitro::cardscanner::NitroDetection>::toJSI(runtime,
                                                                 detection)
      .asObject(runtime);
}

ScannerConfig
JSISerializer::parseScannerConfig(jsi::Runtime &runtime,
                                  const jsi::Object &configObj) {
  ScannerConfig config;

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

  // Optional numbers keep their ScannerConfig default when absent
  const auto numberOr = [&](const char *name, double fallback) {
    auto prop = configObj.getProperty(runtime, name);
    return prop.isNumber() ? prop.asNumber() : fallback;
  };
  config.disambiguationThreshold = static_cast<float>(
      numberOr("disambiguationThreshold", config.disambiguationThreshold));
  config.minGameConfidence = static_cast<float>(
      numberOr("minGameConfidence", config.minGameConfidence));

  // 3. Parse Search Parameters (Ints)
  config.maxMatches =
      static_cast<int>(configObj.getProperty(runtime, "maxMatches").asNumber());

  config.searchCandidates = static_cast<int>(
      configObj.getProperty(runtime, "searchCandidates").asNumber());

  // 4. Parse Optional Booleans
  // safe check: if property doesn't exist, default to false
  auto captureImageProp = configObj.getProperty(runtime, "captureImage");
  config.captureImage = captureImageProp.isBool() ? captureImageProp.asBool()
                                                  : config.captureImage;

  // 4b. Optional frame gates and low-light correction
  config.blurThreshold = numberOr("blurThreshold", config.blurThreshold);
  config.lowLightThreshold =
      numberOr("lowLightThreshold", config.lowLightThreshold);
  config.lowLightGamma = numberOr("lowLightGamma", config.lowLightGamma);
  config.maxFrameRate =
      static_cast<int>(numberOr("maxFrameRate", config.maxFrameRate));

  // 4c. Multi-card freeze tunables (optional); validate() range-checks them
  config.minCardsForMulti =
      static_cast<int>(numberOr("minCardsForMulti", config.minCardsForMulti));
  config.multiStableFrames =
      static_cast<int>(numberOr("multiStableFrames", config.multiStableFrames));
  auto freezeOnMultiProp = configObj.getProperty(runtime, "freezeOnMulti");
  config.freezeOnMulti = freezeOnMultiProp.isBool() ? freezeOnMultiProp.asBool()
                                                    : config.freezeOnMulti;

  // 4d. Parse game class mapping; validate() checks the map itself
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

      int classId = 0;
      try {
        classId = std::stoi(keyStr);
      } catch (const std::exception &) {
        throw jsi::JSError(runtime, "initializeScanner: gameClassMapping key '" +
                                        keyStr + "' is not an integer class id");
      }
      jsi::Value value = mappingObj.getProperty(runtime, propName);

      // A class maps to one game name, or several when games are merged
      std::vector<std::string> gameNames;
      if (value.isString()) {
        gameNames.push_back(value.asString(runtime).utf8(runtime));
      } else if (value.isObject() && value.asObject(runtime).isArray(runtime)) {
        jsi::Array namesArray = value.asObject(runtime).asArray(runtime);
        size_t nameCount = namesArray.size(runtime);
        for (size_t n = 0; n < nameCount; n++) {
          jsi::Value name = namesArray.getValueAtIndex(runtime, n);
          if (name.isString()) {
            gameNames.push_back(name.asString(runtime).utf8(runtime));
          }
        }
      }

      config.gameClassMapping[classId] = gameNames;
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
        ScannerConfig::GameConfig gameConfig;

        // Parse game-specific embedding model (optional)
        auto embeddingModelPathProp =
            gameConfigObj.getProperty(runtime, "embeddingModelPath");
        if (embeddingModelPathProp.isString()) {
          gameConfig.embeddingModelPath =
              embeddingModelPathProp.asString(runtime).utf8(runtime);
        }

        // Parse game-specific confidence threshold (optional)
        auto confidenceThreshProp =
            gameConfigObj.getProperty(runtime, "confidenceThreshold");
        if (confidenceThreshProp.isNumber()) {
          gameConfig.confidenceThreshold =
              static_cast<float>(confidenceThreshProp.asNumber());
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

          auto imageSize = setSymbolObj.getProperty(runtime, "imageSize");
          if (imageSize.isNumber()) {
            gameConfig.setSymbolImageSize =
                static_cast<int>(imageSize.asNumber());
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
} // namespace cardscanner
