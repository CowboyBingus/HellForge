/* More Voices: the game's Wwise setup plays at most 140 sounds at once (Init bank, artifacts/audio-voices/NOTES.md),
 * and in big fights the lowest priority ones, among them the player's own footsteps (priority 55, killed at the limit),
 * are cut first. Wwise's AK::SoundEngine::SetMaxNumVoicesLimit, exported by the game's Wwise plugin and never called by
 * the game, overrides the bank's limit: this mod sets it to 256 once the sound engine is up. */
#include <windows.h>
#include <stdbool.h>
#include "hellforge_mod.h"

#define VOICES 256

typedef int (*SetMaxNumVoicesLimitFn)(unsigned short voices); /* AKRESULT; AK_Success is 1 */
typedef bool (*IsInitializedFn)(void);

static const HfApi* hellforge;

static void MoreVoices(HfMod* mod, HfEvent event, void* user)
{
    HMODULE wwise = GetModuleHandleW(L"wwise_pluginw64_release.dll");
    SetMaxNumVoicesLimitFn setLimit = wwise ? (SetMaxNumVoicesLimitFn)(void*)GetProcAddress(wwise, "?SetMaxNumVoicesLimit@SoundEngine@AK@@YA?AW4AKRESULT@@G@Z") : NULL;
    IsInitializedFn initialized = wwise ? (IsInitializedFn)(void*)GetProcAddress(wwise, "?IsInitialized@SoundEngine@AK@@YA_NXZ") : NULL;
    if (setLimit == NULL || initialized == NULL)
    {
        hellforge->log(mod, "the game's Wwise plugin or its voice limit was not found; nothing changed");
        return;
    }
    if (!initialized())
    {
        hellforge->log(mod, "the sound engine is not initialised yet; nothing changed");
        return;
    }
    int result = setLimit(VOICES);
    hellforge->log(mod, result == 1 ? "sounds playing at once: up to %d (the game's limit is 140)" : "the voice limit could not be raised (AKRESULT %d)",
        result == 1 ? VOICES : result);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 1) return 1;
    hellforge = api;
    hellforge->on(mod, HF_EVENT_ENTITIES_READY, MoreVoices, 0);
    return 0;
}
