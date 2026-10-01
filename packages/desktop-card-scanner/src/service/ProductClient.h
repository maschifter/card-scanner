#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

namespace cardscanner {
namespace desktop {

struct Product {
  std::string name;
  std::string setName;
  std::string imageUrl;
};

/**
 * @brief Card names and art, resolved off the scan path.
 *
 * The lookup is an HTTPS request with second-scale timeouts, so it must never
 * run on the scan worker. `lookup` answers from cache or queues the work and
 * returns nothing; the caller re-broadcasts when the result arrives.
 */
class ProductClient {
public:
  /// Fires on the lookup thread once a card resolves.
  using ResolvedCallback = std::function<void(const std::string &cardId)>;

  explicit ProductClient(std::string endpoint);
  ~ProductClient();

  ProductClient(const ProductClient &) = delete;
  ProductClient &operator=(const ProductClient &) = delete;

  void start(ResolvedCallback onResolved);
  void stop();

  /// Cached product, or nothing while queued or failed. Never blocks.
  std::optional<Product> lookup(const std::string &cardId);

private:
  void workerLoop();

  std::string endpoint_;
  ResolvedCallback onResolved_;

  mutable std::mutex mutex_;
  std::map<std::string, Product> cache_;
  /// Cards the server rejected (4xx), so a state change does not re-request them.
  std::set<std::string> failed_;
  std::set<std::string> queued_;
  std::deque<std::string> pending_;

  std::condition_variable wake_;
  std::atomic<bool> running_{false};
  std::thread worker_;
};

} // namespace desktop
} // namespace cardscanner
