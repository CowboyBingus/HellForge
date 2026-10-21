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

The launcher, the session runner, the local backend and the game all run inside it. Under Wine, the launcher checks the
isolation before each session (`src/HellForge.Launcher/LinuxNetworkBlock.cs`), and both it and the session runner refuse
to start a session outside the namespace (an installation copied from Windows included).

The game still needs the Steam client, which runs outside with its normal network. `linux/hf-bridge-host.sh` exposes
Steam's local ports as files (unix sockets, which cross into the namespace), and `linux/hf-inner.sh` turns them back
into the same local ports inside. The bridges use `nodelay`: without it the game's many small calls to Steam were held
back, and the frame rate dropped badly.

It is a network block, not a sandbox: what the namespace holds back is the game's own connections. Programs it asks to
act for it outside still can (the Steam client through the bridge, the desktop session's services through their
sockets, and, on systems that resolve names through systemd-resolved, name lookups). The Windows block is the same kind
of block.

The galactic war is fetched just before the namespace is entered (`HellForge.LocalBackend.exe fetch-live-war`), and the
session serves that copy: with "Follow the live galactic war" the war is the one from launch time, and moves on at the
next launch.

## Setting it up

You need: Steam for Linux with HELLDIVERS 2 installed and run once through Proton, `iproute2` (`ip`), `socat`, and
`unshare` (util-linux).

1. Install HellForge on Windows (HellForge-Setup.exe) and copy the installation folder to the Linux PC.
2. Give HellForge a Wine prefix of its own: in Steam, add the copied `HellForge.exe` as a non-Steam game, set it to use
   Proton, and start it once so Steam creates `steamapps/compatdata/<number>`; then close it. Start HellForge only
   with the script from then on: started from Steam it runs outside the namespace, and refuses to start a session.
   Sharing HELLDIVERS 2's prefix (553850) works too, but then nothing else may run in that prefix while HellForge
   runs: a Windows program of the prefix running outside would share its network with HellForge's (and the real game,
   started while HellForge runs, would have no network).
3. Make the scripts runnable: `chmod +x HellForge/app/linux/*.sh`.
   On Ubuntu 24.04 and later (and other systems where `/proc/sys/kernel/apparmor_restrict_unprivileged_userns` is 1),
   only allowed programs may make the namespace. Run once, from your own account:
   `sudo HellForge/app/linux/allow-namespace.sh`. It installs a copy of `unshare` that only you (and root) can run, and
   an AppArmor profile that lets that copy make namespaces, as Ubuntu does for Steam. For you this lifts Ubuntu's
   restriction (for any namespace, not only HellForge's); other accounts keep it. `--remove` undoes it; run it again
   after a util-linux update. `hellforge-offline.sh` says when it is needed.
4. Start Steam and sign in, then run `HellForge/app/linux/hellforge-linux.sh` with `STEAM_COMPAT_DATA_PATH` set to the
   prefix from step 2 and `PROTON` to the Proton you want. It starts the bridge to Steam itself and stops it when
   HellForge closes; a bridge already running (`hf-bridge-host.sh`) is used as it is.

`hellforge-linux.sh` reads these settings from the environment:

| Variable | Default |
|---|---|
| `PROTON` | a Proton the Steam client has (`steamapps/common/Proton*` or `compatibilitytools.d/*`, last by version sort); better set it |
| `INSTALL` | the installation the script is in |
| `STEAM_COMPAT_CLIENT_INSTALL_PATH` | `~/.local/share/Steam` |
| `STEAM_COMPAT_DATA_PATH` | HELLDIVERS 2's prefix, `steamapps/compatdata/553850` |

For example: `PROTON=~/.local/share/Steam/compatibilitytools.d/<name>/proton HellForge/app/linux/hellforge-linux.sh`.

## One prefix, one network

Wine opens a Windows program's sockets in the prefix's `wineserver`, so a program gets the wineserver's network, not
its own. A wineserver of HellForge's prefix still running outside the namespace would give the programs started inside
the internet. `hellforge-offline.sh` therefore waits up to 20 s for one to end (it does a few seconds after its last
program) and refuses to start while one runs; should one start in the moment between that check and the namespace, the
session's network probe (in the session runner, before the game starts) refuses the session. A prefix of HellForge's
own avoids all of this.

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
- Steam as a Flatpak or Snap (its own `/tmp` and paths), and the Steam Deck (no `socat` by default), are untested.
- The session runner's Steam ownership check runs a PowerShell script; how it ran in the testers' prefixes is to be
  confirmed.
