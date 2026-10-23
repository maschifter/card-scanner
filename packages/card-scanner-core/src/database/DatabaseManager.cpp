#include "DatabaseManager.h"
#include "../Constants.h"
#include "../utils/PathUtils.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <Log.h>
#include <filesystem>

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
  // .string() is explicit because fs::path's value_type is wchar_t on Windows,
  // and without that it would crash when trying to convert.
  return fullPath.string();
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

GameStorePtr DatabaseManager::getStore(const std::string &gameName) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!knownGames_.count(gameName)) {
    return nullptr;
  }
  auto it = activeStores_.find(gameName);
  if (it != activeStores_.end()) {
    return it->second;
  }

  // Closed after a rolled-back swap; the file is still there.
  GameStorePtr store = std::make_shared<ObjectBoxDB>(resolvePathFor(gameName));
  activeStores_.emplace(gameName, store);
  return store;
}

void DatabaseManager::scanForExistingStores() {
  fs::path basePath(baseDbPath_);

  if (!fs::exists(basePath) || !fs::is_directory(basePath)) {
    log(LOG_LEVEL::Error, "[CardScanner]",
        "base DB path does not exist or is not a directory:", baseDbPath_);
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

        if (!fs::exists(dataFilePath) || !fs::is_regular_file(dataFilePath)) {
          continue;
        }

        // Older SDK versions left an empty store behind for every game a
        // search or lookup touched, so the file alone does not mean the game
        // is installed. Installed stores stay open for the search.
        try {
          auto it = activeStores_.find(gameName);
          GameStorePtr store =
              it != activeStores_.end()
                  ? it->second
                  : std::make_shared<ObjectBoxDB>(resolvePathFor(gameName));
          if (!store->is_empty()) {
            knownGames_.insert(gameName);
            activeStores_[gameName] = store;
          } else {
            activeStores_.erase(gameName);
          }
        } catch (const std::exception &e) {
          log(LOG_LEVEL::Error, "[CardScanner]", "opening the database for",
              gameName, "failed:", e.what());
        }
      }
    }
  } catch (const fs::filesystem_error &e) {
    log(LOG_LEVEL::Error, "[CardScanner]", "filesystem error during scan:",
        e.what());
  }
}

std::string DatabaseManager::getStorePath(const std::string &gameName) const {
  fs::path path(resolvePathFor(gameName));
  path /= database::DB_FILENAME;
  return path.string();
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
      log(LOG_LEVEL::Error, "[CardScanner]",
          "failed to initialize SetSymbol store:", e.what());
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
// concurrent getStore cannot reopen the old file mid-swap.
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
      log(LOG_LEVEL::Error, "[CardScanner]", "source file does not exist:",
          source.string());
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
      // A read-only source (an iOS Release asset in the app bundle) is copied
      std::error_code ec;
      fs::rename(source, target, ec);
      if (ec) {
        fs::copy_file(source, target);
      }
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
          log(LOG_LEVEL::Error, "[CardScanner]", "swap rollback for", gameName,
              "failed:", e.what());
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
    log(LOG_LEVEL::Error, "[CardScanner]", "swap for", gameName, "failed:",
        e.what());
    return false;
  }
}

bool DatabaseManager::cardIdExists(const std::string &gameName,
                                    const std::string &cardId) {
  GameStorePtr store = getStore(gameName);
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
      log(LOG_LEVEL::Error, "[CardScanner]",
          "database directory does not exist:", dbDirectory.string());
      return false;
    }

    if (!fs::is_directory(dbDirectory)) {
      log(LOG_LEVEL::Error, "[CardScanner]", "path is not a directory:",
          dbDirectory.string());
      return false;
    }

    fs::remove_all(dbDirectory);
    knownGames_.erase(gameName);

    return true;

  } catch (const fs::filesystem_error &e) {
    log(LOG_LEVEL::Error, "[CardScanner]",
        "deleting the database directory for", gameName, "failed:", e.what());
    return false;
  } catch (const std::exception &e) {
    log(LOG_LEVEL::Error, "[CardScanner]", "deleting the database for",
        gameName, "failed unexpectedly:", e.what());
    return false;
  }
}

} // namespace cardscanner
