#ifndef apitoken_h
#define apitoken_h
#include <stddef.h>
#include <stdint.h>

// The token key's size: 32 random bytes the device makes on first boot and
// keeps in NVS.  It never leaves the device.
#define API_TOKEN_KEY_SIZE 32
// A token's buffer: an HMAC-SHA256 in hex and its terminator.
#define API_TOKEN_SIZE 65

// True when given is the expected token.  It takes the same time whatever
// given holds, so the time doesn't tell how much of a guess was right.  Only
// expected's length shows, and every token has the same one.  An empty
// expected token, which is what a failed HMAC leaves, matches nothing.
inline bool tokenMatches(const char *expected, const char *given) {
  if(!expected || !given || !expected[0]) return false;
  uint8_t diff = 0;
  for(size_t i = 0; expected[i]; i++) {
    diff |= (uint8_t)(expected[i] ^ *given);
    // A shorter given compares its terminator with the rest of expected.
    if(*given) given++;
  }
  // A longer given doesn't match either.
  diff |= (uint8_t)*given;
  return diff == 0;
}
#endif
