#!/usr/bin/env bash
# Trails install.
#   ./install.sh               -> ~/.local            (no sudo)
#   ./install.sh --system      -> /usr/local          (sudo if not writable)
#   ./install.sh --prefix DIR  -> DIR
#   --build                    -> run ./build.sh first
# Installs: bin/trails, the .desktop, the scalable + symbolic hicolor icons,
# and share/trails/install-manifest.txt (what uninstall.sh removes, exactly).
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

APP=trails
ID=io.github.scott8420.Trails
PREFIX="$HOME/.local"
DO_BUILD=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --system) PREFIX=/usr/local ;;
        --prefix) shift; PREFIX="${1:?--prefix needs a directory}" ;;
        --build)  DO_BUILD=1 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "install.sh: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done
PREFIX="${PREFIX%/}"

(( DO_BUILD )) && ./build.sh
[[ -x build/$APP ]] || { echo "install.sh: build/$APP not found -- run ./build.sh (or pass --build)" >&2; exit 1; }

# sudo only when the prefix isn't writable by us.
probe="$PREFIX"; while [[ ! -e "$probe" ]]; do probe="$(dirname "$probe")"; done
SUDO=""; [[ -w "$probe" ]] || SUDO=sudo

BIN="$PREFIX/bin"
APPS="$PREFIX/share/applications"
ICONS="$PREFIX/share/icons/hicolor"
SHARE="$PREFIX/share/$APP"
MANIFEST="$SHARE/install-manifest.txt"

tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
sed "s|@BINDIR@|$BIN|g" "data/$ID.desktop.in" > "$tmp/$ID.desktop"

files=()
put() {  # put MODE SRC DEST
    $SUDO install -Dm"$1" "$2" "$3"
    files+=("$3")
}
put 755 "build/$APP"                    "$BIN/$APP"
put 644 "$tmp/$ID.desktop"              "$APPS/$ID.desktop"
put 644 "data/$ID.svg"                  "$ICONS/scalable/apps/$ID.svg"
put 644 "data/$ID-symbolic.svg"         "$ICONS/symbolic/apps/$ID-symbolic.svg"

printf '%s\n' "${files[@]}" "$MANIFEST" > "$tmp/manifest"
$SUDO install -Dm644 "$tmp/manifest" "$MANIFEST"

command -v gtk-update-icon-cache   >/dev/null && $SUDO gtk-update-icon-cache -qtf "$ICONS" 2>/dev/null || true
command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$APPS" 2>/dev/null || true

echo "Installed Trails to $PREFIX"
printf '  %s\n' "${files[@]}"
[[ ":$PATH:" == *":$BIN:"* ]] || echo "Note: $BIN is not on your PATH."
