#define OBX_CPP_FILE

#include "ObjectBoxDB.h"
#include "../Constants.h"
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

using namespace rncardscanner::constants;

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
  }

  obx::Options options(create_obx_model());
  options.directory(db_path_);
  store = std::make_unique<obx::Store>(options);
}

ObjectBoxDB::~ObjectBoxDB() {
  if (store) {
    store->close();
    store.reset();
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

  if (query_embedding.size() != database::EMBEDDING_VECTOR_SIZE) {
    std::cerr << "Error: Query embedding must have "
              << database::EMBEDDING_VECTOR_SIZE << " dimensions, got "
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
                       limit) // Fetch more candidates for better accuracy
                   .build();

  auto cards = query.find();

  // Calculate dot product similarity for each candidate
  for (const auto &card : cards) {
    CardSearchResult result;
    result.card_id = card.card_id;
    result.name = card.card_id;

    // Calculate similarity score using dot product (cosine similarity for
    // normalized embeddings)
    float dot_product = 0.0f;
    for (size_t i = 0; i < database::EMBEDDING_VECTOR_SIZE; i++) {
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

std::vector<SetSymbolMatch> ObjectBoxDB::search_similar_set_symbols(
    const std::vector<float> &query_embedding, int limit) {
  std::vector<SetSymbolMatch> results;

  try {
    obx::Box<SetSymbol> box(*store);

    // Perform HNSW vector search
    auto query = box.query()
                     .nearestNeighborsFloat32(SetSymbol_::embedding,
                                              query_embedding.data(), limit)
                     .build();

    auto symbols = query.find();

    // Calculate dot product similarity for each symbol
    for (const auto &symbol : symbols) {
      SetSymbolMatch match;
      match.setCode = symbol.set_code;

      // Calculate cosine similarity (dot product of normalized vectors)
      float dotProduct = 0.0f;
      for (size_t i = 0;
           i < query_embedding.size() && i < symbol.embedding.size(); i++) {
        dotProduct += query_embedding[i] * symbol.embedding[i];
      }
      match.similarity = dotProduct;
      results.push_back(match);
    }

    // Sort by similarity (highest first)
    std::sort(results.begin(), results.end(),
              [](const SetSymbolMatch &a, const SetSymbolMatch &b) {
                return a.similarity > b.similarity;
              });

  } catch (const std::exception &e) {
    std::cerr << "SetSymbol search failed: " << e.what() << std::endl;
  }

  return results;
}

uint64_t ObjectBoxDB::get_card_count() {
  obx::Box<Card> box(*store);
  return box.count();
}

uint64_t ObjectBoxDB::get_set_symbol_count() {
  try {
    obx::Box<SetSymbol> box(*store);
    return box.count();
  } catch (const std::exception &e) {
    std::cerr << "Failed to get set symbol count: " << e.what() << std::endl;
    return 0;
  }
}

std::string ObjectBoxDB::get_metadata_value(const std::string &key) {
  try {
    obx::Box<Metadata> box(*store);
    auto query = box.query(Metadata_::key.equals(key)).build();
    auto results = query.find();
    if (!results.empty()) {
      return results[0].value;
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to get metadata value for key '" << key
              << "': " << e.what() << std::endl;
  }
  return "";
}
