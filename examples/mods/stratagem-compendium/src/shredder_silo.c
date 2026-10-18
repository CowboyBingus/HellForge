/* MS-N223 Shredder Silo: the MS-11 Solo Silo's single missile, detonating with the blast that destroys a Cyborg
 * Production Unit (explosion 293: 10000 damage over a 50 to 100 m radius). One use, with a targeting remote of its own
 * that counts down 10 seconds, with the Launch ICBM objective's countdown, before the missile launches.
 *
 *   missile   a copy of the silo's missile entity (0xDDDB2910FF2B24E9) sharing no record with it (own_all) whose
 *             ExplosiveComponentData names the new blast (+0x24, the explosion it requests when it detonates). HellForge
 *             loads the blast's packages itself (a reference field: the factory's objective packages, about 300 MB, loaded
 *             with the ship). The pack fell back to the NUX-223 Hellbomb's blast (242) when those could not load; data
 *             names one blast, so a mod wanting the lighter one sets 242 here.
 *   remote    a copy of the silo's targeting remote (0xFC13460592CA79AA) sharing nothing with it, named for the Shredder
 *             (its own loadout record: HellForge gives an entity copy of a weapon one). Its fire modes
 *             (AbilityWeaponComponentData, empty on the silo's remote) name the countdown.
 *   countdown a HellForge ability of the compendium's own (add_ability, HF_ABILITY_HOLDS_LINKED_LAUNCH): firing the
 *             remote holds the launch of the missile it is linked to for 10 s, counting down on the missile (so it goes
 *             on once the remote is tossed), then launches it at the point the laser marked last. While the laser stays
 *             on, the missile follows it, as the silo's does. The sound is the Launch ICBM objective's countdown (its
 *             bank's package is loaded with the session).
 *   rack      a copy of the silo's rack (0xDE18775FA447A9BF: slot 0 the missile, slot 1 the targeting remote) holding the
 *             new missile and remote (it has no name record: the stratagem's title carries the designation).
 *   stratagem a copy of the Solo Silo's (its beacon, the silo hellpod, its package) whose payload is {the new rack, the silo
 *             hellpod}, one use, with the pack's code, cooldown and icon. The pack's one-per-player limit has no data form.
 *   editor    the Solo Silo's page on the new missile and rack. */
#include "compendium.h"

#define SILO_RACK 0xDE18775FA447A9BFull     /* the Solo Silo's rack: its missile and its remote */
#define SILO_HELLPOD 0xFBE75EC44E7C9A50ull  /* the silo itself (payload[1]) */
#define SILO_MISSILE 0xDDDB2910FF2B24E9ull  /* the missile it launches (explosion 135) */
#define SILO_REMOTE 0xFC13460592CA79AAull   /* the targeting remote (HF_ENTITY_SOLO_SILO_TARGETING_REMOTE) */
#define EXPLOSION_CYBORG_FACTORY 293u       /* a destroyed Cyborg Production Unit (the Hellbomb's: 242) */
#define COUNTDOWN_SECONDS 10.0f
/* The Launch ICBM objective's sounds (bank obj_gen_icbm, HD2Runtime research sound-events; the events have no names,
 * picked by listening, 2026-10-18): 88ADD4DA is its countdown (11 s, global, three layers; CA1AF45A sounds the same).
 * 55BE7599 (10 s) is its rocket's launch, much like the silo's own. */
#define ICBM_COUNTDOWN 0x88ADD4DAu
#define PACKAGE_ICBM 0x20BDEC2A51065E1Full  /* packages/content/gen_icbm */

static const char kDescription[] = "A silo that fits one single, tactical nuclear missile. Possible side effects include shell shock, "
    "mutation, and/or death. A laser targetting remote is provided.";

static HfSet missileSet[2]; /* its blast */
static const HfEntityDef missile = {.size = sizeof(HfEntityDef), .name = "skyeshade/compendium/shredder_missile", .base = SILO_MISSILE,
    .own_all = 1, .set = missileSet};
static HfSet remoteSet[3];  /* its fire modes: the countdown */
static const HfEntityDef remote = {.size = sizeof(HfEntityDef), .name = "skyeshade/compendium/shredder_remote", .base = SILO_REMOTE,
    .own_all = 1, .set = remoteSet, .title = "Shredder Silo Targeting Remote"};
static HfSet rackSet[3];
static HfEntityDef rack = {.size = sizeof(HfEntityDef), .name = "skyeshade/compendium/shredder_rack", .base = SILO_RACK, .own_all = 1,
    .set = rackSet, .title = "Shredder Silo", .designation = "MS-N223", .description = kDescription};

int AddShredderSilo(const HfApi* hf, HfMod* mod)
{
    missileSet[0] = (HfSet){HF_FIELD(Explosive, explosion), HF_U32(EXPLOSION_CYBORG_FACTORY)};
    missileSet[1] = (HfSet){0};
    HfRef missileRef = hf->register_entity(mod, &missile);
    if (!missileRef) return 1;

    HfAbility countdown = {sizeof countdown};
    countdown.hold_seconds = COUNTDOWN_SECONDS;
    countdown.flags = HF_ABILITY_HOLDS_LINKED_LAUNCH;
    countdown.start_event = ICBM_COUNTDOWN;
    countdown.package = PACKAGE_ICBM;
    uint32_t countdownId = hf->size >= offsetof(HfApi, add_ability) + sizeof hf->add_ability ? hf->add_ability(mod, &countdown) : 0;
    /* without it (an older HellForge), the silo's remote: it launches at once */
    remoteSet[0] = (HfSet){HF_FIELD(AbilityWeapon, mode0_ability), HF_U32(countdownId)};
    remoteSet[1] = (HfSet){HF_FIELD(AbilityWeapon, mode1_ability), HF_U32(countdownId)};
    remoteSet[2] = (HfSet){0};
    HfRef remoteRef = countdownId ? hf->register_entity(mod, &remote) : 0;
    int n = 0;
    rackSet[n++] = (HfSet){HF_FIELD(HellpodRack, slot0_item), HF_U64(missileRef)};
    if (remoteRef) rackSet[n++] = (HfSet){HF_FIELD(HellpodRack, slot1_item), HF_U64(remoteRef)};
    rackSet[n] = (HfSet){0};
    HfRef rackRef = hf->register_entity(mod, &rack);
    if (!rackRef) return 1;

    static uint64_t payload[2];
    static HfEditorEntity editor[3];
    payload[0] = rackRef;
    payload[1] = SILO_HELLPOD;
    editor[0] = (HfEditorEntity){missileRef, SILO_MISSILE};
    editor[1] = (HfEditorEntity){rackRef, SILO_RACK};
    HfStratagem s = {sizeof s};
    s.base = HF_STRATAGEM_MS11_SOLO_SILO;
    s.name = "skyeshade/compendium/shredder_silo";
    s.title = "MS-N223 Shredder Silo"; /* the rack has no name record of its own for a designation */
    s.description = kDescription;
    s.code = "DURUDDR";
    s.icon = "skyeshade/compendium/icons/shredder_silo";
    s.cooldown = 180.0f;
    s.uses = 1;
    s.payload = payload;
    s.payload_count = 2;
    s.editor_entities = editor;
    uint32_t type = AddCompendiumStratagem(hf, mod, &s, COMPENDIUM_SUPPLY);
    hf->log(mod, "MS-N223 Shredder Silo: stratagem type %u (0: not added), countdown ability %u", type, countdownId);
    return type ? 0 : 1;
}
