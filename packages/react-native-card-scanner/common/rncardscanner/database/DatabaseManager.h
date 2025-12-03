#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "ObjectBoxDB.h"
#include <filesystem>
#include <map>
#include <memory>
#include <objectbox.hpp>
#include <set>
#include <string>

namespace cardscanner {
using GameStorePtr = std::unique_ptr<ObjectBoxDB>;

class DatabaseManager {
private:
  DatabaseManager();
  static std::unique_ptr<DatabaseManager> instance_;

  // Standard Game Stores
  std::map<std::string, GameStorePtr> activeStores_;
  std::set<std::string> knownGames_;

  // Special Set Symbol Store
  std::unique_ptr<obx::Store> setSymbolStore_;

  const std::string baseDbPath_;

  // Helper to init the set symbol directory structure
  void initSetSymbolStoreInternal();

public:
  DatabaseManager(const DatabaseManager &) = delete;
  DatabaseManager &operator=(const DatabaseManager &) = delete;

  static DatabaseManager &getInstance();

  std::string resolvePathFor(const std::string &gameName) const;
  std::set<std::string> getKnownGames() const;

  // --- Game Access ---
  ObjectBoxDB *getOrCreateStore(const std::string &gameName);
  void openStore(const std::string &gameName);
  void closeStore(const std::string &gameName);
  bool isClosedAndSwappable(const std::string &gameName) const;

  // --- Set Symbol Access (NEW) ---
  /**
   * @brief Returns the raw ObjectBox store for Set Symbols.
   * Initializes it if it's not already open.
   */
  obx::Store *getSetSymbolStore();

  /**
   * @brief Closes the set symbol store (e.g., for swapping).
   */
  void closeSetSymbolStore();

  // --- Management ---
  void scanForExistingStores();
  std::string getStorePath(const std::string &gameName) const;
  bool swapDatabaseFile(const std::string &gameName,
                        const std::string &sourcePath);
};

} // namespace cardscanner

#endif // DATABASE_MANAGER_H