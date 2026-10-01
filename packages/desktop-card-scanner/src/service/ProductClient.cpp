#include "ProductClient.h"

#include "../net/HttpClient.h"

#include <nlohmann/json.hpp>

#include <iostream>

namespace cardscanner {
namespace desktop {

namespace {
using json = nlohmann::json;
constexpr const char *kImageBase = "https://ik.imagekit.io/cardnexus/production";
} // namespace

ProductClient::ProductClient(std::string endpoint) : endpoint_(std::move(endpoint)) {}

ProductClient::~ProductClient() { stop(); }

void ProductClient::start(ResolvedCallback onResolved) {
  if (running_.exchange(true)) {
    return;
  }
  onResolved_ = std::move(onResolved);
  worker_ = std::thread([this] { workerLoop(); });
}

void ProductClient::stop() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_.exchange(false)) {
      return;
    }
  }
  wake_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::optional<Product> ProductClient::lookup(const std::string &cardId) {
  std::lock_guard<std::mutex> lock(mutex_);

  const auto cached = cache_.find(cardId);
  if (cached != cache_.end()) {
    return cached->second;
  }
  if (failed_.count(cardId) != 0 || queued_.count(cardId) != 0) {
    return std::nullopt;
  }

  // First sighting: queue it and answer with nothing.
  queued_.insert(cardId);
  pending_.push_back(cardId);
  wake_.notify_one();
  return std::nullopt;
}

void ProductClient::workerLoop() {
  while (running_) {
    std::string cardId;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(200),
                     [this] { return !running_ || !pending_.empty(); });
      if (!running_) {
        return;
      }
      if (pending_.empty()) {
        continue;
      }
      cardId = pending_.front();
      pending_.pop_front();
    }

    const json body = {
        {"json", {{"productId", cardId}, {"prices", {{"marketplace", "Cardmarket"}}}}}};
    const auto response = net::postJson(endpoint_, body.dump(), &running_);
    if (!running_) {
      return; // aborted by stop()
    }

    bool resolved = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      queued_.erase(cardId);

      if (response.status != 200) {
        if (response.status >= 400 && response.status < 500) {
          if (failed_.insert(cardId).second) {
            std::cerr << "product lookup " << cardId << ": HTTP " << response.status
                      << " (not retrying this card)\n";
          }
        } else {
          std::cerr << "product lookup " << cardId << ": HTTP " << response.status
                    << " (retrying)\n";
        }
      } else {
        try {
          const auto parsed = json::parse(response.body).at("json").at("product");
          Product product;
          product.name = parsed.value("name", "");
          if (parsed.contains("image") && parsed["image"].contains("path")) {
            product.imageUrl =
                kImageBase + parsed["image"]["path"].get<std::string>() + "/tr:w-500,q-80";
          }
          if (parsed.contains("expansion") && parsed["expansion"].is_object()) {
            product.setName = parsed["expansion"].value("name", "");
          }
          cache_[cardId] = std::move(product);
          resolved = true;
        } catch (const std::exception &) {
          failed_.insert(cardId);
        }
      }
    }

    // Outside the lock; the callback broadcasts and takes other locks.
    if (resolved && onResolved_) {
      try {
        onResolved_(cardId);
      } catch (const std::exception &e) {
        std::cerr << "product callback threw: " << e.what() << "\n";
      }
    }
  }
}

} // namespace desktop
} // namespace cardscanner
