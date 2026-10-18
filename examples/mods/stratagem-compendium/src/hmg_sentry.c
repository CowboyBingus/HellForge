/* A/MG-101 Heavy MG Sentry: a heavy machine gun on the A/MG-43 Machine Gun Sentry's tripod.
 *
 *   entity    a copy of the A/MG-43 sentry with a record of its own in every component (own_all): its health, turret,
 *             targeting, magazine, weapon and the rest are its own, so nothing shares stats with the MG-43. Text of its
 *             own: the game names a sentry's stratagem "<its payload's designation> <the stratagem's title>". It fires a
 *             round of its own with the MG-206 Heavy Machine Gun's casings and shot, at 400 RPM with 5 mrad of spread,
 *             300 rounds. The magazine's pattern picks each shot's round, so every slot of it names the new round.
 *   round     a copy of the MG-206's armour-piercing round (275) dealing a copy of its damage (205).
 *   stratagem a copy of the A/MG-43 sentry's (its hellpod drop, beacon and defensive slot) whose payload is the new sentry,
 *             loading the MG-206's package for the round, casings and sound.
 *   events    its log says when its beacon activates and when one of its sentries lands (HF_ON_STRATAGEM, HF_ON_SPAWN),
 *             and counts each mission's deployments (HF_ON_MISSION_START, HF_ON_MISSION_END). */
#include "compendium.h"

#define HMG_RATE 400.0f
#define HMG_SPREAD 5.0f
#define HMG_ROUNDS 300u

static const char kDescription[] = "An automated sentry turret firing with the prowess of an heavy machine gun. More sluggish "
    "than lighter variants. Hazardous to Helldivers in the crossfire.";

static const HfCopy hmgCopies[] = {
    HF_COPY_GROUP(HF_ENTITY_MG206_HEAVY_MACHINE_GUN, ProjectileWeapon, casings),
    HF_COPY_GROUP(HF_ENTITY_MG206_HEAVY_MACHINE_GUN, ProjectileWeapon, firing_sounds),
    {0}
};
static HfSet hmgSet[48]; /* filled once its round's type is known */

static HfEntityDef hmgSentry = {
    .size = sizeof(HfEntityDef),
    .name = "skyeshade/hmg_sentry/hmg_sentry",
    .base = HF_ENTITY_AMG43_MACHINE_GUN_SENTRY,
    .copy = hmgCopies,
    .set = hmgSet,
    .own_all = 1,
    .title = "Heavy MG Sentry",
    .designation = "A/MG-101",
    .description = kDescription,
};

static HfRef hmgRef;
static uint32_t hmgType;
static int deployed;

static void Armed(HfMod* mod, HfEvent event, void* user)
{
    const HfProjectileWeapon* weapon = hf_record(mod, hmgRef, HfProjectileWeapon);
    const HfWeaponMagazine* magazine = hf_record(mod, hmgRef, HfWeaponMagazine);
    if (weapon != NULL && magazine != NULL)
        HF_API(mod)->log(mod, "HMG sentry armed: projectile %u at %.0f RPM, %u rounds", weapon->projectile, (double)weapon->fire_rate[1],
            magazine->capacity);
}

static void Called(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfStratagemEvent* called = (const HfStratagemEvent*)event;
    if (called->type == hmgType)
        HF_API(mod)->log(mod, "HMG sentry called in at (%.1f, %.1f, %.1f)", (double)called->position[0], (double)called->position[1],
            (double)called->position[2]);
}

static void Spawned(HfMod* mod, const HfEventInfo* event, void* user)
{
    const HfSpawnEvent* spawn = (const HfSpawnEvent*)event;
    if (spawn->resource == hmgRef) HF_API(mod)->log(mod, "HMG sentry %u is down (%d this mission)", spawn->entity, ++deployed);
}

static void Mission(HfMod* mod, const HfEventInfo* event, void* user)
{
    if (event->kind == HF_ON_MISSION_END) HF_API(mod)->log(mod, "mission over: %d HMG sentr%s deployed", deployed, deployed == 1 ? "y" : "ies");
    deployed = 0;
}

int AddHmgSentry(const HfApi* hf, HfMod* mod)
{
    HfDamage damage = {sizeof damage};
    damage.base = 205; /* the MG-206's DamageInfo */
    damage.name = "skyeshade/hmg_sentry/hmg_round_damage";
    HfProjectile round = {sizeof round};
    round.base = HF_PROJECTILE_MG206_HEAVY_MACHINE_GUN;
    round.name = "skyeshade/hmg_sentry/hmg_round";
    round.damage = hf->add_damage(mod, &damage);
    uint32_t type = round.damage ? hf->add_projectile(mod, &round) : 0;
    if (type == 0) return 1;

    int n = 0;
    hmgSet[n++] = (HfSet){HF_FIELD(ProjectileWeapon, projectile), HF_U32(type)};
    hmgSet[n++] = (HfSet){HF_FIELD_AT(ProjectileWeapon, fire_rate, 1), HF_F32(HMG_RATE)}; /* the rate it spawns with */
    hmgSet[n++] = (HfSet){HF_FIELD(WeaponData, horizontal_spread), HF_F32(HMG_SPREAD)};
    hmgSet[n++] = (HfSet){HF_FIELD(WeaponData, vertical_spread), HF_F32(HMG_SPREAD)};
    hmgSet[n++] = (HfSet){HF_FIELD(WeaponMagazine, capacity), HF_U32(HMG_ROUNDS)};
    hmgSet[n++] = (HfSet){HF_FIELD(WeaponMagazine, first_projectile), HF_U32(type)};
    for (int i = 0; i < 32; i++) hmgSet[n++] = (HfSet){HF_FIELD_AT(WeaponMagazine, pattern, i), HF_U32(type)};
    hmgSet[n] = (HfSet){0};
    hmgRef = hf->register_entity(mod, &hmgSentry);
    if (!hmgRef) return 1;
    hf->on(mod, HF_EVENT_ENTITIES_READY, Armed, NULL);

    static uint64_t payload[2];
    payload[0] = hmgRef;
    payload[1] = SUPPORT_HELLPOD;
    HfStratagem sentry = {sizeof sentry};
    sentry.base = HF_STRATAGEM_AMG43_MACHINE_GUN_SENTRY;
    sentry.name = "skyeshade/hmg_sentry/hmg_sentry";
    sentry.title = "Heavy MG Sentry"; /* shown after its sentry's designation: A/MG-101 Heavy MG Sentry */
    sentry.description = kDescription;
    sentry.code = "DURRRL";
    sentry.icon = "skyeshade/compendium/icons/hmg_sentry";
    sentry.cooldown = 150.0f;
    sentry.payload = payload;
    sentry.payload_count = 2;
    /* the pack's traits, Sentry and armour piercing, delivered by hellpod as the MG-43 sentry is (its own: Hellpod,
     * Sentry, Crowd Control) */
    sentry.flags = HF_TRAIT_HELLPOD | HF_TRAIT_SENTRY | HF_TRAIT_ARMOR_PIERCING;
    sentry.loads_item = HF_ITEM_MG206_HEAVY_MACHINE_GUN; /* its round, casings and sound */
    hmgType = AddCompendiumStratagem(hf, mod, &sentry, COMPENDIUM_DEFENSIVE);
    hf->log(mod, "A/MG-101 Heavy MG Sentry: stratagem type %u (0: not added)", hmgType);
    if (hmgType == 0) return 1;
    hf->subscribe(mod, HF_ON_STRATAGEM, Called, NULL, 0);
    hf->subscribe(mod, HF_ON_SPAWN, Spawned, NULL, 0);
    hf->subscribe(mod, HF_ON_MISSION_START, Mission, NULL, 0);
    hf->subscribe(mod, HF_ON_MISSION_END, Mission, NULL, 0);
    return 0;
}
