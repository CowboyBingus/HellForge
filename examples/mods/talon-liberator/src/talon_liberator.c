/* AR-23T Liberator Talon: a new arsenal weapon (SDK 2) that fires a new projectile type with a new damage type (SDK 3).
 *
 * All of it is game data, read the way the game reads its own weapons:
 *   - damage: a copy of the LAS-58 Talon bolt's DamageInfo (200 standard / 20 durable, armour penetration 3) under an
 *     id of its own, with five times the damage;
 *   - projectile: a copy of the Talon's bolt (1,300 m/s) under a type of its own, dealing that damage, drawn by a blue
 *     copy of the Talon bolt's effect (data/: the effect with its colour operator blue, and the materials and textures
 *     it uses, in the rifle's archive patch so they load with it);
 *   - sound: the LAS-16 Sickle's firing sounds (data/ also carries the Sickle's two sound banks);
 *   - weapon: a copy of the AR-23 Liberator with a blue-metal model (data/, built from your game copy by
 *     artifacts/registry/build_talon_liberator.py for now) that fires that projectile: its own projectile record names
 *     it and it has no default ammunition to set another.
 * The vanilla Liberator and Talon are unchanged. Build: open this folder as a CMake project, or scripts/Build-Mod.ps1. */
#include "hellforge_mod.h"

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* api, HfMod* mod)
{
    if (api->version < 3) return 1;

    HfDamage heavy = {sizeof heavy};
    heavy.base = HF_DAMAGE_LAS58_TALON;
    heavy.name = "bolt_damage"; /* SDK 6: short names are the mod's ("skyeshade.talon-liberator:bolt_damage") */
    heavy.scale = 5.0f;
    uint32_t damage = api->add_damage(mod, &heavy);

    HfProjectile bolt = {sizeof bolt};
    bolt.base = HF_PROJECTILE_LAS58_TALON;
    bolt.name = "bolt";
    bolt.damage = damage;
    bolt.spawn_effect = api->resource_hash("skyeshade/talon_liberator/assault_rifle_talon/bolt_blue");
    bolt.flight_effect = api->resource_hash("skyeshade/talon_liberator/assault_rifle_talon/bolt_flight_blue");
    uint32_t projectile = api->add_projectile(mod, &bolt);

    HfWeapon rifle = {sizeof rifle};
    rifle.base = HF_ITEM_AR23_LIBERATOR;
    rifle.unit = "skyeshade/talon_liberator/assault_rifle_talon";
    rifle.designation = "AR-23T";
    rifle.name = "Liberator Talon";
    rifle.description = "A Liberator rebuilt around the LAS-58 Talon's bolt, tuned well past its rated output. Five times the "
        "damage per hit; the warranty did not survive the modification.";
    rifle.projectile = projectile;
    rifle.sound_from = HF_ITEM_LAS16_SICKLE; /* its banks are in data/ with the rifle's model */
    uint32_t weapon = api->add_weapon(mod, &rifle);

    if (damage == 0 || projectile == 0 || weapon == 0) return 1;
    api->log(mod, "AR-23T Liberator Talon is loadout item %08x; it fires projectile type %u (damage id %u)", weapon, projectile, damage);
    return 0;
}
