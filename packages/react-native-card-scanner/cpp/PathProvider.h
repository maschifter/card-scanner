#ifndef PATHPROVIDER_H
#define PATHPROVIDER_H

#include <string>

namespace pathprovider {
    void set_db_path(const std::string& path);
    std::string get_db_path();
}

#endif // PATHPROVIDER_H
