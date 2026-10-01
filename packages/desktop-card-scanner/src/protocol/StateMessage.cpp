#include "StateMessage.h"

#include "../service/ProductClient.h"
#include "../service/ScanSession.h"
#include "../service/ScannerService.h"

#include <nlohmann/json.hpp>

namespace cardscanner {
namespace desktop {

namespace {

using json = nlohmann::json;

json cardToJson(const CardInfo &card, ProductClient &products) {
  json out = {{"cardId", card.cardId},
              {"game", card.gameName},
              {"score", card.score},
              {"detections", card.detections}};
  // Absent until the lookup thread resolves it; the overlay shows the id.
  if (auto product = products.lookup(card.cardId)) {
    out["name"] = product->name;
    out["set"] = product->setName;
    out["imageUrl"] = product->imageUrl;
  }
  return out;
}

} // namespace

std::string stateMessage(ScannerService &service, ProductClient &products) {
  auto &session = service.session();
  const auto diagnostics = service.diagnostics();

  const auto config = session.config();
  json history = json::array();
  for (const auto &entry : session.history()) {
    history.push_back(cardToJson(entry, products));
  }

  json payload = {{"status", toString(session.status())},
                  {"mode", toString(session.mode())},
                  {"history", history},
                  {"settings",
                   {{"acceptScore", config.acceptScore},
                    {"stableDetections", config.stableDetections},
                    {"gracePeriodMs", config.gracePeriodMs},
                    {"emittedTimeoutMs", config.emittedTimeoutMs}}},
                  {"candidate", nullptr},
                  {"emitted", nullptr},
                  // Always present, so the overlay can say why nothing shows.
                  {"scan",
                   {{"live", diagnostics.hasFrame},
                    {"detections", diagnostics.detections},
                    {"detectionConfidence", diagnostics.detectionConfidence},
                    {"game", diagnostics.predictedGame},
                    {"gameConfidence", diagnostics.predictedGameConfidence},
                    {"topScore", diagnostics.topScore},
                    {"topCardId", diagnostics.topCardId},
                    {"ms", diagnostics.processingMs}}}};

  if (auto candidate = session.candidate()) {
    payload["candidate"] = cardToJson(*candidate, products);
  }
  if (auto emitted = session.emitted()) {
    payload["emitted"] = cardToJson(*emitted, products);
  }
  return json{{"type", "state"}, {"payload", payload}}.dump();
}

} // namespace desktop
} // namespace cardscanner
