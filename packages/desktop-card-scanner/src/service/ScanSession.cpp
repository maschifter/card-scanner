#include "ScanSession.h"

namespace cardscanner {
namespace desktop {

const char *toString(Status status) {
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

const char *toString(Mode mode) { return mode == Mode::Manual ? "manual" : "auto"; }

bool parseMode(const std::string &text, Mode &out) {
  if (text == "auto") {
    out = Mode::Auto;
    return true;
  }
  if (text == "manual") {
    out = Mode::Manual;
    return true;
  }
  return false;
}

ScanSession::ScanSession(SessionConfig config) : config_(config) {}

Mode ScanSession::mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return mode_;
}

SessionConfig ScanSession::config() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

std::vector<CardInfo> ScanSession::history() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return history_;
}

bool ScanSession::setMode(Mode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (mode_ == mode) {
    return false;
  }
  mode_ = mode;
  if (mode_ == Mode::Auto && status_ == Status::CandidateReady && candidate_) {
    return commitLocked();
  }
  return true;
}

bool ScanSession::emitCurrent() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!candidate_) {
    return false;
  }
  return commitLocked();
}

bool ScanSession::clearEmitted() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!emitted_) {
    return false;
  }
  emitted_.reset();
  status_ = candidate_ ? Status::Detecting : Status::Idle;
  return true;
}

bool ScanSession::emitFromHistory(const std::string &cardId) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto &entry : history_) {
    if (entry.cardId == cardId) {
      emitted_ = entry;
      emittedAt_ = Clock::now();
      status_ = Status::Emitted;
      return true;
    }
  }
  return false;
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

Status ScanSession::status() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return status_;
}

std::optional<CardInfo> ScanSession::candidate() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return candidate_;
}

std::optional<CardInfo> ScanSession::emitted() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return emitted_;
}

bool ScanSession::commitLocked() {
  emitted_ = candidate_;
  emittedAt_ = Clock::now();
  status_ = Status::Emitted;
  if (emitted_) {
    recordHistoryLocked(*emitted_);
  }
  return true;
}

bool ScanSession::onScanResult(const ScanResult &result) {
  std::lock_guard<std::mutex> lock(mutex_);

  // A detection with no match neither extends nor resets a streak.
  const ProcessedCard *best = nullptr;
  for (const auto &card : result.cards) {
    if (!card.matches.empty() && (!best || card.matches[0].score > best->matches[0].score)) {
      best = &card;
    }
  }

  if (best == nullptr) {
    // The grace period is applied in tick(): one empty frame should not drop
    // a candidate that is close to committing.
    return false;
  }

  const auto &top = best->matches[0];
  if (top.score < config_.acceptScore) {
    // Deliberately does not refresh lastSeen_, so weak matches cannot hold a
    // candidate alive.
    return false;
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

  const int required =
      ambiguous_ ? config_.ambiguousDetections : config_.stableDetections;
  if (candidate_->detections < required) {
    status_ = Status::Detecting;
    return true;
  }

  // Already showing this card; refresh liveness rather than re-emitting.
  if (emitted_ && emitted_->cardId == candidate_->cardId) {
    emittedAt_ = Clock::now();
    return false;
  }

  if (mode_ == Mode::Manual) {
    const bool changed = status_ != Status::CandidateReady;
    status_ = Status::CandidateReady;
    return changed;
  }
  return commitLocked();
}

bool ScanSession::tick() {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto now = Clock::now();
  bool changed = false;

  const auto sinceSeen =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - lastSeen_).count();

  if (candidate_ && sinceSeen > config_.gracePeriodMs) {
    candidate_.reset();
    ambiguous_ = false;
    if (status_ == Status::Detecting || status_ == Status::CandidateReady) {
      status_ = emitted_ ? Status::Emitted : Status::Idle;
    }
    changed = true;
  }

  if (emitted_) {
    const auto sinceEmit =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - emittedAt_).count();
    if (sinceEmit > config_.emittedTimeoutMs && sinceSeen > config_.gracePeriodMs) {
      emitted_.reset();
      status_ = candidate_ ? Status::Detecting : Status::Idle;
      changed = true;
    }
  }

  return changed;
}

} // namespace desktop
} // namespace cardscanner
