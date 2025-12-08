#pragma once

#include "ObjectBoxDB.h"
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cardscanner {

using namespace executorch::extension;
using ::executorch::extension::module::Module;

/**
 * @struct CardEmbeddingResult
 * @brief Result container for card embedding computation
 *
 * Contains the computed 256-dimensional embedding vector that represents
 * the visual features of a card image.
 */
struct CardEmbeddingResult {
  std::vector<float> embedding; ///< 256-dim L2-normalized embedding vector
};

/**
 * @class CardEmbeddingModel
 * @brief Deep learning model for computing card image embeddings
 *
 * This model converts card images into fixed-size embedding vectors that
 * capture visual similarity. Cards with similar visual appearance will have
 * embeddings close together in the embedding space.
 *
 * Architecture: MobileNetV3-based encoder
 * Input: 224x224 RGB image
 * Output: 256-dimensional L2-normalized embedding
 *
 * The embeddings are used for:
 * - Fast similarity search in card databases
 * - Matching unknown cards to known cards
 * - Visual card recognition
 *
 * Preprocessing:
 * - Resize to 224x224
 * - Apply ImageNet normalization (mean/std)
 * - Convert to CHW tensor format
 *
 * @note Model runs on CPU via ExecuTorch for cross-platform compatibility
 */
class CardEmbeddingModel {
public:
  /**
   * @brief Construct embedding model from ExecuTorch .pte file
   *
   * @param modelPath Path to the .pte model file (supports file:// URIs)
   * @throws std::runtime_error if model loading fails
   */
  explicit CardEmbeddingModel(const std::string &modelPath);

  /**
   * @brief Compute embedding vector for a card image
   *
   * @param cardImg Input card image (any size, will be resized to 224x224)
   * @return CardEmbeddingResult containing 256-dim normalized embedding
   * @throws std::runtime_error if inference fails or image is invalid
   */
  CardEmbeddingResult computeEmbedding(const cv::Mat &cardImg);

private:
  std::unique_ptr<Module> module_; ///< ExecuTorch model instance

  /**
   * @brief Preprocess image with ImageNet normalization
   *
   * Applies ImageNet mean/std normalization and converts HWC to CHW format.
   *
   * @param img Input image (must be 224x224)
   * @return Normalized CHW tensor data
   */
  std::vector<float> normalizeImage(const cv::Mat &img) const;
};

} // namespace cardscanner
