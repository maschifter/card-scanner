#include "JSISerializer.h"
#include <Constants.h>
#include "NitroSerializer.h"
#include <iostream>

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

  // Parse disambiguation threshold (optional, defaults to 2%)
  auto disambiguationThresholdProp =
      configObj.getProperty(runtime, "disambiguationThreshold");
  config.disambiguationThreshold =
      disambiguationThresholdProp.isNumber()
          ? static_cast<float>(disambiguationThresholdProp.asNumber())
          : ScannerConfig::DEFAULT_DISAMBIGUATION_THRESHOLD;

  // Parse min game confidence (optional, defaults to 0.1)
  auto minGameConfidenceProp =
      configObj.getProperty(runtime, "minGameConfidence");
  config.minGameConfidence =
      minGameConfidenceProp.isNumber()
          ? static_cast<float>(minGameConfidenceProp.asNumber())
          : ScannerConfig::DEFAULT_MIN_GAME_CONFIDENCE;

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
                             : ScannerConfig::DEFAULT_BLUR_THRESHOLD;

  // 4c. Parse low light threshold (optional, defaults to 65)
  auto lowLightThresholdProp =
      configObj.getProperty(runtime, "lowLightThreshold");
  config.lowLightThreshold =
      lowLightThresholdProp.isNumber()
          ? static_cast<double>(lowLightThresholdProp.asNumber())
          : ScannerConfig::DEFAULT_LOW_LIGHT_THRESHOLD;

  // 4d. Parse low light gamma (optional, defaults to 2.0)
  auto lowLightGammaProp = configObj.getProperty(runtime, "lowLightGamma");
  config.lowLightGamma = lowLightGammaProp.isNumber()
                             ? static_cast<double>(lowLightGammaProp.asNumber())
                             : ScannerConfig::DEFAULT_LOW_LIGHT_GAMMA;

  // 4e. Parse max frame rate (optional, defaults to 5)
  auto maxFrameRateProp = configObj.getProperty(runtime, "maxFrameRate");
  config.maxFrameRate = maxFrameRateProp.isNumber()
                            ? static_cast<int>(maxFrameRateProp.asNumber())
                            : ScannerConfig::DEFAULT_MAX_FRAME_RATE;

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

        // A class maps to one game name, or several when games are merged
        std::vector<std::string> gameNames;
        if (value.isString()) {
          gameNames.push_back(value.asString(runtime).utf8(runtime));
        } else if (value.isObject() &&
                   value.asObject(runtime).isArray(runtime)) {
          jsi::Array namesArray = value.asObject(runtime).asArray(runtime);
          size_t nameCount = namesArray.size(runtime);
          for (size_t n = 0; n < nameCount; n++) {
            jsi::Value name = namesArray.getValueAtIndex(runtime, n);
            if (name.isString()) {
              gameNames.push_back(name.asString(runtime).utf8(runtime));
            }
          }
        }

        if (gameNames.empty()) {
          std::cerr << "[CardScanner] gameClassMapping entry '" << keyStr
                    << "' is neither a string nor a non-empty string array; "
                       "skipping."
                    << std::endl;
          continue;
        }

        config.gameClassMapping[classId] = gameNames;
      } catch (const std::exception &e) {
        std::cerr << "[CardScanner] Invalid gameClassMapping key '" << keyStr
                  << "': " << e.what() << std::endl;
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
