#!/usr/bin/env bash
# Tests package_release.sh against docs/release-packaging.md. No network, no X-Plane SDK, no
# real build: the script is copied into a sandbox repository of placeholder files, and each
# platform's plugin is a minimal file of the right object format (PE DLL, universal Mach-O,
# ELF), made here with python3. Runs on Linux and macOS.
#   tests/test-package-release.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=0
check() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; failures=$((failures+1)); fi; }

sandbox="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-test.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT

# A sandbox repository: the script plus the shared files it packs.
mkdir -p "$sandbox/src/scripts" "$sandbox/config/yokes"
cp "$ROOT/src/scripts/package_release.sh" "$sandbox/src/scripts/"
script="$sandbox/src/scripts/package_release.sh"
for file in config/BrunnerDirect.ini config/BrunnerDirect.profiles.json config/skunkcrafts_updater.cfg README.md LICENSE NOTICE SAFETY.md config/yokes/cls-e.json config/yokes/cls-e-ng.json; do
    echo "placeholder $file" > "$sandbox/$file"
done

# Minimal binaries file(1) recognises as each platform's plugin.
python3 -I - "$sandbox/bin" <<'EOF'
import os,struct,sys
out=sys.argv[1]
def put(folder,data):
    os.makedirs(os.path.join(out,folder),exist_ok=True)
    open(os.path.join(out,folder,'BrunnerDirect.xpl'),'wb').write(data)
dos=b'MZ'+b'\0'*58+struct.pack('<I',0x40)
pe=b'PE\0\0'+struct.pack('<HHIIIHH',0x8664,0,0,0,0,240,0x2022)+struct.pack('<H',0x20b)+b'\0'*238
put('win',dos+pe)
put('lin',b'\x7fELF\x02\x01\x01'+b'\0'*9+struct.pack('<HHIQQQIHHHHHH',3,0x3e,1,0,64,0,0,64,56,0,64,0,0))
def thin(cpu,sub): return struct.pack('<IIIIII',0xfeedfacf,cpu,sub,8,0,0)+b'\0'*8
def fat(archs):
    slices=[thin(c,s) for c,s in archs]
    hdr=struct.pack('>II',0xcafebabe,len(archs));data=b'';offs=[]
    off=4096
    for (c,s),sl in zip(archs,slices):
        hdr+=struct.pack('>iiIII',c,s,off,len(sl),12);offs.append(off);off+=4096
    data=hdr.ljust(4096,b'\0')
    for sl in slices: data+=sl.ljust(4096,b'\0')
    return data
X86=(0x01000007,3);ARM=(0x0100000c,0)
put('mac',fat([X86,ARM]))
put('mac-arm-only',fat([ARM]))
put('empty',b'')
open(os.path.join(out,'mac-text.xpl'),'w').write('not a binary')
EOF
bin="$sandbox/bin"
mkdir -p "$bin/mac-text" "$bin/no-xpl"
mv "$bin/mac-text.xpl" "$bin/mac-text/BrunnerDirect.xpl"

run() { "$script" "$@" 2>&1; }
zip_list() { unzip -Z1 "$1" | LC_ALL=C sort; }

# Spec: --help prints usage and exits 0, writing nothing.
out="$(run --help)"; code=$?
check $code "--help exits 0"
echo "$out" | grep -q -- "--version V"; check $? "--help documents --version"

# Spec: win + mac makes a zip with exactly the shared files, yokes and both platforms, and a
# SHA256SUMS line that matches it.
out="$(run --version 1.2.3 --out "$sandbox/o1" --win "$bin/win" --mac "$bin/mac")"; code=$?
check $code "win + mac packages"
zip="$sandbox/o1/Mesmer-1.2.3.zip"
[ -f "$zip" ]; check $? "writes Mesmer-<version>.zip"
want="BrunnerDirect/BrunnerDirect.ini
BrunnerDirect/BrunnerDirect.profiles.json
BrunnerDirect/LICENSE
BrunnerDirect/NOTICE
BrunnerDirect/README.md
BrunnerDirect/SAFETY.md
BrunnerDirect/mac_x64/BrunnerDirect.xpl
BrunnerDirect/skunkcrafts_updater.cfg
BrunnerDirect/win_x64/BrunnerDirect.xpl
BrunnerDirect/yokes/cls-e-ng.json
BrunnerDirect/yokes/cls-e.json"
got="$(zip_list "$zip" | grep -v '/$')"
[ "$got" = "$want" ]; check $? "the zip holds the shared files, yokes and win_x64 + mac_x64"
sum="$(cat "$sandbox/o1/SHA256SUMS")"
if command -v sha256sum >/dev/null 2>&1; then actual="$(sha256sum "$zip" | awk '{print $1}')"; else actual="$(shasum -a 256 "$zip" | awk '{print $1}')"; fi
[ "$sum" = "$actual  Mesmer-1.2.3.zip" ]; check $? "SHA256SUMS is '<hash>  <zip>' for the zip"
unzip -p "$zip" BrunnerDirect/mac_x64/BrunnerDirect.xpl | cmp -s - "$bin/mac/BrunnerDirect.xpl"; check $? "the mac plugin is stored unchanged"

# Spec: all three platforms, and a pre-release version, are accepted.
out="$(run --version 0.1.0-alpha.4 --out "$sandbox/o2" --win "$bin/win" --mac "$bin/mac" --linux "$bin/lin")"; code=$?
check $code "win + mac + linux packages"
zip_list "$sandbox/o2/Mesmer-0.1.0-alpha.4.zip" | grep -q 'BrunnerDirect/lin_x64/BrunnerDirect.xpl'; check $? "linux lands in lin_x64/"

# Spec: one platform alone is allowed.
out="$(run --version 1.0.0 --out "$sandbox/o3" --mac "$bin/mac")"; code=$?
check $code "mac alone packages"
zip_list "$sandbox/o3/Mesmer-1.0.0.zip" | grep -q 'win_x64' && r=1 || r=0; check $r "a platform not given is not in the zip"

# Spec: each failure names its cause, exits non-zero and leaves --out unwritten.
fails() { # fails LABEL PATTERN args...   (args get --out "$sandbox/bad")
    local label="$1" pattern="$2"; shift 2
    rm -rf "$sandbox/bad"
    out="$(run "$@" --out "$sandbox/bad")"; code=$?
    [ $code -ne 0 ] && echo "$out" | grep -q -e "$pattern" && [ ! -e "$sandbox/bad" ]
    check $? "$label"
}
fails "no platform is rejected" "no platform given" --version 1.0.0
fails "a missing version is rejected" "--version is required" --win "$bin/win"
fails "a bad version is rejected" "is not X.Y.Z" --version v1.0 --win "$bin/win"
fails "an unknown option is rejected" "unknown option: --bogus" --bogus
fails "a missing folder is rejected" "no such folder" --version 1.0.0 --win "$bin/nowhere"
fails "a folder without the plugin is rejected" "has no BrunnerDirect.xpl" --version 1.0.0 --win "$bin/no-xpl"
fails "an empty plugin is rejected" "is empty" --version 1.0.0 --win "$bin/empty"
fails "a Linux plugin passed as Windows is rejected" "not a 64-bit Windows DLL" --version 1.0.0 --win "$bin/lin"
fails "a Windows plugin passed as macOS is rejected" "not a universal arm64 + x86_64 Mach-O" --version 1.0.0 --mac "$bin/win"
fails "a one-slice macOS plugin is rejected" "not a universal arm64 + x86_64 Mach-O" --version 1.0.0 --mac "$bin/mac-arm-only"
fails "a text file as macOS is rejected" "not a universal arm64 + x86_64 Mach-O" --version 1.0.0 --mac "$bin/mac-text"
fails "a macOS plugin passed as Linux is rejected" "not a 64-bit ELF" --version 1.0.0 --linux "$bin/mac"

# Spec: a missing shared file is a failure (the zip is never partial).
mv "$sandbox/NOTICE" "$sandbox/NOTICE.away"
fails "a missing shared file is rejected" "has no NOTICE" --version 1.0.0 --win "$bin/win"
mv "$sandbox/NOTICE.away" "$sandbox/NOTICE"

[ "$failures" -eq 0 ] && echo "all package_release tests passed" || { echo "$failures failed"; exit 1; }
