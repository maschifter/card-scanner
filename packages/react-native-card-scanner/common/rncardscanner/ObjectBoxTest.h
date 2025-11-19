
#pragma once

#include "ObjectBoxDB.h"
#include <string>

namespace objectboxtest {

class ObjectBoxTest {
public:
  static std::string runTest();

private:
  static void run_all_tests(ObjectBoxDB &db);
  static void test_insert(ObjectBoxDB &db);
  static void test_read(ObjectBoxDB &db);
  static void test_update(ObjectBoxDB &db);
  static void test_delete(ObjectBoxDB &db);
  static void test_similarity_search(ObjectBoxDB &db);
};

} // namespace objectboxtest
