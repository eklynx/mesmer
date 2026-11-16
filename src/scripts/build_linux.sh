#!/usr/bin/env bash
# Build the Mesmer X-Plane 12 plugin for Linux (x86_64). Specification: docs/dev/linux-build.md
# (the source of truth for what this script guarantees and how it fails).
#
#   src/scripts/build_linux.sh [--sdk PATH] [--clean] [--no-test] [--help]
#
# Output: build/stage/BrunnerDirect/lin_x64/BrunnerDirect.xpl (plus the files the other
# platforms' releases ship). Needs: build-essential cmake libgl-dev (and curl unzip when it downloads the SDK).
set -euo pipefail

# The X-Plane SDK pin. Keep it equal to XPLANE_SDK_URL / XPLANE_SDK_SHA256 in
# .github/workflows/release.yml and ci.yml, and to build_macos.sh.
SDK_URL="${MESMER_SDK_URL:-https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip}"
SDK_SHA256="${MESMER_SDK_SHA256:-b9875ab27b593927b4f9b3e0ddfffe7401ee5dce6d86b50aea0da65f70ff7816}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)" # the repository root: this script is src/scripts/build_linux.sh
START="$(pwd -P)" # where it was run from
BUILD="$ROOT/build"
sdk="${XPLANE_SDK:-}"
clean=0
run_tests=1

usage() {
    sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<EOT

  --sdk PATH   unpacked X-Plane SDK directory (the one holding CHeaders/); overrides \$XPLANE_SDK
  --clean      remove build/ first (a downloaded SDK in build/sdk is re-downloaded)
  --no-test    skip ctest
  --help       this text

With no --sdk and no \$XPLANE_SDK, the pinned SDK is downloaded into build/sdk
and checked against its SHA-256 before it is unpacked.
EOT
}

die() { echo "build_linux.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --sdk) [ $# -ge 2 ] || die "--sdk needs a path"; sdk="$2"; shift 2 ;;
        --clean) clean=1; shift ;;
        --no-test) run_tests=0; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
done

# Run from the repository root, src/ or src/scripts/ (or anywhere else): everything happens at the
# root, so build/ is always $ROOT/build. A relative --sdk or $XPLANE_SDK means relative to where it
# was run from, so it is made absolute before the working directory changes.
case "$sdk" in ""|/*) ;; *) sdk="$START/$sdk" ;; esac
case "$START" in
    "$ROOT") ;;
    "$ROOT/src") echo "Run from src/; building from the repository root: $ROOT" ;;
    "$ROOT/src/scripts") echo "Run from src/scripts/; building from the repository root: $ROOT" ;;
    *) echo "Building from the repository root: $ROOT" ;;
esac
cd "$ROOT"

[ "$(uname -s)" = "Linux" ] || die "this script builds for Linux and must run on Linux"
[ "$(uname -m)" = "x86_64" ] || die "X-Plane 12 on Linux is x86_64 only; this machine is $(uname -m)"
for tool in cmake g++ file; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found (sudo apt install build-essential cmake libgl-dev file)"
done
[ -f /usr/include/GL/gl.h ] || die "OpenGL headers not found (sudo apt install libgl-dev)"

[ "$clean" -eq 1 ] && rm -rf "$BUILD"
mkdir -p "$BUILD"

fetch_sdk() {
    local dir="$BUILD/sdk" zip="$BUILD/sdk/xplane-sdk.zip" got
    for tool in curl sha256sum unzip; do
        command -v "$tool" >/dev/null 2>&1 || die "$tool not found (sudo apt install curl coreutils unzip), or pass --sdk PATH"
    done
    mkdir -p "$dir"
    if [ ! -f "$zip" ]; then
        echo "Downloading the X-Plane SDK: $SDK_URL"
        curl --fail --silent --show-error --location --output "$zip" "$SDK_URL" \
            || { rm -f "$zip"; die "could not download the X-Plane SDK; pass --sdk PATH or set XPLANE_SDK"; }
    fi
    got="$(sha256sum "$zip" | awk '{print $1}')"
    if [ "$got" != "$SDK_SHA256" ]; then
        rm -f "$zip"
        die "X-Plane SDK checksum $got does not match the pin $SDK_SHA256"
    fi
    rm -rf "$dir/unpacked"
    unzip -q "$zip" -d "$dir/unpacked" || die "could not unpack the X-Plane SDK"
    sdk="$dir/unpacked/SDK"
}

if [ -z "$sdk" ]; then fetch_sdk; fi
[ -f "$sdk/CHeaders/XPLM/XPLMPlugin.h" ] || die "not an X-Plane SDK directory (no CHeaders/XPLM/XPLMPlugin.h): $sdk"
sdk="$(cd "$sdk" && pwd)"

echo "Configuring (x86_64, SDK $sdk)"
cmake -S "$ROOT/src" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release "-DXPLANE_SDK=$sdk"
cmake --build "$BUILD" --parallel

xpl="$BUILD/BrunnerDirect.xpl"
[ -f "$xpl" ] || die "the build did not produce $xpl"
file "$xpl" | grep -q "ELF 64-bit.*x86-64" || die "BrunnerDirect.xpl is not a 64-bit x86-64 ELF: $(file "$xpl")"

if [ "$run_tests" -eq 1 ]; then
    ctest --test-dir "$BUILD" --output-on-failure
fi

stage="$BUILD/stage/BrunnerDirect"
rm -rf "$BUILD/stage"
mkdir -p "$stage/lin_x64"
cp "$xpl" "$stage/lin_x64/"
for file in config/BrunnerDirect.ini config/BrunnerDirect.profiles.json config/skunkcrafts_updater.cfg README.md LICENSE NOTICE SAFETY.md; do
    cp "$ROOT/$file" "$stage/"
done
cp -R "$ROOT/config/yokes" "$stage/yokes"

echo "Built the plugin: $stage/lin_x64/BrunnerDirect.xpl"
