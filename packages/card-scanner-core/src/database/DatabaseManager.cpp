#include "DatabaseManager.h"
#include "../Constants.h"
#include "../database/objectbox-model.h"
#include "../utils/PathUtils.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
namespace cardscanner {

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

GameStorePtr DatabaseManager::getOrCreateStore(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);
  knownGames_.insert(gameName);
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    return it->second; // Store found in activeStores - Return it
  }

  const std::string path = resolvePathFor(gameName);
  GameStorePtr newGameStore = std::make_shared<ObjectBoxDB>(path);
  activeStores_.emplace(gameName, newGameStore);
  return newGameStore;
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

GameStorePtr DatabaseManager::getSetSymbolStore() {
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

      setSymbolStore_ = std::make_shared<ObjectBoxDB>(dbDir.string());
    } catch (const std::exception &e) {
      std::cerr << "Failed to initialize SetSymbol store: " << e.what()
                << std::endl;
      setSymbolStore_.reset(); // Ensure nullptr on failure
    }
  }
  return setSymbolStore_;
}

void DatabaseManager::closeSetSymbolStore() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (setSymbolStore_) {
    setSymbolStore_.reset();
  }
}

// Held under mutex_ for the whole close -> rename -> reopen sequence, so a
// concurrent getOrCreateStore cannot reopen the old file mid-swap.
bool DatabaseManager::swapDatabaseFile(const std::string &gameName,
                                       const std::string &sourcePath) {
  std::lock_guard<std::mutex> lock(mutex_);

  const bool isSetSymbols = (gameName == database::SET_SYMBOL_DB_NAME);
  if (isSetSymbols) {
    setSymbolStore_.reset();
  } else {
    activeStores_.erase(gameName);
  }

  fs::path source = fs::path(utils::PathUtils::stripFilePrefix(sourcePath));

  fs::path target;
  if (isSetSymbols) {
    target = fs::path(baseDbPath_) / database::SET_SYMBOL_DB_NAME /
             database::DB_FILENAME;
  } else {
    target = fs::path(getStorePath(gameName));
  }

  try {
    fs::create_directories(target.parent_path());

    if (!fs::exists(source)) {
      std::cerr << "Source file does not exist: " << source << std::endl;
      return false;
    }

    // The old file survives as a backup until the new one opens, so a
    // corrupt download rolls back to a working database. After a rollback
    // the store stays closed and reopens lazily on the next access.
    const fs::path backup =
        target.parent_path() /
        (std::string(database::TEMP_SWAP_PREFIX) + target.filename().string());
    const bool hadOld = fs::exists(target);
    if (hadOld) {
      fs::rename(target, backup);
    }
    try {
      fs::rename(source, target);
      const std::string storeDir = target.parent_path().string();
      if (isSetSymbols) {
        setSymbolStore_ = std::make_shared<ObjectBoxDB>(storeDir);
      } else {
        activeStores_[gameName] = std::make_shared<ObjectBoxDB>(storeDir);
      }
    } catch (...) {
      if (hadOld) {
        try {
          fs::rename(backup, target);
        } catch (const std::exception &e) {
          std::cerr << "Swap rollback failed for " << gameName << ": "
                    << e.what() << std::endl;
        }
      }
      throw;
    }
    if (hadOld) {
      fs::remove(backup);
    }
    if (!isSetSymbols) {
      knownGames_.insert(gameName);
    }
    return true;

  } catch (const std::exception &e) {
    std::cerr << "Swap failed for " << gameName << ": " << e.what()
              << std::endl;
    return false;
  }
}

bool DatabaseManager::cardIdExists(const std::string &gameName,
                                    const std::string &cardId) {
  GameStorePtr store = getOrCreateStore(gameName);
  if (store) {
    return store->card_id_exists(cardId);
  }
  return false;
}

bool DatabaseManager::deleteDatabaseDirectory(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);
  activeStores_.erase(gameName);

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

    fs::remove_all(dbDirectory);
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

} // namespace cardscanner
