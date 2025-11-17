#include "DatabaseManager.h"

namespace cardscanner {

DatabaseManager& DatabaseManager::getInstance() {
    static DatabaseManager instance;
    return instance;
}

void DatabaseManager::setBasePath(const std::string& basePath) {
    this->basePath = basePath;
}

std::string DatabaseManager::getDatabasePath(const std::string& dbName) {
    if (basePath.empty()) {
        // Or handle this error more gracefully
        return "";
    }
    return basePath + "/" + dbName + "/data.mdb";
}

} // namespace cardscanner
