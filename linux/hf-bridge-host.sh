#!/usr/bin/env bash
# From DreamyBranch's LinuxSupport (SkyeShade/HellForge issue 1), with joshua-software-dev; docs/linux.md.
# Run this OUTSIDE the offline wrapper, after Steam is running and online. Leave it running.
# It exposes each loopback TCP port that the Steam client listens on as a unix socket in
# $XDG_RUNTIME_DIR. Unix sockets are files, so they cross the network namespace; hf-inner.sh
# then recreates the same loopback ports inside the namespace. No route to the internet is added.
set -euo pipefail
command -v socat >/dev/null || { echo "need socat (install it with your package manager)" >&2; exit 1; }
dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

# Loopback TCP listeners owned by the process named "steam".
ports=$(ss -Htlnp 2>/dev/null | awk '/users:\(\("steam",/ && $4 ~ /^127\.0\.0\.1:/ {sub(/.*:/,"",$4); print $4}' | sort -un)
[ -n "$ports" ] || { echo "no loopback ports found for the steam process; is Steam running?" >&2; exit 1; }

rm -f "$dir"/hf-steam-*.sock
pids=()
cleanup() { kill "${pids[@]}" 2>/dev/null || true; rm -f "$dir"/hf-steam-*.sock; }
trap cleanup EXIT INT TERM

for p in $ports; do
  socat UNIX-LISTEN:"$dir/hf-steam-$p.sock",fork,mode=600 TCP:127.0.0.1:"$p",nodelay &
  pids+=($!)
  echo "bridging Steam loopback port $p"
done
echo "Bridge running. Leave this open; Ctrl-C stops it."
wait
