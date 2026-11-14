#!/usr/bin/env bash
# Merge the per-platform plugin builds into the one release zip. Specification:
# docs/release-packaging.md (the source of truth for what this script guarantees and how it
# fails). Tests: tests/test-package-release.sh.
#
#   src/scripts/package_release.sh --version V --out DIR [--win DIR] [--mac DIR] [--linux DIR] [--help]
#
# Output: DIR/Mesmer-V.zip (one folder, BrunnerDirect/, holding every platform given) and
# DIR/SHA256SUMS. Written for macOS's stock bash 3.2; needs zip, file and sha256sum or shasum.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)" # the repository root: this script is src/scripts/package_release.sh
# The files every platform's plugin folder holds, the list build_macos.sh stages too.
SHARED_FILES="config/BrunnerDirect.ini config/BrunnerDirect.profiles.json config/skunkcrafts_updater.cfg README.md LICENSE NOTICE SAFETY.md"

version=""
out=""
win=""
mac=""
linux=""

usage() {
    sed -n '2,7p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<EOF

  --version V   the release version: X.Y.Z, optionally -alpha.N, -beta.N or -rc.N
  --out DIR     where Mesmer-V.zip and SHA256SUMS are written
  --win DIR     a folder holding the Windows BrunnerDirect.xpl (a PE DLL)     -> win_x64/
  --mac DIR     a folder holding the macOS BrunnerDirect.xpl (universal)      -> mac_x64/
  --linux DIR   a folder holding the Linux BrunnerDirect.xpl (an ELF object)  -> lin_x64/
  --help        this text

At least one platform is required. Nothing is written unless every check passes.
EOF
}

die() { echo "package_release.sh: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --version|--out|--win|--mac|--linux)
            [ $# -ge 2 ] || die "$1 needs a value"
            case "$1" in
                --version) version="$2" ;;
                --out) out="$2" ;;
                --win) win="$2" ;;
                --mac) mac="$2" ;;
                --linux) linux="$2" ;;
            esac
            shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
done

[ -n "$version" ] || die "--version is required"
[ -n "$out" ] || die "--out is required"
echo "$version" | grep -E -q '^[0-9]+\.[0-9]+\.[0-9]+(-(alpha|beta|rc)\.[0-9]+)?$' \
    || die "version $version is not X.Y.Z or X.Y.Z-(alpha|beta|rc).N"
[ -n "$win$mac$linux" ] || die "no platform given; pass --win, --mac and/or --linux"
for tool in zip file; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool not found"
done
if command -v sha256sum >/dev/null 2>&1; then sha256() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then sha256() { shasum -a 256 "$1" | awk '{print $1}'; }
else die "neither sha256sum nor shasum found"; fi

# check_plugin NAME DIR WANT: DIR holds a non-empty BrunnerDirect.xpl of the right object format.
check_plugin() {
    local name="$1" dir="$2" want="$3" xpl="$2/BrunnerDirect.xpl" kind
    [ -d "$dir" ] || die "$name: no such folder: $dir"
    [ -f "$xpl" ] || die "$name: $dir has no BrunnerDirect.xpl"
    [ -s "$xpl" ] || die "$name: $xpl is empty"
    kind="$(file -b "$xpl")"
    case "$want" in
        pe) case "$kind" in *"PE32+ executable (DLL)"*) ;; *) die "$name: $xpl is not a 64-bit Windows DLL (file says: $kind)" ;; esac ;;
        mach) case "$kind" in
                  *"Mach-O universal binary"*x86_64*arm64*|*"Mach-O universal binary"*arm64*x86_64*) ;;
                  *) die "$name: $xpl is not a universal arm64 + x86_64 Mach-O (file says: $kind)" ;;
              esac ;;
        elf) case "$kind" in "ELF 64-bit"*) ;; *) die "$name: $xpl is not a 64-bit ELF object (file says: $kind)" ;; esac ;;
    esac
}

[ -z "$win" ] || check_plugin Windows "$win" pe
[ -z "$mac" ] || check_plugin macOS "$mac" mach
[ -z "$linux" ] || check_plugin Linux "$linux" elf
for file in $SHARED_FILES; do [ -f "$ROOT/$file" ] || die "the repository has no $file"; done
[ -d "$ROOT/config/yokes" ] || die "the repository has no config/yokes"

work="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-package.XXXXXX")"
trap 'rm -rf "$work"' EXIT
plugin="$work/BrunnerDirect"
mkdir -p "$plugin"
for file in $SHARED_FILES; do cp "$ROOT/$file" "$plugin/"; done
cp -R "$ROOT/config/yokes" "$plugin/yokes"
stage_platform() { mkdir -p "$plugin/$1" && cp "$2/BrunnerDirect.xpl" "$plugin/$1/"; }
[ -z "$win" ] || stage_platform win_x64 "$win"
[ -z "$mac" ] || stage_platform mac_x64 "$mac"
[ -z "$linux" ] || stage_platform lin_x64 "$linux"

zip_name="Mesmer-$version.zip"
(cd "$work" && zip -X -r -q "$zip_name" BrunnerDirect) || die "zip failed"
hash="$(sha256 "$work/$zip_name")"
printf '%s  %s\n' "$hash" "$zip_name" > "$work/SHA256SUMS"

mkdir -p "$out"
mv "$work/$zip_name" "$out/$zip_name"
mv "$work/SHA256SUMS" "$out/SHA256SUMS"
echo "Packaged $out/$zip_name:$([ -z "$win" ] || echo ' win_x64')$([ -z "$mac" ] || echo ' mac_x64')$([ -z "$linux" ] || echo ' lin_x64')"
cat "$out/SHA256SUMS"
