#ifndef OBJECTBOXDB_H
#define OBJECTBOXDB_H

#include "objectbox.hpp"
#include <string>
#include <vector>

namespace objectboxtest {
class ObjectBoxTest;
}

struct CardSearchResult {
  std::string card_id;
  std::string name;
  std::string gameName; // Which game database this result is from
  double score;
};

class ObjectBoxDB {
  friend class objectboxtest::ObjectBoxTest;

public:
  ObjectBoxDB(const std::string &db_path);
  ~ObjectBoxDB();

  // Similarity search: returns top N most similar cards
  std::vector<CardSearchResult>
  search_similar_cards(const std::vector<float> &query_embedding,
                       int limit = 100);

  // Get total card count
  uint64_t get_card_count();

private:
  std::unique_ptr<obx::Store> store;
  // Takes Full Path!
  std::string db_path_;
  void clear_box();
};

#endif // OBJECTBOXDB_H
