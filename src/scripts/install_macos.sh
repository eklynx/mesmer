#!/usr/bin/env bash
# Install the plugin built by build_macos.sh (build/stage/BrunnerDirect) into X-Plane on
# this Mac. Specification: docs/macos-build.md ("Installing"); tests: tests/test-install-macos.sh.
#
#   src/scripts/install_macos.sh [--version N] [--xplane-dir PATH] [--force] [--dry-run] [--help]
#
# X-Plane's install folder is the first line of ~/Library/Preferences/x-plane_install_<N>.txt
# (N = X-Plane's major version; the newest one found unless --version says otherwise). Written for macOS's stock bash 3.2.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)" # the repository root
STAGE="$ROOT/build/stage/BrunnerDirect"
version=""
xplane=""
force=0
dry=0

usage() {
    sed -n '2,8p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    cat <<EOT

  --version N        X-Plane major version whose x-plane_install_N.txt names the folder
                     (default: the newest version that has such a file)
  --xplane-dir PATH  the X-Plane folder itself (the one holding Resources/), instead of the file
  --force            install even if X-Plane appears to be running (it will not see the plugin until restart)
  --dry-run          say what would happen and change nothing
  --help             this text

An existing Resources/plugins/BrunnerDirect moves to <X-Plane>/Mesmer-backups/BrunnerDirect-<UTC time>
(X-Plane loads every folder in plugins/, so a backup cannot stay there). Your BrunnerDirect.ini,
BrunnerDirect.feel.ini and BrunnerDirect.yokes.json, and any other file the new plugin does not ship,
are carried into the new folder.
EOT
}

die() { echo "install_macos.sh: $*" >&2; exit 1; }
say() { echo "$*"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --version) [ $# -ge 2 ] || die "--version needs a number"; version="$2"; shift 2 ;;
        --xplane-dir) [ $# -ge 2 ] || die "--xplane-dir needs a path"; xplane="$2"; shift 2 ;;
        --force) force=1; shift ;;
        --dry-run) dry=1; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
done

[ "$(uname -s)" = "Darwin" ] || die "this script installs on macOS and must run on macOS"
case "$version" in *[!0-9]*) die "--version must be a number such as 12" ;; esac

# ---- the staged plugin: what build_macos.sh produced ----
[ -d "$STAGE" ] || die "no staged plugin at $STAGE; run src/scripts/build_macos.sh first"
for file in mac_x64/BrunnerDirect.xpl BrunnerDirect.ini BrunnerDirect.profiles.json yokes/cls-e.json yokes/cls-e-ng.json; do
    [ -f "$STAGE/$file" ] || die "the staged plugin has no $file; run src/scripts/build_macos.sh again"
done

# ---- X-Plane's folder ----
# Either the folder the pilot named (--xplane-dir), or the one X-Plane recorded; if neither is
# available the install stops and says how to name it. Never a guess.
if [ -n "$xplane" ]; then
    source_of="--xplane-dir"
else
    if [ -z "$version" ]; then
        # The newest X-Plane: the highest N among x-plane_install_N.txt (numeric, so 12 beats 9).
        for file in "$HOME"/Library/Preferences/x-plane_install_*.txt; do
            n="${file##*/x-plane_install_}"; n="${n%.txt}"
            case "$n" in ''|*[!0-9]*) continue ;; esac
            { [ -z "$version" ] || [ "$n" -gt "$version" ]; } && version="$n"
        done
        [ -n "$version" ] || die "no X-Plane install folder was found (no x-plane_install_N.txt in $HOME/Library/Preferences: is X-Plane installed, and has it been run once?); pass the folder with --xplane-dir PATH"
    fi
    prefs="$HOME/Library/Preferences/x-plane_install_$version.txt"
    [ -f "$prefs" ] || die "X-Plane $version's install folder was not found ($prefs does not exist: is it installed, and has it been run once?); pass the folder with --xplane-dir PATH"
    xplane="$(sed -n '/[^[:space:]]/{p;q;}' "$prefs" | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')"
    [ -n "$xplane" ] || die "$prefs is empty; pass the X-Plane folder with --xplane-dir PATH"
    source_of="$prefs"
fi
case "$xplane" in "~") xplane="$HOME" ;; "~/"*) xplane="$HOME/${xplane#\~/}" ;; esac
xplane="${xplane%/}"
# Checked whichever way the folder was named, before anything is changed.
[ -d "$xplane" ] || die "the X-Plane folder does not exist: $xplane (from $source_of); pass the right one with --xplane-dir PATH"
[ -d "$xplane/Resources/plugins" ] || die "not an X-Plane folder, it has no Resources/plugins: $xplane (from $source_of); pass the right one with --xplane-dir PATH"

# ---- X-Plane must be closed: it loads plugins at start only ----
if pgrep -f "X-Plane.app/Contents/MacOS" >/dev/null 2>&1; then
    [ "$force" -eq 1 ] || die "X-Plane is running; close it first (or use --force)"
    say "X-Plane is running; the plugin loads at its next start"
fi

target="$xplane/Resources/plugins/BrunnerDirect"
backups="$xplane/Mesmer-backups"
stamp="$(date -u +%Y%m%d-%H%M%S)"
say "X-Plane: $xplane"

kept=()
if [ -d "$target" ]; then
    # What is the pilot's, or the plugin's own state, goes with the new install.
    for own in BrunnerDirect.ini BrunnerDirect.feel.ini BrunnerDirect.yokes.json; do
        [ -f "$target/$own" ] && kept+=("$own")
    done
    for path in "$target"/*; do
        [ -f "$path" ] || continue
        name="$(basename "$path")"
        [ -e "$STAGE/$name" ] && continue
        case " ${kept[*]:-} " in *" $name "*) ;; *) kept+=("$name") ;; esac
    done
fi

if [ "$dry" -eq 1 ]; then
    [ -d "$target" ] && say "Would move the existing plugin to $backups/BrunnerDirect-$stamp"
    say "Would install $STAGE to $target"
    [ "${#kept[@]}" -gt 0 ] && say "Would keep your ${kept[*]}"
    exit 0
fi

work="$(mktemp -d "${TMPDIR:-/tmp}/mesmer-install.XXXXXX")"
trap 'rm -rf "$work"' EXIT
cp -R "$STAGE" "$work/BrunnerDirect"
if [ -d "$target" ]; then
    for name in ${kept[@]+"${kept[@]}"}; do cp -p "$target/$name" "$work/BrunnerDirect/$name"; done
    mkdir -p "$backups"
    backup="$backups/BrunnerDirect-$stamp"
    n=1; while [ -e "$backup" ]; do n=$((n+1)); backup="$backups/BrunnerDirect-$stamp-$n"; done # two installs in a second
    mv "$target" "$backup"
    say "Moved the existing plugin to $backup"
fi
cp -R "$work/BrunnerDirect" "$target"
[ -f "$target/mac_x64/BrunnerDirect.xpl" ] || die "the copy to $target failed"
say "Installed the plugin to $target"
[ "${#kept[@]}" -gt 0 ] && say "Kept your ${kept[*]}"
say "Start X-Plane; Log.txt will say \"Yoke models loaded from ...\" when it finds the yoke."
exit 0
