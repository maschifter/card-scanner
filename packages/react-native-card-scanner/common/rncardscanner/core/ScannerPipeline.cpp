#include "ScannerPipeline.h"
#include "../utils/ImageUtils.h"
#include <chrono>

namespace rncardscanner {
namespace core {

dto::ScanResult ScannerPipeline::processFrame(
    const cv::Mat &frameImage, const dto::ScannerConfig &config,
    cardscanner::DatabaseManager &dbManager,
    cardscanner::YoloSegmentationModel *yoloModel,
    cardscanner::CardEmbeddingModel *embeddingModel,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder) {

  auto startTime = std::chrono::high_resolution_clock::now();

  dto::ScanResult result;

  // Stage 1: Segmentation
  auto segResult = performSegmentation(frameImage, config, yoloModel);

  // Stage 2-5: Process each detection through the pipeline
  for (size_t i = 0; i < segResult.detections.size(); i++) {
    try {
      auto processedCard = processDetection(
          frameImage, segResult.detections[i], i, config, dbManager,
          embeddingModel, setSymbolYolo, setSymbolEmbedder);
      result.cards.push_back(processedCard);
    } catch (const std::exception &e) {
      // Create empty card on error
      dto::ProcessedCard emptyCard;
      emptyCard.boundingBox =
          utils::ImageUtils::boundingBoxToRect(segResult.detections[i].box);
      emptyCard.detectionConfidence = segResult.detections[i].box.conf;
      result.cards.push_back(emptyCard);
    }
  }

  // Calculate total processing time
  auto endTime = std::chrono::high_resolution_clock::now();
  result.processingTimeMs =
      std::chrono::duration<double, std::milli>(endTime - startTime).count();

  return result;
}

cardscanner::SegmentationResult ScannerPipeline::performSegmentation(
    const cv::Mat &frameImage, const dto::ScannerConfig &config,
    cardscanner::YoloSegmentationModel *yoloModel) {

  if (!yoloModel) {
    throw std::runtime_error("YOLO model not initialized");
  }

  // Run YOLO segmentation
  auto segResult = yoloModel->segment(frameImage);

  // Apply scanMode: keep only highest confidence if "single"
  if (config.scanMode == "single" && !segResult.detections.empty()) {
    auto maxConfDet = std::max_element(
        segResult.detections.begin(), segResult.detections.end(),
        [](const auto &a, const auto &b) { return a.box.conf < b.box.conf; });
    segResult.detections = {*maxConfDet};
  }

  return segResult;
}

std::string ScannerPipeline::saveCardImage(const cv::Mat &cardImage,
                                           const dto::ScannerConfig &config,
                                           size_t index) {
  if (!config.captureImage || cardImage.empty()) {
    return "";
  }

  std::string cacheDir = pathprovider::get_db_path();
  return utils::ImageUtils::saveCardImage(cardImage, cacheDir, index);
}

std::vector<dto::CardMatch> ScannerPipeline::recognizeCard(
    const cv::Mat &cardImage, const cardscanner::Detection &detection,
    const dto::ScannerConfig &config, cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel *embeddingModel) {

  if (cardImage.empty() || !embeddingModel) {
    return {};
  }

  // Compute embedding
  auto embeddingResult = embeddingModel->computeEmbedding(cardImage);

  // Search databases using SearchStrategy
  return SearchStrategy::searchCard(embeddingResult.embedding, detection,
                                    config, dbManager);
}

dto::SetSymbolInfo ScannerPipeline::detectSetSymbol(
    const cv::Mat &cardImage, const std::vector<dto::CardMatch> &cardMatches,
    const dto::ScannerConfig &config,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder,
    ObjectBoxDB *setSymbolDb) {

  if (!config.mtgConfig.has_value()) {
    return dto::SetSymbolInfo(); // Return empty if MTG config is not present
  }

  return SetSymbolProcessor::processSetSymbol(
      cardImage, cardMatches, config.mtgConfig->confidenceThreshold,
      setSymbolYolo, setSymbolEmbedder, setSymbolDb);
}

dto::ProcessedCard ScannerPipeline::processDetection(
    const cv::Mat &frameImage, const cardscanner::Detection &detection,
    size_t index, const dto::ScannerConfig &config,
    cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel *embeddingModel,
    cardscanner::SetSymbolYoloModel *setSymbolYolo,
    cardscanner::SetSymbolEmbedder *setSymbolEmbedder) {

  dto::ProcessedCard card;
  ObjectBoxDB *setSymbolDb = dbManager.getSetSymbolStore();
  // Store detection info
  card.boundingBox = utils::ImageUtils::boundingBoxToRect(detection.box);
  card.detectionConfidence = detection.box.conf;

  // Stage 2: Extract card image
  card.croppedImage =
      utils::ImageUtils::extractCardImage(frameImage, detection);
  if (card.croppedImage.empty()) {
    return card; // Early exit if extraction failed
  }

  // Stage 3: Save image (if enabled)
  card.savedImagePath = saveCardImage(card.croppedImage, config, index);

  // Stage 4: Recognize card
  card.matches = recognizeCard(card.croppedImage, detection, config, dbManager,
                               embeddingModel);

  // Stage 5: Detect set symbol (MTG only)
  if (card.hasMatches()) {
    card.setSymbol =
        detectSetSymbol(card.croppedImage, card.matches, config, setSymbolYolo,
                        setSymbolEmbedder, setSymbolDb);
  }

  return card;
}

} // namespace core
} // namespace rncardscanner
