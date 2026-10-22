/* Machine Gun 10k (skyeshade.machine-gun-10k-code): the M-1000 Maxigun's backpack holds 10,000 rounds.
 *
 * The SDK's reference example: one short mod that shows the three things most mods do.
 *   1. A definition block (HfEditDef) changes an existing entity's fields by name. The loader applies it whenever the
 *      game loads its entity data, so it needs no event and no timing.
 *   2. An event (HF_EVENT_NETWORK_READY) runs code once the game is ready for it: here, widening a network field type.
 *   3. The mod's log (logs/sessions/<session>/mod-skyeshade.machine-gun-10k-code.log) says what happened.
 *
 * Why the network field: a backpack's live ammo is sent between machines as the network field type deposit_value, an
 * integer of 10 bits, so the engine clamps it to 1,023 at the first shot whatever the backpack's capacity says
 * (HD2Runtime research: local_research/events/maxigun/deposit-limit-notes.md). 14 bits hold up to 16,383. Lua mods
 * cannot change the network schema; a code mod can.
 *
 * Fields, components and constants: docs/sdk-reference.md. Every call: hellforge_mod.h. A walk-through:
 * docs/modding/first-mod.md; every guide (hooks among them): docs/modding/README.md.
 * Build: Build now in the mod's menu on the Mods page, HellForge.Cli build-mod --mod <this folder>, or (in the
 * repository) scripts/Build-Mod.ps1 examples/mods/machine-gun-10k-code. Sessions build it themselves from src/. */
#include "hellforge_mod.h"

/* 1. The backpack's DepositComponentData record. Each value is tagged with its field's type (HF_U32, HF_I32): a wrong
 * type or a value out of range is refused, with this file's line, in the mod's log. */
static const HfSet backpackSet[] = {
    {HF_FIELD(Deposit, capacity), HF_U32(10000)},     /* vanilla 1000: the most it holds */
    {HF_FIELD(Deposit, start_amount), HF_I32(10000)}, /* vanilla 1000: what it holds at call-in; the HUD's maximum */
    {HF_FIELD(Deposit, refill_amount), HF_U32(5000)}, /* vanilla 500: what a resupply adds */
    {0}
};
static const HfEditDef backpack = {.entity = HF_ENTITY_MAXIGUN_BACKPACK, .set = backpackSet};

/* 2. Network field types exist once the engine's network configuration has loaded. set_network_field_bits only widens
 * a type that is exactly what we expect (or already widened), so a game update that changes it is left alone. */
static void WidenAmmoField(HfMod* mod, HfEvent event, void* user)
{
    const HfApi* hf = HF_API(mod);
    HfFieldType vanilla = {.kind = 1, .bits = 10, .min = 0}; /* integer, 10 bits, from 0: up to 1,023 */
    if (hf->set_network_field_bits(mod, "deposit_value", vanilla, 14) == 0)
        hf->log(mod, "deposit_value holds up to 16383");
    else
        hf->log(mod, "deposit_value is not the 10-bit integer this mod expects; left alone, so the backpack stops at 1023");
}

/* 3. Once the entity data has loaded, read the record back (read only: a vanilla record is the game's data) and log it. */
static void BackpackReady(HfMod* mod, HfEvent event, void* user)
{
    const HfDeposit* deposit = hf_record(mod, HF_ENTITY_MAXIGUN_BACKPACK, HfDeposit);
    if (deposit != NULL)
        HF_API(mod)->log(mod, "Maxigun backpack: capacity %u, start %d, refill %u",
            deposit->capacity, deposit->start_amount, deposit->refill_amount);
}

/* Called once, while the game starts. Return 0 to stay loaded; anything else unloads the mod, and its log says why. */
HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6) return 1; /* edit_entity, hf_record and HF_API(mod) need SDK 6 */
    hf->log(mod, "Machine Gun 10k for game build %s", hf->build_id());
    hf->on(mod, HF_EVENT_NETWORK_READY, WidenAmmoField, NULL);
    hf->on(mod, HF_EVENT_ENTITIES_READY, BackpackReady, NULL);
    return hf->edit_entity(mod, &backpack); /* 0: queued for whenever the entity data loads */
}
