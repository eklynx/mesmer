#!/usr/bin/env bash
# Build the Mesmer X-Plane 12 plugin for macOS as a universal (arm64 + x86_64)
# binary. Specification: docs/macos-build.md (the source of truth for what this
# script guarantees and how it fails). Tests: tests/test-build-macos.sh.
#
#   ./build_macos.sh [--sdk PATH] [--clean] [--no-test] [--help]
#
# Output: build/stage/BrunnerDirect/mac_x64/BrunnerDirect.xpl (plus the files
# the Windows release ships). Written for macOS's stock bash 3.2.
set -euo pipefail

# The X-Plane SDK pin. Keep it equal to XPLANE_SDK_URL / XPLANE_SDK_SHA256 in
# .github/workflows/release.yml. The overrides exist for tests/test-build-macos.sh.
SDK_URL="${MESMER_SDK_URL:-https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip}"
SDK_SHA256="${MESMER_SDK_SHA256:-b9875ab27b593927b4f9b3e0ddfffe7401ee5dce6d86b50aea0da65f70ff7816}"
DEPLOYMENT_TARGET="11.0"
ARCHS="arm64;x86_64"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$ROOT/build"
sdk="${XPLANE_SDK:-}"
clean=0
run_tests=1

usage() {
    sed -n '2,9p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<EOF

  --sdk PATH   unpacked X-Plane SDK directory (the one holding CHeaders/); overrides \$XPLANE_SDK
  --clean      remove build/ first (a downloaded SDK in build/sdk is re-downloaded)
  --no-test    skip ctest
  --help       this text

With no --sdk and no \$XPLANE_SDK, the pinned SDK is downloaded into build/sdk
and checked against its SHA-256 before it is unpacked.
EOF
}

die() { echo "build_macos.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --sdk) [ $# -ge 2 ] || die "--sdk needs a path"; sdk="$2"; shift 2 ;;
        --clean) clean=1; shift ;;
        --no-test) run_tests=0; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || die "this script builds for macOS and must run on macOS"
for tool in cmake lipo codesign curl shasum unzip; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found (install Xcode command line tools and CMake)"
done

[ "$clean" -eq 1 ] && rm -rf "$BUILD"
mkdir -p "$BUILD"

fetch_sdk() {
    local dir="$BUILD/sdk" zip="$BUILD/sdk/xplane-sdk.zip" got
    mkdir -p "$dir"
    if [ ! -f "$zip" ]; then
        echo "Downloading the X-Plane SDK: $SDK_URL"
        curl --fail --silent --show-error --location --output "$zip" "$SDK_URL" \
            || { rm -f "$zip"; die "could not download the X-Plane SDK; pass --sdk PATH or set XPLANE_SDK"; }
    fi
    got="$(shasum -a 256 "$zip" | awk '{print $1}')"
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
[ -d "$sdk/Libraries/Mac/XPLM.framework" ] || die "the SDK has no Libraries/Mac/XPLM.framework: $sdk"
sdk="$(cd "$sdk" && pwd)"

# Xcode installed under a path with a space (e.g. on an external volume) breaks
# the generated Makefiles, because make recurses through its own unquoted path.
# Hand CMake a space-free symlink to the same make instead.
make_program="$(xcrun -f make 2>/dev/null || true)"
make_link=""
case "$make_program" in
    *" "*)
        make_link="$(mktemp -d /tmp/mesmer-make.XXXXXX)"
        trap 'rm -rf "$make_link"' EXIT
        ln -s "$make_program" "$make_link/make"
        echo "Xcode's make path contains a space; using $make_link/make"
        ;;
esac

echo "Configuring ($ARCHS, macOS $DEPLOYMENT_TARGET, SDK $sdk)"
cmake -S "$ROOT/src" -B "$BUILD" \
    -DCMAKE_BUILD_TYPE=Release \
    ${make_link:+"-DCMAKE_MAKE_PROGRAM=$make_link/make"} \
    "-DCMAKE_OSX_ARCHITECTURES=$ARCHS" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$DEPLOYMENT_TARGET" \
    "-DXPLANE_SDK=$sdk"
cmake --build "$BUILD" --parallel

xpl="$BUILD/BrunnerDirect.xpl"
[ -f "$xpl" ] || die "the build did not produce $xpl"

# A universal binary must carry both slices; a single-arch result is a failure.
archs="$(lipo -archs "$xpl" | tr ' ' '\n' | sort | tr '\n' ' ' | sed 's/ $//')"
[ "$archs" = "arm64 x86_64" ] || die "BrunnerDirect.xpl is not universal (slices: $archs)"

# X-Plane on Apple silicon refuses unsigned arm64 code; ad hoc is enough for local use.
codesign --force --sign - "$xpl"
codesign --verify --strict "$xpl"

if [ "$run_tests" -eq 1 ]; then
    ctest --test-dir "$BUILD" --output-on-failure
fi

stage="$BUILD/stage/BrunnerDirect"
rm -rf "$BUILD/stage"
mkdir -p "$stage/mac_x64"
cp "$xpl" "$stage/mac_x64/"
for file in config/BrunnerDirect.ini config/BrunnerDirect.profiles.json config/skunkcrafts_updater.cfg README.md LICENSE NOTICE SAFETY.md; do
    cp "$ROOT/$file" "$stage/"
done
cp -R "$ROOT/config/yokes" "$stage/yokes"

echo "Built a universal plugin ($archs): $stage/mac_x64/BrunnerDirect.xpl"
