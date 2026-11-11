// Minimal stand-in for the Arduino core so firmware sources can be built for
// the native test env (pio test -e native).  Only add what tested code uses.
#ifndef arduino_stub_h
#define arduino_stub_h
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <type_traits>

typedef uint8_t byte;

// Fixed so anything formatted from it is deterministic.
inline unsigned long millis() { return 0; }
// Tests that measure time set this.
inline unsigned long stubMicros = 0;
inline unsigned long micros() { return stubMicros; }
inline bool getLocalTime(struct tm *info, uint32_t ms = 5000) {
  (void)ms;
  time_t now = time(nullptr);
  return localtime_r(&now, info) != nullptr;
}

// The parts of Arduino's String the firmware code under test uses.
class String {
  std::string _s;
  public:
    String() {}
    String(const char *s) : _s(s ? s : "") {}
    String(const std::string &s) : _s(s) {}
    // Arduino formats integers in decimal.
    template<typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0>
    explicit String(T val) : _s(std::to_string(val)) {}
    const char *c_str() const { return _s.c_str(); }
    unsigned int length() const { return (unsigned int)_s.length(); }
    bool equalsIgnoreCase(const String &other) const {
      if(_s.length() != other._s.length()) return false;
      for(size_t i = 0; i < _s.length(); i++)
        if(tolower((unsigned char)_s[i]) != tolower((unsigned char)other._s[i])) return false;
      return true;
    }
    bool startsWith(const String &prefix) const { return _s.compare(0, prefix._s.length(), prefix._s) == 0; }
    friend String operator+(const String &a, const String &b) { return String(a._s + b._s); }
    friend bool operator==(const String &a, const String &b) { return a._s == b._s; }
};
#endif
