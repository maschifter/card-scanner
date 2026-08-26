#include "PathProvider.h"

#include <stdexcept>

namespace pathprovider {
    static std::string db_path;
    static std::mutex db_path_mutex;

    static std::string cache_path;
    static std::mutex cache_path_mutex;

    void set_db_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(db_path_mutex);
        db_path = path;
    }

    std::string get_db_path() {
        std::lock_guard<std::mutex> lock(db_path_mutex);
        // Failing here beats returning "" - an empty base path makes the store
        // scan resolve to nothing and log a warning, which reads like "no
        // databases installed" rather than "the integration forgot to call
        // set_db_path()".
        if (db_path.empty()) {
            throw std::logic_error(
                "pathprovider: db path not set. Call pathprovider::set_db_path() "
                "before using DatabaseManager (see PathProvider.h).");
        }
        return db_path;
    }

    void set_cache_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(cache_path_mutex);
        cache_path = path;
    }

    std::string get_cache_path() {
        std::lock_guard<std::mutex> lock(cache_path_mutex);
        if (cache_path.empty()) {
            throw std::logic_error(
                "pathprovider: cache path not set. Call "
                "pathprovider::set_cache_path() before saving images "
                "(see PathProvider.h).");
        }
        return cache_path;
    }
}
