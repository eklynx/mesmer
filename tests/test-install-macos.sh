#!/usr/bin/env bash
# Tests src/scripts/install_macos.sh against docs/macos-build.md ("Installing"). Everything runs in a
# sandbox: a fake HOME with an x-plane_install_12.txt, a fake X-Plane folder and a fake staged plugin.
#   tests/test-install-macos.sh
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
failures=0
check() { if [ "$1" -eq 0 ]; then echo "ok   $2"; else echo "FAIL $2"; failures=$((failures+1)); fi; }

[ "$(uname -s)" = "Darwin" ] || { echo "skip: needs macOS"; exit 0; }

sandbox="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-install-test.XXXXXX")"
trap 'rm -rf "$sandbox"' EXIT
mkdir -p "$sandbox/src/scripts" "$sandbox/bin"
cp "$ROOT/src/scripts/install_macos.sh" "$sandbox/src/scripts/"
script="$sandbox/src/scripts/install_macos.sh"
export HOME="$sandbox/home"
mkdir -p "$HOME/Library/Preferences"
# A pgrep that finds no X-Plane unless told otherwise.
printf '#!/bin/sh\n[ -n "$FAKE_XPLANE_RUNNING" ]\n' >"$sandbox/bin/pgrep"; chmod +x "$sandbox/bin/pgrep"
export PATH="$sandbox/bin:$PATH"

stage="$sandbox/build/stage/BrunnerDirect"
mkdir -p "$stage/mac_x64" "$stage/yokes"
for f in mac_x64/BrunnerDirect.xpl BrunnerDirect.ini BrunnerDirect.profiles.json yokes/cls-e.json yokes/cls-e-ng.json; do echo "new $f" >"$stage/$f"; done
xp="$sandbox/X-Plane 12"   # a space, as on the author's machine
mkdir -p "$xp/Resources/plugins"
printf '%s/\n   \n' "$xp" >"$HOME/Library/Preferences/x-plane_install_12.txt"

# Spec: --help prints usage and exits 0 and changes nothing.
out="$("$script" --help 2>&1)"; code=$?
check $code "--help exits 0"
echo "$out" | grep -q -- "--xplane-dir PATH"; check $? "--help documents --xplane-dir"

# Spec: an unknown option, a bad version and a missing value fail and say so.
out="$("$script" --bogus 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "unknown option: --bogus"; check $? "unknown option is rejected"
out="$("$script" --version twelve 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "must be a number"; check $? "a non-numeric version is rejected"
out="$("$script" --xplane-dir 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "needs a path"; check $? "--xplane-dir without a path is rejected"

# Spec: nothing staged means nothing is installed, and the way to fix it is named.
mv "$stage" "$sandbox/stage-away"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "run src/scripts/build_macos.sh first"; check $? "no staged plugin is refused"
mv "$sandbox/stage-away" "$stage"
rm "$stage/yokes/cls-e-ng.json"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "no yokes/cls-e-ng.json"; check $? "an incomplete stage is refused"
echo "new yokes/cls-e-ng.json" >"$stage/yokes/cls-e-ng.json"

# Spec: the folder comes from x-plane_install_<version>.txt; a missing file says which.
out="$("$script" --version 11 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "x-plane_install_11.txt does not exist" && echo "$out" | grep -q -- "--xplane-dir PATH"; check $? "a missing install file is named, with the way out"
out="$("$script" --xplane-dir "$sandbox/nowhere" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "does not exist"; check $? "a named folder that does not exist is refused"
mkdir -p "$sandbox/notxplane"
out="$("$script" --xplane-dir "$sandbox/notxplane" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "no Resources/plugins" && echo "$out" | grep -q "from --xplane-dir"; check $? "a named folder without Resources/plugins is refused"
# The recorded folder gets the same checks, and the error names the file it came from.
cp "$HOME/Library/Preferences/x-plane_install_12.txt" "$sandbox/prefs-keep"
printf '%s\n' "$sandbox/notxplane" >"$HOME/Library/Preferences/x-plane_install_12.txt"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "no Resources/plugins" && echo "$out" | grep -q "x-plane_install_12.txt"; check $? "a recorded folder without Resources/plugins is refused"
printf '%s\n' "$sandbox/gone" >"$HOME/Library/Preferences/x-plane_install_12.txt"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "does not exist"; check $? "a recorded folder that is gone is refused"
: >"$HOME/Library/Preferences/x-plane_install_12.txt"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "is empty; pass the X-Plane folder with --xplane-dir"; check $? "an empty install file is refused with the way out"
out="$("$script" --dry-run --xplane-dir "$xp" 2>&1)"; code=$?; [ $code -eq 0 ]; check $? "--xplane-dir works when the install file is unusable"
mv "$sandbox/prefs-keep" "$HOME/Library/Preferences/x-plane_install_12.txt"

# Spec: with several x-plane_install_N.txt files the newest version (numerically) is used, unless --version names one.
xp11="$sandbox/X-Plane 11"; xp9="$sandbox/X-Plane 9"; mkdir -p "$xp11/Resources/plugins" "$xp9/Resources/plugins"
printf '%s\n' "$xp11" >"$HOME/Library/Preferences/x-plane_install_11.txt"
printf '%s\n' "$xp9" >"$HOME/Library/Preferences/x-plane_install_9.txt"
out="$("$script" --dry-run 2>&1)"; code=$?; [ $code -eq 0 ] && echo "$out" | grep -q "X-Plane: $xp\$"; check $? "the newest version's file is used (12 over 11 and 9)"
rm "$HOME/Library/Preferences/x-plane_install_12.txt"
out="$("$script" --dry-run 2>&1)"; code=$?; [ $code -eq 0 ] && echo "$out" | grep -q "X-Plane: $xp11\$"; check $? "11 is newer than 9 (numeric, not text, order)"
out="$("$script" --dry-run --version 9 2>&1)"; code=$?; [ $code -eq 0 ] && echo "$out" | grep -q "X-Plane: $xp9\$"; check $? "--version picks a file"
rm "$HOME/Library/Preferences/x-plane_install_11.txt" "$HOME/Library/Preferences/x-plane_install_9.txt"
out="$("$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "no x-plane_install_N.txt" && echo "$out" | grep -q -- "--xplane-dir PATH"; check $? "no install file at all is refused with the way out"
printf '%s/\n   \n' "$xp" >"$HOME/Library/Preferences/x-plane_install_12.txt"

# Spec: a running X-Plane stops the install unless --force.
out="$(FAKE_XPLANE_RUNNING=1 "$script" 2>&1)"; code=$?; [ $code -ne 0 ] && echo "$out" | grep -q "X-Plane is running"; check $? "a running X-Plane is refused"
[ ! -e "$xp/Resources/plugins/BrunnerDirect" ]; check $? "a refused install changes nothing"

# Spec: --dry-run says what it would do and changes nothing.
out="$("$script" --dry-run 2>&1)"; code=$?
[ $code -eq 0 ] && echo "$out" | grep -q "Would install" && [ ! -e "$xp/Resources/plugins/BrunnerDirect" ]; check $? "--dry-run changes nothing"

# Spec: a first install copies the staged plugin to Resources/plugins/BrunnerDirect.
out="$("$script" 2>&1)"; code=$?
[ $code -eq 0 ] && [ "$(cat "$xp/Resources/plugins/BrunnerDirect/mac_x64/BrunnerDirect.xpl")" = "new mac_x64/BrunnerDirect.xpl" ] \
    && [ -f "$xp/Resources/plugins/BrunnerDirect/yokes/cls-e-ng.json" ] && [ ! -e "$xp/Mesmer-backups" ]
check $? "a first install copies the plugin and makes no backup"

# Spec: a reinstall moves the old plugin to Mesmer-backups and keeps the pilot's files and the plugin's state.
target="$xp/Resources/plugins/BrunnerDirect"
echo "mine" >"$target/BrunnerDirect.ini"; echo "{yokes}" >"$target/BrunnerDirect.yokes.json"; echo "state" >"$target/BrunnerDirect.state.json"
echo "old" >"$target/mac_x64/BrunnerDirect.xpl"
out="$("$script" 2>&1)"; code=$?
backup="$(ls -d "$xp"/Mesmer-backups/BrunnerDirect-* 2>/dev/null | head -1)"
[ $code -eq 0 ] && [ -n "$backup" ] && [ "$(cat "$backup/mac_x64/BrunnerDirect.xpl")" = "old" ]; check $? "a reinstall backs the old plugin up outside plugins/"
[ "$(cat "$target/BrunnerDirect.ini")" = "mine" ] && [ "$(cat "$target/BrunnerDirect.yokes.json")" = "{yokes}" ]; check $? "the pilot's ini and yoke overrides are kept"
[ "$(cat "$target/BrunnerDirect.state.json")" = "state" ]; check $? "a file the new plugin does not ship is carried over"
[ "$(cat "$target/mac_x64/BrunnerDirect.xpl")" = "new mac_x64/BrunnerDirect.xpl" ]; check $? "the plugin itself is the new one"
[ "$(ls "$xp/Resources/plugins")" = "BrunnerDirect" ]; check $? "no backup is left in plugins/"

# Spec: --force installs while X-Plane runs; --xplane-dir bypasses the install file.
out="$(FAKE_XPLANE_RUNNING=1 "$script" --force --xplane-dir "$xp/" 2>&1)"; code=$?; [ $code -eq 0 ] && echo "$out" | grep -q "loads at its next start"; check $? "--force installs while X-Plane runs"
rm -f "$HOME/Library/Preferences/x-plane_install_12.txt"
out="$("$script" --xplane-dir "$xp" 2>&1)"; code=$?; [ $code -eq 0 ]; check $? "--xplane-dir needs no install file"

[ $failures -eq 0 ] && echo "all passed" || { echo "$failures failed"; exit 1; }
