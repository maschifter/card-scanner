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

  // Use HNSW nearest neighbor search to get candidate results efficiently
  // Note: HNSW may use different distance metric internally, so we recalculate
  // and sort by dot product Still much faster than brute force: HNSW gives us
  // top candidates in O(log N), we only sort those
  auto query = box.query()
                   .nearestNeighborsFloat32(
                       Card_::embedding, query_embedding.data(),
                       100) // Fetch more candidates for better accuracy
                   .build();

  auto cards = query.find();

  // Calculate dot product similarity for each candidate
  for (const auto &card : cards) {
    CardSearchResult result;
    result.card_id = card.card_id;
    result.name = card.text;

    // Calculate similarity score using dot product (cosine similarity for
    // normalized embeddings)
    float dot_product = 0.0f;
    for (size_t i = 0; i < 256; i++) {
      dot_product += query_embedding[i] * card.embedding[i];
    }
    result.score = dot_product;

    results.push_back(result);
  }

  // Sort by dot product score (highest similarity first)
  std::sort(results.begin(), results.end(),
            [](const CardSearchResult &a, const CardSearchResult &b) {
              return a.score > b.score;
            });

  // Return only the requested number of results
  if (results.size() > static_cast<size_t>(limit)) {
    results.resize(limit);
  }

  return results;
}

uint64_t ObjectBoxDB::get_card_count() {
  obx::Box<Card> box(*store);
  return box.count();
}

