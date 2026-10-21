#!/usr/bin/env bash
# Runs a command (hf-inner.sh: HellForge's launcher under Proton) inside a network namespace with no route out, without
# root: loopback for the local backend and Steam's bridge, a veth adapter so Wine sees a network, and "prohibit" default
# routes so every outside connection is refused at once (WSAEACCES, as the Windows block's probe expects). The launcher,
# the session runner, the backend and the game all inherit it; a self-test refuses to start if 1.1.1.1 is reachable.
# From DreamyBranch's LinuxSupport (SkyeShade/HellForge issue 1), with joshua-software-dev; docs/linux.md.
set -euo pipefail
[ $# -gt 0 ] || { echo "usage: $0 command [args...]" >&2; exit 2; }

selftest='
if timeout 3 bash -c "exec 3<>/dev/tcp/1.1.1.1/443" 2>/dev/null; then
  echo "hellforge-offline: outside connection SUCCEEDED, refusing to start" >&2; exit 1
fi
exec "$@"'

command -v ip >/dev/null || { echo "need iproute2 (ip)" >&2; exit 1; }
uid=$(id -u); gid=$(id -g)

exec unshare --user --map-root-user --net bash -c '
  # 1. Bring up loopback for local backend and Steam socat bridge
  ip link set lo up

  # 2. Create a virtual network adapter so Wine/the game sees a valid network interface
  ip link add eth0 type veth peer name veth0
  ip addr add 192.168.1.50/24 dev eth0
  ip link set eth0 up
  ip link set veth0 up

  # 3. Block all WAN traffic with prohibit (returns 10013 WSAEACCES for NetworkProbe)
  ip route add prohibit default
  ip -6 route add prohibit default 2>/dev/null || true

  # 4. Drop back to your normal user account
  exec unshare --user --map-user="$0" --map-group="$1" \
    env HELLFORGE_LINUX_NETNS=1 bash -c "$2" _ "${@:3}"
' "$uid" "$gid" "$selftest" "$@"
