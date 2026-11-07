#!/usr/bin/env bash
# Trails uninstall -- removes exactly what install.sh's manifest lists.
#   ./uninstall.sh               -> ~/.local
#   ./uninstall.sh --system      -> /usr/local
#   ./uninstall.sh --prefix DIR  -> DIR
#   --purge  also removes settings, recents and cache (~/.config/trails,
#            ~/.local/share/trails, ~/.cache/trails). Never touches recordings
#            you saved elsewhere.
set -euo pipefail

APP=trails
PREFIX="$HOME/.local"
PURGE=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --system) PREFIX=/usr/local ;;
        --prefix) shift; PREFIX="${1:?--prefix needs a directory}" ;;
        --purge)  PURGE=1 ;;
        -h|--help) sed -n '2,8p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "uninstall.sh: unknown option '$1'" >&2; exit 2 ;;
    esac
    shift
done
PREFIX="$(realpath -m "${PREFIX%/}")"
MANIFEST="$PREFIX/share/$APP/install-manifest.txt"
[[ -f "$MANIFEST" ]] || { echo "uninstall.sh: no manifest at $MANIFEST -- nothing installed there?" >&2; exit 1; }

SUDO=""; [[ -w "$PREFIX" && -w "$(dirname "$MANIFEST")" ]] || SUDO=sudo

while IFS= read -r f; do
    [[ -z "$f" ]] && continue
    real="$(realpath -m "$f")"
    case "$real" in
        "$PREFIX"/*) ;;
        *) echo "  skip (outside prefix): $f"; continue ;;
    esac
    if [[ -e "$real" || -L "$real" ]]; then $SUDO rm -f -- "$real" && echo "  removed $real"; fi
done < "$MANIFEST"
$SUDO rmdir --ignore-fail-on-non-empty "$PREFIX/share/$APP" 2>/dev/null || true

ICONS="$PREFIX/share/icons/hicolor"; APPS="$PREFIX/share/applications"
command -v gtk-update-icon-cache   >/dev/null && $SUDO gtk-update-icon-cache -qtf "$ICONS" 2>/dev/null || true
command -v update-desktop-database >/dev/null && $SUDO update-desktop-database -q "$APPS" 2>/dev/null || true

if (( PURGE )); then
    for d in "${XDG_CONFIG_HOME:-$HOME/.config}/$APP" "${XDG_DATA_HOME:-$HOME/.local/share}/$APP" "${XDG_CACHE_HOME:-$HOME/.cache}/$APP"; do
        [[ -d "$d" ]] && rm -rf -- "$d" && echo "  purged $d"
    done
fi
echo "Trails uninstalled from $PREFIX"
