/* The game's own menu sounds on the editor's buttons, the ones the HD2R Editor plays: posted through the game's sound
 * engine the way the loadout screen posts its own (game.dll's UI sound function, build 25480438), so they follow the
 * game's volume. Requests come from any thread; Flush posts them on the game's main (window) thread. */
#pragma once
#include "hellforge_mod.h"

namespace sounds
{
/* SHOW and HIDE: the editor itself opening and closing */
enum Kind { CLICK, OPEN, BACK, APPLY, ERROR_, SHOW, HIDE, KIND_COUNT };

/* Finds the game's UI sound function and checks it is the reviewed one; false (silent) otherwise. Call once game.dll
 * runs (its code is unpacked when it starts, after mods load): HF_EVENT_ENTITIES_READY. */
bool Init(const HfApi* api, HfMod* mod);
void Request(Kind kind);
bool Waiting();
/* The editor opened or closed: the game's menu effect goes up or down with it (on the window's thread, at Flush). */
void EditorOpen(bool open);
/* Posts the requested sounds; true when any were waiting. The game thread calls it every frame (Init hooks the menu
 * sound manager's update); OnGameThread is false when that hook is not there, and the window's thread calls it then. */
bool Flush();
bool OnGameThread();
void SetEnabled(bool on);
/* Logs what happened since the last report (the editor opening or closing). */
void Report(const char* when);
} // namespace sounds
