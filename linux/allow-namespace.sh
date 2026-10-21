#!/usr/bin/env bash
# Run once with sudo on systems that only let allowed programs make user namespaces (Ubuntu 24.04 and later, and others
# with kernel.apparmor_restrict_unprivileged_userns = 1): hellforge-offline.sh cannot make its network namespace there.
# This installs HellForge's own copy of unshare (/usr/local/lib/hellforge/unshare) and an AppArmor profile that lets that
# copy, and nothing else, make one: what Ubuntu itself does for Steam (/etc/apparmor.d/steam) and lxc-unshare. The copy
# can be run only by root and the user who ran this (its group); for them it lifts the restriction (any namespace, not
# only HellForge's), for everyone else the system keeps it. hellforge-offline.sh uses the copy when it is there. The copy
# does not follow util-linux updates: run this again after one.
#   sudo ./allow-namespace.sh            install (again after a system update of util-linux, to refresh the copy)
#   sudo ./allow-namespace.sh --remove   undo
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run it with sudo: sudo $0 ${*:-}" >&2; exit 1; }
copy=/usr/local/lib/hellforge/unshare
profile=/etc/apparmor.d/hellforge-unshare

if [ "${1:-}" = "--remove" ]; then
  [ -f "$profile" ] && apparmor_parser -R "$profile" 2>/dev/null || true
  rm -f "$profile" "$copy"
  rmdir /usr/local/lib/hellforge 2>/dev/null || true
  echo "Removed HellForge's namespace permission."
  exit 0
fi

command -v apparmor_parser >/dev/null || { echo "AppArmor is not installed here; hellforge-offline.sh should work without this." >&2; exit 1; }
user="${SUDO_USER:-}"
[ -n "$user" ] && [ "$user" != root ] || { echo "run it with sudo from the account that plays HellForge" >&2; exit 1; }
install -d -m 755 /usr/local/lib/hellforge
install -m 750 -o root -g "$(id -gn "$user")" "$(command -v unshare)" "$copy"
cat > "$profile" <<EOF
# HellForge (docs/linux.md): its copy of unshare may make the user namespace its network namespace needs.
abi <abi/4.0>,
include <tunables/global>

profile hellforge-unshare $copy flags=(unconfined) {
  userns,

  include if exists <local/hellforge-unshare>
}
EOF
apparmor_parser -r "$profile"
echo "HellForge may now make its network namespace ($copy, for $user)."
