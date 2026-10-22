/* {{name}} ({{id}}): a HellForge code mod, made from HellForge's mod template.
 *
 * It starts with one change you can see in game: the AR-23 Liberator fires at 1200 rounds per minute with no recoil.
 * Replace it with your own. The first-mod guide (docs/modding/first-mod.md) walks through it; the SDK reference
 * (docs/sdk-reference.md) lists every component, field and constant; hellforge_mod.h documents every call.
 *
 *   - Fields have names: HF_FIELD(Component, field), from the game's records (hellforge_components.h). A misspelt name
 *     does not compile, and your IDE completes them.
 *   - Vanilla content has constants: HF_ENTITY_..., HF_ITEM_..., HF_STRATAGEM_... (hellforge_content.h).
 *   - Definition blocks say what changes; the loader applies them whenever the game loads its data, and refuses (in the
 *     mod's log, with the line) anything it cannot check.
 * Build: Build now in the mod's menu on the Mods page, or HellForge.Cli build-mod --mod <this folder>. */
#include "hellforge_mod.h"

static const HfSet liberatorSet[] = {
    {HF_FIELD_AT(ProjectileWeapon, fire_rate, 1), HF_F32(1200)}, /* the rate slot the weapon fires at */
    HF_SET_GROUP(WeaponData, recoil, HF_F32(0)),                  /* both recoil blocks, sideways and up */
    {0}
};
static const HfEditDef liberator = {.entity = HF_ENTITY_AR23_LIBERATOR, .set = liberatorSet};

/* Events are for logic, not for timing edits: here, a line in the log once the game's data has loaded. */
static void Ready(HfMod* mod, HfEvent event, void* user)
{
    const HfProjectileWeapon* weapon = hf_record(mod, HF_ENTITY_AR23_LIBERATOR, HfProjectileWeapon);
    if (weapon != NULL) HF_API(mod)->log(mod, "AR-23 Liberator: %.0f rounds per minute", (double)weapon->fire_rate[1]);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6) return 1; /* needs HellForge's mod SDK 6 */
    hf->on(mod, HF_EVENT_ENTITIES_READY, Ready, NULL);
    /* 0: loaded. Otherwise the session's log folder has mod-{{id}}.log saying why. */
    return hf->edit_entity(mod, &liberator);
}
