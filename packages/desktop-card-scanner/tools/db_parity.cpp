// Compares two card databases: card-id coverage and, for the cards both hold,
// whether the stored embeddings agree.
//
// File hashes cannot answer this, because two stores built from identical data
// differ byte-for-byte over LMDB page state, and row counts only say the sets
// are the same size.
//
// Core owns the single OBX_CPP_FILE translation unit (ObjectBoxDB.cpp), so this
// file must not define it again.
//
//   db_parity <dirA> <dirB>     each a directory containing data.mdb

#include <objectbox-model.h>
#include <objectbox.hpp>
#include <schema.obx.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Store {
  std::unique_ptr<obx::Store> store;

  explicit Store(const std::string &dir) {
    obx::Options options(create_obx_model());
    options.directory(dir);
    // Opened the way core opens it, so migration behaviour matches the app.
    store = std::make_unique<obx::Store>(options);
  }
  ~Store() {
    if (store) {
      store->close();
    }
  }
};

void dumpMetadata(obx::Store &store, const char *label) {
  obx::Box<Metadata> box(store);
  const auto entries = box.getAll();
  std::printf("  %s metadata: %zu entr%s", label, entries.size(),
              entries.size() == 1 ? "y" : "ies");
  for (const auto &entry : entries) {
    std::printf("\n    %s = %s", entry->key.c_str(), entry->value.c_str());
  }
  std::printf("\n");
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <dirA> <dirB>\n", argv[0]);
    return 1;
  }

  try {
    Store a(argv[1]);
    Store b(argv[2]);

    obx::Box<Card> boxA(*a.store);
    obx::Box<Card> boxB(*b.store);

    std::printf("A: %s\nB: %s\n\n", argv[1], argv[2]);
    std::printf("  A cards: %llu\n", (unsigned long long)boxA.count());
    std::printf("  B cards: %llu\n", (unsigned long long)boxB.count());
    dumpMetadata(*a.store, "A");
    dumpMetadata(*b.store, "B");

    // Load A, then stream B against it so only one full copy is resident.
    std::map<std::string, std::vector<float>> left;
    for (const auto &card : boxA.getAll()) {
      left.emplace(card->card_id, card->embedding);
    }

    size_t common = 0, identical = 0, onlyB = 0, dimMismatch = 0;
    double worstDiff = 0.0;
    double worstCosine = 2.0, cosineSum = 0.0;
    size_t cosineCount = 0;
    std::vector<std::string> examplesOnlyB;

    for (const auto &card : boxB.getAll()) {
      const auto it = left.find(card->card_id);
      if (it == left.end()) {
        onlyB++;
        if (examplesOnlyB.size() < 3) {
          examplesOnlyB.push_back(card->card_id);
        }
        continue;
      }

      common++;
      if (it->second.size() != card->embedding.size()) {
        dimMismatch++;
      } else {
        double worst = 0.0;
        double dot = 0.0;
        for (size_t i = 0; i < card->embedding.size(); i++) {
          worst = std::max(worst,
                           std::fabs(double(it->second[i]) - double(card->embedding[i])));
          dot += double(it->second[i]) * double(card->embedding[i]);
        }
        if (worst == 0.0) {
          identical++;
        }
        worstDiff = std::max(worstDiff, worst);
        // Both sides are L2-normalised, so the dot product is the cosine
        // similarity the scanner would score.
        worstCosine = std::min(worstCosine, dot);
        cosineSum += dot;
        cosineCount++;
      }
      left.erase(it);
    }

    const size_t onlyA = left.size();
    std::printf("\n  common card ids:   %zu\n", common);
    std::printf("  only in A:         %zu\n", onlyA);
    std::printf("  only in B:         %zu\n", onlyB);
    for (const auto &id : examplesOnlyB) {
      std::printf("    e.g. only in B: %s\n", id.c_str());
    }
    size_t shownA = 0;
    for (const auto &[id, _] : left) {
      if (shownA++ >= 3) {
        break;
      }
      std::printf("    e.g. only in A: %s\n", id.c_str());
    }

    if (dimMismatch) {
      std::printf("  DIMENSION MISMATCH on %zu card(s)\n", dimMismatch);
    }
    if (common) {
      std::printf("\n  embeddings identical: %zu/%zu (%.1f%%)\n", identical, common,
                  100.0 * double(identical) / double(common));
      std::printf("  worst element diff:   %g\n", worstDiff);
      if (cosineCount) {
        std::printf("  cosine same-card:     min %.6f  mean %.6f\n",
                    worstCosine, cosineSum / double(cosineCount));
      }
    }

    const bool sameIds = onlyA == 0 && onlyB == 0;
    const bool sameVectors = common > 0 && identical == common;
    std::printf("\n  verdict: card ids %s, embeddings %s\n",
                sameIds ? "MATCH" : "DIFFER",
                sameVectors ? "MATCH" : "DIFFER");
    return (sameIds && sameVectors) ? 0 : 2;

  } catch (const std::exception &e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
