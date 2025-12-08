#include "PathProvider.h"

namespace pathprovider {
    static std::string db_path;
    static std::mutex db_path_mutex;

    void set_db_path(const std::string& path) {
        std::lock_guard<std::mutex> lock(db_path_mutex);
        db_path = path;
    }

    std::string get_db_path() {
        std::lock_guard<std::mutex> lock(db_path_mutex);
        return db_path;
    }
}
