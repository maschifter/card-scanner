#pragma once

#include "../dto/ScanResults.h"
#include "../dto/ScannerConfig.h"
#include "../models/CardEmbeddingModel.h"
#include <DatabaseManager.h>
#include <ObjectBoxDB.h>
#include <YoloSegmentationModel.h>
#include <opencv2/opencv.hpp>
#include <vector>

namespace rncardscanner {
namespace core {

/**
 * @class SearchStrategy
 * @brief Implements the multi-database adaptive search logic
 *
 * Responsibilities:
 * - Extract top game predictions from YOLO
 * - Search multiple game databases adaptively
 * - Filter results to best-matching game
 *
 */
class SearchStrategy {
public:
  /**
   * @brief Search for card across multiple game databases
   *
   * Strategy:
   * 1. Extract top N game predictions from YOLO (with confidence filter)
   * 2. For each probable game:
   *    - Select game-specific embedder (or use default)
   *    - Compute embedding with that model
   *    - Search that game's database with its specialized embedding
   * 3. If high confidence match, return early (optimization)
   * 4. Otherwise, search additional databases
   * 5. Merge and filter results to single best game
   *
   * @param cardImage Cropped card image
   * @param detection YOLO detection with game predictions
   * @param config Scan configuration
   * @param dbManager Database manager
   * @param defaultEmbedder Default embedding model (fallback)
   * @return Vector of matches from best game (empty if no confident match)
   */
  static std::vector<dto::CardMatch>
  searchCard(const cv::Mat &cardImage,
             const rncardscanner::Detection &detection,
             const dto::ScannerConfig &config,
             rncardscanner::DatabaseManager &dbManager,
             rncardscanner::CardEmbeddingModel *defaultEmbedder);

private:
  /**
   * @brief Extract top N game names from YOLO predictions
   *
   * @param detection YOLO detection
   * @return Vector of game names (up to MAX_GAME_PREDICTIONS)
   */
  static std::vector<std::string>
  extractTopGames(const rncardscanner::Detection &detection);

  /**
   * @brief Perform adaptive multi-database search
   *
   * Searches databases sequentially with early exit optimization.
   * Computes game-specific embeddings for each database search.
   *
   * @param cardImage Cropped card image
   * @param topGames Game names to search
   * @param config Scan configuration
   * @param dbManager Database manager
   * @param defaultEmbedder Default embedding model (fallback)
   * @return All results from all databases (unsorted)
   */
  static std::vector<CardSearchResult>
  searchMultipleDatabases(const cv::Mat &cardImage,
                          const std::vector<std::string> &topGames,
                          const dto::ScannerConfig &config,
                          rncardscanner::DatabaseManager &dbManager,
                          rncardscanner::CardEmbeddingModel *defaultEmbedder);

  /**
   * @brief Filter results to best-matching game only
   *
   * Strategy:
   * 1. Sort by score descending
   * 2. Find best game (highest score >= threshold)
   * 3. Keep only matches from best game
   *
   * @param allResults All search results
   * @param config Scan configuration
   * @return Filtered matches from best game (up to maxMatches)
   */
  static std::vector<dto::CardMatch>
  filterToBestGame(const std::vector<CardSearchResult> &allResults,
                   const dto::ScannerConfig &config);

  /**
   * @brief Convert CardSearchResult to CardMatch
   *
   * @param result Database search result
   * @return DTO CardMatch
   */
  static dto::CardMatch convertToCardMatch(const CardSearchResult &result);
};

} // namespace core
} // namespace rncardscanner
