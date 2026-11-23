#pragma once
#include <cstdint>
#include <cstring>
#include <limits>

class FocusClock {
 public:
  enum class Result { Accepted, Duplicate, Busy, Invalid };
  Result start(const char *id, uint32_t seconds, uint64_t now) {
    if (!id || !*id || std::strlen(id) > 64 || seconds < 1 || seconds > 86400 ||
        now > std::numeric_limits<uint64_t>::max() - uint64_t(seconds) * 1000) return Result::Invalid;
    for (const char *p = id; *p; ++p) {
      if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) return Result::Invalid;
    }
    if (!std::strcmp(id, id_)) return seconds == seconds_ ? Result::Duplicate : Result::Invalid;
    if (running_) return Result::Busy;
    std::strcpy(id_, id);
    seconds_ = seconds;
    deadline_ = now + uint64_t(seconds) * 1000;
    running_ = true;
    completed_ = false;
    return Result::Accepted;
  }
  bool poll(uint64_t now) {
    if (!running_ || now < deadline_) return false;
    running_ = false;
    completed_ = true;
    return true;
  }
  uint32_t remaining(uint64_t now) const {
    return running_ && now < deadline_ ? uint32_t((deadline_ - now + 999) / 1000) : 0;
  }
  bool running() const { return running_; }
  bool completed() const { return completed_; }
  uint32_t seconds() const { return seconds_; }
  const char *id() const { return id_; }
 private:
  char id_[65] = {};
  uint64_t deadline_ = 0;
  uint32_t seconds_ = 0;
  bool running_ = false;
  bool completed_ = false;
};
