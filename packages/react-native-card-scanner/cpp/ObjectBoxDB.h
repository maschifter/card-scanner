#ifndef OBJECTBOXDB_H
#define OBJECTBOXDB_H

#include "objectbox.hpp"
#include <string>
#include <vector>

struct CardSearchResult {
    std::string card_id;
    std::string name;
    double score;
};

class ObjectBoxDB {
public:
    ObjectBoxDB();
    ObjectBoxDB(const std::string& db_path);
    ~ObjectBoxDB();
    void run_all_tests();

    // Bulk load embeddings from JSON file
    int load_embeddings_from_json(const std::string& json_path);

    // Similarity search: returns top N most similar cards
    std::vector<CardSearchResult> search_similar_cards(const std::vector<float>& query_embedding, int limit = 10);

    // Get total card count
    uint64_t get_card_count();

private:
    std::unique_ptr<obx::Store> store;
    std::string db_path_;
    void clear_box();
    void test_insert();
    void test_read();
    void test_update();
    void test_delete();
    void test_similarity_search();
};

#endif // OBJECTBOXDB_H
