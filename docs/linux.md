# HellForge on Linux (Proton)

Experimental. Linux support is the work of **DreamyBranch** and **joshua-software-dev**, who worked it out and shared
it in [issue 1](https://github.com/SkyeShade/HellForge/issues/1). Thank you both. The scripts live in `linux/` (in an
installation: `app/linux/`).

## How it keeps the game offline

On Windows the copied game is kept off the internet by Windows filtering rules. Wine has none, so on Linux HellForge
runs inside a **network namespace**: a private network with no route out. `linux/hellforge-offline.sh` makes it
without root:

- loopback, for the local backend and the bridge to Steam;
- a virtual network adapter (`veth`), so Wine and the game see an ordinary network;
- "prohibit" default routes for IPv4 and IPv6, so every outside connection is refused at once: the same refusal the
  Windows block gives, which the session's network probe checks before every session;
- a self-test that refuses to start if a connection to 1.1.1.1 gets through.

The launcher, the session runner, the local backend and the game all run inside it. The launcher checks the isolation
at setup and again before each session (`src/HellForge.Launcher/LinuxNetworkBlock.cs`), and the session runner refuses to
start a Linux installation outside the namespace.

The game still needs the Steam client, which runs outside with its normal network. `linux/hf-bridge-host.sh` exposes
Steam's local ports as files (unix sockets, which cross into the namespace), and `linux/hf-inner.sh` turns them back
into the same local ports inside. Nothing else gets out. The bridges use `nodelay`: without it the game's many small
calls to Steam were held back, and the frame rate dropped badly.

The galactic war is fetched just before the namespace is entered (`HellForge.LocalBackend.exe fetch-live-war`), and the
session serves that copy: with "Follow the live galactic war" the war is the one from launch time, and moves on at the
next launch.

## Setting it up

You need: Steam for Linux with HELLDIVERS 2 installed and run once through Proton, `iproute2` (`ip`), `socat`, and
`unshare` (util-linux).

1. Give HellForge a Wine prefix of its own: in Steam, add `HellForge.exe` as a non-Steam game, set it to use Proton,
   and start it once (it creates `steamapps/compatdata/<number>`). Sharing HELLDIVERS 2's prefix works too, but then
   nothing else may be running in that prefix while HellForge starts.
2. Install HellForge on Windows (HellForge-Setup.exe) and copy the installation folder to the Linux PC, for example
   into that prefix (`.../compatdata/<number>/pfx/drive_c/HellForge`).
3. Make the scripts runnable: `chmod +x HellForge/app/linux/*.sh`.
   On Ubuntu 24.04 and later (and other systems where `/proc/sys/kernel/apparmor_restrict_unprivileged_userns` is 1),
   only allowed programs may make the namespace. Run once: `sudo HellForge/app/linux/allow-namespace.sh`. It installs
   HellForge's own copy of `unshare` and an AppArmor profile that allows that copy alone, as Ubuntu does for Steam;
   `--remove` undoes it. `hellforge-offline.sh` says so when it is needed.
4. Start Steam and sign in, then run `HellForge/app/linux/hellforge-linux.sh` (with `STEAM_COMPAT_DATA_PATH` set to
   the prefix from step 1). It starts the bridge to Steam itself and stops it when HellForge closes; a bridge already
   running (`hf-bridge-host.sh`) is used as it is.

`hellforge-linux.sh` reads these settings from the environment:

| Variable | Default |
|---|---|
| `PROTON` | the newest Proton in the Steam client (`steamapps/common/Proton*` or `compatibilitytools.d/*`) |
| `INSTALL` | the installation the script is in |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | `~/.local/share/Steam` |
| `STEAM_COMPAT_DATA_PATH` | HELLDIVERS 2's prefix, `steamapps/compatdata/553850` |

For example: `PROTON=~/.local/share/Steam/compatibilitytools.d/<name>/proton HellForge/app/linux/hellforge-linux.sh`.

## One prefix, one network

Wine opens a Windows program's sockets in the prefix's `wineserver`, so a program gets the wineserver's network, not
its own. A wineserver of HellForge's prefix still running outside the namespace would give the programs started inside
the internet. `hellforge-offline.sh` therefore waits up to 20 s for one to end (it does a few seconds after its last
program) and refuses to start while one runs. A prefix of HellForge's own avoids the wait.

## Tested so far

In an Ubuntu 26.04 VM with plain Wine 10 (no Steam, no GPU; 2026-10-21): the namespace as a normal user, with
`allow-namespace.sh` where AppArmor restricts it; Windows programs inside refused with WSAEACCES; the war fetched
outside and served inside without a request to the mirror; the session runner's network probe; the refusal while a
wineserver of the prefix runs outside; the Steam bridge (against a stand-in for Steam), started and stopped by
`hellforge-linux.sh`. The game itself, Proton and the Steam client: by the testers in issue 1.

## Not known yet

- Co-op on Linux has not been tried. Friends' games are reached through Steam's networking, which runs in the Steam
  client outside the namespace, so it may work as it is.
- Setup runs on Windows only; a Linux setup is to come.
- The session runner's Steam ownership check runs a PowerShell script; how it ran in the testers' prefixes is to be
  confirmed.
