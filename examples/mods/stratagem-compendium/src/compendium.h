/* The Stratagem Compendium: the HD2Runtime custom stratagem pack 0.4.1's eleven stratagems as HellForge game data
 * (SDK 8). All eleven share the Stratagem Compendium heading in the native selection screen.
 * One file per family, each a worked example of the SDK:
 *
 *   pelicans.c          Pelican Gatling, Cannon, EMS and Gas Support: new entities with a mount, a registered behaviour,
 *                       turrets with rounds of their own (add_projectile, add_damage, a ProjectileSettings edit)
 *   hmg_sentry.c        A/MG-101 Heavy MG Sentry: an entity sharing nothing with its base (own_all), entity text
 *   orbital_barrages.c  Orbital Gas and EMS Barrages: a bombardment entity whose shell list names a shell of its own
 *   expendables.c       EAT-77 Cluster, EAT-40 Gas, EAT-23 EMS: a weapon entity and the hellpod rack delivering it
 *   shredder_silo.c     MS-N223 Shredder Silo: a missile entity with another explosion, in a rack of its own
 *
 * The rule the compendium follows: a new stratagem owns everything it changes and as much else as HellForge can give it
 * (its entities with own_all, its rounds, its damage), so tuning it, with code or in the in-game editor, never changes a
 * vanilla stratagem or weapon, and a vanilla edit never changes it. The editor lists each under its category with the
 * page of the vanilla stratagem it is built on, pointed at its own records. Values follow the pack's
 * (its custom_stratagems.json). */
#pragma once
#include <stdio.h>
#include "hellforge_mod.h"

int AddPelicans(const HfApi* hf, HfMod* mod);
int AddHmgSentry(const HfApi* hf, HfMod* mod);
int AddOrbitalBarrages(const HfApi* hf, HfMod* mod);
int AddExpendables(const HfApi* hf, HfMod* mod);
int AddShredderSilo(const HfApi* hf, HfMod* mod);

/* Register every family under the pack's shared selection heading, offensive (red) first, then supply (blue), then
 * defensive (green); call order within each. Native HUD/beam colours stay on each definition. */
enum { COMPENDIUM_OFFENSIVE, COMPENDIUM_SUPPLY, COMPENDIUM_DEFENSIVE };
uint32_t AddCompendiumStratagem(const HfApi* hf, HfMod* mod, HfStratagem* stratagem, int32_t order);

/* A package the session holds once the game runs (load_package): what a stratagem uses that neither its payload's
 * package nor its loads_item loads (a shell of another stratagem, a round of another weapon). */
void HoldPackage(uint64_t package);

/* Shared vanilla resources (generated_entities.dl_bin, build 25480438; HD2Runtime 0.40.1 research). */
#define SUPPORT_HELLPOD 0x73F8498BFFDCF415ull /* the hellpod support weapons and sentries come in (payload[1]) */
#define EXPLOSION_ORBITAL_GAS 82u             /* the Orbital Gas Strike's shell impact: a 15 s caustic cloud */
#define EXPLOSION_ORBITAL_EMS 188u            /* the Orbital EMS Strike's shell impact: a 15 s static field */
