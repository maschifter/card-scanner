#include "ObjectBoxTest.h"
#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include <iostream>

namespace objectboxtest {

std::string ObjectBoxTest::runTest() {
  try {
    std::cout << "Running ObjectBox test..." << std::endl;
    std::string db_path = pathprovider::get_db_path() + "/lorocana";
    ObjectBoxDB db(db_path);
    db.run_all_tests();
    return "ObjectBox tests executed successfully!";
  } catch (const std::exception &e) {
    return "ObjectBox tests failed: " + std::string(e.what());
  }
}

} // namespace objectboxtest
