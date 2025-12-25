#ifndef OBJECTBOXDB_H
#define OBJECTBOXDB_H

#include "objectbox.hpp"
#include <string>
#include <vector>

struct CardSearchResult {
  std::string card_id;
  std::string name;
  std::string gameName; // Which game database this result is from
  double score;
};

struct SetSymbolMatch {
  std::string setCode;
  float similarity;
};

class ObjectBoxDB {

public:
  ObjectBoxDB(const std::string &db_path);
  ~ObjectBoxDB();

  // Similarity search: returns top N most similar cards
  std::vector<CardSearchResult>
  search_similar_cards(const std::vector<float> &query_embedding,
                       int limit = 100);

  // Similarity search for MTG set symbols
  std::vector<SetSymbolMatch>
  search_similar_set_symbols(const std::vector<float> &query_embedding,
                             int limit = 5);

  // Get total card count
  uint64_t get_card_count();

  // Get total set symbol count (for MTG set symbol databases)
  uint64_t get_set_symbol_count();

  // Get metadata value for a given key
  std::string get_metadata_value(const std::string &key);

private:
  std::unique_ptr<obx::Store> store;
  // Takes Full Path!
  std::string db_path_;
  void clear_box();
};

#endif // OBJECTBOXDB_H
