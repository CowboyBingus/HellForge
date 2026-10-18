/* Vanilla Plus: small fixes to how the game plays, each a change to the game's own data, made once.
 *
 * Stratagem bounce. A hellpod stratagem's beacon (the blue ones: support weapons, backpacks, sentries, mines, supply)
 * sticks only where the navmesh lies within 0.1 m of the hit; on rocks, props and other surfaces without navmesh it
 * bounces on and is steered back toward the navmesh. Its settings row says so in bit 1 of +0x170 (beacon_flags), which
 * the game's beacon code reads at its hit test and its steering and nowhere else; orbitals and Eagles have it clear and
 * stick anywhere. Clearing it makes a hellpod beacon stick like theirs. The other landing rules stay: the surface must
 * not be steeper than about 45 degrees, and a hellpod still may not land inside the areas the mission keeps hellpods out
 * of. Vehicles and the SEAF squad keep their navmesh rule (they need ground to drive or walk on).
 * (artifacts/difficulty11/research-beacons/NOTES.md in the research repository; mappings/components.json.)
 *
 * Reinforcements at the beacon. The host picks a reinforced player's drop point in game.dll 0xAC7280: the first
 * reinforcement beacon not used for that player yet (or, with none, the player's own body), then 0xAC6DB0 moves it 25
 * to 50 m away at a random angle (16 tries at a spot the game's spawn finder accepts), then a per-player spread of 2.5
 * to 12.5 m. So a pod lands far from the beacon thrown for it. Here a beacon's position goes on without that scatter;
 * the spread stays (several pods called at once do not land on each other), and so does everything without a beacon:
 * the first drop, a respawn at the body, the beacon the game throws by itself when everyone is down. Made once, when
 * game.dll is loaded: the call at 0xAC752A goes through a few bytes of code that skip the scatter for a beacon
 * (artifacts/vanillaplus-analysis/reinforce/NOTES.md in the research repository).
 *
 * Hellpod steering. While a player's own hellpod falls (a first drop or a reinforcement), game.dll 0x92B770 asks
 * 0x92A550 for a push away from the mission's keep-out areas (round or boxed footprints around some structures and
 * props, built per mission by op 0xB1; and, on city maps, toward a clearing): wherever the push is not zero it replaces
 * the stick input outright, so the pod cannot be steered over that ground. Here the stick input is always used, once,
 * when game.dll is loaded: the branch at 0x92E0D0 that takes the push is made to go past it. Nothing else reads those
 * areas differently: stratagem beacons and the spawn finder still keep out of them
 * (artifacts/vanillaplus-analysis/steering/NOTES.md in the research repository). */
#include <windows.h>
#include <string.h>
#include "hellforge_mod.h"

#define DELIVERY_HELLPOD 2u
#define NAVMESH_ONLY 0x2u
#define LAST_TYPE 255u /* the game's 149 and the ones mods add (150 and up) */

static const HfApi* hellforge;

static void StratagemBounce(HfMod* mod, HfEvent event, void* user)
{
    HfField delivery = HF_FIELD(StratagemSettings, delivery), flags = HF_FIELD(StratagemSettings, beacon_flags);
    unsigned changed = 0, kept = 0;
    for (uint32_t type = 1; type <= LAST_TYPE; type++)
    {
        HfLiveValue kind, beacon;
        if (!hellforge->get_live(mod, type, &delivery, &kind) || !hellforge->get_live(mod, type, &flags, &beacon)) continue;
        if ((uint32_t)kind.now.number != DELIVERY_HELLPOD) continue;
        uint32_t now = (uint32_t)beacon.now.number;
        if (!(now & NAVMESH_ONLY)) { kept++; continue; }
        HfValue value = HF_U8(now & ~NAVMESH_ONLY);
        if (hellforge->set_live(mod, type, &flags, &value, 0)) changed++;
        else hellforge->log(mod, "stratagem type %u: its beacon was left as it is (%s)", type, hellforge->last_log(mod));
    }
    hellforge->log(mod, "stratagem bounce: %u hellpod stratagems' beacons now stick like orbitals' (%u already did)", changed, kept);
}

/* game.dll 25480438: the drop point's call of the scatter, its argument set-up first (23 bytes), the beacon id that
 * means none, and where the code goes on. */
#define REINFORCE_SITE 0xAC7518u
#define REINFORCE_SITE_SIZE 23u
#define REINFORCE_BACK 0xAC752Fu
#define SCATTER 0xAC6DB0u
#define NO_BEACON 0x3484514u
static const char kReinforceSite[] = "4C 8D 44 24 60 49 8B CC F2 0F 11 44 24 60 48 8D 55 A8 E8 81 F8 FF FF F2 0F 10 30 8B";

static void Put64(uint8_t* at, uint64_t value) { for (int i = 0; i < 8; i++) at[i] = (uint8_t)(value >> (8 * i)); }

static void ReinforceAtBeacon(HfMod* mod, HfEvent event, void* user)
{
    size_t size = 0;
    uintptr_t game = hellforge->module_base("game.dll", &size);
    if (game == 0 || hellforge->find_pattern("game.dll", kReinforceSite) != game + REINFORCE_SITE)
    {
        hellforge->log(mod, "reinforcements: the drop point code is not the reviewed one (another game build?); left as it is");
        return;
    }
    uint8_t* stub = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (stub == NULL) { hellforge->log(mod, "reinforcements: no memory for the stub; left as it is"); return; }
    /* The site's own set-up, then: a beacon -> its position as it is; none -> the game's scatter, as before. */
    static const uint8_t code[] = {
        0x4C, 0x8D, 0x44, 0x24, 0x60,             /* lea r8, [rsp+0x60]   the position (x, y in xmm0, z in ecx) */
        0x49, 0x8B, 0xCC,                         /* mov rcx, r12 */
        0xF2, 0x0F, 0x11, 0x44, 0x24, 0x60,       /* movsd [rsp+0x60], xmm0 */
        0x48, 0x8D, 0x55, 0xA8,                   /* lea rdx, [rbp-0x58] */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,       /* mov rax, game+NO_BEACON           (imm at 20) */
        0x3B, 0x30,                               /* cmp esi, [rax]     esi: the beacon chosen (0xAC748C) */
        0x74, 0x11,                               /* je scatter */
        0x4C, 0x89, 0xC0,                         /* mov rax, r8        the beacon's position, unmoved */
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* jmp [rip]: game+REINFORCE_BACK     (imm at 41) */
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0,       /* scatter: mov rax, game+SCATTER     (imm at 51) */
        0xFF, 0xD0,                               /* call rax           (rsp as at the original call) */
        0xFF, 0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* jmp [rip]: game+REINFORCE_BACK     (imm at 67) */
    };
    memcpy(stub, code, sizeof code);
    Put64(stub + 20, game + NO_BEACON);
    Put64(stub + 41, game + REINFORCE_BACK);
    Put64(stub + 51, game + SCATTER);
    Put64(stub + 67, game + REINFORCE_BACK);
    DWORD old;
    VirtualProtect(stub, 128, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), stub, 128);
    uint8_t jump[REINFORCE_SITE_SIZE];
    memset(jump, 0x90, sizeof jump);
    jump[0] = 0xFF; jump[1] = 0x25; memset(jump + 2, 0, 4); /* jmp [rip]: the stub */
    Put64(jump + 6, (uint64_t)(uintptr_t)stub);
    if (hellforge->write(game + REINFORCE_SITE, jump, sizeof jump) != 0)
    {
        VirtualFree(stub, 0, MEM_RELEASE);
        hellforge->log(mod, "reinforcements: the drop point code could not be changed; left as it is");
        return;
    }
    hellforge->log(mod, "reinforcements: pods called with a beacon drop at the beacon (the game's 25-50 m scatter is skipped; its 2.5-12.5 m spread per player stays)");
}

/* game.dll 25480438: the pod's steering, from loading the push (0x92E0A1); at +0x2F `jp` (7A 0A), which with the
 * `jne` after it takes the push over the input, becomes `jmp 0x92E0E8` (EB 16), past it. */
#define STEERING_SITE 0x92E0A1u
#define STEERING_BRANCH 0x2Fu
static const char kSteeringSite[] = "49 8B 47 60 48 8B 55 F8 F3 0F 10 25 3F DF A9 01 F3 0F 10 4D 50 41 0F 2E CE F3 0F 10 14 D0 F3 0F 10 5C D0 04 "
                                    "0F 57 D4 F3 0F 10 45 54 0F 57 DC 7A 0A 75 08 41 0F 2E C6 7A 02 74 0C";

static void SteerAnywhere(HfMod* mod, HfEvent event, void* user)
{
    uintptr_t game = hellforge->module_base("game.dll", NULL);
    if (game == 0 || hellforge->find_pattern("game.dll", kSteeringSite) != game + STEERING_SITE)
    {
        hellforge->log(mod, "hellpod steering: the steering code is not the reviewed one (another game build?); left as it is");
        return;
    }
    static const uint8_t jump[] = {0xEB, 0x16};
    if (hellforge->write(game + STEERING_SITE + STEERING_BRANCH, jump, sizeof jump) != 0)
        hellforge->log(mod, "hellpod steering: the steering code could not be changed; left as it is");
    else
        hellforge->log(mod, "hellpod steering: your hellpod follows your steering over any ground (the keep-out push is skipped)");
}

/* Once game.dll's code is decoded: the two code changes. */
static void GameLoaded(HfMod* mod, HfEvent event, void* user)
{
    SteerAnywhere(mod, event, user);
    ReinforceAtBeacon(mod, event, user);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 8) return 1;
    hellforge = api;
    hellforge->on(mod, HF_EVENT_GAME_LOADED, GameLoaded, 0);
    /* The stratagem settings are loaded before the entity data; the live edits stay for the session and are made again
     * if the game reloads its data. */
    hellforge->on(mod, HF_EVENT_ENTITIES_READY, StratagemBounce, 0);
    return 0;
}
