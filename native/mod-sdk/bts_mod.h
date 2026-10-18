/* The mod SDK under its names from before the rename (HD2 Beyond The Stars): mods written against bts_mod.h still
 * build unchanged. New mods include hellforge_mod.h (docs/mods.md). The loader also accepts a DLL exporting
 * bts_mod_init, so mods built before the rename keep working without a rebuild. */
#ifndef BTS_MOD_H
#define BTS_MOD_H

#include "hellforge_mod.h"

#define BTS_MOD_SDK_VERSION HF_MOD_SDK_VERSION
#define BTS_MOD_EXPORT HF_MOD_EXPORT
#define BTS_EVENT_GAME_LOADED HF_EVENT_GAME_LOADED
#define BTS_EVENT_NETWORK_READY HF_EVENT_NETWORK_READY
#define BTS_EVENT_ENTITIES_READY HF_EVENT_ENTITIES_READY
#define BTS_ITEM_AR23_LIBERATOR HF_ITEM_AR23_LIBERATOR

typedef HfMod BtsMod;
typedef HfEvent BtsEvent;
typedef HfEventCallback BtsEventCallback;
typedef HfWeapon BtsWeapon;
typedef HfFieldType BtsFieldType;
typedef HfApi BtsApi;

#ifndef HF_MOD_LOADER
#ifdef __cplusplus
extern "C" {
#endif
HF_MOD_EXPORT int bts_mod_init(const HfApi* api, HfMod* mod);
#ifdef __cplusplus
}
#endif
#endif

#endif
