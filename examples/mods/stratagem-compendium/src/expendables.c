/* EAT-77 Expendable Cluster, EAT-40 Expendable Gas and EAT-23 Expendable EMS: single-use launchers that come down in pairs,
 * as the EAT-17 does.
 *
 *   round     the Cluster fires a copy of the RL-77 Airburst's round (312: it bursts into 25 bomblets near its target)
 *             with a copy of its damage (380); the Gas and EMS fire a copy of the EAT-17's rocket (132) dealing a copy of
 *             its damage at a quarter (500 instead of 2000, its armour penetration kept) that bursts into the Orbital Gas
 *             Strike's cloud (explosion 82) or the EMS Strike's static field (188) where it hits. HellForge loads those
 *             explosions' packages itself (a reference field); the RL-77's round needs the RL-77's package, held for the
 *             session (compendium.c).
 *   weapon    a copy of the EAT-17 (its entity, 0x80932FA0ED6901D3) sharing no record with it (own_all) that fires the new
 *             round. It takes the EAT-17's loadout entry (an entity copy of a weapon does), so the game equips it as an
 *             EAT and the HUD names it as one.
 *   rack      a copy of the EAT-17's hellpod rack (0x0DC7A18342B62BEC: HellpodRackComponentData, eight slots of 64
 *             bytes, +0 the entity a slot delivers) with both of its slots holding the new weapon. It has no name record,
 *             so the stratagem's title carries the designation ("EAT-77 Expendable Cluster").
 *   stratagem a copy of the EAT-17's (its beacon, the support hellpod, its cooldown) whose payload is {the new rack, the
 *             support hellpod}, with the pack's code, traits and icon.
 *   editor    the EAT-17's page on the new weapon (and rack), its round and its damage. */
#include "compendium.h"

#define EAT17_RACK 0x0DC7A18342B62BECull       /* weapon_rack_lat_oneshot: the EAT-17's two launchers */
#define EAT17_DAMAGE 227u                      /* the EAT-17 rocket's DamageInfo (2000 standard and durable) */
#define RL77_DAMAGE 380u                       /* the RL-77 round's DamageInfo */
#define PACKAGE_RL77 0xF7646E79610C124Dull     /* the RL-77's loadout package: its round's unit */
#define EAT17_TRAITS (HF_TRAIT_SUPPORT_WEAPON | HF_TRAIT_HELLPOD | HF_TRAIT_ANTI_TANK | HF_TRAIT_EXPENDABLE) /* the EAT-17's */

typedef struct Expendable
{
    const char* name, * title, * designation, * description, * code, * icon;
    uint32_t round, damage;     /* the vanilla round and damage it copies */
    float scale;                /* x the damage (0: as is) */
    uint32_t explosion;         /* its impact explosion, or 0 for the round's own */
    uint32_t flags;
    uint64_t package;           /* a package to hold, or 0 */
    HfSet weaponSet[2], rackSet[3];
    HfEntityDef weapon, rack;
    HfEditorEntity editor[3];
    uint64_t payload[2];
    char weaponName[160], rackName[160], roundName[160], damageName[160], fullTitle[96];
} Expendable;

static Expendable expendables[] = {
    {"skyeshade/compendium/eat_cluster", "Expendable Cluster", "EAT-77",
        "A single-use weapon that comes down in pairs. Explodes into a cluster of lesser explosions upon impact.", "DDLDL",
        "skyeshade/compendium/icons/eat_cluster", HF_PROJECTILE_RL77_AIRBURST_ROCKET_LAUNCHER, RL77_DAMAGE, 0.0f, 0, EAT17_TRAITS, PACKAGE_RL77},
    {"skyeshade/compendium/eat_gas", "Expendable Gas", "EAT-40",
        "A single-use weapon that comes down in pairs. Outfitted with a caustic warhead that causes confusion and blindness in "
        "those affected.", "DDLDR", "skyeshade/compendium/icons/eat_gas", HF_PROJECTILE_EAT17_EXPENDABLE_ANTITANK, EAT17_DAMAGE, 0.25f,
        EXPLOSION_ORBITAL_GAS, EAT17_TRAITS | HF_TRAIT_GAS, 0},
    {"skyeshade/compendium/eat_ems", "Expendable EMS", "EAT-23",
        "A single-use \"compliance weapon\" that comes down in pairs and modifies enemy behaviour. The EMS warhead stuns targets "
        "within the impact radius.", "DDLRR", "skyeshade/compendium/icons/eat_ems", HF_PROJECTILE_EAT17_EXPENDABLE_ANTITANK, EAT17_DAMAGE, 0.25f,
        EXPLOSION_ORBITAL_EMS, EAT17_TRAITS | HF_TRAIT_STUN, 0},
};

static int AddExpendable(const HfApi* hf, HfMod* mod, Expendable* e)
{
    snprintf(e->damageName, sizeof e->damageName, "%s_damage", e->name);
    snprintf(e->roundName, sizeof e->roundName, "%s_round", e->name);
    HfDamage damage = {sizeof damage};
    damage.base = e->damage;
    damage.name = e->damageName;
    damage.scale = e->scale;
    HfProjectile round = {sizeof round};
    round.base = e->round;
    round.name = e->roundName;
    round.damage = hf->add_damage(mod, &damage);
    uint32_t type = round.damage ? hf->add_projectile(mod, &round) : 0;
    if (type == 0) return 1;
    if (e->explosion != 0 && hf_set(mod, type, ProjectileSettings, impact_explosion, HF_U32(e->explosion)) != 0) return 1;
    if (e->package != 0) HoldPackage(e->package);

    snprintf(e->weaponName, sizeof e->weaponName, "%s_weapon", e->name);
    e->weaponSet[0] = (HfSet){HF_FIELD(ProjectileWeapon, projectile), HF_U32(type)};
    e->weaponSet[1] = (HfSet){0};
    e->weapon = (HfEntityDef){.size = sizeof(HfEntityDef), .name = e->weaponName, .base = HF_ENTITY_EAT17_EXPENDABLE_ANTITANK,
        .own_all = 1, .set = e->weaponSet, .title = e->title, .designation = e->designation, .description = e->description};
    HfRef weapon = hf->register_entity(mod, &e->weapon);
    if (!weapon) return 1;

    snprintf(e->rackName, sizeof e->rackName, "%s_rack", e->name);
    e->rackSet[0] = (HfSet){HF_FIELD(HellpodRack, slot0_item), HF_U64(weapon)};
    e->rackSet[1] = (HfSet){HF_FIELD(HellpodRack, slot1_item), HF_U64(weapon)};
    e->rackSet[2] = (HfSet){0};
    e->rack = (HfEntityDef){.size = sizeof(HfEntityDef), .name = e->rackName, .base = EAT17_RACK, .own_all = 1, .set = e->rackSet,
        .title = e->title, .designation = e->designation, .description = e->description};
    HfRef rack = hf->register_entity(mod, &e->rack);
    if (!rack) return 1;

    e->payload[0] = rack;
    e->payload[1] = SUPPORT_HELLPOD;
    e->editor[0] = (HfEditorEntity){weapon, HF_ENTITY_EAT17_EXPENDABLE_ANTITANK};
    e->editor[1] = (HfEditorEntity){rack, EAT17_RACK};
    HfStratagem s = {sizeof s};
    s.base = HF_STRATAGEM_EAT17_EXPENDABLE_ANTITANK;
    s.name = e->name;
    /* its full name: the rack has no name record of its own for a designation, so the title carries it */
    snprintf(e->fullTitle, sizeof e->fullTitle, "%s %s", e->designation, e->title);
    s.title = e->fullTitle;
    s.description = e->description;
    s.code = e->code;
    s.icon = e->icon;
    s.cooldown = 70.0f;
    s.payload = e->payload;
    s.payload_count = 2;
    s.flags = e->flags;
    s.editor_entities = e->editor;
    uint32_t stratagem = AddCompendiumStratagem(hf, mod, &s, COMPENDIUM_SUPPLY);
    hf->log(mod, "%s %s: stratagem type %u (0: not added), round %u", e->designation, e->title, stratagem, type);
    return stratagem ? 0 : 1;
}

int AddExpendables(const HfApi* hf, HfMod* mod)
{
    int refused = 0;
    for (size_t i = 0; i < sizeof expendables / sizeof expendables[0]; i++) refused += AddExpendable(hf, mod, &expendables[i]);
    return refused;
}
