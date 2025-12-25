#include "PathProvider.h"

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
        return db_path;
    }

    void set_cache_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(cache_path_mutex);
        cache_path = path;
    }

    std::string get_cache_path() {
        std::lock_guard<std::mutex> lock(cache_path_mutex);
        return cache_path;
    }
}
