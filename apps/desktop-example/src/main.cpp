// Headless harness: drives card-scanner-core with no OBS in the loop, which is
// how the inference seam and accuracy work get exercised.
//
// Usage: desktop_card_scanner <config.json> <image>
//        desktop_card_scanner <config.json> --benchmark <image-dir>
//            [--out <file>] [--warmup N] [--iterations N] [--manifest <file>]
// Exit:  0 identified, 2 scanned but nothing identified, 1 error.
//        --benchmark: 0 on a completed run, 1 error.

#include <benchmark/OfflineBenchmark.h>
#include <config/ScannerConfigLoader.h>

#include <DatabaseManager.h>
#include <Log.h>
#include <ScannerRegistry.h>
#include <types/ScanResults.h>
#include <util/OpenCvThreads.h>

#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

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

/// Config -> data paths -> models, the order both modes need: pathprovider
/// has to be pointed at the data directories before anything constructs
/// DatabaseManager, which reads the db path exactly once.
cardscanner::desktop::LoadedConfig
startScanner(const std::filesystem::path &configPath) {
  auto loaded = cardscanner::desktop::loadConfigFile(configPath);
  cardscanner::desktop::applyDataPaths(loaded.paths);

  cardscanner::util::configureOpenCvThreads();

  cardscanner::ScannerRegistry::setConfig(loaded.scanner);
  cardscanner::ScannerRegistry::initializeModels();
  return loaded;
}

int run(const std::filesystem::path &configPath,
        const std::filesystem::path &imagePath) {
  if (!std::filesystem::exists(imagePath)) {
    throw std::runtime_error("image not found: " + imagePath.string());
  }

  const auto loaded = startScanner(configPath);

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
    cardscanner::log(cardscanner::LOG_LEVEL::Error, "[Scan]",
                     "no databases found; detections cannot be identified");
  }

  const auto result =
      cardscanner::ScannerRegistry::scanImageFile(imagePath.string());

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

int runBenchmark(int argc, char **argv) {
  cardscanner::desktop::OfflineBenchmarkOptions options;
  options.imagesDir = argv[3];
  if ((argc - 4) % 2 != 0) {
    throw std::runtime_error("options come in '--flag value' pairs");
  }
  for (int i = 4; i + 1 < argc; i += 2) {
    const std::string flag = argv[i];
    if (flag == "--out") {
      options.outPath = argv[i + 1];
    } else if (flag == "--warmup") {
      options.warmupIterations = std::stoi(argv[i + 1]);
    } else if (flag == "--iterations") {
      options.benchmarkIterations = std::stoi(argv[i + 1]);
    } else if (flag == "--manifest") {
      options.manifestPath = argv[i + 1];
    } else {
      throw std::runtime_error("unknown option: " + flag);
    }
  }
  if (options.warmupIterations < 0 || options.benchmarkIterations < 1) {
    throw std::runtime_error("--warmup must be >= 0, --iterations >= 1");
  }

  const auto loaded = startScanner(argv[1]);
  if (options.outPath.empty()) {
    options.outPath =
        loaded.paths.cache / cardscanner::desktop::benchmarkFileName();
  }

  if (cardscanner::DatabaseManager::getInstance().getKnownGames().empty()) {
    cardscanner::log(cardscanner::LOG_LEVEL::Info, "[Benchmark]",
                     "no databases found, dbSearch timings will be hollow:",
                     loaded.paths.databases.string());
  }

  const size_t records = cardscanner::desktop::runOfflineBenchmark(options);

  cardscanner::ScannerRegistry::releaseModels();

  std::cout << records << " record(s) -> " << options.outPath << "\n";
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const bool benchmark = argc >= 3 && std::string(argv[2]) == "--benchmark";
  if (benchmark ? argc < 4 : argc != 3) {
    std::cerr << "usage: " << argv[0] << " <config.json> <image>\n"
              << "       " << argv[0]
              << " <config.json> --benchmark <image-dir>"
              << " [--out <file>] [--warmup N] [--iterations N]"
              << " [--manifest <file>]\n";
    return 1;
  }

  try {
    return benchmark ? runBenchmark(argc, argv) : run(argv[1], argv[2]);
  } catch (const std::exception &e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
