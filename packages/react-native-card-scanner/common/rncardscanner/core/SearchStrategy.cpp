#include "SearchStrategy.h"
#include <algorithm>

namespace rncardscanner {
namespace core {

std::vector<dto::CardMatch>
SearchStrategy::searchCard(const std::vector<float> &embedding,
                           const cardscanner::Detection &detection,
                           const dto::ScannerConfig &config,
                           cardscanner::DatabaseManager &dbManager) {
  // Extract top game predictions from YOLO
  auto topGames = extractTopGames(detection);

  if (topGames.empty()) {
    return {}; // No games to search
  }

  // Adaptive multi-database search
  auto allResults =
      searchMultipleDatabases(embedding, topGames, config, dbManager);

  // Filter to best game
  return filterToBestGame(allResults, config);
}

std::vector<std::string>
SearchStrategy::extractTopGames(const cardscanner::Detection &detection) {
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
    const std::vector<float> &embedding,
    const std::vector<std::string> &topGames, const dto::ScannerConfig &config,
    cardscanner::DatabaseManager &dbManager) {
  std::vector<CardSearchResult> allResults;
  bool shouldSearchMore = false;

  for (size_t i = 0; i < topGames.size(); i++) {
    const auto &gameToSearch = topGames[i];

    try {
      ObjectBoxDB *gameDb = dbManager.getOrCreateStore(gameToSearch);
      if (!gameDb) {
        continue;
      }

      // Search in this game's database
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
  match.name = result.name;
  match.gameName = result.gameName;
  match.score = result.score;
  return match;
}

} // namespace core
} // namespace rncardscanner
