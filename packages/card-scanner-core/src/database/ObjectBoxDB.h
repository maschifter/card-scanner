#ifndef OBJECTBOXDB_H
#define OBJECTBOXDB_H

#include "../Constants.h"
#include "../types/SearchResults.h"
#include "objectbox.hpp"
#include <string>
#include <vector>

class ObjectBoxDB {

public:
  ObjectBoxDB(const std::string &db_path);
  ~ObjectBoxDB();

  // Similarity search: returns top N most similar cards
  // NOTE: this class predates the cardscanner namespace and still sits at
  // global scope, hence the fully-qualified constants.
  std::vector<cardscanner::CardSearchResult> search_similar_cards(
      const std::vector<float> &query_embedding,
      int limit = cardscanner::constants::database::MAX_SEARCH_RESULTS);

  // Similarity search for MTG set symbols
  std::vector<cardscanner::SetSymbolMatch> search_similar_set_symbols(
      const std::vector<float> &query_embedding,
      int limit = cardscanner::constants::database::SET_SYMBOL_MAX_RESULTS);

  // Exact card_id lookup, for verifying benchmark ground-truth labels
  // independently of the embedding similarity search.
  bool card_id_exists(const std::string &card_id);

  // Get total card count
  uint64_t get_card_count();

  // Get metadata value for a given key
  std::string get_metadata_value(const std::string &key);

private:
  std::unique_ptr<obx::Store> store;
  // Takes Full Path!
  std::string db_path_;
};

#endif // OBJECTBOXDB_H
