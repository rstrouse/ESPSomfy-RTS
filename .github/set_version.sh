#!/usr/bin/env bash
# Writes a release's version into the sources: bash .github/set_version.sh v4.0.1
# The build jobs build with it, the release job commits it.
# Tested by test/scripts/test_set_version.sh.
set -euo pipefail

VERSION="${1#v}"
# The version goes into sed replacements and into the firmware, so only
# digits, letters, dots and dashes: 4.0.1, 4.0.0.beta, 4.1.0-rc1.
if [[ ! "$VERSION" =~ ^[0-9]+(\.[0-9A-Za-z-]+)*$ ]]; then
  echo "Not a version: $1" >&2
  exit 1
fi

# Each pattern matches whatever version is there now, suffixes included
# (v4.0.0.beta, ?v=3.0.14d). The ?v= suffix letter busts the browser cache.
sed -i "s/#define FW_VERSION \"v[^\"]*\"/#define FW_VERSION \"v${VERSION}\"/" src/ConfigSettings.h
sed -i "s/^[0-9].*/${VERSION}/" data-src/appversion
sed -i "s/[?]v=[0-9][0-9A-Za-z.-]*/?v=${VERSION}c/g" data-src/index.html
sed -i "s/appVersion = 'v[^']*'/appVersion = 'v${VERSION}'/" data-src/index.js

# sed succeeds when nothing matches, so check each file got the version.
check() {
  if ! grep -qF -- "$2" "$1"; then
    echo "$1 doesn't have $2: its version string changed shape" >&2
    exit 1
  fi
}
check src/ConfigSettings.h "#define FW_VERSION \"v${VERSION}\""
check data-src/appversion "${VERSION}"
check data-src/index.html "?v=${VERSION}c"
check data-src/index.js "appVersion = 'v${VERSION}'"
