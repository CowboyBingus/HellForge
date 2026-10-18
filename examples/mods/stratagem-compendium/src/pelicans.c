/* Pelican Gatling, Cannon, EMS and Gas Support: stratagems that call in a Pelican of their own, hovering over the beacon
 * for 90 seconds with a turret of its own on its chin.
 *
 *   Pelican     a copy of the transport Pelican with the extraction Pelican's model (model_from) and a mount carrying
 *               its turret, running a behaviour HellForge registers: the transport Pelican's (fly in, hover, depart)
 *               holding 90 s at the beacon, circling it 40 m out and 60 m up and turning toward what its turret shoots.
 *   turret      a copy of the Pelican's chin turret sharing nothing with it (own_all), crediting its kills to the players
 *               (no "no kill credit" tag). The Gatling, EMS and Gas turrets fire full auto from 4000 rounds (a 2000-round
 *               magazine refilled once; a turret behaviour of its own) with 15 mrad of spread and no recoil; the Cannon's
 *               keeps the chin turret's autocannon and bursts.
 *   round       the Gatling, EMS and Gas turrets fire a copy of the MG-206's armour-piercing round (275) dealing a copy of
 *               its damage (205); the EMS and Gas rounds burst into the EMS Mortar's static field (explosion 180) or a
 *               G-4 gas cloud (177) where they hit. HellForge loads those explosions' packages itself (a reference field):
 *               the A/M-23 and A/GM-17 sentries' packages, which hold their shot sounds too.
 *   stratagem   a Pelican drop (the EXO-45 Patriot's row as the template) whose payload is {no cargo, the Pelican}, as an
 *               offensive (red) stratagem with the pack's code, cooldown, uses, traits and icon. The Pelican's package
 *               (its model) loads with it, inferred from the payload; the MG-206's (the round) through loads_item.
 *   editor      the A/G-16 Gatling Sentry's page (the A/AC-8 Autocannon Sentry's for the Cannon) on the turret, its round
 *               and its damage. */
#include "compendium.h"

#define EXPLOSION_EMS_FIELD 180u /* the A/M-23 EMS Mortar's shell expiry: a 7 s static field */
#define EXPLOSION_GAS_CLOUD 177u /* the G-4 Gas grenade's detonation: a 15 s caustic cloud */

static const HfBehaviour pelicanSupport = {
    .size = sizeof(HfBehaviour),
    .base = HF_BEHAVIOUR_PELICAN_TRANSPORT,
    .hold_seconds = 90.0f,
    .orbit_radius = 40.0f,
    .orbit_height = 60.0f,
    .face_target = 1,
};
/* Full auto, with HellForge's target choice; 4000 rounds in all. */
static const HfBehaviour fullAuto = {
    .size = sizeof(HfBehaviour),
    .base = HF_BEHAVIOUR_PELICAN_TURRET,
    .reserve_rounds = 2000,
    .aim_height = 0.15f, /* aims at the feet + 0.15 m: from 60 m up the game's aim point overshoots (0.7 and 0.3 still did) */
};
static const uint64_t chinTurretTags[] = {HF_TAG_NO_KILL_CREDIT, 0};

/* What each gun takes from another entity: the Gatling the MG-206's casings and shot (the Bastion HMG's) and the Gatling
 * Sentry's rate (1600 RPM); the EMS and Gas guns their mortar sentry's shot, keeping the chin turret's own casings. */
static const HfCopy gatlingCopies[] = {
    HF_COPY_GROUP(HF_ENTITY_MG206_HEAVY_MACHINE_GUN, ProjectileWeapon, casings),
    HF_COPY_GROUP(HF_ENTITY_MG206_HEAVY_MACHINE_GUN, ProjectileWeapon, firing_sounds),
    {HF_ENTITY_GATLING_SENTRY, HF_FIELD(ProjectileWeapon, fire_rate)},
    {0}
};
static const HfCopy emsCopies[] = {HF_COPY_GROUP(HF_ENTITY_AM23_EMS_MORTAR_SENTRY, ProjectileWeapon, firing_sounds), {0}};
static const HfCopy gasCopies[] = {HF_COPY_GROUP(HF_ENTITY_AGM17_GAS_MORTAR_SENTRY, ProjectileWeapon, firing_sounds), {0}};

typedef struct Pelican
{
    const char* name;          /* its identity (the Gatling's and Cannon's are pelican-support's, so loadouts keep them) */
    const char* title, * description, * code, * icon;
    int32_t uses;
    uint32_t flags;
    const HfCopy* copies;      /* NULL: the Cannon (the chin turret's own gun) */
    uint32_t explosion;        /* its round's impact explosion, or 0 for the round's own (none) */
    float rpm;                 /* 0: the rate it copies */
    uint32_t editorLike;       /* the stratagem whose editor page its turret takes, and that page's entity */
    uint64_t editorEntity;
    /* filled in at registration (the loader reads definitions when they are registered; they stay put) */
    HfSet set[24];
    char turretName[160], stratagemName[160];
    HfEntityDef turret, pelican;
    HfMountDef mount[2];
    HfEditorEntity editor[2];
    uint64_t payload[2];
} Pelican;

static Pelican pelicans[] = {
    {"skyeshade/pelican_support/pelican_gatling", "Pelican Gatling Support",
        "Calls down a Pelican for close air support. Hovers around the beacon for 90 seconds and bombards nearby enemies with its "
        "high fire rate machine gun.", "LDLLUU", "skyeshade/compendium/icons/pelican_gatling_support", 4,
        HF_TRAIT_CROWD_CONTROL | HF_TRAIT_ARMOR_PIERCING, gatlingCopies, 0, 0.0f, HF_STRATAGEM_AG16_GATLING_SENTRY, HF_ENTITY_GATLING_SENTRY},
    {"skyeshade/pelican_support/pelican_cannon", "Pelican Cannon Support",
        "Calls down a Pelican for close air support. Hovers around the beacon for 90 seconds, firing its autocannon bursts and "
        "turning to face its target.", "LDLULU", "skyeshade/compendium/icons/pelican_cannon_support", 3,
        HF_TRAIT_ANTI_TANK | HF_TRAIT_EXPLOSIVE, NULL, 0, 0.0f, HF_STRATAGEM_AAC8_AUTOCANNON_SENTRY, HF_ENTITY_AAC8_AUTOCANNON_SENTRY},
    {"skyeshade/compendium/pelican_ems", "Pelican EMS Support",
        "Calls down a Pelican for close air support. Hovers around the beacon for 90 seconds and assists Helldivers with EMS shells "
        "to modify enemy behaviour. Stunning enemies around the impact of each shell.", "LDLRLD", "skyeshade/compendium/icons/pelican_ems_support", 4,
        HF_TRAIT_STUN, emsCopies, EXPLOSION_EMS_FIELD, 30.0f, HF_STRATAGEM_AG16_GATLING_SENTRY, HF_ENTITY_GATLING_SENTRY},
    {"skyeshade/compendium/pelican_gas", "Pelican Gas Support",
        "Calls down a Pelican for close air support. Hovers around the beacon for 90 seconds and assists Helldivers with a gas "
        "bombardment, causing confusion and blindness in hit enemies.", "LDLRDR", "skyeshade/compendium/icons/pelican_gas_support", 4,
        HF_TRAIT_GAS, gasCopies, EXPLOSION_GAS_CLOUD, 60.0f, HF_STRATAGEM_AG16_GATLING_SENTRY, HF_ENTITY_GATLING_SENTRY},
};

/* A round of the gun's own: a copy of the MG-206's, with a copy of its damage and, given one, its own impact explosion. */
static uint32_t Round(const HfApi* hf, HfMod* mod, const char* name, uint32_t explosion)
{
    char key[160];
    HfDamage damage = {sizeof damage};
    damage.base = 205; /* the MG-206's DamageInfo */
    snprintf(key, sizeof key, "%s_round_damage", name);
    damage.name = key;
    HfProjectile round = {sizeof round};
    round.base = HF_PROJECTILE_MG206_HEAVY_MACHINE_GUN;
    round.damage = hf->add_damage(mod, &damage);
    char roundName[160];
    snprintf(roundName, sizeof roundName, "%s_round", name);
    round.name = roundName;
    uint32_t type = round.damage ? hf->add_projectile(mod, &round) : 0;
    if (type != 0 && explosion != 0 && hf_set(mod, type, ProjectileSettings, impact_explosion, HF_U32(explosion)) != 0) return 0;
    return type;
}

static int AddPelican(const HfApi* hf, HfMod* mod, Pelican* p)
{
    int n = 0;
    if (p->copies != NULL)
    {
        uint32_t round = Round(hf, mod, p->name, p->explosion);
        if (round == 0) return 1;
        p->set[n++] = (HfSet){HF_FIELD(ProjectileWeapon, projectile), HF_U32(round)};
        p->set[n++] = (HfSet){HF_FIELD(WeaponData, horizontal_spread), HF_F32(15)};
        p->set[n++] = (HfSet){HF_FIELD(WeaponData, vertical_spread), HF_F32(15)};
        p->set[n++] = (HfSet){HF_FIELD(WeaponMagazine, capacity), HF_U32(2000)}; /* the network field holds 2047; the behaviour refills */
        if (p->rpm > 0) p->set[n++] = (HfSet){HF_FIELD_AT(ProjectileWeapon, fire_rate, 1), HF_F32(p->rpm)};
        /* the chin turret kicks 10 up a shot: none */
        static const HfSet recoil[] = {HF_SET_GROUP(WeaponData, recoil, HF_F32(0))};
        for (size_t i = 0; i < sizeof recoil / sizeof recoil[0]; i++) p->set[n++] = recoil[i];
    }
    p->set[n] = (HfSet){0};
    snprintf(p->turretName, sizeof p->turretName, "%s_turret", p->name);
    p->turret = (HfEntityDef){.size = sizeof(HfEntityDef), .name = p->turretName, .base = HF_ENTITY_PELICAN_TURRET, .own_all = 1,
        .behaviour = p->copies != NULL ? &fullAuto : NULL, .copy = p->copies, .set = p->set, .remove_tags = chinTurretTags};
    p->mount[0] = (HfMountDef){.slot = 0, .entity = &p->turret};
    p->pelican = (HfEntityDef){.size = sizeof(HfEntityDef), .name = p->name, .base = HF_ENTITY_PELICAN_TRANSPORT,
        .model_from = HF_ENTITY_PELICAN_GUNSHIP, .behaviour = &pelicanSupport, .mount = p->mount};
    HfRef pelican = hf->register_entity(mod, &p->pelican);
    HfRef turret = hf->register_entity(mod, &p->turret); /* registered with the Pelican: the same entity */
    if (!pelican || !turret) return 1;

    p->payload[1] = pelican; /* {no cargo, the Pelican} */
    p->editor[0] = (HfEditorEntity){turret, p->editorEntity};
    HfStratagem s = {sizeof s};
    s.base = HF_STRATAGEM_EXO45_PATRIOT;
    snprintf(p->stratagemName, sizeof p->stratagemName, "%s_support", p->name);
    s.name = p->stratagemName;
    s.title = p->title;
    s.description = p->description;
    s.code = p->code;
    s.icon = p->icon;
    s.cooldown = 300.0f;
    s.uses = p->uses;
    s.payload = p->payload;
    s.payload_count = 2;
    /* its own place in the loadout, not the Patriot's: an offensive (red) stratagem with a red beam, and traits of its
     * own instead of a vehicle's (HF_TRAIT_VEHICLE: one per loadout) */
    s.category = HF_CATEGORY_OFFENSIVE;
    s.beam = HF_BEAM_RED;
    s.flags = p->flags;
    if (p->copies != NULL) s.loads_item = HF_ITEM_MG206_HEAVY_MACHINE_GUN; /* its round */
    s.editor_like = p->editorLike;
    s.editor_entities = p->editor;
    uint32_t type = AddCompendiumStratagem(hf, mod, &s, COMPENDIUM_OFFENSIVE);
    hf->log(mod, "%s: stratagem type %u (0: not added)", p->title, type);
    return type ? 0 : 1;
}

int AddPelicans(const HfApi* hf, HfMod* mod)
{
    int refused = 0;
    for (size_t i = 0; i < sizeof pelicans / sizeof pelicans[0]; i++) refused += AddPelican(hf, mod, &pelicans[i]);
    return refused;
}
