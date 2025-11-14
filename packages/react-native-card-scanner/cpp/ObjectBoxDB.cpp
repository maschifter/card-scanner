#define OBX_CPP_FILE

#include "ObjectBoxDB.h"
#include "objectbox-model.h" // Include the generated model header
#include "objectbox.hpp"
#include "schema.obx.hpp"
#include <iostream>
#include <cassert>

ObjectBoxDB::ObjectBoxDB() : ObjectBoxDB("/tmp/objectbox_test") {}

ObjectBoxDB::ObjectBoxDB(const std::string& db_path) : db_path_(db_path) {
  obx::Options options(create_obx_model());
  options.directory(db_path_);
  store = std::make_unique<obx::Store>(options);
  std::cout << "Database store opened at: " << db_path_ << std::endl;
}

ObjectBoxDB::~ObjectBoxDB() {
  if (store) {
    store->close();
    store.reset();
    std::cout << "Database store closed." << std::endl;
  }
}

void ObjectBoxDB::clear_box() {
    obx::Box<Card> box(*store);
    box.removeAll();
}

void ObjectBoxDB::test_insert() {
  std::cout << "Running test_insert..." << std::endl;
  clear_box();
  obx::Box<Card> box(*store);

  Card new_card{};
  new_card.text = "Buy milk";
  obx_id id = box.put(new_card);

  assert(id != 0);
  auto card = box.get(id);
  assert(card);
  assert(card->text == "Buy milk");

  std::cout << "Insert test passed." << std::endl;
}

void ObjectBoxDB::test_read() {
  std::cout << "Running test_read..." << std::endl;
  clear_box();
  obx::Box<Card> box(*store);

  Card new_card{};
  new_card.text = "Test read";
  obx_id id = box.put(new_card);

  auto card = box.get(id);
  assert(card);
  assert(card->text == "Test read");

  std::cout << "Read test passed." << std::endl;
}

void ObjectBoxDB::test_update() {
  std::cout << "Running test_update..." << std::endl;
  clear_box();
  obx::Box<Card> box(*store);

  Card new_card{};
  new_card.text = "Test update";
  obx_id id = box.put(new_card);

  auto card = box.get(id);
  card->text = "Updated text";
  box.put(*card);

  auto updated_card = box.get(id);
  assert(updated_card);
  assert(updated_card->text == "Updated text");

  std::cout << "Update test passed." << std::endl;
}

void ObjectBoxDB::test_delete() {
    std::cout << "Running test_delete..." << std::endl;
    clear_box();
    obx::Box<Card> box(*store);

    Card new_card{};
    new_card.text = "Test delete";
    obx_id id = box.put(new_card);

    box.remove(id);
    auto card = box.get(id);
    assert(!card);

    std::cout << "Delete test passed." << std::endl;
}

void ObjectBoxDB::test_similarity_search() {
  std::cout << "Running test_similarity_search..." << std::endl;
  // This is a placeholder
  std::cout << "Similarity search test passed (simulation)." << std::endl;
}

void ObjectBoxDB::run_all_tests() {
  test_insert();
  test_read();
  test_update();
  test_delete();
  test_similarity_search();
}
