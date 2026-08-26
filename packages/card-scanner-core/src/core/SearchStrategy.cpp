#include "SearchStrategy.h"
#include "../benchmark/BenchmarkCollector.h"
#include "../Constants.h"
#include <Log.h>
#include <algorithm>
#include <memory>
#include <optional>

namespace cardscanner {
namespace core {

std::vector<CardMatch> SearchStrategy::searchCard(
    const cv::Mat &cardImage, const cardscanner::Detection &detection,
    const ScannerConfig &config, cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel &defaultEmbedder,
    const cardscanner::GameEmbedders *gameEmbedders) {
  // Extract top game predictions from YOLO
  auto topGames = extractTopGames(detection, config);
  log(LOG_LEVEL::Debug,
      "[CardScanner] Top YOLO game predictions: %s",
      topGames.empty() ? "None"
                       : [&topGames]() {
                           std::string gamesList;
                           for (const auto &game : topGames) {
                             gamesList += game + " ";
                           }
                           return gamesList;
                         }()
                           .c_str());
  // Yolo predicted games
  for (const auto &game : topGames) {
    benchmark::BenchmarkCollector::appendStringWithSpace(
        benchmark::Label::YoloPredictedGames, game);
  }

  if (topGames.empty()) {
    return {}; // No games to search
  }

  // Adaptive multi-database search with per-game embeddings
  auto allResults =
      searchMultipleDatabases(cardImage, topGames, config, dbManager,
                              defaultEmbedder, gameEmbedders);

  log(LOG_LEVEL::Debug,
      "[CardScanner] Retrieved %zu total matches from databases",
      allResults.size());
  // Filter to best game
  return filterToBestGame(allResults, config);
}

std::vector<std::string>
SearchStrategy::extractTopGames(const cardscanner::Detection &detection,
                                const ScannerConfig &config) {
  // class_confs is sorted descending, so the first classes are the strongest
  // and the first one below the floor ends the walk.
  std::vector<std::string> databases;
  int classesTaken = 0;

  for (const auto &[conf, classId] : detection.box.class_confs) {
    if (classesTaken >= constants::yolo::MAX_TOP_PREDICTIONS ||
        static_cast<int>(databases.size()) >=
            ScannerConfig::MAX_GAME_DATABASES) {
      break;
    }
    if (conf < config.minGameConfidence) {
      break;
    }

    auto it = config.gameClassMapping.find(classId);
    if (it == config.gameClassMapping.end() || it->second.empty()) {
      continue; // an unmapped class does not consume a slot
    }

    for (const auto &name : it->second) {
      if (static_cast<int>(databases.size()) >=
          ScannerConfig::MAX_GAME_DATABASES) {
        break;
      }
      if (std::find(databases.begin(), databases.end(), name) ==
          databases.end()) {
        databases.push_back(name);
      }
    }
    classesTaken++;
  }

  return databases;
}

std::vector<CardSearchResult> SearchStrategy::searchMultipleDatabases(
    const cv::Mat &cardImage, const std::vector<std::string> &topGames,
    const ScannerConfig &config, cardscanner::DatabaseManager &dbManager,
    cardscanner::CardEmbeddingModel &defaultEmbedder,
    const cardscanner::GameEmbedders *gameEmbedders) {
  std::vector<CardSearchResult> allResults;
  bool shouldSearchMore = false;

  auto computeEmbedding = [&cardImage](cardscanner::CardEmbeddingModel &model) {
    benchmark::BenchmarkCollector::ScopedTimer timer(benchmark::Stage::Embed);
    return model.computeEmbedding(cardImage);
  };

  // Every game without its own embedder searches with this one embedding.
  std::optional<CardEmbeddingResult> defaultEmbedding;

  for (size_t i = 0; i < topGames.size(); i++) {
    const auto &gameToSearch = topGames[i];

    try {
      GameStorePtr gameDb = dbManager.getOrCreateStore(gameToSearch);
      if (!gameDb) {
        continue;
      }

      // Select game-specific embedder, or leave empty to use the default
      std::shared_ptr<cardscanner::CardEmbeddingModel> gameEmbedder;
      if (gameEmbedders != nullptr) {
        const auto it = gameEmbedders->find(gameToSearch);
        if (it != gameEmbedders->end() && it->second) {
          gameEmbedder = it->second;
          log(LOG_LEVEL::Debug,
              "[CardScanner] Using game-specific embedder for %s",
              gameToSearch.c_str());
        }
      }

      CardEmbeddingResult gameEmbedding;
      if (gameEmbedder) {
        gameEmbedding = computeEmbedding(*gameEmbedder);
      } else if (!defaultEmbedding) {
        defaultEmbedding = computeEmbedding(defaultEmbedder);
      }
      const auto &embedding =
          gameEmbedder ? gameEmbedding.embedding : defaultEmbedding->embedding;

      // Search in this game's database with its specialized embedding
      std::vector<CardSearchResult> gameResults;
      {
        benchmark::BenchmarkCollector::ScopedTimer timer(
            benchmark::Stage::DbSearch);
        gameResults =
            gameDb->search_similar_cards(embedding, config.searchCandidates);
      }

      // Accumulated: the adaptive search may probe several game databases.
      benchmark::BenchmarkCollector::add(benchmark::Metric::GamesSearched, 1);

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
                           ScannerConfig::SEARCH_MORE_THRESHOLD_DELTA ||
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
          "[CardScanner] Failed to search database for %s: %s",
          gameToSearch.c_str(), e.what());
      continue;
    }
  }

  return allResults;
}

namespace {
float getEffectiveConfidenceThreshold(const std::string &gameName,
                                      const ScannerConfig &config) {
  auto gameIt = config.gameSpecificConfig.find(gameName);
  if (gameIt != config.gameSpecificConfig.end() &&
      gameIt->second.confidenceThreshold.has_value()) {
    return *gameIt->second.confidenceThreshold;
  }

  return config.confidenceThreshold;
}
} // namespace

std::vector<CardMatch> SearchStrategy::filterToBestGame(
    const std::vector<CardSearchResult> &allResults,
    const ScannerConfig &config) {
  if (allResults.empty()) {
    return {};
  }

  // Sort all results by score (descending)
  auto sortedResults = allResults;
  std::sort(sortedResults.begin(), sortedResults.end(),
            [](const CardSearchResult &a, const CardSearchResult &b) {
              return a.score > b.score;
            });

  // Captured before the threshold gate discards it, so a near-miss stays
  // distinguishable from a total non-result.
  benchmark::BenchmarkCollector::set(benchmark::Metric::RawTopScore,
                                     sortedResults[0].score);
  benchmark::BenchmarkCollector::set(benchmark::Label::RawTopCardId,
                                     sortedResults[0].card_id);

  // Find the best match game (highest similarity score)
  std::string bestMatchGame = "";
  float effectiveThreshold =
      getEffectiveConfidenceThreshold(sortedResults[0].gameName, config);
  if (sortedResults[0].score >= effectiveThreshold) {
    bestMatchGame = sortedResults[0].gameName;
  }

  log(LOG_LEVEL::Debug, "[CardScanner] Best match game: %s (score: %.4f)",
      bestMatchGame.empty() ? "None" : bestMatchGame.c_str(),
      sortedResults[0].score);

  if (bestMatchGame.empty()) {
    return {}; // No confident match
  }

  // Filter: keep only cards from best match game
  std::vector<CardMatch> filteredMatches;
  for (const auto &result : sortedResults) {
    float gameThreshold =
        getEffectiveConfidenceThreshold(result.gameName, config);
    if (result.gameName == bestMatchGame && result.score >= gameThreshold) {
      filteredMatches.push_back(convertToCardMatch(result));

      if (filteredMatches.size() >= static_cast<size_t>(config.maxMatches)) {
        break;
      }
    }
  }

  return filteredMatches;
}

CardMatch
SearchStrategy::convertToCardMatch(const CardSearchResult &result) {
  CardMatch match;
  match.cardId = result.card_id;
  match.gameName = result.gameName;
  match.score = result.score;
  return match;
}

} // namespace core
} // namespace cardscanner
