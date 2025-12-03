#include "DatabaseManager.h"
#include "../database/objectbox-model.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
namespace cardscanner {
// Privates:
std::unique_ptr<DatabaseManager> DatabaseManager::instance_ = nullptr;

DatabaseManager::DatabaseManager() : baseDbPath_(pathprovider::get_db_path()) {

  // Constructor should scan for existing stores, and attach them
  scanForExistingStores();
}

std::string DatabaseManager::resolvePathFor(const std::string &gameName) const {

  fs::path fullPath = baseDbPath_;
  // This operations should be fail proofed in case gameName path doesnt exist
  fullPath /= gameName;
  return fullPath;
}

// Publics:
DatabaseManager &DatabaseManager::getInstance() {
  if (instance_ == nullptr) {
    // If not created, create the single instance now
    instance_ = std::unique_ptr<DatabaseManager>(new DatabaseManager());
  }
  // Return a reference to the existing single instance
  return *instance_;
}

std::set<std::string> DatabaseManager::getKnownGames() const {
  return knownGames_;
}

ObjectBoxDB *DatabaseManager::getOrCreateStore(const std::string &gameName) {

  if (knownGames_.find(gameName) == knownGames_.end()) {
    knownGames_.insert(gameName);
  }
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    return it->second.get(); // Store found in activeStores - Retrun it
  }

  const std::string path = resolvePathFor(gameName);
  GameStorePtr newGameStore = std::make_unique<ObjectBoxDB>(path);
  ObjectBoxDB *rawStorePtr = newGameStore.get();
  activeStores_.emplace(gameName, std::move(newGameStore));
  return rawStorePtr;
}

void DatabaseManager::openStore(const std::string &gameName) {
  if (knownGames_.find(gameName) == knownGames_.end()) {
    knownGames_.insert(gameName);
  }
  if (isClosedAndSwappable(gameName)) {
    const std::string path = resolvePathFor(gameName);

    GameStorePtr newGameStore = std::make_unique<ObjectBoxDB>(path);
    activeStores_.emplace(gameName, std::move(newGameStore));
  }
}

void DatabaseManager::closeStore(const std::string &gameName) {
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    activeStores_.erase(it); // Unique_ptr should handle closing and destruciton
  }
}

bool DatabaseManager::isClosedAndSwappable(const std::string &gameName) const {
  // If the gameName is NOT in the map, the store is closed (not active).
  return activeStores_.find(gameName) == activeStores_.end();
}

void DatabaseManager::scanForExistingStores() {
  fs::path basePath(baseDbPath_);

  if (!fs::exists(basePath) || !fs::is_directory(basePath)) {
    std::cerr << "Error: Base DB path does not exist or is not a directory: "
              << baseDbPath_ << std::endl;
    return;
  }

  knownGames_.clear();

  try {
    for (const auto &entry : fs::directory_iterator(basePath)) {

      if (entry.is_directory()) {
        const std::string gameName = entry.path().filename().string();

        // Skip set-symbols directory (used by SetSymbolDatabase, not game
        // cards)
        if (gameName == "set-symbols") {
          continue;
        }

        fs::path dataFilePath = getStorePath(gameName);

        if (fs::exists(dataFilePath) && fs::is_regular_file(dataFilePath)) {

          knownGames_.insert(gameName);

          std::cout << "Found existing store for game: " << gameName << " at "
                    << dataFilePath.string() << std::endl;

          // openStore(gameName); - we might not need to open it right away!
        }
      }
    }
  } catch (const fs::filesystem_error &e) {
    std::cerr << "Filesystem Error during scan: " << e.what() << std::endl;
  }
}

std::string DatabaseManager::getStorePath(const std::string &gameName) const {
  fs::path path(resolvePathFor(gameName));
  path /= "data.mdb";
  return path;
}

fs::path clean_path(const std::string &path_str) {
  std::string cleaned = path_str;
  const std::string prefix = "file://";
  if (cleaned.rfind(prefix, 0) == 0) {
    cleaned.erase(0, prefix.length());
  }
  return fs::path(cleaned);
}

void DatabaseManager::initSetSymbolStoreInternal() {
  try {
    // Define paths
    fs::path baseDbPath(baseDbPath_);
    fs::path dbDir = baseDbPath / "set-symbols";
    fs::path targetPath = dbDir / "data.mdb";

    // Ensure directory exists
    if (!fs::exists(dbDir)) {
      fs::create_directories(dbDir);
    }

    // Open the store
    // Note: We assume create_obx_model() works for SetSymbols here.
    // If you have multiple models, ensure you call the correct model creation
    // function.
    std::cout << "Opening SetSymbol database at: " << dbDir << std::endl;
    obx::Options options(create_obx_model());
    options.directory(dbDir.string().c_str());

    setSymbolStore_ = std::make_unique<obx::Store>(options);
  } catch (const std::exception &e) {
    std::cerr << "Failed to initialize SetSymbol store: " << e.what()
              << std::endl;
    setSymbolStore_.reset(); // Ensure nullptr on failure
  }
}

obx::Store *DatabaseManager::getSetSymbolStore() {
  if (!setSymbolStore_) {
    initSetSymbolStoreInternal();
  }
  return setSymbolStore_.get();
}

void DatabaseManager::closeSetSymbolStore() {
  if (setSymbolStore_) {
    setSymbolStore_.reset();
    std::cout << "SetSymbol store closed." << std::endl;
  }
}

// --- UPDATED SWAP LOGIC ---

bool DatabaseManager::swapDatabaseFile(const std::string &gameName,
                                       const std::string &sourcePath) {

  // Handle Special Case: Set Symbols
  if (gameName == "set-symbols") {
    closeSetSymbolStore();
  } else {
    if (!isClosedAndSwappable(gameName)) {
      closeStore(gameName);
    }
  }

  // Path cleanup helper (moved from old SetSymbol class)
  auto clean_path_local = [](const std::string &path_str) {
    std::string cleaned = path_str;
    const std::string prefix = "file://";
    if (cleaned.find(prefix) == 0) {
      cleaned = cleaned.substr(prefix.length());
    }
    return fs::path(cleaned);
  };

  fs::path source = clean_path_local(sourcePath);

  // Determine target path
  fs::path target;
  if (gameName == "set-symbols") {
    target = fs::path(baseDbPath_) / "set-symbols" / "data.mdb";
  } else {
    target = fs::path(getStorePath(gameName));
  }

  try {
    fs::create_directories(target.parent_path());

    // Basic copy/swap logic
    if (!fs::exists(source)) {
      std::cerr << "Source file does not exist: " << source << std::endl;
      return false;
    }

    // If target doesn't exist, simple move
    if (!fs::exists(target)) {
      fs::rename(source, target);
    } else {
      // Atomic swap attempt
      fs::path tempPath =
          target.parent_path() / ("temp_swap_" + target.filename().string());
      fs::rename(target, tempPath); // Backup old
      try {
        fs::rename(source, target); // Move new in
        fs::remove(tempPath);       // Delete backup
      } catch (...) {
        // Rollback if move fails
        fs::rename(tempPath, target);
        throw;
      }
    }

    // Re-open logic
    if (gameName == "set-symbols") {
      getSetSymbolStore(); // Re-initializes
    } else {
      openStore(gameName);
    }

    return true;

  } catch (const std::exception &e) {
    std::cerr << "Swap failed for " << gameName << ": " << e.what()
              << std::endl;
    return false;
  }
}

} // namespace cardscanner
