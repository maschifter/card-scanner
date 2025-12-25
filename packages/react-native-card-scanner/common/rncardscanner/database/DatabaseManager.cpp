#include "DatabaseManager.h"
#include "../Constants.h"
#include "../database/objectbox-model.h"
#include "../utils/PathUtils.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
namespace rncardscanner {

using namespace constants;

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
  // Meyer's Singleton - thread-safe since C++11
  static DatabaseManager instance;
  return instance;
}

std::set<std::string> DatabaseManager::getKnownGames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return knownGames_;
}

ObjectBoxDB *DatabaseManager::getOrCreateStore(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (knownGames_.find(gameName) == knownGames_.end()) {
    knownGames_.insert(gameName);
  }
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    return it->second.get(); // Store found in activeStores - Return it
  }

  const std::string path = resolvePathFor(gameName);
  GameStorePtr newGameStore = std::make_unique<ObjectBoxDB>(path);
  ObjectBoxDB *rawStorePtr = newGameStore.get();
  activeStores_.emplace(gameName, std::move(newGameStore));
  return rawStorePtr;
}

void DatabaseManager::openStore(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (knownGames_.find(gameName) == knownGames_.end()) {
    knownGames_.insert(gameName);
  }
  if (activeStores_.find(gameName) == activeStores_.end()) {
    const std::string path = resolvePathFor(gameName);
    GameStorePtr newGameStore = std::make_unique<ObjectBoxDB>(path);
    activeStores_.emplace(gameName, std::move(newGameStore));
  }
}

void DatabaseManager::closeStore(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    activeStores_.erase(it); // Unique_ptr should handle closing and destruction
  }
}

bool DatabaseManager::isClosedAndSwappable(const std::string &gameName) const {
  std::lock_guard<std::mutex> lock(mutex_);
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

  std::lock_guard<std::mutex> lock(mutex_);
  knownGames_.clear();

  try {
    for (const auto &entry : fs::directory_iterator(basePath)) {

      if (entry.is_directory()) {
        const std::string gameName = entry.path().filename().string();

        // Skip set-symbols directory (used by SetSymbolDatabase, not game
        // cards)
        if (gameName == database::SET_SYMBOL_DB_NAME) {
          continue;
        }

        fs::path dataFilePath = getStorePath(gameName);

        if (fs::exists(dataFilePath) && fs::is_regular_file(dataFilePath)) {

          knownGames_.insert(gameName);

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
  path /= database::DB_FILENAME;
  return path;
}

ObjectBoxDB *DatabaseManager::getSetSymbolStore() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!setSymbolStore_) {
    try {
      // Define path for set symbols database
      fs::path baseDbPath(baseDbPath_);
      fs::path dbDir = baseDbPath / database::SET_SYMBOL_DB_NAME;

      // Ensure directory exists
      if (!fs::exists(dbDir)) {
        fs::create_directories(dbDir);
      }

      setSymbolStore_ = std::make_unique<ObjectBoxDB>(dbDir.string());
    } catch (const std::exception &e) {
      std::cerr << "Failed to initialize SetSymbol store: " << e.what()
                << std::endl;
      setSymbolStore_.reset(); // Ensure nullptr on failure
    }
  }
  return setSymbolStore_.get();
}

void DatabaseManager::closeSetSymbolStore() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (setSymbolStore_) {
    setSymbolStore_.reset();
  }
}

// --- UPDATED SWAP LOGIC ---

bool DatabaseManager::swapDatabaseFile(const std::string &gameName,
                                       const std::string &sourcePath) {

  // Handle Special Case: Set Symbols
  if (gameName == database::SET_SYMBOL_DB_NAME) {
    closeSetSymbolStore();
  } else {
    if (!isClosedAndSwappable(gameName)) {
      closeStore(gameName);
    }
  }

  fs::path source = fs::path(utils::PathUtils::stripFilePrefix(sourcePath));

  // Determine target path
  fs::path target;
  if (gameName == database::SET_SYMBOL_DB_NAME) {
    target = fs::path(baseDbPath_) / database::SET_SYMBOL_DB_NAME /
             database::DB_FILENAME;
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
          target.parent_path() / (std::string(database::TEMP_SWAP_PREFIX) +
                                  target.filename().string());
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
    if (gameName == database::SET_SYMBOL_DB_NAME) {
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

std::string DatabaseManager::getMetadataValue(const std::string &gameName,
                                            const std::string &key) {
  ObjectBoxDB *store = getOrCreateStore(gameName);
  if (store) {
    return store->get_metadata_value(key);
  }
  return "";
}

bool DatabaseManager::deleteDatabaseDirectory(const std::string &gameName) {
  // Close the store if it's currently open
  if (!isClosedAndSwappable(gameName)) {
    closeStore(gameName);
  }

  fs::path dbDirectory = fs::path(baseDbPath_) / gameName;

  try {
    if (!fs::exists(dbDirectory)) {
      std::cerr << "Database directory does not exist: " << dbDirectory
                << std::endl;
      return false;
    }

    if (!fs::is_directory(dbDirectory)) {
      std::cerr << "Path is not a directory: " << dbDirectory << std::endl;
      return false;
    }

    // Remove entire directory and all contents
    fs::remove_all(dbDirectory);

    // Remove from known games
    std::lock_guard<std::mutex> lock(mutex_);
    knownGames_.erase(gameName);

    return true;

  } catch (const fs::filesystem_error &e) {
    std::cerr << "Failed to delete database directory for " << gameName << ": "
              << e.what() << std::endl;
    return false;
  } catch (const std::exception &e) {
    std::cerr << "Unexpected error deleting database for " << gameName << ": "
              << e.what() << std::endl;
    return false;
  }
}

} // namespace rncardscanner
