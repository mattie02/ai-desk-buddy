#include "focus_clock.h"
#include <cassert>
#include <cstdint>
#include <limits>
int main() {
  using R = FocusClock::Result;
  FocusClock timer;
  assert(timer.start("first", 10, 1000) == R::Accepted);
  assert(timer.remaining(1000) == 10);
  assert(timer.remaining(1001) == 10);
  assert(timer.remaining(2000) == 9);
  assert(timer.start("first", 10, 5000) == R::Duplicate);
  assert(timer.remaining(5000) == 6); // Duplicate must never restart the countdown.
  assert(timer.start("other", 30, 5000) == R::Busy);
  assert(timer.start("first", 11, 5000) == R::Invalid);
  assert(!timer.poll(10999));
  assert(timer.poll(11000));
  assert(!timer.poll(11001)); // Alarm fires once.
  assert(timer.completed() && !timer.running() && timer.remaining(12000) == 0);
  assert(timer.start("first", 10, 12000) == R::Duplicate);
  assert(timer.completed());
  assert(timer.start("next", 1, 20000) == R::Accepted);
  assert(!timer.completed());
  assert(timer.poll(999999)); // A stalled polling loop still recognizes an elapsed deadline.
  FocusClock longClock;
  const uint64_t nearMillisWrap = uint64_t(UINT32_MAX) - 500;
  assert(longClock.start("wrap", 2, nearMillisWrap) == R::Accepted);
  assert(!longClock.poll(nearMillisWrap + 1999));
  assert(longClock.poll(nearMillisWrap + 2000));
  assert(longClock.start("", 1, 0) == R::Invalid);
  assert(longClock.start("bad/id", 1, 0) == R::Invalid);
  assert(longClock.start("zero", 0, 0) == R::Invalid);
  assert(longClock.start("large", 86401, 0) == R::Invalid);
  assert(longClock.start("overflow", 1, std::numeric_limits<uint64_t>::max() - 500) == R::Invalid);
  assert(longClock.start("day", 86400, 0) == R::Accepted);
  assert(longClock.remaining(0) == 86400);
}
