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

1. Install HellForge on Windows (HellForge-Setup.exe) and copy the installation folder to the Linux PC, for example
   into the game's Wine prefix (`.../steamapps/compatdata/553850/pfx/drive_c/HellForge`).
2. Make the scripts runnable: `chmod +x HellForge/app/linux/*.sh`.
3. Start Steam and sign in. In a terminal, run `HellForge/app/linux/hf-bridge-host.sh` and leave it open.
4. In another terminal, run `HellForge/app/linux/hellforge-linux.sh`.

`hellforge-linux.sh` reads these settings from the environment:

| Variable | Default |
|---|---|
| `PROTON` | the newest Proton in the Steam client (`steamapps/common/Proton*` or `compatibilitytools.d/*`) |
| `INSTALL` | the installation the script is in |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | `~/.local/share/Steam` |
| `STEAM_COMPAT_DATA_PATH` | HELLDIVERS 2's prefix, `steamapps/compatdata/553850` |

For example: `PROTON=~/.local/share/Steam/compatibilitytools.d/<name>/proton HellForge/app/linux/hellforge-linux.sh`.

## Not known yet

- Co-op on Linux has not been tried. Friends' games are reached through Steam's networking, which runs in the Steam
  client outside the namespace, so it may work as it is.
- Setup runs on Windows only; a Linux setup is to come.
- The session runner's Steam ownership check runs a PowerShell script; how it ran in the testers' prefixes is to be
  confirmed.
