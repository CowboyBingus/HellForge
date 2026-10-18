/* Orbital Gas Barrage and Orbital EMS Barrage: the Orbital 120mm HE Barrage's pattern (5 salvos of 3 shells over a wide
 * area) firing the Orbital Gas Strike's or EMS Strike's shell, so every shell leaves a caustic cloud or a static field.
 *
 *   shell     a copy of the strike's shell (197 gas, 74 EMS) dealing a copy of its damage (261), so neither the strike
 *             nor the barrage changes the other.
 *   barrage   a copy of the 120mm barrage's bombardment entity (0x2D3BD00B1ED411B1) with every record its own (own_all),
 *             whose shell list (BombardmentComponentData +0x40, eight projectile types fired in turn) names the new shell
 *             three times, as the 120mm's names its three (194, 137, 137). The game counts the non-zero entries when the
 *             barrage starts, so the list stays packed.
 *   stratagem a copy of the 120mm barrage's (its beacon, the orbital ship, its call-in) whose payload is the new barrage,
 *             with the pack's code, cooldown, traits and icon. Its payload's package (orbital_strike) loads with it; the
 *             strike's (the shell, its cloud or field) is held for the session (compendium.c).
 *   editor    the 120mm barrage's page on the new barrage: its pattern. */
#include "compendium.h"

#define BARRAGE_120MM 0x2D3BD00B1ED411B1ull  /* the Orbital 120mm HE Barrage's bombardment (its payload) */
#define SHELL_GAS 197u                      /* the Orbital Gas Strike's shell (impact: explosion 82) */
#define SHELL_EMS 74u                       /* the Orbital EMS Strike's shell (impact: explosion 188) */
#define SHELL_DAMAGE 261u                   /* both strikes' shell damage */
#define PACKAGE_ORBITAL_GAS 0x6369816737A36A40ull
#define PACKAGE_ORBITAL_STUN 0x1E6C958B95568DD7ull

typedef struct Barrage
{
    const char* name, * title, * description, * code, * icon;
    uint32_t shell, flags;
    uint64_t package;
    HfSet set[4];
    HfEntityDef entity;
    uint64_t payload[1];
    char entityName[160], damageName[160], shellName[160];
} Barrage;

static Barrage barrages[] = {
    {"skyeshade/compendium/orbital_gas_barrage", "Orbital Gas Barrage",
        "A prolonged caustic barrage, spreading corrosive gas over a large area. Causes confusion and blindness on those affected. "
        "Breathing it in is not advised.", "RRDLDL", "skyeshade/compendium/icons/orbital_gas_barrage", SHELL_GAS,
        HF_TRAIT_ORBITAL | HF_TRAIT_ANTI_TANK | HF_TRAIT_GAS, PACKAGE_ORBITAL_GAS},
    {"skyeshade/compendium/orbital_ems_barrage", "Orbital EMS Barrage",
        "A prolonged \"compliance barrage\" to modify enemy behavior in a wide area. Stuns enemies in a large area.", "RDURLD",
        "skyeshade/compendium/icons/orbital_ems_barrage", SHELL_EMS, HF_TRAIT_ORBITAL | HF_TRAIT_ANTI_TANK | HF_TRAIT_STUN, PACKAGE_ORBITAL_STUN},
};

static int AddBarrage(const HfApi* hf, HfMod* mod, Barrage* b)
{
    snprintf(b->damageName, sizeof b->damageName, "%s_shell_damage", b->name);
    snprintf(b->shellName, sizeof b->shellName, "%s_shell", b->name);
    HfDamage damage = {sizeof damage};
    damage.base = SHELL_DAMAGE;
    damage.name = b->damageName;
    HfProjectile shell = {sizeof shell};
    shell.base = b->shell;
    shell.name = b->shellName;
    shell.damage = hf->add_damage(mod, &damage);
    uint32_t type = shell.damage ? hf->add_projectile(mod, &shell) : 0;
    if (type == 0) return 1;

    for (int i = 0; i < 3; i++) b->set[i] = (HfSet){HF_FIELD_AT(Bombardment, shells, i), HF_U32(type)};
    b->set[3] = (HfSet){0};
    snprintf(b->entityName, sizeof b->entityName, "%s_bombardment", b->name);
    b->entity = (HfEntityDef){.size = sizeof(HfEntityDef), .name = b->entityName, .base = BARRAGE_120MM, .own_all = 1, .set = b->set};
    HfRef barrage = hf->register_entity(mod, &b->entity);
    if (!barrage) return 1;
    HoldPackage(b->package);

    b->payload[0] = barrage;
    HfStratagem s = {sizeof s};
    s.base = HF_STRATAGEM_ORBITAL_120MM_BARRAGE;
    s.name = b->name;
    s.title = b->title;
    s.description = b->description;
    s.code = b->code;
    s.icon = b->icon;
    s.cooldown = 60.0f;
    s.payload = b->payload;
    s.payload_count = 1;
    s.flags = b->flags;
    uint32_t stratagem = AddCompendiumStratagem(hf, mod, &s, COMPENDIUM_OFFENSIVE);
    hf->log(mod, "%s: stratagem type %u (0: not added), shell %u", b->title, stratagem, type);
    return stratagem ? 0 : 1;
}

int AddOrbitalBarrages(const HfApi* hf, HfMod* mod)
{
    int refused = 0;
    for (size_t i = 0; i < sizeof barrages / sizeof barrages[0]; i++) refused += AddBarrage(hf, mod, &barrages[i]);
    return refused;
}
