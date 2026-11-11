#ifndef LOG_LOCK_POLICY_H
#define LOG_LOCK_POLICY_H
#include <stdint.h>

// When a log call stops waiting for the log lock (src/LogLock.cpp).  Kept free
// of FreeRTOS so it can be unit tested on the PC (pio test -e native).

// What the task holding the log lock is doing.
enum class LogHolder {
  unknown,            // Just took the lock and hasn't recorded itself yet.
  running,            // Running on a core, so it lets go within a line.
  preemptedHere,      // Waiting for this core, so blocking lets it finish.
  preemptedElsewhere  // Preempted on another core (the radio task during a
                      // whole transmission), so waiting gets nowhere.
};
// Whether to keep waiting after waitedMs.  Never past maxMs.
inline bool keepWaitingForLogLock(LogHolder holder, uint32_t waitedMs, uint32_t maxMs) {
  if(waitedMs >= maxMs) return false;
  return holder != LogHolder::preemptedElsewhere;
}
#endif
