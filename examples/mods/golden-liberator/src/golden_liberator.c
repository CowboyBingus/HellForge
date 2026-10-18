/* AR-23 Liberator Gold: a new arsenal weapon, added with the HellForge weapon registry (SDK 2).
 *
 * It is a copy of the AR-23 Liberator - same handling, magazine and ammunition - with its own loadout id, name and
 * model: the unit resource below, which this mod's data/ folder provides (the Liberator's unit naming a gold copy of
 * its body material). HellForge adds the weapon to the game's entity data and armory and gives it to the player.
 * Build: scripts/Build-Mod.ps1 examples/mods/golden-liberator */
#include "hellforge_mod.h"

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 2) return 1;
    HfWeapon gold = {sizeof gold};
    gold.base = HF_ITEM_AR23_LIBERATOR;
    gold.unit = "skyeshade/golden_liberator/assault_rifle_gold";
    gold.designation = "AR-23G";
    gold.name = "Liberator Gold";
    gold.description = "The SEAF standard assault rifle, plated in gold for those who serve Liberty with distinction. Performs "
        "exactly like the Liberator; looks considerably more democratic.";
    uint32_t id = api->add_weapon(mod, &gold);
    api->log(mod, id ? "AR-23G Liberator Gold is loadout item %08x" : "the weapon was not added", id);
    return id ? 0 : 1;
}
