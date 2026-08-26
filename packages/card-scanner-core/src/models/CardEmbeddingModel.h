#pragma once

#include "../inference/InferenceSession.h"
#include "../types/Embeddings.h"
#include <opencv2/opencv.hpp>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cardscanner {


/// Game name to the embedder trained for it. Games absent from the map fall
/// back to the default embedder. Owned by whatever configures the scanner and
/// passed down, so core never has to ask a registry for it.
using GameEmbedders =
    std::map<std::string, std::shared_ptr<class CardEmbeddingModel>>;

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
  std::unique_ptr<inference::InferenceSession> session_;

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
