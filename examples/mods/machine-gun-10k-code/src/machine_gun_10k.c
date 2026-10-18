/* Machine Gun 10k, as a HellForge code mod: the M-1000 Maxigun's backpack holds 10,000 rounds (SDK 6).
 *
 * A backpack's live ammo is replicated as the network field type deposit_value, an integer of 10 bits, so the engine
 * clamps it to 1,023 at the first shot (HD2Runtime research: local_research/events/maxigun/deposit-limit-notes.md). Lua
 * cannot change the engine's network schema; this mod widens the type to 14 bits (up to 16,383) once the network
 * configuration is loaded. The backpack's own DepositComponentData record gets its new amounts as a definition block
 * (HfEditDef), which the loader applies whenever the game loads its entity data.
 * Build: scripts/Build-Mod.ps1 examples/mods/machine-gun-10k-code */
#include "hellforge_mod.h"

static const HfSet backpackSet[] = {
    {HF_FIELD(Deposit, capacity), HF_U32(10000)},    /* vanilla 1000 */
    {HF_FIELD(Deposit, start_amount), HF_I32(10000)}, /* vanilla 1000 */
    {HF_FIELD(Deposit, refill_amount), HF_U32(5000)}, /* vanilla 500 */
    {0}
};
static const HfEditDef backpack = {.entity = HF_ENTITY_MAXIGUN_BACKPACK, .set = backpackSet};

static void WidenAmmoField(HfMod* mod, HfEvent event, void* user)
{
    HfFieldType vanilla = {1, 10, 0}; /* integer, 10 bits, from 0 */
    if (HF_API(mod)->set_network_field_bits(mod, "deposit_value", vanilla, 14) == 0)
        HF_API(mod)->log(mod, "deposit_value now holds up to 16383");
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6) return 1;
    hf->log(mod, "Machine Gun 10k for game build %s", hf->build_id());
    hf->on(mod, HF_EVENT_NETWORK_READY, WidenAmmoField, 0);
    return hf->edit_entity(mod, &backpack);
}
