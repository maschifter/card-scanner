#pragma once

#include "../types/ScannerConfig.h"
#include <CardEmbeddingModel.h>
#include <memory>

namespace cardscanner {

class YoloSegmentationModel;
class SetSymbolYoloModel;
class SetSymbolEmbedder;
class FABColorClassifier;
class CardLanguageModel;

namespace core {

/**
 * @brief One scan's config and models, snapshotted together. Owns the models,
 * so a concurrent swap cannot pull one out mid-scan; optional ones are null.
 */
struct ScannerContext {
  ScannerConfig config;
  std::shared_ptr<YoloSegmentationModel> yolo;
  std::shared_ptr<CardEmbeddingModel> embedding;
  std::shared_ptr<SetSymbolYoloModel> setSymbolYolo;
  std::shared_ptr<SetSymbolEmbedder> setSymbolEmbedder;
  std::shared_ptr<FABColorClassifier> fabColor;
  std::shared_ptr<CardLanguageModel> language;
  /// Per-game embedders; games absent here use `embedding`.
  GameEmbedders gameEmbedders;
};

} // namespace core
} // namespace cardscanner
