#ifndef loginthrottle_h
#define loginthrottle_h
#include <stdint.h>

// Failed /login attempts per client IP (SECURITY_PLAN.md, PR C).  The first
// LOGIN_FREE_FAILURES failures cost nothing, and the last of them locks the
// IP out for LOGIN_FIRST_LOCKOUT_MS.  After that every failure locks it out
// again, for twice as long as the lockout before, up to LOGIN_MAX_LOCKOUT_MS.
// A success forgets the IP, and so does LOGIN_FORGET_MS without a failure.
//
// It keeps LOGIN_THROTTLE_SLOTS IPs.  Once each holds a recent failure, every
// other IP shares one more slot, so an attacker with many addresses can't
// get the free failures on each: while the table is full, an IP it doesn't
// hold may have to wait even with the right credentials.
//
// The time is passed in, in ms on a 64-bit clock (esp_timer_get_time() /
// 1000), so native tests cover it and a slot left alone for 49.7 days isn't
// woken by millis() wrapping.  Only async_tcp's task calls it: both web
// servers run their handlers there.

#define LOGIN_FREE_FAILURES 5
#define LOGIN_FIRST_LOCKOUT_MS 30000UL
#define LOGIN_MAX_LOCKOUT_MS 900000UL
#define LOGIN_FORGET_MS 3600000UL
#define LOGIN_THROTTLE_SLOTS 8

struct login_attempts_t {
  bool used = false;
  uint32_t ip = 0;
  uint16_t failures = 0;
  uint64_t lastFailure = 0;     // the time of the last failure
  uint32_t lockoutMs = 0;       // the lockout that failure began, 0 for none
};

class LoginThrottle {
  public:
    // How long ip must wait before it may try again, in ms; 0 when it may now.
    uint32_t retryAfter(uint32_t ip, uint64_t now) const {
      const login_attempts_t *a = this->find(ip, now);
      return a ? remaining(*a, now) : 0;
    }
    // A wrong PIN or password from ip.  Returns the lockout it begins, in ms,
    // or 0 while ip still has free failures.
    uint32_t failed(uint32_t ip, uint64_t now) {
      login_attempts_t *a = this->claim(ip, now);
      if(a->failures < UINT16_MAX) a->failures++;
      a->lastFailure = now;
      a->lockoutMs = lockoutFor(a->failures);
      return a->lockoutMs;
    }
    // The right credentials from ip.  The shared slot is left as it is: one
    // user's success says nothing about the other addresses in it.
    void succeeded(uint32_t ip) {
      for(auto &a : this->slots)
        if(a.used && a.ip == ip) a = login_attempts_t();
    }
    // The lockout the failures-th failure in a row begins.
    static uint32_t lockoutFor(uint16_t failures) {
      if(failures < LOGIN_FREE_FAILURES) return 0;
      uint32_t ms = LOGIN_FIRST_LOCKOUT_MS;
      for(uint16_t i = LOGIN_FREE_FAILURES; i < failures && ms < LOGIN_MAX_LOCKOUT_MS; i++) ms *= 2;
      return ms < LOGIN_MAX_LOCKOUT_MS ? ms : LOGIN_MAX_LOCKOUT_MS;
    }
  private:
    login_attempts_t slots[LOGIN_THROTTLE_SLOTS];
    login_attempts_t shared;

    static uint32_t remaining(const login_attempts_t &a, uint64_t now) {
      if(!a.used || a.lockoutMs == 0) return 0;
      uint64_t elapsed = now - a.lastFailure;
      return elapsed < a.lockoutMs ? (uint32_t)(a.lockoutMs - elapsed) : 0;
    }
    // Unused, or its last failure is old enough to forget.
    static bool vacant(const login_attempts_t &a, uint64_t now) {
      return !a.used || now - a.lastFailure >= LOGIN_FORGET_MS;
    }
    // ip's record: its own slot, nothing while a slot is vacant, else the
    // shared one.
    const login_attempts_t *find(uint32_t ip, uint64_t now) const {
      bool anyVacant = false;
      for(const auto &a : this->slots) {
        if(vacant(a, now)) anyVacant = true;
        else if(a.ip == ip) return &a;
      }
      if(anyVacant || vacant(this->shared, now)) return nullptr;
      return &this->shared;
    }
    // ip's record to count a failure in, taking a vacant slot when it has none.
    login_attempts_t *claim(uint32_t ip, uint64_t now) {
      login_attempts_t *vacantSlot = nullptr;
      for(auto &a : this->slots) {
        if(vacant(a, now)) { if(!vacantSlot) vacantSlot = &a; }
        else if(a.ip == ip) return &a;
      }
      login_attempts_t *a = vacantSlot ? vacantSlot : &this->shared;
      if(vacant(*a, now)) {
        *a = login_attempts_t();
        a->used = true;
        a->ip = ip;
      }
      return a;
    }
};
#endif
