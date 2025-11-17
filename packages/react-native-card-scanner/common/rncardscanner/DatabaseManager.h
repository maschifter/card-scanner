#pragma once

#include <string>
#include <map>

namespace cardscanner {

class DatabaseManager {
public:
    // Get the singleton instance of the DatabaseManager
    static DatabaseManager& getInstance();

    // Set the base path for all databases
    void setBasePath(const std::string& basePath);

    // Get the path to a specific database
    std::string getDatabasePath(const std::string& dbName);

private:
    // Private constructor for singleton pattern
    DatabaseManager() = default;

    // Delete copy constructor and assignment operator for singleton
    DatabaseManager(const DatabaseManager&) = delete;
    void operator=(const DatabaseManager&) = delete;

    std::string basePath;
};

} // namespace cardscanner
