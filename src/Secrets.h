#ifndef secrets_h
#define secrets_h
#include <string.h>

// Stored secrets (WiFi passphrase, MQTT password, login password/PIN) are
// write-only: responses carry hasSecret() flags and logs carry maskSecret().

inline bool hasSecret(const char *s) { return s && s[0] != '\0'; }

inline const char *maskSecret(const char *s) { return hasSecret(s) ? "<set>" : "<empty>"; }

// Updates dest from an incoming value, truncating to size.  Pass
// obj["key"].as<const char *>(), which is null when the key is absent.
//   absent -> keep the stored secret.
//   ""     -> keep it too, unless identityChanged (a new SSID or username in
//             the same request), then clear it.  UIs older than write-only
//             secrets send "" for a field the device no longer fills in.
//   value  -> replace it.
// Returns true when dest changed.
inline bool applySecret(const char *incoming, char *dest, size_t size, bool identityChanged = false) {
  if(!incoming || size == 0) return false;
  if(incoming[0] == '\0' && !identityChanged) return false;
  size_t len = strlen(incoming);
  if(len >= size) len = size - 1;
  if(strncmp(dest, incoming, len) == 0 && dest[len] == '\0') return false;
  memcpy(dest, incoming, len);
  dest[len] = '\0';
  return true;
}
#endif
