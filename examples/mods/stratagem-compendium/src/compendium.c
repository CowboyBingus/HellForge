/* The Stratagem Compendium's entry point: every family registers its stratagems from hellforge_mod_init (compendium.h).
 * Build: HellForge builds the sources before each session (docs/modding/tools.md), or
 * scripts/Build-Mod.ps1 examples/mods/stratagem-compendium. */
#include "compendium.h"

static uint64_t g_packages[8];
static int g_packageCount;
static uint32_t g_selectionCategory;

uint32_t AddCompendiumStratagem(const HfApi* hf, HfMod* mod, HfStratagem* stratagem, int32_t order)
{
    stratagem->selection_category = g_selectionCategory;
    stratagem->selection_order = order;
    return hf->add_stratagem(mod, stratagem);
}

void HoldPackage(uint64_t package)
{
    for (int i = 0; i < g_packageCount; i++) if (g_packages[i] == package) return;
    if (g_packageCount < 8) g_packages[g_packageCount++] = package;
}

/* Once the entity data is in, the packages the stratagems need besides their own: HellForge holds them for the session
 * (on the game thread; HF_PACKAGE_LOADING until the engine has them). */
static void Hold(HfMod* mod, HfEvent event, void* user)
{
    for (int i = 0; i < g_packageCount; i++)
        if (!HF_API(mod)->load_package(mod, g_packages[i]))
            HF_API(mod)->log(mod, "package %016llx could not be loaded: %s", (unsigned long long)g_packages[i], HF_API(mod)->last_log(mod));
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 8 || hf->size < offsetof(HfApi, add_stratagem_category) + sizeof hf->add_stratagem_category) return 1;
    HfStratagemCategory category = {sizeof category,
        .name = "compendium", .title = "Stratagem Compendium"};
    g_selectionCategory = hf->add_stratagem_category(mod, &category);
    if (!g_selectionCategory) return 1;
    int refused = AddPelicans(hf, mod) + AddHmgSentry(hf, mod) + AddOrbitalBarrages(hf, mod) + AddExpendables(hf, mod) + AddShredderSilo(hf, mod);
    hf->on(mod, HF_EVENT_ENTITIES_READY, Hold, NULL);
    hf->log(mod, "Stratagem Compendium: %d stratagem(s) refused (0: all eleven added)", refused);
    return 0; /* the others still work when one is refused */
}
