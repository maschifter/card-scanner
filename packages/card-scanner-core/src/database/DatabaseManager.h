#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "ObjectBoxDB.h"
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <objectbox.hpp>
#include <set>
#include <string>

namespace cardscanner {
using GameStorePtr = std::shared_ptr<ObjectBoxDB>;

/**
 * @class DatabaseManager
 * @brief Thread-safe singleton managing multiple ObjectBox database instances
 *
 * This class manages two types of databases:
 * 1. Game Stores - One database per card game (MTG, Pokemon, etc.)
 * 2. Set Symbol Store - Special database for set symbol embeddings (MTG-specific)
 *
 * Key Features:
 * - Thread-safe singleton using Meyer's pattern (C++11 guarantees)
 * - Lazy initialization - databases opened on first access
 * - Store management - open/close/swap operations
 * - Auto-discovery - scans filesystem for existing databases
 *
 * Database Structure:
 * - Base path: Application's database directory
 * - Game stores: <baseDbPath>/<gameName>/data.mdb
 * - Set symbol store: <baseDbPath>/set-symbols/data.mdb
 *
 * Thread Safety:
 * All public methods are protected by an internal mutex. Safe to call from
 * any thread (JS thread, native module threads, background threads).
 *
 * Usage:
 * @code
 * auto& manager = DatabaseManager::getInstance();
 * GameStorePtr mtgDb = manager.getOrCreateStore("mtg");
 * GameStorePtr setSymbolDb = manager.getSetSymbolStore();
 * @endcode
 */
class DatabaseManager {
private:
  DatabaseManager();

  // Standard Game Stores
  std::map<std::string, GameStorePtr> activeStores_;
  std::set<std::string> knownGames_;

  // Set Symbol Store (now using ObjectBoxDB like game stores)
  GameStorePtr setSymbolStore_;

  const std::string baseDbPath_;

  // Mutex for thread-safe access to shared state
  mutable std::mutex mutex_;

public:
  // Prevent copying and assignment (singleton pattern)
  DatabaseManager(const DatabaseManager &) = delete;
  DatabaseManager &operator=(const DatabaseManager &) = delete;

  /**
   * @brief Returns the singleton instance (Meyer's singleton - thread-safe)
   * @return Reference to the DatabaseManager singleton
   */
  static DatabaseManager &getInstance();

  /**
   * @brief Resolves the full filesystem path for a game's database directory
   * @param gameName Name of the game (e.g., "mtg", "pokemon")
   * @return Full path to the game's database directory
   */
  std::string resolvePathFor(const std::string &gameName) const;

  /**
   * @brief Returns set of all known games discovered during initialization
   * @return Set of game names (e.g., {"mtg", "pokemon"})
   */
  std::set<std::string> getKnownGames() const;

  // --- Game Access ---

  /**
   * @brief Gets or creates a database store for the specified game
   * @param gameName Name of the game (e.g., "mtg", "pokemon")
   * @return The ObjectBoxDB instance, or nullptr on failure.
   *
   * Opens the database if not already open. Creates the directory structure
   * if it doesn't exist. Thread-safe.
   *
   * Hold on to what you get back for as long as you are reading from it. The
   * database stays alive while anyone still has it, so closing or swapping it
   * elsewhere cannot pull it away mid-search.
   */
  GameStorePtr getOrCreateStore(const std::string &gameName);

  /**
   * @brief Explicitly opens a database store for a game
   * @param gameName Name of the game to open
   *
   * Initializes the ObjectBoxDB instance. No-op if already open.
   */
  void openStore(const std::string &gameName);

  /**
   * @brief Closes a game's database store
   * @param gameName Name of the game to close
   *
   * Releases resources and removes from active stores. Required before
   * swapping database files.
   */
  void closeStore(const std::string &gameName);

  /**
   * @brief Checks if a store is closed and can be swapped
   * @param gameName Name of the game to check
   * @return true if store is closed and swappable, false otherwise
   */
  bool isClosedAndSwappable(const std::string &gameName) const;

  // --- Set Symbol Access ---

  /**
   * @brief Returns the ObjectBoxDB for Set Symbols
   * @return The set symbol database, or nullptr on failure.
   *
   * Initializes the database if not already open. Used for MTG set symbol
   * recognition via embedding search.
   *
   * Hold on to what you get back for as long as you are reading from it, same
   * as getOrCreateStore().
   */
  GameStorePtr getSetSymbolStore();

  /**
   * @brief Closes the set symbol store
   *
   * Required before swapping the set symbol database file. Releases all
   * resources associated with the set symbol database.
   */
  void closeSetSymbolStore();

  // --- Management ---

  /**
   * @brief Scans filesystem for existing database directories
   *
   * Populates knownGames_ with all games that have database directories.
   * Called automatically during initialization.
   */
  void scanForExistingStores();

  /**
   * @brief Returns the full path to a game's database directory
   * @param gameName Name of the game
   * @return Full filesystem path (e.g., "/path/to/db/mtg")
   */
  std::string getStorePath(const std::string &gameName) const;

  /**
   * @brief Atomically swaps a game's database file with a new one
   * @param gameName Name of the game whose database to swap
   * @param sourcePath Path to the new database file (data.mdb)
   * @return true if swap succeeded, false otherwise
   *
   * Process:
   * 1. Verifies store is closed
   * 2. Copies source to temporary location
   * 3. Removes old database
   * 4. Moves temporary to final location
   *
   * Thread-safe. Fails if store is currently open.
   */
  bool swapDatabaseFile(const std::string &gameName,
                        const std::string &sourcePath);

  /**
   * @brief Checks whether an exact card_id exists in a game's database
   * @param gameName Name of the game (e.g., "mtg", "riftbound")
   * @param cardId The exact card_id string to look up
   * @return true if a card with this exact card_id exists, false otherwise
   */
  bool cardIdExists(const std::string &gameName, const std::string &cardId);

  /**
   * @brief Deletes a game's database directory and all its contents
   * @param gameName Name of the game whose database to delete
   * @return true if deletion succeeded, false otherwise
   *
   * Process:
   * 1. Closes the store if it's open
   * 2. Removes the entire database directory
   * 3. Removes from knownGames set
   *
   * Thread-safe. Fails if directory doesn't exist or can't be removed.
   */
  bool deleteDatabaseDirectory(const std::string &gameName);
};

} // namespace cardscanner

#endif // DATABASE_MANAGER_H