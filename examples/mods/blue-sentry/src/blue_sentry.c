/* Blue Metal Sentry: a new sentry stratagem whose sentry has a model of its own, the A/MG-43 Machine Gun Sentry's in
 * blue metal. A test of HfEntityDef.unit: the model is a unit resource hellforge-data.json builds from each player's
 * game copy (the MG-43's unit, its two painted materials copied, their colour lookup textures turned blue), so only the
 * new sentry is blue. Everything else is the MG-43's.
 * Build: HellForge builds the sources before each session, or scripts/Build-Mod.ps1 examples/mods/blue-sentry. */
#include "hellforge_mod.h"

#define SUPPORT_HELLPOD 0x73F8498BFFDCF415ull /* the hellpod support weapons and sentries come in (payload[1]) */

static const HfEntityDef sentryDef = {
    .size = sizeof(HfEntityDef),
    .name = "skyeshade/blue_sentry/sentry_entity",
    .base = HF_ENTITY_AMG43_MACHINE_GUN_SENTRY,
    .unit = "skyeshade/blue_sentry/sentry", /* the blue model hellforge-data.json builds */
    .title = "Blue Metal Sentry",
    .designation = "A/MG-43B",
};

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 7) return 1;
    HfRef sentry = hf->register_entity(mod, &sentryDef);
    if (!sentry) return 1;
    static uint64_t payload[2];
    payload[0] = sentry;
    payload[1] = SUPPORT_HELLPOD;
    HfStratagem stratagem = {sizeof stratagem};
    stratagem.base = HF_STRATAGEM_AMG43_MACHINE_GUN_SENTRY;
    stratagem.name = "skyeshade/blue_sentry/stratagem";
    stratagem.title = "Blue Metal Sentry"; /* shown after its sentry's designation */
    stratagem.code = "DUDURR";
    stratagem.payload = payload;
    stratagem.payload_count = 2;
    uint32_t type = hf->add_stratagem(mod, &stratagem);
    hf->log(mod, "Blue Metal Sentry: entity %016llx, stratagem type %u (0: not added)", (unsigned long long)sentry, type);
    return type ? 0 : 1;
}
