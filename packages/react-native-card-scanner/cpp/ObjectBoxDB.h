#ifndef OBJECTBOXDB_H
#define OBJECTBOXDB_H

#include "objectbox.hpp"
#include <string>

class ObjectBoxDB {
public:
    ObjectBoxDB();
    ObjectBoxDB(const std::string& db_path);
    ~ObjectBoxDB();
    void run_all_tests();

private:
    std::unique_ptr<obx::Store> store;
    std::string db_path_;
    void clear_box();
    void test_insert();
    void test_read();
    void test_update();
    void test_delete();
    void test_similarity_search();
};

#endif // OBJECTBOXDB_H
