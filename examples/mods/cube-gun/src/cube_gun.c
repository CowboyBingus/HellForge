/* Purple Metal Cube Gun: a new arsenal weapon with a model of the mod's own. It is a copy of the AR-23 Liberator (its
 * handling, magazine and ammunition) whose model, built by hellforge-data.json from each player's game copy, is the
 * Liberator's unit with the mesh of models/cube.obj in place of the rifle, and its body material's colour lookup turned
 * purple metal. Only what is drawn changes: hits, physics and the muzzle stay the Liberator's.
 * Build: HellForge builds the sources before each session, or scripts/Build-Mod.ps1 examples/mods/cube-gun. */
#include "hellforge_mod.h"

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 2) return 1;
    HfWeapon cube = {sizeof cube};
    cube.base = HF_ITEM_AR23_LIBERATOR;
    cube.unit = "skyeshade/cube_gun/cube_gun";
    cube.designation = "CB-1";
    cube.name = "Purple Metal Cube";
    cube.description = "A cube. Of purple metal. It fires like a Liberator, somehow.";
    uint32_t id = api->add_weapon(mod, &cube);
    api->log(mod, id ? "CB-1 Purple Metal Cube is loadout item %08x" : "the weapon was not added", id);
    return id ? 0 : 1;
}
