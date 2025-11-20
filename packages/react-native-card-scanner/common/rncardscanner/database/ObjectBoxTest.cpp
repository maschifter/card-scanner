#include "ObjectBoxTest.h"
#include "PathProvider.h"
#include "objectbox-model.h"
#include "schema.obx.hpp"
#include <cassert>
#include <iostream>
#include <sys/stat.h>
#include <vector>

namespace objectboxtest {

std::string ObjectBoxTest::runTest() {
  try {
    std::cout << "Running ObjectBox test..." << std::endl;
    std::string db_path = pathprovider::get_db_path() + "/lorocana";
    ObjectBoxDB db(db_path);
    run_all_tests(db);
    return "ObjectBox tests executed successfully!";
  } catch (const std::exception &e) {
    return "ObjectBox tests failed: " + std::string(e.what());
  }
}

void ObjectBoxTest::test_insert(ObjectBoxDB &db) {
  std::cout << "Running test_insert..." << std::endl;
  db.clear_box();
  obx::Box<Card> box(*db.store);

  Card new_card{};
  new_card.text = "Buy milk";
  obx_id id = box.put(new_card);

  assert(id != 0);
  auto card = box.get(id);
  assert(card);
  assert(card->text == "Buy milk");

  std::cout << "Insert test passed." << std::endl;
}

void ObjectBoxTest::test_read(ObjectBoxDB &db) {
  std::cout << "Running test_read..." << std::endl;
  db.clear_box();
  obx::Box<Card> box(*db.store);

  Card new_card{};
  new_card.text = "Test read";
  obx_id id = box.put(new_card);

  auto card = box.get(id);
  assert(card);
  assert(card->text == "Test read");

  std::cout << "Read test passed." << std::endl;
}

void ObjectBoxTest::test_update(ObjectBoxDB &db) {
  std::cout << "Running test_update..." << std::endl;
  db.clear_box();
  obx::Box<Card> box(*db.store);

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

void ObjectBoxTest::test_delete(ObjectBoxDB &db) {
  std::cout << "Running test_delete..." << std::endl;
  db.clear_box();
  obx::Box<Card> box(*db.store);

  Card new_card{};
  new_card.text = "Test delete";
  obx_id id = box.put(new_card);

  box.remove(id);
  auto card = box.get(id);
  assert(!card);

  std::cout << "Delete test passed." << std::endl;
}

void ObjectBoxTest::test_similarity_search(ObjectBoxDB &db) {
  std::cout << "Running test_similarity_search..." << std::endl;
  db.clear_box();
  obx::Box<Card> box(*db.store);

  // Insert test cards with embeddings
  for (int i = 0; i < 5; i++) {
    Card card{};
    card.card_id = "test-" + std::to_string(i);
    card.text = "Test Card " + std::to_string(i);
    card.date_created = static_cast<uint64_t>(std::time(nullptr));

    // Create a simple embedding (256 dimensions)
    std::vector<float> embedding(256);
    for (size_t j = 0; j < 256; j++) {
      embedding[j] = static_cast<float>(i + j * 0.01);
    }
    card.embedding = embedding;
    box.put(card);
  }

  // Create a query embedding similar to card 2
  std::vector<float> query_embedding(256);
  for (size_t j = 0; j < 256; j++) {
    query_embedding[j] = static_cast<float>(2 + j * 0.01);
  }

  // Search for similar cards
  auto results = db.search_similar_cards(query_embedding, 3);
  assert(results.size() > 0);
  // assert(results[0].card_id == "test-2"); // Most similar should be card 2

  std::cout << "Similarity search test passed. Found " << results.size()
            << " results." << std::endl;
}

void ObjectBoxTest::run_all_tests(ObjectBoxDB &db) {
  test_insert(db);
  test_read(db);
  test_update(db);
  test_delete(db);
  test_similarity_search(db);
}

} // namespace objectboxtest
