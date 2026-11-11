#ifndef httpaccess_h
#define httpaccess_h
#include <string.h>
#include <strings.h>

// Who may reach what on port 80 under Secured
// (docs/adr/0006-two-security-modes-eleven-http-routes.md).  Paths are the
// request's path, without the query.

// The files the web UI loads, which all sit in the root.  Static serving
// answers these alone, so the rolling codes in shades.cfg leave the device
// only through /backup.  A path below a route, such as /backup/a.js, isn't one.
inline bool isWebAsset(const char *path) {
  if(!path || path[0] != '/' || strchr(path + 1, '/') || strstr(path, "..")) return false;
  if(strcmp(path, "/") == 0 || strcmp(path, "/appversion") == 0) return true;
  static const char *const types[] = {".html", ".js", ".css", ".png", ".svg", ".ico"};
  const size_t len = strlen(path);
  for(const char *type : types) {
    const size_t n = strlen(type);
    if(len > n + 1 && strcmp(path + len - n, type) == 0) return true;
  }
  return false;
}

// True for what anyone may reach without a token: the web UI's files, what
// logging in needs, SSDP's description and preflight requests.  /ws is here
// because the socket checks its own token in the handshake, from the query.
inline bool isPublicRoute(const char *path, bool preflight) {
  if(preflight) return true;
  if(!path) return false;
  static const char *const routes[] = {"/loginContext", "/login", "/upnp.xml", "/ws"};
  for(const char *route : routes)
    if(strcmp(path, route) == 0) return true;
  return isWebAsset(path);
}

// True when a socket handshake comes from a page the device served.  A
// browser always sends Origin with a handshake, so another site's page open
// on the LAN can't use a token the browser holds.  Clients that aren't
// browsers send none.
inline bool originMatchesHost(const char *origin, const char *host) {
  if(!origin) return true;
  static const char *const schemes[] = {"http://", "https://"};
  for(const char *scheme : schemes) {
    const size_t n = strlen(scheme);
    if(strncasecmp(origin, scheme, n) == 0) return host && host[0] && strcasecmp(origin + n, host) == 0;
  }
  return false;
}
#endif
