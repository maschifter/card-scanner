#include "DatabaseManager.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
namespace cardscanner {
// Privates:
std::unique_ptr<DatabaseManager> DatabaseManager::instance_ = nullptr;

DatabaseManager::DatabaseManager() : baseDbPath_(pathprovider::get_db_path()) {

  // Constructor should scan for exisitng stores, and attach them
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

  // If store is not found

  // This gets us full path, but ObjectBoxDB files are handling that - lets
  const std::string path = getStorePath(gameName);
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

bool DatabaseManager::swapDatabaseFile(const std::string &gameName,
                                       const std::string &sourcePath) {

  if (!isClosedAndSwappable(gameName)) {
    closeStore(gameName);
  }

  // 2. Define paths using the Manager's logic
  fs::path source = clean_path(sourcePath);

  fs::path target = getStorePath(gameName);

  // Target is: DB_PATH/gameName/data.mdb
  // Source is: the path to the incoming new file

  try {
    fs::create_directories(target.parent_path());

    if (!fs::exists(source)) {
      return false;
    }

    if (!fs::exists(target)) {
      fs::rename(source, target);
      openStore(gameName); // This adds it to the map
      return true;
    }

    // Create temporary path near the target (for atomic swap)
    fs::path tempPath =
        target.parent_path() / ("temp_swap_" + target.filename().string());

    fs::rename(target, tempPath);

    fs::rename(source, target);

    fs::remove(tempPath);

    openStore(gameName);

  } catch (const fs::filesystem_error &e) {
    // Log error and return failure
    std::cerr << "Filesystem Error during swap for " << gameName << ": "
              << e.what() << std::endl;
    return false;
  } catch (const std::exception &e) {
    // Log other errors
    std::cerr << "Standard Exception during swap for " << gameName << ": "
              << e.what() << std::endl;
    return false;
  }

  return true;
}

} // namespace cardscanner
