#ifndef PATHPROVIDER_H
#define PATHPROVIDER_H

#include <mutex>
#include <string>

namespace pathprovider {
    // Database directory (persistent storage)
    void set_db_path(const std::string& path);
    std::string get_db_path();

    // Cache directory (temporary storage for images)
    void set_cache_path(const std::string& path);
    std::string get_cache_path();
}

#endif // PATHPROVIDER_H
