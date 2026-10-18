#include "hellforge_mod.h"
#include "hellforge_content.h"

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 8 || hf->size < offsetof(HfApi, add_stratagem_category) + sizeof hf->add_stratagem_category) return 1;
    HfStratagemCategory category = {sizeof category, .name = "experimental", .title = "Experimental", .sort_order = 10};
    uint32_t experimental = hf->add_stratagem_category(mod, &category);
    category.name = "fire_support"; category.title = "Fire Support"; category.sort_order = 20;
    uint32_t fireSupport = hf->add_stratagem_category(mod, &category);
    if (!experimental || !fireSupport) return 1;

    /* A blue support weapon and a green defensive sentry share one menu heading without changing their colours.
     * selection_order puts the sentry first although it is added second (equal orders keep call order). */
    HfStratagem mg = {sizeof mg, .base = HF_STRATAGEM_MG43_MACHINE_GUN, .name = "experimental_mg",
        .title = "Experimental Machine Gun", .selection_category = experimental, .selection_order = 2};
    HfStratagem sentry = {sizeof sentry, .base = HF_STRATAGEM_AG16_GATLING_SENTRY, .name = "experimental_sentry",
        .title = "Experimental Gatling Sentry", .selection_category = experimental, .selection_order = 1};
    HfStratagem orbital = {sizeof orbital, .base = HF_STRATAGEM_ORBITAL_PRECISION_STRIKE, .name = "precision_support",
        .title = "Precision Fire Support", .selection_category = fireSupport};
    if (!hf->add_stratagem(mod, &mg) || !hf->add_stratagem(mod, &sentry) || !hf->add_stratagem(mod, &orbital)) return 1;
    return 0;
}
