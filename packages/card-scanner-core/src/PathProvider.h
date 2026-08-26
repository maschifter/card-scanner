#ifndef PATHPROVIDER_H
#define PATHPROVIDER_H

#include <mutex>
#include <string>

/**
 * @file PathProvider.h
 * @brief Where this process keeps its databases and scratch images.
 *
 * CONTRACT: the integration layer MUST call set_db_path() and set_cache_path()
 * before anything touches DatabaseManager. DatabaseManager reads the db path
 * once, when its singleton is first constructed, so a later call has no effect
 * on an already-created instance.
 *
 * Both mobile shims already do this - see CardScanner.mm and
 * CardScannerInstallerModule.cpp, which set the paths before installing the JSI
 * bindings. A desktop wrapper must do the same from main(), e.g.
 * ~/Library/Application Support/<app> on macOS or %LOCALAPPDATA%\<app> on
 * Windows.
 *
 * get_db_path() throws if the path was never set, rather than returning an
 * empty string and letting the database silently resolve to nothing.
 */
namespace pathprovider {
    // Database directory (persistent storage).
    void set_db_path(const std::string& path);

    /// @throws std::logic_error if set_db_path() has not been called.
    std::string get_db_path();

    // Cache directory (temporary storage for images).
    void set_cache_path(const std::string& path);

    /// @throws std::logic_error if set_cache_path() has not been called.
    std::string get_cache_path();
}

#endif // PATHPROVIDER_H
