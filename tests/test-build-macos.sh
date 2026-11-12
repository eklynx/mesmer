#!/usr/bin/env bash
# Tests build_macos.sh against docs/macos-build.md. The failure paths need no
# network and no X-Plane SDK: the script is copied into a sandbox and the SDK
# download is pointed at a local file:// zip. The full universal build runs only
# when MESMER_TEST_SDK names an unpacked SDK directory (the one with CHeaders/).
#   tests/test-build-macos.sh
#   MESMER_TEST_SDK=/path/to/SDK tests/test-build-macos.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=0
check() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; failures=$((failures+1)); fi; }

[ "$(uname -s)" = "Darwin" ] || { echo "skip: needs macOS"; exit 0; }

sandbox="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-test.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT
cp "$ROOT/build_macos.sh" "$sandbox/"
script="$sandbox/build_macos.sh"

# Spec: --help prints usage and exits 0 without building.
out="$("$script" --help 2>&1)"; code=$?
check $code "--help exits 0"
echo "$out" | grep -q -- "--sdk PATH"; check $? "--help documents --sdk"
[ ! -d "$sandbox/build" ]; check $? "--help builds nothing"

# Spec: an unknown option fails and says which.
out="$("$script" --bogus 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "unknown option: --bogus"; check $? "unknown option is rejected"

# Spec: --sdk without a path fails.
out="$("$script" --sdk 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "needs a path"; check $? "--sdk without a path is rejected"

# Spec: a directory that is not the SDK is rejected before any build.
mkdir "$sandbox/notsdk"
out="$("$script" --sdk "$sandbox/notsdk" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "not an X-Plane SDK directory"; check $? "a non-SDK directory is rejected"

# Spec: the XPLANE_SDK environment variable is honoured when --sdk is absent...
out="$(XPLANE_SDK="$sandbox/notsdk" "$script" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "notsdk"; check $? "XPLANE_SDK is used when no --sdk is given"
# ...and --sdk wins over it.
mkdir "$sandbox/other"
out="$(XPLANE_SDK="$sandbox/notsdk" "$script" --sdk "$sandbox/other" 2>&1)"
echo "$out" | grep -q "other"; check $? "--sdk overrides XPLANE_SDK"

# Spec: with no SDK given, the pinned download is checksum-verified.
mkdir "$sandbox/zipsrc" && echo "not an sdk" > "$sandbox/zipsrc/README.txt"
(cd "$sandbox/zipsrc" && zip -q ../fake.zip README.txt)
# file:// URLs need the sandbox path percent-encoded (it may hold spaces).
url_base="file://${sandbox// /%20}"
good="$(shasum -a 256 "$sandbox/fake.zip" | awk '{print $1}')"

out="$(MESMER_SDK_URL="$url_base/fake.zip" MESMER_SDK_SHA256="$(printf '0%.0s' $(seq 64))" "$script" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "does not match the pin"; check $? "a download with the wrong checksum is rejected"
[ ! -f "$sandbox/build/sdk/xplane-sdk.zip" ]; check $? "a rejected download is deleted"

out="$(MESMER_SDK_URL="$url_base/fake.zip" MESMER_SDK_SHA256="$good" "$script" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "not an X-Plane SDK directory"; check $? "a verified download is unpacked and then checked for SDK content"

rm -rf "$sandbox/build" # the verified zip above is cached; start from nothing
out="$(MESMER_SDK_URL="$url_base/missing.zip" "$script" 2>&1)"; code=$?
[ $code -ne 0 ] && echo "$out" | grep -q "could not download"; check $? "an unreachable download fails with advice"

# Spec: the pin matches the release workflow's.
url="$(grep -o 'https://[^"]*XPSDK[0-9]*\.zip' "$ROOT/build_macos.sh" | head -1)"
sha="$(grep -o '[0-9a-f]\{64\}' "$ROOT/build_macos.sh" | head -1)"
grep -q "XPLANE_SDK_URL: $url" "$ROOT/.github/workflows/release.yml" && grep -q "XPLANE_SDK_SHA256: $sha" "$ROOT/.github/workflows/release.yml"
check $? "the SDK pin equals release.yml's"

# Spec: build/ is ignored by git.
(cd "$ROOT" && git check-ignore -q build/)
check $? "build/ is git-ignored"

# Spec: the full build yields a universal, signed plugin, staged where X-Plane looks.
if [ -n "${MESMER_TEST_SDK:-}" ]; then
    "$ROOT/build_macos.sh" --clean --sdk "$MESMER_TEST_SDK" >"$sandbox/full.log" 2>&1
    check $? "full build succeeds (log: $sandbox/full.log)"
    xpl="$ROOT/build/stage/BrunnerDirect/mac_x64/BrunnerDirect.xpl"
    [ "$(lipo -archs "$xpl" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ')" = "arm64 x86_64 " ]; check $? "plugin has arm64 and x86_64 slices"
    codesign --verify --strict "$xpl" 2>/dev/null; check $? "plugin is signed"
    nm -gU "$xpl" 2>/dev/null | grep -q "_XPluginStart"; check $? "plugin exports XPluginStart"
    [ -f "$ROOT/build/stage/BrunnerDirect/BrunnerDirect.ini" ] && [ -f "$ROOT/build/stage/BrunnerDirect/LICENSE" ]; check $? "staging carries the release extras"
    vtool -show-build "$xpl" 2>/dev/null | grep -q "minos 11.0"; check $? "deployment target is macOS 11.0"
else
    echo "skip full build (set MESMER_TEST_SDK to an unpacked SDK directory)"
fi

[ $failures -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
