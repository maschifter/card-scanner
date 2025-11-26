#define OBX_CPP_FILE
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "objectbox-model.h"
#include "objectbox.hpp"
#include "schema.obx.hpp"

using namespace obx;
using json = nlohmann::json;
int main(int argc, char *argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " <json_path> <db_directory>"
              << std::endl;
    return 1;
  }

  std::string json_path = argv[1];
  std::string db_dir = argv[2];

  std::cout << "Loading data from '" << json_path << "'..." << std::endl;

  std::vector<Card> cards_to_put;

  try {
    std::ifstream f(json_path);
    if (!f.is_open()) {
      std::cerr << "Error: Cannot open JSON file: " << json_path << std::endl;
      return 1;
    }

    json data = json::parse(f);
    f.close();

    if (!data.is_array()) {
      std::cerr << "Error: Was expecting array [...]." << std::endl;
      return 1;
    }

    std::cout << "Parsed " << data.size() << " cards from JSON file."
              << std::endl;
    cards_to_put.reserve(data.size());

    for (const auto &item : data) {
      Card card;
      card.id = 0;

      card.card_id = item.at("card_id").get<std::string>();
      card.text = item.at("text").get<std::string>();
      card.date_created = item.at("date_created").get<uint64_t>();

      card.embedding = item.at("embedding").get<std::vector<float>>();

      cards_to_put.push_back(std::move(card));
    }

  } catch (const json::parse_error &e) {
    std::cerr << "Error parsing JSON file: " << e.what() << std::endl;
    return 1;
  } catch (const json::type_error &e) {
    std::cerr << "Error: Type mismatch in JSON data (e.g., string instead of "
                 "number): "
              << e.what() << std::endl;
    return 1;
  } catch (const json::out_of_range &e) {
    std::cerr
        << "Error: Missing key in JSON object (e.g., missing 'embedding'): "
        << e.what() << std::endl;
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "Other error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Initializing ObjectBox Store in '" << db_dir << "'..."
            << std::endl;
  obx::Options options(create_obx_model());
  options.directory(db_dir);

  Store store(options);
  Box<Card> box(store);

  box.removeAll();
  std::cout << "Removed old data." << std::endl;

  std::cout << "Inserting objects..." << std::endl;

  box.put(cards_to_put);

  std::cout << "Successfully inserted " << box.count() << " cards."
            << std::endl;

  std::cout << "Finished." << std::endl;
  return 0;
}
