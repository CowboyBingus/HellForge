#!/usr/bin/env bash
# Starts HellForge on Linux (Proton), with no way out to the internet: docs/linux.md.
#   1. Steam running and signed in, then ./hf-bridge-host.sh in another terminal (leave it open).
#   2. ./hellforge-linux.sh
# What it does: fetches the live galactic war while this PC still has its network (the session that follows has none
# and serves this copy), then starts the launcher inside a network namespace with loopback only
# (hellforge-offline.sh), where hf-inner.sh forwards Steam's local ports and runs HellForge.exe through Proton.
#
# Settings (environment variables; the defaults suit the Steam client in ~/.local/share/Steam):
#   PROTON                            the proton script to run HellForge with (required unless one is found)
#   INSTALL                           the HellForge installation (default: the one this script is in, app/linux)
#   STEAM_COMPAT_CLIENT_INSTALL_PATH  the Steam client (default ~/.local/share/Steam)
#   STEAM_COMPAT_DATA_PATH            the Wine prefix (default: HELLDIVERS 2's, steamapps/compatdata/553850)
#
# Linux support is the work of DreamyBranch and joshua-software-dev (SkyeShade/HellForge issue 1).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.local/share/Steam}"
export STEAM_COMPAT_DATA_PATH="${STEAM_COMPAT_DATA_PATH:-$STEAM_COMPAT_CLIENT_INSTALL_PATH/steamapps/compatdata/553850}"
if [ -z "${INSTALL:-}" ] && [ -f "$here/../../hellforge-install.json" ]; then INSTALL="$(cd "$here/../.." && pwd)"; fi
: "${INSTALL:?INSTALL is not set: the HellForge installation folder (the one with hellforge-install.json)}"
[ -f "$INSTALL/hellforge-install.json" ] || { echo "$INSTALL is not a HellForge installation (no hellforge-install.json)" >&2; exit 1; }
export INSTALL
if [ -z "${PROTON:-}" ]; then
  # the newest Proton the Steam client has, if any
  PROTON="$(ls -d "$STEAM_COMPAT_CLIENT_INSTALL_PATH"/steamapps/common/Proton*/proton "$STEAM_COMPAT_CLIENT_INSTALL_PATH"/compatibilitytools.d/*/proton 2>/dev/null | sort -V | tail -n 1 || true)"
fi
: "${PROTON:?PROTON is not set and no Proton was found: set it to a proton script, e.g. .../compatibilitytools.d/<name>/proton}"
export PROTON
echo "HellForge: $INSTALL with $PROTON (prefix $STEAM_COMPAT_DATA_PATH)"

dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
compgen -G "$dir/hf-steam-*.sock" >/dev/null || { echo "Steam's bridge is not running: start ./hf-bridge-host.sh first (with Steam signed in)" >&2; exit 1; }

# The live war, now, while there is a network (Wine sees / as Z:). A failure is not fatal: the session then serves the
# last war fetched, or HellForge's built-in one.
backend="$INSTALL/app/backend/HellForge.LocalBackend.exe"
cache="Z:$(printf '%s' "$INSTALL/live-war.json" | tr '/' '\\')"
if timeout 60 "$PROTON" run "$backend" fetch-live-war "$cache"; then echo "Fetched the live galactic war."
else echo "Could not fetch the live galactic war; the session serves the last one fetched (or the built-in war)." >&2; fi
# (proton can return before a Windows program ends: the fetch finishes before the namespace is entered)
for _ in $(seq 1 30); do pgrep -f "LocalBackend.exe fetch-live-war" >/dev/null || break; sleep 1; done

exec "$here/hellforge-offline.sh" "$here/hf-inner.sh"
