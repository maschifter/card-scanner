#include "ScanSession.h"

#include <algorithm>

namespace cardscanner {
namespace core {

const char *toString(ScanSession::Status status) {
  using Status = ScanSession::Status;
  switch (status) {
  case Status::Idle:
    return "idle";
  case Status::Detecting:
    return "detecting";
  case Status::CandidateReady:
    return "candidate_ready";
  case Status::Emitted:
    return "emitted";
  }
  return "idle";
}

const char *toString(ScanSession::Mode mode) {
  return mode == ScanSession::Mode::Manual ? "manual" : "auto";
}

bool parseMode(const std::string &text, ScanSession::Mode &out) {
  if (text == "auto") {
    out = ScanSession::Mode::Auto;
    return true;
  }
  if (text == "manual") {
    out = ScanSession::Mode::Manual;
    return true;
  }
  return false;
}

const ProcessedCard *bestMatchedCard(const ScanResult &result) {
  const ProcessedCard *best = nullptr;
  for (const auto &card : result.cards) {
    if (card.hasMatches() &&
        (!best || card.matches[0].score > best->matches[0].score)) {
      best = &card;
    }
  }
  return best;
}

const ProcessedCard *bestVisibleCard(const ScanResult &result) {
  if (const ProcessedCard *matched = bestMatchedCard(result)) {
    return matched;
  }
  const ProcessedCard *best = nullptr;
  for (const auto &card : result.cards) {
    if (!best || card.detectionConfidence > best->detectionConfidence) {
      best = &card;
    }
  }
  return best;
}

ScanSession::ScanSession(SessionConfig config) : config_(config) {}

ScanSession::Snapshot ScanSession::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  // Designated so candidate/emitted - same type - cannot be swapped silently.
  return Snapshot{.status = status_,
                  .mode = mode_,
                  .candidate = candidate_,
                  .emitted = emitted_,
                  .history = history_,
                  .config = config_};
}

SessionConfig ScanSession::config() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void ScanSession::setMode(Mode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == mode) {
    return;
  }
  mode_ = mode;
  if (mode_ == Mode::Auto && status_ == Status::CandidateReady && candidate_) {
    commitLocked();
  }
}

void ScanSession::emitCurrent() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (candidate_) {
    commitLocked();
  }
}

void ScanSession::clearEmitted() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!emitted_) {
    return;
  }
  emitted_.reset();
  if (!candidate_) {
    status_ = Status::Idle;
  } else if (mode_ == Mode::Manual &&
             candidate_->detections >= requiredDetectionsLocked()) {
    // Past the bar already, so ready again now rather than one frame later.
    status_ = Status::CandidateReady;
  } else {
    status_ = Status::Detecting;
  }
}

void ScanSession::emitFromHistory(const std::string &cardId) {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found =
      std::find_if(history_.begin(), history_.end(),
                   [&](const CardInfo &entry) { return entry.cardId == cardId; });
  if (found == history_.end()) {
    return;
  }

  // Copied out first: recordHistoryLocked reorders history_, which would
  // leave a reference into it dangling.
  const CardInfo card = *found;
  emitted_ = card;
  emittedAt_ = Clock::now();
  status_ = Status::Emitted;
  // Re-showing moves the card to the front, same as a fresh commit.
  recordHistoryLocked(card);
}

void ScanSession::recordHistoryLocked(const CardInfo &card) {
  for (size_t i = 0; i < history_.size(); i++) {
    if (history_[i].cardId == card.cardId) {
      history_.erase(history_.begin() + long(i));
      break;
    }
  }
  history_.insert(history_.begin(), card);
  if (history_.size() > kMaxHistory) {
    history_.pop_back();
  }
}

void ScanSession::setConfig(SessionConfig config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

int ScanSession::requiredDetectionsLocked() const {
  // max(): set_settings can raise stableDetections above the fixed ambiguous
  // bar, and an ambiguous match must never clear a lower one than a clear match.
  return ambiguous_ ? std::max(config_.ambiguousDetections, config_.stableDetections)
                    : config_.stableDetections;
}

void ScanSession::commitLocked() {
  emitted_ = candidate_;
  emittedAt_ = Clock::now();
  status_ = Status::Emitted;
  if (emitted_) {
    recordHistoryLocked(*emitted_);
  }
}

bool ScanSession::onScanResult(const ScanResult &result) {
  std::lock_guard<std::mutex> lock(mutex_);

  // A detection with no match neither extends nor resets a streak.
  const ProcessedCard *best = bestMatchedCard(result);
  if (best == nullptr) {
    // The grace period is applied in tick(): one empty frame should not drop
    // a candidate that is close to committing.
    return false;
  }

  const auto &top = best->matches[0];
  if (top.score < config_.acceptScore) {
    // Deliberately does not refresh lastSeen_, so weak matches cannot hold a
    // candidate alive. A weak re-sighting of the on-stream card still
    // reports it as accepted.
    return emitted_ && emitted_->cardId == top.cardId;
  }
  lastSeen_ = Clock::now();

  const bool ambiguousNow =
      best->matches.size() > 1 &&
      (top.score - best->matches[1].score) <= config_.ambiguityDelta;

  if (candidate_ && candidate_->cardId == top.cardId) {
    candidate_->detections++;
    candidate_->score = top.score;
    ambiguous_ = ambiguous_ || ambiguousNow;
  } else {
    candidate_ = CardInfo{top.cardId, top.gameName, top.score, 1};
    ambiguous_ = ambiguousNow;
    status_ = Status::Detecting;
  }

  if (candidate_->detections < requiredDetectionsLocked()) {
    status_ = Status::Detecting;
    return emitted_ && emitted_->cardId == top.cardId;
  }

  // Already showing this card; refresh liveness rather than re-emitting.
  // A re-sighting past the grace period rebuilds the candidate from zero,
  // which drops the status to Detecting above. The card never left the
  // overlay, so the status has to come back with it.
  if (emitted_ && emitted_->cardId == candidate_->cardId) {
    emittedAt_ = Clock::now();
    status_ = Status::Emitted;
    return true;
  }

  if (mode_ == Mode::Manual) {
    status_ = Status::CandidateReady;
    return false;
  }
  commitLocked();
  return true;
}

bool ScanSession::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = Clock::now();
  bool changed = false;

  const bool seenRecently =
      now - lastSeen_ <= std::chrono::milliseconds(config_.gracePeriodMs);

  if (candidate_ && !seenRecently) {
    candidate_.reset();
    ambiguous_ = false;
    if (status_ == Status::Detecting || status_ == Status::CandidateReady) {
      status_ = emitted_ ? Status::Emitted : Status::Idle;
    }
    changed = true;
  }

  if (emitted_ &&
      now - emittedAt_ > std::chrono::milliseconds(config_.emittedTimeoutMs) &&
      !seenRecently) {
    emitted_.reset();
    status_ = candidate_ ? Status::Detecting : Status::Idle;
    changed = true;
  }

  return changed;
}

} // namespace core
} // namespace cardscanner
