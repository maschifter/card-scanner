#ifndef SET_SYMBOL_DATABASE_H
#define SET_SYMBOL_DATABASE_H

#include <string>
#include <vector>

namespace cardscanner {

struct SetSymbolMatch {
  std::string setCode;
  std::string setName;
  std::string variant;
  float similarity;
};

class SetSymbolDatabase {
public:
  // Constructor no longer needs a path!
  SetSymbolDatabase() = default;
  ~SetSymbolDatabase() = default;

  // Logic remains the same, but implementation changes
  std::vector<SetSymbolMatch> search(const std::vector<float> &embedding,
                                     int maxResults = 5);

  uint64_t count() const;
};

} // namespace cardscanner

#endif // SET_SYMBOL_DATABASE_H