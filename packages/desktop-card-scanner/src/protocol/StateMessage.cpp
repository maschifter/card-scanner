#include "StateMessage.h"

#include <core/ScanSession.h>
#include <service/ProductClient.h>
#include <service/ScannerService.h>

#include <nlohmann/json.hpp>

namespace cardscanner {
namespace desktop {

namespace {

using json = nlohmann::json;

json cardToJson(const CardSearchResult &card, ProductClient &products) {
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
  // Snapshot for one consistent view; diagnostics first, mirroring the
  // worker's publish order, so they can lag the session but never lead it.
  const auto diagnostics = service.diagnostics();
  const auto session = service.session().snapshot();

  json history = json::array();
  for (const auto &entry : session.history) {
    history.push_back(cardToJson(entry, products));
  }

  json payload = {{"status", toString(session.status)},
                  {"mode", toString(session.mode)},
                  {"history", history},
                  {"settings",
                   {{"acceptScore", session.config.acceptScore},
                    {"stableDetections", session.config.stableDetections},
                    {"gracePeriodMs", session.config.gracePeriodMs},
                    {"emittedTimeoutMs", session.config.emittedTimeoutMs}}},
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
                    {"ms", diagnostics.processingMs},
                    // Stage timings are junk when measured is false; 
                    // clients hold the last measured values, and drop
                    // them when timings goes false.
                    {"timings", diagnostics.timingsEnabled},
                    {"measured", diagnostics.measured},
                    {"yoloMs", diagnostics.yoloMs},
                    {"preprocMs", diagnostics.preprocMs},
                    {"embedMs", diagnostics.embedMs},
                    {"dbSearchMs", diagnostics.dbSearchMs}}}};

  if (session.candidate) {
    payload["candidate"] = cardToJson(*session.candidate, products);
  }
  if (session.emitted) {
    payload["emitted"] = cardToJson(*session.emitted, products);
  }
  return json{{"type", "state"}, {"payload", payload}}.dump();
}

} // namespace desktop
} // namespace cardscanner
