#include "SetSymbolDatabase.h"
#include "../database/schema.obx.hpp" // Include schema for query builders
#include "DatabaseManager.h"          // Include the manager
#include <algorithm>
#include <iostream>

namespace cardscanner {

std::vector<SetSymbolMatch>
SetSymbolDatabase::search(const std::vector<float> &embedding, int maxResults) {
  std::vector<SetSymbolMatch> results;

  // 1. Get Store from Singleton
  obx::Store *store = DatabaseManager::getInstance().getSetSymbolStore();

  if (!store) {
    std::cerr << "SetSymbol database could not be loaded via DatabaseManager"
              << std::endl;
    return results;
  }

  try {
    // 2. Create Box using the borrowed store
    obx::Box<SetSymbol> box(*store);

    // Perform HNSW vector search
    auto query = box.query()
                     .nearestNeighborsFloat32(SetSymbol_::embedding,
                                              embedding.data(), maxResults)
                     .build();

    auto symbols = query.find();

    // ... (Result processing logic remains exactly the same) ...
    for (const auto &symbol : symbols) {
      SetSymbolMatch match;
      match.setCode = symbol.set_code;
      match.setName = symbol.set_name;
      match.variant = symbol.variant;

      float dotProduct = 0.0f;
      for (size_t i = 0; i < embedding.size() && i < symbol.embedding.size();
           i++) {
        dotProduct += embedding[i] * symbol.embedding[i];
      }
      match.similarity = dotProduct;
      results.push_back(match);
    }

    std::sort(results.begin(), results.end(),
              [](const SetSymbolMatch &a, const SetSymbolMatch &b) {
                return a.similarity > b.similarity;
              });

  } catch (const std::exception &e) {
    std::cerr << "SetSymbol search failed: " << e.what() << std::endl;
  }

  return results;
}

uint64_t SetSymbolDatabase::count() const {
  obx::Store *store = DatabaseManager::getInstance().getSetSymbolStore();
  if (!store)
    return 0;

  try {
    obx::Box<SetSymbol> box(*store);
    return box.count();
  } catch (const std::exception &e) {
    return 0;
  }
}

} // namespace cardscanner