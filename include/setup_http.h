#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>

// Incremental, bounded HTTP input. No dynamic body allocation or blocking reads.
class SetupHttpRequest {
 public:
  enum class State { Reading, Complete, Invalid, TooLarge };
  static constexpr size_t kHeaderLimit = 2048;
  static constexpr size_t kBodyLimit = 512;
  State feed(char c) {
    if (state_ != State::Reading) return state_;
    if (!c) return state_ = State::Invalid;
    if (size_ >= sizeof(data_) - 1) return state_ = State::TooLarge;
    data_[size_++] = c;
    data_[size_] = 0;
    if (!bodyAt_) {
      if (size_ >= 4 && !std::memcmp(data_ + size_ - 4, "\r\n\r\n", 4)) {
        bodyAt_ = size_;
        if (bodyAt_ > kHeaderLimit) return state_ = State::TooLarge;
        if (!parseHeaders()) return state_;
      } else if (size_ >= kHeaderLimit) return state_ = State::TooLarge;
    }
    if (bodyAt_ && size_ == bodyAt_ + length_) state_ = State::Complete;
    return state_;
  }
  const char *method() const { return method_; }
  const char *path() const { return path_; }
  const char *body() const { return bodyAt_ ? data_ + bodyAt_ : ""; }
 private:
  bool parseHeaders() {
    char headers[kHeaderLimit + 1];
    std::memcpy(headers, data_, bodyAt_); headers[bodyAt_] = 0;
    char *lineEnd = std::strstr(headers, "\r\n");
    if (!lineEnd) { state_ = State::Invalid; return false; }
    *lineEnd = 0;
    const char *start = nullptr;
    if (!std::strncmp(headers, "GET ", 4)) { std::strcpy(method_, "GET"); start = headers + 4; }
    else if (!std::strncmp(headers, "POST ", 5)) { std::strcpy(method_, "POST"); start = headers + 5; }
    else { state_ = State::Invalid; return false; }
    const char *space = std::strchr(start, ' ');
    if (!space || space == start || size_t(space - start) >= sizeof(path_) ||
        (std::strcmp(space, " HTTP/1.1") && std::strcmp(space, " HTTP/1.0"))) { state_ = State::Invalid; return false; }
    std::memcpy(path_, start, space - start);
    bool hasLength = false, hasHost = false, form = false;
    char *line = lineEnd + 2;
    while (*line) {
      lineEnd = std::strstr(line, "\r\n"); if (!lineEnd) break;
      *lineEnd = 0; if (!*line) break;
      char *colon = std::strchr(line, ':');
      if (!colon) { state_ = State::Invalid; return false; }
      *colon = 0;
      for (char *p = line; *p; ++p) if (*p >= 'A' && *p <= 'Z') *p += 'a' - 'A';
      char *value = colon + 1; while (*value == ' ' || *value == '\t') ++value;
      if (!std::strcmp(line, "host")) {
        if (hasHost || (std::strcmp(value, "192.168.4.1") && std::strcmp(value, "192.168.4.1:80"))) { state_ = State::Invalid; return false; }
        hasHost = true;
      } else if (!std::strcmp(line, "content-length")) {
        if (hasLength || !*value) { state_ = State::Invalid; return false; }
        hasLength = true;
        for (char *p = value; *p; ++p) {
          if (*p < '0' || *p > '9') { state_ = State::Invalid; return false; }
          length_ = length_ * 10 + (*p - '0');
          if (length_ > kBodyLimit) { state_ = State::TooLarge; return false; }
        }
      } else if (!std::strcmp(line, "transfer-encoding")) { state_ = State::Invalid; return false; }
      else if (!std::strcmp(line, "content-type")) {
        form = !std::strcmp(value, "application/x-www-form-urlencoded") ||
               !std::strcmp(value, "application/x-www-form-urlencoded;charset=UTF-8");
      }
      line = lineEnd + 2;
    }
    if (!hasHost || (!std::strcmp(method_, "POST") && (!hasLength || !form)) ||
        (!std::strcmp(method_, "GET") && length_)) { state_ = State::Invalid; return false; }
    return true;
  }
  char data_[kHeaderLimit + kBodyLimit + 1] = {};
  char method_[5] = {};
  char path_[32] = {};
  size_t size_ = 0, bodyAt_ = 0, length_ = 0;
  State state_ = State::Reading;
};

inline int setupHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
// Reject duplicate fields and embedded NUL/control characters, not silently truncate.
inline bool setupFormValue(const char *body, const char *key, char *out, size_t capacity) {
  bool found = false; out[0] = 0;
  for (const char *p = body; *p;) {
    const char *end = std::strchr(p, '&'); if (!end) end = p + std::strlen(p);
    const char *equal = static_cast<const char *>(std::memchr(p, '=', end - p));
    if (!equal) return false;
    if (size_t(equal - p) == std::strlen(key) && !std::memcmp(p, key, equal - p)) {
      if (found) return false;
      found = true; size_t used = 0;
      for (const char *v = equal + 1; v < end; ++v) {
        unsigned char c = *v;
        if (c == '+') c = ' ';
        else if (c == '%') {
          if (end - v < 3 || setupHex(v[1]) < 0 || setupHex(v[2]) < 0) return false;
          c = setupHex(v[1]) * 16 + setupHex(v[2]); v += 2;
        }
        if (c < 32 || c == 127 || used + 1 >= capacity) return false;
        out[used++] = c;
      }
      out[used] = 0;
    }
    p = *end ? end + 1 : end;
  }
  return found;
}
