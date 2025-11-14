#include "PathProvider.h"

namespace pathprovider {
    static std::string db_path;

    void set_db_path(const std::string& path) {
        db_path = path;
    }

    std::string get_db_path() {
        return db_path;
    }
}
