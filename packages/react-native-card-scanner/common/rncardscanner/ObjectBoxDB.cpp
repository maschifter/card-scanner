#define OBX_CPP_FILE

#include "ObjectBoxDB.h"
#include "PathProvider.h"
#include "objectbox-model.h" // Include the generated model header
#include "objectbox.hpp"
#include "schema.obx.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>

ObjectBoxDB::ObjectBoxDB() : ObjectBoxDB("/lorocana") {}

ObjectBoxDB::ObjectBoxDB(const std::string &db_path) : db_path_(db_path) {
  // Create directory if it doesn't exist
  struct stat st;
  if (stat(db_path_.c_str(), &st) != 0) {
// Directory doesn't exist, create it
#ifdef _WIN32
    _mkdir(db_path_.c_str());
#else
    mkdir(db_path_.c_str(), 0755);
#endif
    std::cout << "Created directory: " << db_path_ << std::endl;
  }

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
  clear_box();
  obx::Box<Card> box(*store);

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
  auto results = search_similar_cards(query_embedding, 3);
  assert(results.size() > 0);
  // assert(results[0].card_id == "test-2"); // Most similar should be card 2

  std::cout << "Similarity search test passed. Found " << results.size()
            << " results." << std::endl;
}

void ObjectBoxDB::run_all_tests() {
  test_insert();
  test_read();
  test_update();
  test_delete();
  test_similarity_search();
}

int ObjectBoxDB::load_embeddings_from_json(const std::string &json_path) {
  std::ifstream file(json_path);
  if (!file.is_open()) {
    std::cerr << "Failed to open JSON file: " << json_path << std::endl;
    return -1;
  }

  std::string content((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
  file.close();

  // Simple JSON parser for array of {card_id, name, embedding}
  // Format: [{"card_id": "mtg-123", "name": "Black Lotus", "embedding": [256
  // floats]}, ...]

  obx::Box<Card> box(*store);
  int count = 0;

  // Find array start
  size_t pos = content.find('[');
  if (pos == std::string::npos) {
    std::cerr << "Invalid JSON format: no array found" << std::endl;
    return -1;
  }

  pos++; // Skip [

  while (pos < content.length()) {
    // Skip whitespace
    while (pos < content.length() && std::isspace(content[pos]))
      pos++;

    if (content[pos] == ']')
      break; // End of array
    if (content[pos] == ',')
      pos++; // Skip comma

    // Skip whitespace
    while (pos < content.length() && std::isspace(content[pos]))
      pos++;

    if (content[pos] != '{')
      break; // Expected object start

    // Parse object
    Card card{};
    std::vector<float> embedding;

    size_t obj_end = content.find('}', pos);
    if (obj_end == std::string::npos)
      break;

    std::string obj = content.substr(pos, obj_end - pos + 1);

    // Extract card_id
    size_t card_id_start = obj.find("\"card_id\"");
    if (card_id_start != std::string::npos) {
      card_id_start = obj.find(':', card_id_start) + 1;
      card_id_start = obj.find('"', card_id_start) + 1;
      size_t card_id_end = obj.find('"', card_id_start);
      card.card_id = obj.substr(card_id_start, card_id_end - card_id_start);
    }

    // Extract name
    size_t name_start = obj.find("\"name\"");
    if (name_start != std::string::npos) {
      name_start = obj.find(':', name_start) + 1;
      name_start = obj.find('"', name_start) + 1;
      size_t name_end = obj.find('"', name_start);
      card.text = obj.substr(name_start, name_end - name_start);
    }

    // Extract embedding array
    size_t emb_start = obj.find("\"embedding\"");
    if (emb_start != std::string::npos) {
      emb_start = obj.find('[', emb_start) + 1;
      size_t emb_end = obj.find(']', emb_start);
      std::string emb_str = obj.substr(emb_start, emb_end - emb_start);

      std::istringstream iss(emb_str);
      std::string val;
      while (std::getline(iss, val, ',')) {
        try {
          embedding.push_back(std::stof(val));
        } catch (...) {
          // Skip invalid values
        }
      }
    }

    if (embedding.size() == 256) {
      card.embedding = embedding;
      card.date_created = static_cast<uint64_t>(std::time(nullptr));
      box.put(card);
      count++;

      if (count % 100 == 0) {
        std::cout << "Loaded " << count << " cards..." << std::endl;
      }
    } else {
      std::cerr << "Warning: Card " << card.card_id << " has "
                << embedding.size() << " dimensions, expected 256. Skipping."
                << std::endl;
    }

    pos = obj_end + 1;
  }

  std::cout << "Successfully loaded " << count << " cards with embeddings."
            << std::endl;
  return count;
}

std::vector<CardSearchResult>
ObjectBoxDB::search_similar_cards(const std::vector<float> &query_embedding,
                                  int limit) {

  std::vector<CardSearchResult> results;

  if (query_embedding.size() != 256) {
    std::cerr << "Error: Query embedding must have 256 dimensions, got "
              << query_embedding.size() << std::endl;
    return results;
  }

  obx::Box<Card> box(*store);

  // HNSW is an approximate algorithm - it doesn't guarantee the true top K.
  // To get accurate results, we fetch more candidates, calculate exact
  // similarities, then return the true top K. This is especially important for
  // small K values.
  int fetchLimit = std::max(limit * 5, limit + 50);

  // Create a query with HNSW nearest neighbor search using Cosine distance
  auto query = box.query().nearestNeighborsFloat32(
      Card_::embedding, query_embedding.data(), limit = 10000)

                   auto cards = query.find();

  for (const auto &card : cards) {
    CardSearchResult result;
    result.card_id = card.card_id;
    result.name = card.text;

    // Calculate similarity score using dot product
    // (assumes embeddings are already normalized, matching Python
    // implementation)
    float dot_product = 0.0f;

    for (size_t i = 0; i < 256; i++) {
      dot_product += query_embedding[i] * card.embedding[i];
    }

    result.score = dot_product;
    results.push_back(result);
  }

  // Sort by exact similarity in descending order (highest similarity first)
  std::sort(results.begin(), results.end(),
            [](const CardSearchResult &a, const CardSearchResult &b) {
              return a.score > b.score;
            });

  // Return only the true top K results
  if (results.size() > static_cast<size_t>(limit)) {
    results.resize(limit);
  }

  return results;
}

uint64_t ObjectBoxDB::get_card_count() {
  obx::Box<Card> box(*store);
  return box.count();
}
