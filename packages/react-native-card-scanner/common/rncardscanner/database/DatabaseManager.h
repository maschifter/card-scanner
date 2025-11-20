#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "ObjectBoxDB.h"
#include <filesystem>
#include <map>
#include <memory>
#include <objectbox.hpp>
#include <stdexcept>
#include <string>

namespace cardscanner {
using GameStorePtr = std::unique_ptr<ObjectBoxDB>;

class DatabaseManager {
private:
  DatabaseManager();
  // --- Singleton Pattern Implementation ---
  static std::unique_ptr<DatabaseManager> instance_;

  std::map<std::string, GameStorePtr> activeStores_;
  std::set<std::string> knownGames_;
  const std::string baseDbPath_;

public:
  // --- Prevent Copying/Moving (Standard Singleton Practice) ---
  DatabaseManager(const DatabaseManager &) = delete;
  DatabaseManager &operator=(const DatabaseManager &) = delete;
  DatabaseManager(DatabaseManager &&) = delete;
  DatabaseManager &operator=(DatabaseManager &&) = delete;

  // --- Public Singleton Access ---
  static DatabaseManager &getInstance();

  /**
   * @brief Resolves the full file path for a game's data file.
   * @param gameName The unique identifier (directory name).
   * @return The full path (e.g., "DB_PATH/GameA/data.mdb").
   */
  std::string resolvePathFor(const std::string &gameName) const;

  /**
   * @brief Gets a list of all game names that have an existing database file.
   * @return A copy of the set of known game names.
   */
  std::set<std::string> getKnownGames() const;

  /**
   * @brief Gets the active store for a game, or opens it if not yet
   * initialized.
   * @param gameName The unique game identifier.
   * @return A raw pointer to the active store. Returns nullptr or throws if
   * opening fails.
   */
  ObjectBoxDB *getOrCreateStore(const std::string &gameName);

  /**
   * @brief Opens a specific game's store and adds it to the active
   * registry. This makes the underlying file unsafe for swapping/modification.
   * @param gameName The unique game identifier.
   */
  void openStore(const std::string &gameName);

  /**
   * @brief Closes a specific game's store and removes it from the active
   * registry. This makes the underlying file safe for swapping/modification.
   * @param gameName The unique game identifier.
   */
  void closeStore(const std::string &gameName);

  /**
   * @brief Checks if a specific game's store is fully closed and safe to
   * swap/modify.
   * @param gameName The unique game identifier.
   * @return true if the store is not active (i.e., closed), false otherwise.
   */
  bool isClosedAndSwappable(const std::string &gameName) const;

  // --- API: Path and Discovery ---

  /**
   * @brief Scans the base path for existing game directories and confirms their
   * DB files. This can be used to populate a list of available games.
   */
  void scanForExistingStores();

  /**
   * @brief Provides the full file path for a store,
   * @param gameName The unique game identifier.
   * @return The full file path to the expected database file.
   */
  std::string getStorePath(const std::string &gameName) const;

  /**
   * @brief Performs a three-step file swap for a specific game's database.
   * * This method ensures the database is closed, performs the swap,
   * and optionally reopens the target database.
   * * @param gameName The unique identifier (directory name) of the target
   * database.
   * @param sourcePath The full path to the new, incoming database file.
   * @return true on successful swap and reopen, false otherwise.
   */
  bool swapDatabaseFile(const std::string &gameName,
                        const std::string &sourcePath);
};

} // namespace cardscanner

#endif // DATABASE_MANAGER_H
