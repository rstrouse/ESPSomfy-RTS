#!/usr/bin/env bash
# Tests .github/set_version.sh on copies of the real files:
#   bash test/scripts/test_set_version.sh
set -uo pipefail

REPO="$(cd "$(dirname "$0")/../.." && pwd)"
FILES=(src/ConfigSettings.h data-src/appversion data-src/index.html data-src/index.js)
failures=0

fail() { echo "FAIL: $1"; failures=$((failures + 1)); }

# A fresh copy of the four files in a temp dir, which becomes the cwd.
fresh() {
  WORK="$(mktemp -d)"
  for f in "${FILES[@]}"; do
    mkdir -p "$WORK/$(dirname "$f")"
    cp "$REPO/$f" "$WORK/$f"
  done
  cd "$WORK"
}

run() { bash "$REPO/.github/set_version.sh" "$1" >/dev/null 2>&1; }

has() { grep -qF -- "$2" "$1" || fail "$3: $1 lacks $2"; }

# Every version string in the sources today gets the new version.
for tag in v4.0.2 v4.1.0.beta v5.0.0-rc1; do
  fresh
  v="${tag#v}"
  run "$tag" || fail "$tag: exited non-zero"
  has src/ConfigSettings.h "#define FW_VERSION \"v$v\"" "$tag"
  [[ "$(cat data-src/appversion)" == "$v" ]] || fail "$tag: appversion is $(cat data-src/appversion)"
  has data-src/index.html "?v=${v}c" "$tag"
  has data-src/index.js "appVersion = 'v$v'" "$tag"
  # No old version left behind in index.html's links.
  if grep -o '?v=[^"'"'"']*' data-src/index.html | grep -vqxF "?v=${v}c"; then
    fail "$tag: index.html keeps an old ?v="
  fi
done

# Running it twice with the same tag still passes its own check.
fresh
run v4.0.2 && run v4.0.2 || fail "a second run with the same tag failed"

# Tags that aren't versions are rejected and nothing is written.
for tag in "v1/2" "v4.0;rm" "v4.0&" "" "vx.y" "v4..0"; do
  fresh
  if run "$tag"; then fail "accepted '$tag'"; fi
  for f in "${FILES[@]}"; do
    cmp -s "$REPO/$f" "$f" || fail "'$tag' changed $f"
  done
done

# A file whose version string changed shape fails loudly instead of
# silently keeping the old version.
fresh
sed -i 's/#define FW_VERSION/#define FIRMWARE_VERSION/' src/ConfigSettings.h
if run v4.0.2; then fail "passed with no FW_VERSION line"; fi

if ((failures)); then
  echo "$failures failure(s)"
  exit 1
fi
echo "set_version.sh: all passed"
