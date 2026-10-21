#!/usr/bin/env bash
# Starts HellForge on Linux (Proton), with no way out to the internet: docs/linux.md. One command, with Steam running and
# signed in: ./hellforge-linux.sh
# What it does: bridges Steam's local ports (hf-bridge-host.sh, unless one is running already), fetches the live galactic
# war while this PC still has its network (the session that follows has none and serves this copy), then starts the
# launcher inside a network namespace with loopback only (hellforge-offline.sh), where hf-inner.sh forwards Steam's
# ports and runs HellForge.exe through Proton. The bridge it started stops when HellForge closes.
#
# Settings (environment variables; the defaults suit the Steam client in ~/.local/share/Steam):
#   PROTON                            the proton script to run HellForge with (default: one the Steam client has; set it)
#   INSTALL                           the HellForge installation (default: the one this script is in, app/linux)
#   STEAM_COMPAT_CLIENT_INSTALL_PATH  the Steam client (default ~/.local/share/Steam)
#   STEAM_COMPAT_DATA_PATH            the Wine prefix (default: HELLDIVERS 2's, steamapps/compatdata/553850; a prefix of
#                                     HellForge's own is better: docs/linux.md)
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
  PROTON="$(ls -d "$STEAM_COMPAT_CLIENT_INSTALL_PATH"/steamapps/common/Proton*/proton "$STEAM_COMPAT_CLIENT_INSTALL_PATH"/compatibilitytools.d/*/proton 2>/dev/null | sort -V | tail -n 1 || true)"
fi
: "${PROTON:?PROTON is not set and no Proton was found: set it to a proton script, e.g. .../compatibilitytools.d/<name>/proton}"
export PROTON
echo "HellForge: $INSTALL with $PROTON (prefix $STEAM_COMPAT_DATA_PATH)"

# Steam's bridge: the one running, else one of our own for as long as HellForge runs.
dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
bridge=""
cleanup() { if [ -n "$bridge" ]; then kill "$bridge" 2>/dev/null || true; wait "$bridge" 2>/dev/null || true; fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM
# a bridge answers on its sockets (ones left by a bridge that was killed do not)
bridged() { for s in "$dir"/hf-steam-*.sock; do [ -S "$s" ] && socat -u OPEN:/dev/null UNIX-CONNECT:"$s" 2>/dev/null && return 0; done; return 1; }
if ! bridged; then
  "$here/hf-bridge-host.sh" &
  bridge=$!
  for _ in $(seq 1 20); do bridged && break; kill -0 "$bridge" 2>/dev/null || break; sleep 0.5; done
fi
bridged || { echo "Steam's local ports could not be bridged: is Steam running and signed in?" >&2; exit 1; }

# The live war, now, while there is a network (Wine sees / as Z:). A failure is not fatal: the session then serves the
# last war fetched, or HellForge's built-in one.
backend="$INSTALL/app/backend/HellForge.LocalBackend.exe"
cache="Z:$(printf '%s' "$INSTALL/live-war.json" | tr '/' '\\')"
if timeout 60 "$PROTON" run "$backend" fetch-live-war "$cache"; then echo "Fetched the live galactic war."
else echo "Could not fetch the live galactic war; the session serves the last one fetched (or the built-in war)." >&2; fi
# (proton can return before a Windows program ends: the fetch finishes before the namespace is entered)
for _ in $(seq 1 30); do pgrep -f "LocalBackend.exe fetch-live-war" >/dev/null || break; sleep 1; done

"$here/hellforge-offline.sh" "$here/hf-inner.sh"
