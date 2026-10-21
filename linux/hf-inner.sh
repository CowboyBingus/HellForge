#!/usr/bin/env bash
# From DreamyBranch's LinuxSupport (SkyeShade/HellForge issue 1), with joshua-software-dev; docs/linux.md.
# Run INSIDE the offline wrapper (hellforge-linux.sh does: hellforge-offline.sh hf-inner.sh).
# For every socket made by hf-bridge-host.sh it opens the same loopback TCP port inside the
# namespace and forwards it to Steam. Then it starts HellForge with Proton.
# Needs PROTON, INSTALL, STEAM_COMPAT_DATA_PATH and STEAM_COMPAT_CLIENT_INSTALL_PATH in the environment.
set -euo pipefail
command -v socat >/dev/null || { echo "need socat" >&2; exit 1; }
: "${PROTON:?PROTON is not set}" "${INSTALL:?INSTALL is not set}"
dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

n=0
for f in "$dir"/hf-steam-*.sock; do
  [ -S "$f" ] || continue
  p=${f##*hf-steam-}; p=${p%.sock}
  socat TCP-LISTEN:"$p",bind=127.0.0.1,fork,reuseaddr,nodelay UNIX-CONNECT:"$f" &
  echo "forwarding 127.0.0.1:$p -> Steam"
  n=$((n+1))
done
[ "$n" -gt 0 ] || { echo "no bridge sockets found; start hf-bridge-host.sh first" >&2; exit 1; }
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT
sleep 1
"$PROTON" run "$INSTALL/HellForge.exe" || true

# "proton run" returns as soon as the launcher has started, but the launcher keeps running.
# Keep the forwarders alive until HellForge's own processes (launcher, session, backend, game) are gone.
pat='HellForge\.(Launcher|Session)|LocalBackend|helldivers2'
for _ in $(seq 1 30); do pgrep -u "$(id -u)" -f "$pat" >/dev/null && break; sleep 1; done
while pgrep -u "$(id -u)" -f "$pat" >/dev/null; do sleep 2; done
