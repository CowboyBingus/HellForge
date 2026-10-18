/* Hell Swarm: a stress test for difficulty 11 (needs a mod that adds it, such as Hell Forge). It rewrites level 11's
 * difficulty row (difficulty_row, as the generated HfDifficultySettings) once the entity data is ready: enemy budgets 10x
 * Super Helldive's, spawner waves every 0.25 s, patrols every 8 s with room for 400 living patrol enemies. The director
 * reads these from the row every frame. Not for play: it exists to fill the map with enemies. */
#include "hellforge_mod.h"

static void Swarm(HfMod* mod, HfEvent event, void* user)
{
    HfDifficultySettings* level = (HfDifficultySettings*)HF_API(mod)->difficulty_row(11);
    if (level == NULL)
    {
        HF_API(mod)->log(mod, "no difficulty 11 (is Hell Forge installed and enabled?); nothing changed");
        return;
    }
    level->encounter_points = 6000.0f;  /* Super Helldive 600 */
    level->guardforce_points = 6000.0f; /* 600 */
    level->spawner_cooldown = 0.25f;    /* 1.5 s */
    for (int faction = 0; faction < 3; faction++)
    {
        level->patrol_interval[faction] = 8.0f; /* 65 / 78 / 75 s */
        level->patrol_cap[faction] = 400;       /* 41 / 39 / 48 */
    }
    HF_API(mod)->log(mod, "level 11: budgets %.0f, spawner cooldown %.2f s, patrols every %.0f s (up to %u alive)",
        (double)level->encounter_points, (double)level->spawner_cooldown, (double)level->patrol_interval[0], level->patrol_cap[0]);
}

HF_MOD_EXPORT int hellforge_mod_init(const HfApi* hf, HfMod* mod)
{
    if (hf->version < 6) return 1;
    return hf->on(mod, HF_EVENT_ENTITIES_READY, Swarm, 0);
}
