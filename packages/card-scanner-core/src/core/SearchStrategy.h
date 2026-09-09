#pragma once

#include "../types/ScanResults.h"
#include "../types/ScannerConfig.h"
#include "../models/CardEmbeddingModel.h"
#include <DatabaseManager.h>
#include <ObjectBoxDB.h>
#include <YoloSegmentationModel.h>
#include <opencv2/opencv.hpp>
#include <vector>

namespace cardscanner {
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
  static std::vector<CardMatch>
  searchCard(const cv::Mat &cardImage,
             const cardscanner::Detection &detection,
             const ScannerConfig &config,
             cardscanner::DatabaseManager &dbManager,
             cardscanner::CardEmbeddingModel &defaultEmbedder,
             const cardscanner::GameEmbedders *gameEmbedders);

private:
  /**
   * @brief Extract top game database names from YOLO predictions
   *
   * @param detection YOLO detection
   * @param config Scan configuration
   * @return Vector of database names (up to MAX_GAME_DATABASES)
   */
  static std::vector<std::string>
  extractTopGames(const cardscanner::Detection &detection,
                  const ScannerConfig &config);

  /**
   * @brief Perform adaptive multi-database search
   *
   * Searches candidate databases in YOLO-confidence order. Databases that
   * share one YOLO class are always searched together; after a group, a
   * match clearing its game's threshold by EARLY_EXIT_SCORE_MARGIN skips the
   * remaining classes. A failing database is skipped and never ends the
   * search.
   * The default embedder runs once and is reused by every game that falls
   * back to it; game-specific embedders are one instance per game.
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
                          const ScannerConfig &config,
                          cardscanner::DatabaseManager &dbManager,
                          cardscanner::CardEmbeddingModel &defaultEmbedder,
                          const cardscanner::GameEmbedders *gameEmbedders);

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
  static std::vector<CardMatch>
  filterToBestGame(const std::vector<CardSearchResult> &allResults,
                   const ScannerConfig &config);

  /**
   * @brief Convert CardSearchResult to CardMatch
   *
   * @param result Database search result
   * @return DTO CardMatch
   */
  static CardMatch convertToCardMatch(const CardSearchResult &result);
};

} // namespace core
} // namespace cardscanner
