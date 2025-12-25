#include "SearchStrategy.h"
#include "../RnCardScannerInstaller.h"
#include <algorithm>
#include <log.h>

namespace rncardscanner {
namespace core {

std::vector<dto::CardMatch>
SearchStrategy::searchCard(const cv::Mat &cardImage,
                           const rncardscanner::Detection &detection,
                           const dto::ScannerConfig &config,
                           rncardscanner::DatabaseManager &dbManager,
                           rncardscanner::CardEmbeddingModel *defaultEmbedder) {
  // Extract top game predictions from YOLO
  auto topGames = extractTopGames(detection);
  log(LOG_LEVEL::Debug,
      "[RNCardScanner] Top YOLO game predictions: %s",
      topGames.empty() ? "None"
                       : [&topGames]() {
                           std::string gamesList;
                           for (const auto &game : topGames) {
                             gamesList += game + " ";
                           }
                           return gamesList;
                         }()
                           .c_str());
  if (topGames.empty()) {
    return {}; // No games to search
  }

  // Adaptive multi-database search with per-game embeddings
  auto allResults = searchMultipleDatabases(cardImage, topGames, config,
                                           dbManager, defaultEmbedder);

  log(LOG_LEVEL::Debug,
      "[RNCardScanner] Retrieved %zu total matches from databases",
      allResults.size());
  // Filter to best game
  return filterToBestGame(allResults, config);
}

std::vector<std::string>
SearchStrategy::extractTopGames(const rncardscanner::Detection &detection) {
  std::vector<std::string> topGames;

  for (const auto &[gameName, conf] : detection.topGamePredictions) {
    if (conf >= dto::ScannerConfig::MIN_YOLO_GAME_CONFIDENCE) {
      topGames.push_back(gameName);
      if (topGames.size() >= dto::ScannerConfig::MAX_GAME_PREDICTIONS) {
        break;
      }
    }
  }

  return topGames;
}

std::vector<CardSearchResult> SearchStrategy::searchMultipleDatabases(
    const cv::Mat &cardImage, const std::vector<std::string> &topGames,
    const dto::ScannerConfig &config,
    rncardscanner::DatabaseManager &dbManager,
    rncardscanner::CardEmbeddingModel *defaultEmbedder) {
  std::vector<CardSearchResult> allResults;
  bool shouldSearchMore = false;

  for (size_t i = 0; i < topGames.size(); i++) {
    const auto &gameToSearch = topGames[i];

    try {
      ObjectBoxDB *gameDb = dbManager.getOrCreateStore(gameToSearch);
      if (!gameDb) {
        continue;
      }

      // Select game-specific embedder or fall back to default
      rncardscanner::CardEmbeddingModel *embedder = defaultEmbedder;
      auto gameSpecificModel =
          CardScannerInstaller::getEmbeddingModelForGame(gameToSearch);
      if (gameSpecificModel) {
        embedder = gameSpecificModel.get();
        log(LOG_LEVEL::Debug,
            "[RNCardScanner] Using game-specific embedder for %s",
            gameToSearch.c_str());
      }

      // Compute embedding with the selected model for this game
      auto embeddingResult = embedder->computeEmbedding(cardImage);
      const auto &embedding = embeddingResult.embedding;

      // Search in this game's database with its specialized embedding
      auto gameResults =
          gameDb->search_similar_cards(embedding, config.searchCandidates);

      // Tag results with game name
      for (auto &result : gameResults) {
        result.gameName = gameToSearch;
        allResults.push_back(result);
      }

      // Optimization: only search more games if first search is uncertain
      if (i == 0 && !gameResults.empty()) {
        float topScore = gameResults[0].score;

        // Search additional games if:
        // 1. Top score below confidence threshold + delta OR
        // 2. Multiple high-confidence games predicted by YOLO
        if (topScore < config.confidenceThreshold +
                           dto::ScannerConfig::SEARCH_MORE_THRESHOLD_DELTA ||
            topGames.size() > 1) {
          shouldSearchMore = true;
        } else {
          // High confidence match in first game, skip remaining searches
          break;
        }
      }

      // After first search, only continue if needed
      if (i > 0 && !shouldSearchMore) {
        break;
      }

    } catch (const std::exception &e) {
      // Skip failed game database
      log(LOG_LEVEL::Error,
          "[RNCardScanner] Failed to search database for %s: %s",
          gameToSearch.c_str(), e.what());
      continue;
    }
  }

  return allResults;
}

std::vector<dto::CardMatch> SearchStrategy::filterToBestGame(
    const std::vector<CardSearchResult> &allResults,
    const dto::ScannerConfig &config) {
  if (allResults.empty()) {
    return {};
  }

  // Sort all results by score (descending)
  auto sortedResults = allResults;
  std::sort(sortedResults.begin(), sortedResults.end(),
            [](const CardSearchResult &a, const CardSearchResult &b) {
              return a.score > b.score;
            });

  // Find the best match game (highest similarity score)
  std::string bestMatchGame = "";
  if (sortedResults[0].score >= config.confidenceThreshold) {
    bestMatchGame = sortedResults[0].gameName;
  }

  log(LOG_LEVEL::Debug, "[RNCardScanner] Best match game: %s (score: %.4f)",
      bestMatchGame.empty() ? "None" : bestMatchGame.c_str(),
      sortedResults[0].score);

  if (bestMatchGame.empty()) {
    return {}; // No confident match
  }

  // Filter: keep only cards from best match game
  std::vector<dto::CardMatch> filteredMatches;
  for (const auto &result : sortedResults) {
    if (result.gameName == bestMatchGame &&
        result.score >= config.confidenceThreshold) {
      filteredMatches.push_back(convertToCardMatch(result));

      if (filteredMatches.size() >= static_cast<size_t>(config.maxMatches)) {
        break;
      }
    }
  }

  return filteredMatches;
}

dto::CardMatch
SearchStrategy::convertToCardMatch(const CardSearchResult &result) {
  dto::CardMatch match;
  match.cardId = result.card_id;
  match.gameName = result.gameName;
  match.score = result.score;
  return match;
}

} // namespace core
} // namespace rncardscanner
