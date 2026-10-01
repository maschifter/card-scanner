// Headless harness: drives card-scanner-core with no OBS in the loop, which is
// how the inference seam and accuracy work get exercised.
//
// Usage: desktop_card_scanner <config.json> <image>
// Exit:  0 identified, 2 scanned but nothing identified, 1 error.

#include <config/ScannerConfigLoader.h>

#include <DatabaseManager.h>
#include <PathProvider.h>
#include <ScannerRegistry.h>
#include <types/ScanResults.h>

#include <opencv2/core/utility.hpp>

#include <filesystem>
#include <iomanip>
#include <iostream>

namespace {

void printCard(const cardscanner::ProcessedCard &card, size_t index) {
  std::cout << "\ncard " << index << "  [" << card.boundingBox.width << "x"
            << card.boundingBox.height << " @ " << card.boundingBox.x << ","
            << card.boundingBox.y << "]"
            << "  detection=" << std::fixed << std::setprecision(3)
            << card.detectionConfidence << "\n";

  if (!card.predictedGameName.empty()) {
    std::cout << "  game:      " << card.predictedGameName << " ("
              << card.predictedGameConfidence << ")\n";
  }

  if (card.matches.empty()) {
    // Distinct from "no card in frame" when reading the output.
    std::cout << "  matches:   none above threshold\n";
    return;
  }

  std::cout << "  matches:\n";
  for (const auto &match : card.matches) {
    std::cout << "    " << std::setw(6) << match.score << "  " << match.cardId
              << "  (" << match.gameName << ")\n";
  }

  if (!card.setSymbol.isEmpty()) {
    std::cout << "  set:       " << card.setSymbol.setCode << " ("
              << card.setSymbol.similarity << ")\n";
  }
  if (!card.fabColor.isEmpty()) {
    std::cout << "  colour:    " << card.fabColor.color << " ("
              << card.fabColor.similarity << ")\n";
  }
}

int run(const std::filesystem::path &configPath,
        const std::filesystem::path &imagePath) {
  if (!std::filesystem::exists(imagePath)) {
    throw std::runtime_error("image not found: " + imagePath.string());
  }

  auto loaded = cardscanner::desktop::loadConfigFile(configPath);

  // PathProvider's contract: both must be set before DatabaseManager is first
  // constructed, since it reads the db path once.
  std::filesystem::create_directories(loaded.paths.databases);
  std::filesystem::create_directories(loaded.paths.cache);
  pathprovider::set_db_path(loaded.paths.databases.string());
  pathprovider::set_cache_path(loaded.paths.cache.string());

  // Keeps OpenCV's pool out of the inference runtime's way.
  cv::setNumThreads(0);

  cardscanner::ScannerRegistry::setConfig(loaded.scanner);
  cardscanner::ScannerRegistry::initializeModels();

  auto &dbManager = cardscanner::DatabaseManager::getInstance();
  const auto games = dbManager.getKnownGames();
  std::cout << "databases: " << loaded.paths.databases << "\n"
            << "found:     " << games.size() << " game(s)";
  for (const auto &game : games) {
    std::cout << " " << game;
  }
  std::cout << "\n";

  if (games.empty()) {
    // Not fatal, but every match will be empty, so say why rather than let it
    // look like a recognition failure.
    std::cerr << "warning: no databases found; detections cannot be identified\n";
  }

  const auto result =
      cardscanner::ScannerRegistry::scanImageFile(imagePath.string(), dbManager);

  std::cout << "\nscanned " << imagePath.filename() << " in " << std::fixed
            << std::setprecision(1) << result.processingTimeMs << " ms - "
            << result.cards.size() << " detection(s)\n";

  size_t identified = 0;
  for (size_t i = 0; i < result.cards.size(); i++) {
    printCard(result.cards[i], i);
    if (result.cards[i].hasMatches()) {
      identified++;
    }
  }

  cardscanner::ScannerRegistry::releaseModels();

  std::cout << "\n" << identified << " of " << result.cards.size()
            << " detection(s) identified\n";
  return identified > 0 ? 0 : 2;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: " << argv[0] << " <config.json> <image>\n";
    return 1;
  }

  try {
    return run(argv[1], argv[2]);
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
