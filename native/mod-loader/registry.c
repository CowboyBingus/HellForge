/* The weapon registry (SDK 2, docs/modding/content.md "Weapons"): new arsenal weapons copied from existing ones, added
 * to the game's entity data in memory right after game.dll loads it (generated_entities.dl_bin, loader 0xFDB440; the
 * file is never changed, so the game's resource check passes). Included by hellforge_modloader.c.
 *
 * game.dll keeps pointers into the loaded data in its entity state ([game.dll + 0x346BF98]):
 *   +0xF12EA0  entity map: 4096 rows of {u64 entity, u16* components, u64 component count, u32 short id, u32 0},
 *              open addressing from entity % 4096 (lookup 0x4F2E70);
 *   +0xF12EA8  short ids: sorted {u32 short id, u32 map row} (28-byte LDLD header before it, byte size at -12); an
 *              entity's index here identifies its type in the engine (0x7C83B0);
 *   +0xF12478 + 8*c  component table c: rows {u64 entity, u32 record, u32 0}, open addressing from entity % slots (the
 *              slot count is compiled into each lookup: kSlots), then the records.
 * A registered weapon is a new entity named after its unit resource (the mod ships that unit). It shares every
 * component record of its base weapon except its loadout entry (component 29, LoadoutEntryComponentData: {u32 id,
 * u32 type, u32, u32 traits[5]}) and its unit (component 88, UnitComponentData: u64 unit resource at +0, 136-byte
 * records), plus any components the mod names (HfWeapon.own_components), which get records of their own in HellForge's
 * larger copies of those tables (OwnRecord: the copy has room after every vanilla record, and the weapon's index row
 * names the new record; component_record finds them through RegisteredRecord); its short id is appended after
 * every vanilla one so no vanilla index moves. The armory's list (0x9F1CC0 builds it from component 29 into a fixed
 * 193-entry object; every reader uses its per-type starts at +0x1B24 and counts at +0x1B40) is rebuilt by HellForge into a
 * larger object whose entries start after those fields, and the game's pointer to it ([game.dll + 0x33269E8], set once
 * at 0x5683B5) is pointed there. The registered weapons are written to <session>/registered-items.json for the
 * backend's item catalogue and inventory. A weapon given text (HfWeapon.name) also gets its own record in component 76
 * (the encyclopedia: 56-byte records with text ids at +0 name, +0x08 upper-case name, +0x14 description, +0x24
 * designation; the armory shows designation + upper-case name, HD2Runtime game-text-api) naming text ids of its own, and
 * the engine's text function answers those ids with the mod's strings: its slot in the engine API table the game calls
 * ([[game.dll + 0x3326308] + 0x10] + 1000: const char* (u32 id), in helldivers2.exe's data) is pointed at HellForge's, which
 * passes every other id on. The slot, not game.dll's wrapper (0x17802E0), as the armory calls the engine directly.
 * The engine's network configuration ([[helldivers2.exe + 0x1A10268]], the plan's network_config) has the same entity
 * types, index-aligned with the short ids: arrays {u32 count, u32 capacity, data, allocator} at +0x28 (u32 own index),
 * +0x40 (a byte per type, zero) and +0x70 (80-byte entries {u32 short id, ...} sorted by short id, binary-searched by the engine,
 * exe 0x34B700: a missing type crashed the game when the weapon was equipped). Each registered weapon gets its base's
 * entry under its own short id, appended (RegisterNetworkTypes). Short ids, and with them the network types, are given
 * in the order of the entities' resource ids, not the mods' load order (WeaponOrder): a joining machine builds the
 * entity type another names by number, so friends who load the same mods in another order must number them alike
 * (2026-10-18: a friend's Pelican arrived on the others' machines as another mod's entity, sitting on the ground).
 * The armory's customization screen (list builder 0x1493770) finds a weapon's mastery package through its
 * WeaponCustomizationComponentData record (component 271, 4872-byte records, package id32 at +0x12FC, read by
 * 0x11F4F00) and matches the package's unlocks to the weapon's attachments by WeaponChild(attachment, weapon id); with
 * the base's record no unlock matched the new id and the screen crashed sorting empty entries (0x1493700). So every
 * weapon gets its own record naming its own package, MasteryPackage(base package), the id the backend gives the copy of
 * the base's package it serves (src/HellForge.LocalBackend/RegisteredItems.cs, the same derivation).
 * A weapon given a projectile (HfWeapon.projectile) gets its own ProjectileWeaponComponentData record (component 321,
 * 616-byte records) naming it at +0 (ProjType), and its customization record loses its default ammunition (slot 6):
 * the record starts with up to 10 default {u32 slot, u32 option} pairs (+0x00..+0x4F; the slots the player may change
 * follow at +0x50, and ammunition is never one of them), and an ammunition option's entity delta sets ProjType again
 * when the weapon is assembled (the Liberator's "5,5x50mm Full Metal Jacket" sets 276; HD2Runtime
 * active-projectile-sources). Without it the weapon fires what its record names, as weapons without ammunition do.
 * A weapon given another weapon's sounds (HfWeapon.sound_from) takes that weapon's firing-sound members into its own
 * ProjectileWeapon record (HD2Runtime weapon-sounds: +237 MIDI flag, +240..+283 MIDI timing, loop start/stop, single
 * shot (+260), fire mode 4/7 and secondary events, +528..+539 silenced events, +556 silenced flag). The engine frees those arrays through their allocator
 * (a 64 KiB-page allocator that reads its page header to size a block: a VirtualAlloc block crashed it at shutdown,
 * exe 0x630146), so the larger ones come from that allocator (vtable slot 7, exe 0x58EC0: void* (size, align)). Values are build 25480438's (game.dll 2E2C3B7C...). */

#define REG_STATE_GLOBAL 0x346BF98u
#define REG_ENTITY_LOADER 0xFDB440u
#define REG_ARMORY_BUILDER 0x9F1CC0u
#define REG_ARMORY_GLOBAL 0x33269E8u
#define REG_TABLES 0xF12478u
#define REG_MAP 0xF12EA0u
#define REG_LIST 0xF12EA8u
#define REG_MAP_SLOTS 4096u
#define REG_LOADOUT 29u
#define REG_LOADOUT_SLOTS 386u
#define REG_LOADOUT_RECORDS 193u
#define REG_LOADOUT_STRIDE 32u
#define REG_ARMORY_FIRST 219u /* the first entry after the armory list's count fields (0x1B24..0x1B5C) */
#define REG_ARMORY_TYPES 7
#define REG_MAX_WEAPONS 64
#define REG_UNIT 88u
#define REG_UNIT_STRIDE 136u
#define REG_MAX_OWNED 192 /* component tables HellForge keeps its own copies of (an own_all entity: every one it has) */
#define REG_MAX_OWN 8     /* components a weapon may own beyond its unit, text and loadout entry */
#define REG_TEXT_COMPONENT 76u
#define REG_TEXT_STRIDE 56u
#define REG_ENGINE_API 0x3326308u
#define REG_CUSTOMIZATION 271u
#define REG_CUSTOMIZATION_STRIDE 4872u
#define REG_MASTERY_PACKAGE_AT 0x12FCu
#define REG_DEFAULT_PAIRS 10
#define REG_AMMUNITION_SLOT 6u
#define REG_PROJECTILE_WEAPON 321u
#define REG_PROJECTILE_WEAPON_STRIDE 616u
#define REG_BEHAVIOUR 284u
#define REG_BEHAVIOUR_STRIDE 12u /* BehaviorComponentData: +0 behaviour id, +4 task cooldown, +8 task user */

static const uint8_t kLoaderPin[16] = {0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};
static const uint8_t kBuilderPin[16] = {0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x7c, 0x24, 0x10, 0x48, 0x8b, 0x05, 0xc7, 0xa2, 0xa7};

/* Index slot counts of component tables, from the modulo constant in each table's inlined lookup (game.dll scan; 70
 * and 91 divide by multiply-shift, 0x4F7C70 and 0x4FA390, which the scan missed). The rest
 * (from {10, 6} on) from a scan of every table's lookup loop bound (`cmp reg, slots; jb` after the table's load; 2026-10-18),
 * which agrees with every count above and finds each one among the component catalogue's table sizes: the barrage's
 * bombardment (95) and the silo missile (219) were refused for want of theirs. */
typedef struct SlotCount { uint16_t component, slots; } SlotCount;
static const SlotCount kSlots[] = {
    {0, 152}, {1, 114}, {3, 1122}, {5, 540}, {8, 38}, {12, 598}, {15, 58}, {22, 30}, {23, 154}, {25, 14}, {28, 1080},
    {29, 386}, {32, 324}, {35, 258}, {44, 14}, {47, 26}, {50, 298}, {55, 22}, {59, 350}, {61, 26}, {62, 42}, {64, 68},
    {69, 332}, {70, 18}, {76, 1330}, {77, 316}, {79, 118}, {81, 26}, {84, 176}, {86, 38}, {87, 232}, {88, 3382}, {90, 1056},
    {91, 40}, {92, 434},
    {93, 26}, {94, 30}, {96, 44}, {101, 150}, {102, 34}, {103, 594}, {104, 1376}, {105, 948}, {106, 180}, {107, 712},
    {110, 224}, {111, 1010}, {113, 498}, {117, 50}, {120, 150}, {121, 44}, {122, 180}, {124, 60}, {125, 1386}, {129, 310},
    {132, 28}, {133, 22}, {137, 378}, {138, 52}, {146, 114}, {147, 52}, {149, 44}, {150, 42}, {152, 22}, {155, 140},
    {156, 146}, {157, 50}, {158, 1520}, {159, 388}, {160, 240}, {161, 30}, {164, 230}, {174, 14}, {175, 66}, {177, 58},
    {182, 66}, {183, 116}, {184, 90}, {185, 14}, {188, 572}, {191, 626}, {192, 370}, {193, 44}, {196, 58}, {197, 74},
    {198, 544}, {203, 14}, {206, 188}, {207, 494}, {210, 42}, {211, 34}, {212, 38}, {218, 494}, {223, 126}, {224, 1002},
    {225, 28}, {230, 418}, {231, 26}, {233, 688}, {234, 590}, {235, 438}, {236, 730}, {241, 172}, {244, 1114}, {247, 38},
    {248, 146}, {253, 22}, {255, 142}, {257, 182}, {259, 92}, {260, 34}, {261, 28}, {262, 1606}, {263, 54}, {266, 58},
    {268, 22}, {270, 46}, {271, 382}, {273, 386}, {275, 30}, {279, 28}, {280, 390}, {282, 638}, {283, 324}, {284, 2008},
    {285, 46}, {286, 112}, {288, 208}, {289, 22}, {290, 1968}, {296, 120}, {297, 76}, {298, 46}, {299, 54}, {302, 316},
    {303, 398}, {304, 42}, {305, 42}, {307, 648}, {309, 122}, {310, 448}, {311, 408}, {315, 46}, {318, 472}, {321, 542},
    {322, 454},
    {10, 6}, {11, 10}, {18, 8}, {24, 16}, {31, 8}, {36, 4}, {45, 4}, {60, 12}, {68, 12}, {73, 6}, {74, 6}, {75, 18},
    {80, 18}, {85, 24}, {89, 96}, {95, 32}, {97, 12}, {98, 6}, {99, 6}, {108, 72}, {109, 4}, {114, 6}, {123, 16},
    {127, 4}, {130, 16}, {131, 32}, {135, 10}, {139, 4}, {140, 4}, {143, 4}, {144, 4}, {145, 12}, {153, 6}, {154, 4},
    {166, 8}, {168, 8}, {169, 10}, {173, 10}, {176, 16}, {179, 8}, {186, 6}, {187, 4}, {190, 80}, {195, 8}, {199, 16},
    {201, 20}, {202, 6}, {204, 20}, {205, 16}, {215, 4}, {216, 4}, {219, 18}, {221, 12}, {222, 4}, {228, 4}, {229, 12},
    {242, 64}, {245, 18}, {246, 12}, {251, 6}, {252, 6}, {258, 10}, {264, 6}, {265, 20}, {267, 18}, {272, 6}, {281, 4},
    {291, 8}, {293, 4}, {300, 36}, {301, 16}, {308, 4}, {313, 10}, {314, 12}, {317, 6}, {320, 20}, {323, 20}};

#include "component_sizes.h" /* kComponentShapes: every table's slots, records and record size (ComponentStride) */

typedef struct Weapon
{
    HfMod* mod;
    uint32_t base, id;
    uint64_t entity;
    int plain;           /* an entity of add_entity: no loadout record, armory entry, catalogue item or text */
    uint64_t baseEntity; /* add_entity's base */
    uint64_t unit;       /* add_entity: a unit resource of its own, or 0 for the base's */
    uint32_t behaviour;  /* add_entity: the behaviour it runs, or 0 for the base's */
    uint64_t modelFrom;  /* add_entity: the entity whose model (and model-tied records) it takes, or 0 */
    char name[192];
    int state; /* 0 pending, 1 registered, -1 refused, 2 a live set-up's, registered when the change is made (live_registry.c) */
    uint32_t text[4]; /* text ids of its own: name, upper-case name, description, designation (0: the base's) */
    uint32_t shortId, baseShortId;
    uint32_t projectile, baseProjectile;   /* HfWeapon.projectile, and what its base fires */
    uint32_t soundFrom;                    /* HfWeapon.sound_from */
    uint8_t* projectileRecord;             /* its own ProjectileWeapon record, when it has a projectile */
    int ownCount;
    uint32_t own[REG_MAX_OWN], ownStride[REG_MAX_OWN]; /* components it owns records of (HfWeapon.own_components) */
    int ownAll;          /* add_entity: a record of its own in every component of its base (HfEntity.own_all) */
    uint64_t copies;     /* the entity it was registered as a copy of (its map row's components are that entity's) */
    char title[96];      /* add_entity: its title (HfEntity.title), or "" */
    char designation[64]; /* add_entity: its designation (HfEntity.designation), or "" */
} Weapon;

/* Mod text by id, answered by the text lookup detour. */
typedef struct ModText { uint32_t id; char* text; } ModText;
static ModText g_texts[REG_MAX_WEAPONS * 4];
static int g_textCount;
static int g_textHooked;
static volatile LONG g_networkTypesAdded; /* 0, 1 being added, 2 done */
/* The network types RegisterNetworkTypes added, as the configuration numbered them, with the vanilla type each copies
 * and the two entities (netapply.c gives each its base's case in the network apply switch). */
typedef struct NetworkType { uint32_t index, base; uint64_t entity, baseEntity; const char* name; HfMod* mod; } NetworkType;
static NetworkType g_networkTypes[REG_MAX_WEAPONS];
static uint32_t g_networkTypeCount, g_networkVanillaTypes;
static uint8_t *g_netEntries, *g_netOwn, *g_netValues; /* the arrays it made (count + REG_MAX_WEAPONS each), once it has */

typedef void (*EntityLoaderFn)(void* state);
typedef void (*ArmoryBuilderFn)(void* list);

static Weapon g_weapons[REG_MAX_WEAPONS];
static int g_weaponCount;

/* The registered weapons and entities by resource id (hashed from their unit names): an order every machine with the
 * same mods shares, whatever order it loads them in. */
static void WeaponOrder(int order[REG_MAX_WEAPONS])
{
    for (int i = 0; i < g_weaponCount; i++) order[i] = i;
    for (int i = 1; i < g_weaponCount; i++)
        for (int j = i; j > 0 && g_weapons[order[j - 1]].entity > g_weapons[order[j]].entity; j--)
        {
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
}

static uintptr_t g_game;
static uint8_t* g_loadoutTable; /* HellForge's copy of component 29 (pointer to its index, as the game's) */
static uint32_t g_loadoutRecords;
static uint8_t* g_regList;      /* HellForge's copy of the short-id list (its 32-byte header first: room for REG_MAX_WEAPONS more) */
static uint32_t g_regListCount; /* its entries */
static uint8_t* g_armory;
/* HellForge's copies of component tables that registered weapons have records of their own in (besides component 29). */
/* from: the table the copy was made of (fromBytes after its header), which HellForge's addresses in it follow out of
 * (live_own.c HeldFollow). */
typedef struct OwnedTable { uint32_t component, stride, first, next, end; uint8_t* index; uint8_t* from; uint32_t fromBytes; } OwnedTable;
static OwnedTable g_owned[REG_MAX_OWNED];
static int g_ownedCount;
static EntityLoaderFn g_entityLoader;
static ArmoryBuilderFn g_armoryBuilder;
static int g_registryHooked;
static int g_registryActive; /* the hooks are in: weapons get added when the entity data loads */
static int ProjectileUsable(uint32_t type); /* projectiles.c */
static int BehaviourUsable(uint32_t id);    /* behaviours.c */
static void RememberUnitCopy(HfMod* mod, const char* name, uint64_t base, uint64_t copy); /* below: unit copies */

static uint32_t SlotsOf(uint32_t component)
{
    for (size_t i = 0; i < sizeof kSlots / sizeof kSlots[0]; i++)
        if (kSlots[i].component == component) return kSlots[i].slots;
    for (int i = 0; i < g_componentCount; i++)
        if (g_components[i].index == component) return g_components[i].indices;
    return 0;
}

/* Open-addressing tables keyed by a u64 at the start of each row. */
static uint8_t* FindRow(uint8_t* rows, uint32_t slots, uint32_t stride, uint64_t key)
{
    uint32_t at = (uint32_t)(key % slots);
    for (uint32_t i = 0; i < slots; i++)
    {
        uint64_t value;
        memcpy(&value, rows + (size_t)at * stride, 8);
        if (value == key) return rows + (size_t)at * stride;
        if (value == 0) return NULL;
        at = at + 1 == slots ? 0 : at + 1;
    }
    return NULL;
}

static uint8_t* FreeRow(uint8_t* rows, uint32_t slots, uint32_t stride, uint64_t key)
{
    uint32_t at = (uint32_t)(key % slots);
    for (uint32_t i = 0; i < slots; i++)
    {
        uint64_t value;
        memcpy(&value, rows + (size_t)at * stride, 8);
        if (value == 0) return rows + (size_t)at * stride;
        if (value == key) return NULL;
        at = at + 1 == slots ? 0 : at + 1;
    }
    return NULL;
}

static uint8_t* EntityState(void)
{
    uintptr_t state = 0;
    if (g_game == 0 || SafeRead(g_game + REG_STATE_GLOBAL, &state, 8)) return NULL;
    return (uint8_t*)state;
}

static uint8_t* StatePointer(uint8_t* state, uint32_t offset)
{
    uintptr_t value = 0;
    return state != NULL && !SafeRead((uintptr_t)state + offset, &value, 8) ? (uint8_t*)value : NULL;
}

static int SetStatePointer(uint8_t* state, uint32_t offset, const void* value)
{
    uintptr_t v = (uintptr_t)value;
    return ApiWrite((uintptr_t)state + offset, &v, 8);
}

/* The loadout table in use and its record count. */
static uint8_t* LoadoutTable(uint8_t* state, uint32_t* records)
{
    uint8_t* table = StatePointer(state, REG_TABLES + 8 * REG_LOADOUT);
    *records = table != NULL && table == g_loadoutTable ? g_loadoutRecords : REG_LOADOUT_RECORDS;
    return table;
}

/* An entity a mod added that was not registered (its base had a component HellForge could not copy): nothing may name
 * it, or the game looks it up when it spawns it and reads a missing row (a hellpod rack's slot, a payload, a mount). */
static int RefusedEntity(uint64_t entity)
{
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].entity == entity && g_weapons[i].state == -1) return 1;
    return 0;
}

static int PlainCopy(uint64_t entity)
{
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].plain && g_weapons[i].entity == entity) return 1;
    return 0;
}

/* The entity owning a loadout record (its only index row besides entity copies of it), or 0. */
static uint64_t LoadoutOwner(uint8_t* table, uint32_t record)
{
    uint64_t owner = 0;
    for (uint32_t i = 0; i < REG_LOADOUT_SLOTS; i++)
    {
        uint64_t entity;
        uint32_t r;
        memcpy(&entity, table + 16 * i, 8);
        memcpy(&r, table + 16 * i + 8, 4);
        if (entity != 0 && r == record && !PlainCopy(entity)) { if (owner != 0) return 0; owner = entity; }
    }
    return owner;
}

static int StratagemItems(char* text, int n, int size, int first);
static int DifficultyItems(char* text, int n, int size, int first);

static void WriteRegisteredItems(void)
{
    if (g_logs[0] == 0) return;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\registered-items.json", g_logs);
    char text[16384];
    int n = snprintf(text, sizeof text, "{\"format\":1,\"items\":[");
    int listed = 0;
    for (int i = 0; i < g_weaponCount && n < (int)sizeof text - 512; i++)
        if (!g_weapons[i].plain && g_weapons[i].state != 2 && !LiveTomb(LC_WEAPON, (uint32_t)i))
            n += snprintf(text + n, sizeof text - n, "%s{\"mixId\":%u,\"copyOf\":%u,\"mod\":\"%s\",\"unit\":\"%s\"}", listed++ ? "," : "",
                g_weapons[i].id, g_weapons[i].base, g_weapons[i].mod->id, g_weapons[i].name);
    int before = n;
    n = StratagemItems(text, n, (int)sizeof text, listed == 0);
    n = DifficultyItems(text, n, (int)sizeof text, listed == 0 && n == before);
    n += snprintf(text + n, sizeof text - n, "]}\n");
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, text, (DWORD)n, &written, NULL);
    CloseHandle(file);
}

/* The client's id of a weapon's child item (an attachment for that weapon), and its inverse (RegisteredItems.cs). */
#define REG_CHILD_M 0x5BD1E995u
static uint32_t WeaponChild(uint32_t attachment, uint32_t weapon)
{
    uint32_t k = attachment * REG_CHILD_M;
    k ^= k >> 24;
    return k * REG_CHILD_M ^ weapon * REG_CHILD_M;
}

static uint32_t Rehome(uint32_t child, uint32_t from, uint32_t to)
{
    uint32_t inverse = REG_CHILD_M;
    for (int i = 0; i < 5; i++) inverse *= 2 - REG_CHILD_M * inverse;
    uint32_t k = (child ^ from * REG_CHILD_M) * inverse;
    k ^= k >> 24;
    return WeaponChild(k * inverse, to);
}

static const Component* ComponentNamed(const char* name)
{
    for (int i = 0; i < g_componentCount && name != NULL; i++)
        if (strcmp(g_components[i].name, name) == 0) return &g_components[i];
    return NULL;
}

static uint32_t AddText(const char* text, const char* key, uint32_t salt);

static uint32_t ApiAddWeapon(HfMod* mod, const HfWeapon* def)
{
    if (mod == NULL || def == NULL || def->size < offsetof(HfWeapon, own_components) + sizeof def->own_components)
    {
        ApiLog(mod, "add_weapon: no weapon, or its size is not set (HfWeapon w = {sizeof w})");
        return 0;
    }
    const char* unit = def->unit;
    if (unit == NULL || unit[0] == 0 || strlen(unit) >= sizeof g_weapons[0].name) { ApiLog(mod, "add_weapon: it needs a unit name (under 192 bytes)"); return 0; }
    int live = g_liveSetup != NULL && g_liveSetup == mod; /* a mod set up while the game runs (live_registry.c) */
    if ((g_fired[HF_EVENT_ENTITIES_READY] && !live) || g_weaponCount >= REG_MAX_WEAPONS)
    {
        ApiLog(mod, "add_weapon(%s): too late or too many weapons; call it from hellforge_mod_init", unit);
        return 0;
    }
    uint64_t entity = Murmur64A(unit, strlen(unit));
    uint32_t id = (uint32_t)(entity >> 32);
    if (id == 0) id = 1;
    for (int i = 0; i < g_weaponCount; i++)
        if (!LiveGone(LC_WEAPON, (uint32_t)i) && (g_weapons[i].entity == entity || g_weapons[i].id == id))
        {
            ApiLog(mod, "add_weapon(%s): already added by %s", unit, g_weapons[i].mod->id);
            return 0;
        }
    Weapon w = {0};
    w.mod = mod;
    w.base = def->base;
    w.id = id;
    w.entity = entity;
    snprintf(w.name, sizeof w.name, "%s", unit);
    for (const char* const* own = def->own_components; own != NULL && *own != NULL; own++)
    {
        const Component* c = ComponentNamed(*own);
        if (c == NULL || SlotsOf(c->index) == 0 || w.ownCount >= REG_MAX_OWN)
        {
            ApiLog(mod, "add_weapon(%s): can't give it a record of its own in %s (%s)", unit, *own,
                c == NULL ? "HellForge does not know that component; see docs/sdk-reference.md" : "too many components");
            return 0;
        }
        w.own[w.ownCount] = c->index;
        w.ownStride[w.ownCount++] = c->stride;
    }
    if (def->size >= offsetof(HfWeapon, projectile) + sizeof def->projectile && def->projectile != 0)
    {
        if (!ProjectileUsable(def->projectile))
        {
            ApiLog(mod, "add_weapon(%s): projectile %u is neither a vanilla type (1 .. 350) nor one a mod added", unit, def->projectile);
            return 0;
        }
        w.projectile = def->projectile;
    }
    if (def->size >= offsetof(HfWeapon, sound_from) + sizeof def->sound_from) w.soundFrom = def->sound_from;
    if (def->name != NULL && def->name[0] != 0)
    {
        char upper[192];
        size_t n = 0;
        for (; def->name[n] && n + 1 < sizeof upper; n++) upper[n] = (def->name[n] >= 'a' && def->name[n] <= 'z') ? (char)(def->name[n] - 32) : def->name[n];
        upper[n] = 0;
        char key[256];
        snprintf(key, sizeof key, "%s#text", unit);
        w.text[0] = AddText(def->name, key, 0x01);
        w.text[1] = AddText(upper, key, 0x02);
        w.text[2] = AddText(def->description, key, 0x03);
        w.text[3] = AddText(def->designation, key, 0x04);
    }
    w.state = live ? 2 : 0;
    g_weapons[g_weaponCount] = w;
    MemoryBarrier(); /* the entry is whole before the count shows it */
    g_weaponCount++;
    ApiLog(mod, "adding weapon %s \"%s%s%s\" (entity %016llx, loadout id %08x) as a copy of loadout item %08x, %d record(s) of its own",
        unit, def->designation ? def->designation : "", def->designation ? " " : "", def->name ? def->name : "(the base's name)",
        (unsigned long long)entity, id, def->base, w.ownCount);
    if (!live) WriteRegisteredItems(); /* a live change writes it once made */
    return id;
}

/* A new entity of any kind (SDK 4, HfEntity): a copy of a vanilla entity under the mod's name, sharing its component
 * records except the ones it owns (own_components) and, given a unit, its unit record. No loadout, armory or catalogue
 * entry: a Pelican, a turret... It is added with the weapons when the entity data loads (RegisterWeapons). */
static uint64_t ApiAddEntity(HfMod* mod, const HfEntity* def)
{
    if (mod == NULL || def == NULL || def->size < offsetof(HfEntity, own_components) + sizeof def->own_components)
    {
        ApiLog(mod, "add_entity: no entity, or its size is not set (HfEntity e = {sizeof e})");
        return 0;
    }
    char identity[256];
    const char* name = Identity(mod, def->name, identity, sizeof identity);
    if (name == NULL || name[0] == 0 || strlen(name) >= sizeof g_weapons[0].name) { ApiLog(mod, "add_entity: it needs a name (under 192 bytes)"); return 0; }
    int live = g_liveSetup != NULL && g_liveSetup == mod; /* a mod set up while the game runs (live_registry.c) */
    if ((g_fired[HF_EVENT_ENTITIES_READY] && !live) || g_weaponCount >= REG_MAX_WEAPONS)
    {
        ApiLog(mod, "add_entity(%s): too late or too many entities; call it from hellforge_mod_init", name);
        return 0;
    }
    if (def->base == 0) { ApiLog(mod, "add_entity(%s): it needs a base entity", name); return 0; }
    uint64_t entity = Murmur64A(name, strlen(name));
    for (int i = 0; i < g_weaponCount; i++)
        if (!LiveGone(LC_WEAPON, (uint32_t)i) && g_weapons[i].entity == entity) { ApiLog(mod, "add_entity(%s): already added by %s", name, g_weapons[i].mod->id); return 0; }
    Weapon w = {0};
    w.mod = mod;
    w.plain = 1;
    w.baseEntity = def->base;
    w.entity = entity;
    w.id = (uint32_t)(entity >> 32);
    snprintf(w.name, sizeof w.name, "%s", name);
    if (def->unit != NULL && def->unit[0] != 0) w.unit = Murmur64A(def->unit, strlen(def->unit));
    if (def->size >= offsetof(HfEntity, behaviour) + sizeof def->behaviour) w.behaviour = def->behaviour;
    if (def->size >= offsetof(HfEntity, model_from) + sizeof def->model_from) w.modelFrom = def->model_from;
    if (def->size >= offsetof(HfEntity, own_all) + sizeof def->own_all) w.ownAll = def->own_all != 0;
    if (def->size >= offsetof(HfEntity, description) + sizeof def->description)
    {
        char key[256];
        snprintf(key, sizeof key, "%s#text", name);
        if (def->title != NULL && def->title[0])
        {
            char upper[192];
            size_t n = 0;
            for (; def->title[n] && n + 1 < sizeof upper; n++) upper[n] = (def->title[n] >= 'a' && def->title[n] <= 'z') ? (char)(def->title[n] - 32) : def->title[n];
            upper[n] = 0;
            w.text[0] = AddText(def->title, key, 0x01);
            w.text[1] = AddText(upper, key, 0x02);
            snprintf(w.title, sizeof w.title, "%s", def->title);
        }
        w.text[2] = AddText(def->description, key, 0x03);
        w.text[3] = AddText(def->designation, key, 0x04);
        if (def->designation != NULL) snprintf(w.designation, sizeof w.designation, "%s", def->designation);
    }
    for (const char* const* own = def->own_components; own != NULL && *own != NULL; own++)
    {
        const Component* c = ComponentNamed(*own);
        if (c == NULL || SlotsOf(c->index) == 0 || w.ownCount >= REG_MAX_OWN)
        {
            ApiLog(mod, "add_entity(%s): can't give it a record of its own in %s (%s)", name, *own,
                c == NULL ? "HellForge does not know that component; see docs/sdk-reference.md" : "too many components");
            return 0;
        }
        w.own[w.ownCount] = c->index;
        w.ownStride[w.ownCount++] = c->stride;
    }
    w.state = live ? 2 : 0;
    g_weapons[g_weaponCount] = w;
    MemoryBarrier(); /* the entry is whole before the count shows it */
    g_weaponCount++;
    if (w.ownAll) ApiLog(mod, "adding entity %s (%016llx) as a copy of %016llx with records of its own in every component", name,
        (unsigned long long)entity, (unsigned long long)def->base);
    else ApiLog(mod, "adding entity %s (%016llx) as a copy of %016llx, %d record(s) of its own", name, (unsigned long long)entity,
        (unsigned long long)def->base, w.ownCount);
    return entity;
}

static uint64_t ApiWeaponEntity(uint32_t weapon)
{
    for (int i = 0; i < g_weaponCount; i++)
        if (!g_weapons[i].plain && g_weapons[i].id == weapon && g_weapons[i].state != -1) return g_weapons[i].entity;
    return 0;
}

/* component_record for a registered weapon: its own record of the component, or 0 when it shares the base's. */
static uintptr_t RegisteredRecord(const Component* c, uint64_t resource, uint32_t* stride)
{
    int registered = 0;
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].entity == resource && g_weapons[i].state == 1) registered = 1;
    if (!registered) return 0;
    uint8_t* table = StatePointer(EntityState(), REG_TABLES + 8 * c->index);
    uint32_t slots = SlotsOf(c->index);
    for (int i = 0; i < g_ownedCount; i++)
    {
        OwnedTable* owned = &g_owned[i];
        if (owned->component != c->index || owned->index != table) continue;
        uint8_t* row = FindRow(owned->index, slots, 16, resource);
        uint32_t record;
        if (row == NULL) return 0;
        memcpy(&record, row + 8, 4);
        if (record < owned->first || record >= owned->next) return 0;
        if (stride != NULL) *stride = owned->stride;
        return (uintptr_t)(owned->index + (size_t)slots * 16 + (size_t)record * owned->stride);
    }
    return 0;
}

/* An entity a mod added that shares its base's records: it is meant to behave like its base, so its sharing a record
 * does not make the record shared for an edit of the base (the copy changes with it). */
static int CopiesBase(uint64_t entity)
{
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].entity == entity && g_weapons[i].state != -1) return 1;
    return 0;
}

/* An entity's record in the component table the game reads now: HellForge's copy when the registry moved the table
 * (so a vanilla entity's record there is the one the game uses, not the original in the loaded region), else the game's
 * own. Only the entity's one index row counts, and a record other entities share only with `shared`. *used 0 when the
 * entity state is not there (no game.dll: the caller falls back to the profile's region). */
static uintptr_t CurrentTableRecord(const Component* c, uint64_t resource, uint32_t* stride, int shared, int* used)
{
    *used = 0;
    uint8_t* table = StatePointer(EntityState(), REG_TABLES + 8 * c->index);
    if (table == NULL || c->indices == 0 || c->recordOffset != c->indices * 16u) return 0;
    *used = 1;
    const OwnedTable* owned = NULL;
    for (int i = 0; i < g_ownedCount; i++) if (g_owned[i].component == c->index && g_owned[i].index == table) owned = &g_owned[i];
    if (owned == NULL)
    {
        uint8_t header[32];
        if (SafeRead((uintptr_t)table - 32, header, 32) || memcmp(header, &c->index, 4) || memcmp(header + 4, c->header, 28)) return 0;
    }
    uint32_t limit = owned != NULL ? owned->next : c->records;
    int64_t selected = -1;
    int owners = 0;
    for (uint32_t row = 0; row < c->indices; row++)
    {
        uint8_t entry[16];
        uint64_t id;
        uint32_t record, flags;
        if (SafeRead((uintptr_t)table + row * 16u, entry, 16)) return 0;
        memcpy(&id, entry, 8); memcpy(&record, entry + 8, 4); memcpy(&flags, entry + 12, 4);
        if (id == resource) { if (flags) return 0; owners++; selected = record; }
    }
    if (owners != 1 || selected >= limit) return 0;
    if (!shared)
        for (uint32_t row = 0, sharers = 0; row < c->indices; row++)
        {
            uint8_t entry[16];
            uint64_t id;
            uint32_t record;
            if (SafeRead((uintptr_t)table + row * 16u, entry, 16)) return 0;
            memcpy(&id, entry, 8); memcpy(&record, entry + 8, 4);
            if (id != 0 && id != resource && record == (uint32_t)selected && !CopiesBase(id) && ++sharers > 0) return 0;
        }
    if (stride != NULL) *stride = c->stride;
    return (uintptr_t)table + (uintptr_t)c->indices * 16u + (uintptr_t)selected * c->stride;
}

/* The record size of component table c, for a copy that owns all its base's records (HfEntity.own_all), or 0 when it
 * is not known for certain: the profile's component, else the table's shape among every table of the build's entity data
 * (component_sizes.h), its index slots and its size (index rows, then whole records padded to 16 bytes; a table with
 * more after its records matches none). When several record sizes fit, the one whose record count is the number of
 * records the index rows reach. */
static uint32_t ComponentStride(uint8_t* state, uint32_t c)
{
    for (int i = 0; i < g_ownedCount; i++) if (g_owned[i].component == c) return g_owned[i].stride;
    uint32_t slots = SlotsOf(c), bytes = 0;
    uint8_t* table = StatePointer(state, REG_TABLES + 8 * c);
    if (slots == 0 || table == NULL || SafeRead((uintptr_t)table - 12, &bytes, 4) || bytes < slots * 16u) return 0;
    uint64_t recordBytes = bytes - slots * 16u;
    #define FITS(records, size) ((uint64_t)(records) * (size) <= recordBytes && recordBytes - (uint64_t)(records) * (size) < 16)
    for (int i = 0; i < g_componentCount; i++)
        if (g_components[i].index == c && g_components[i].stride > 0)
            return g_components[i].recordOffset == slots * 16u && FITS(g_components[i].records, g_components[i].stride) ? g_components[i].stride : 0;
    /* The table's own size by its index (the build's component catalogue), when its slots and bytes agree. */
    for (size_t i = 0; i < sizeof kComponentSizes / sizeof kComponentSizes[0]; i++)
        if (kComponentSizes[i].component == c)
            return kComponentSizes[i].slots == slots && FITS(kComponentSizes[i].records, kComponentSizes[i].size) ? kComponentSizes[i].size : 0;
    /* Else by shape. Not exact: a table whose last records no row names fits a larger size (WeaponLinker, four 12-byte
     * records, three named, fits three of 16). */
    uint32_t reached = 0;
    for (uint32_t row = 0; row < slots; row++)
    {
        uint64_t id;
        uint32_t record;
        memcpy(&id, table + 16 * row, 8);
        memcpy(&record, table + 16 * row + 8, 4);
        if (id != 0 && record + 1 > reached) reached = record + 1;
    }
    uint32_t fits = 0, reaches = 0; /* the size that fits (UINT32_MAX: several), and among those the records reach */
    for (size_t i = 0; i < sizeof kComponentShapes / sizeof kComponentShapes[0]; i++)
    {
        const ComponentShape* s = &kComponentShapes[i];
        if (s->slots != slots || !FITS(s->records, s->size)) continue;
        fits = fits == 0 || fits == s->size ? s->size : UINT32_MAX;
        if (s->records == reached) reaches = reaches == 0 || reaches == s->size ? s->size : UINT32_MAX;
    }
    #undef FITS
    if (fits != UINT32_MAX) return fits;
    return reaches != UINT32_MAX ? reaches : 0;
}

/* A record of the weapon's own in component table c: HellForge's copy of the table (made once per load: the vanilla table,
 * then room for REG_MAX_WEAPONS records after every vanilla byte), a copy of the base's record placed after the vanilla
 * records, and the weapon's index row naming it. Returns the new record (or the one it has already), or NULL. */
static uint8_t* OwnRecord(uint8_t* state, uint32_t c, uint32_t stride, uint64_t baseEntity, uint64_t entity)
{
    uint32_t slots = SlotsOf(c);
    uint8_t* table = StatePointer(state, REG_TABLES + 8 * c);
    if (slots == 0 || table == NULL) return NULL;
    OwnedTable* owned = NULL;
    for (int i = 0; i < g_ownedCount; i++) if (g_owned[i].component == c && g_owned[i].index == table) owned = &g_owned[i];
    if (owned == NULL)
    {
        if (g_ownedCount >= REG_MAX_OWNED) return NULL;
        uint32_t bytes;
        memcpy(&bytes, table - 12, 4); /* the table's size after its 28-byte header */
        uint32_t recordBytes = bytes > slots * 16u ? bytes - slots * 16u : 0;
        uint32_t first = (recordBytes + stride - 1) / stride; /* the first record index after every vanilla byte */
        size_t size = 32 + (size_t)slots * 16 + ((size_t)first + REG_MAX_WEAPONS) * stride;
        uint8_t* copy = (uint8_t*)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (copy == NULL) return NULL;
        memcpy(copy, table - 32, 32 + (size_t)bytes);
        owned = &g_owned[g_ownedCount++];
        *owned = (OwnedTable){c, stride, first, first, first + REG_MAX_WEAPONS, copy + 32, table, bytes};
        SetStatePointer(state, REG_TABLES + 8 * c, owned->index);
        Note("registry: component %u copied (%u slots, %u-byte records, new records from %u)", c, slots, stride, first);
    }
    uint8_t* records = owned->index + (size_t)slots * 16;
    uint8_t* baseRow = FindRow(owned->index, slots, 16, baseEntity);
    uint8_t* row = FindRow(owned->index, slots, 16, entity);
    if (baseRow == NULL || row == NULL || owned->stride != stride) return NULL;
    uint32_t mine;
    memcpy(&mine, row + 8, 4);
    if (baseEntity != entity && mine >= owned->first && mine < owned->next) return records + (size_t)mine * stride; /* its own already */
    if (owned->next >= owned->end) return NULL;
    uint32_t baseRecord, record = owned->next++;
    memcpy(&baseRecord, baseRow + 8, 4);
    memcpy(records + (size_t)record * stride, records + (size_t)baseRecord * stride, stride);
    memcpy(row + 8, &record, 4);
    return records + (size_t)record * stride;
}

/* The engine's text function; any further register arguments are passed on untouched. */
typedef const char* (*EngineTextFn)(uint32_t id, void* a, void* b, void* c);
static EngineTextFn g_engineText;

/* The engine's text function with the mods' text first. */
static const char* TextDetour(uint32_t id, void* a, void* b, void* c)
{
    for (int i = g_textCount - 1; i >= 0; i--) if (g_texts[i].id == id) return g_texts[i].text; /* the newest: a live change's new version */
    return g_engineText != NULL ? g_engineText(id, a, b, c) : "";
}

/* Points the engine API's text slot at TextDetour once the API is set up (retried until then). */
static void HookText(void)
{
    if (g_textHooked || g_textCount == 0 || g_game == 0) return;
    uintptr_t api = 0, table = 0, current = 0;
    if (SafeRead(g_game + REG_ENGINE_API, &api, 8) || api == 0 || SafeRead(api + 0x10, &table, 8) || table == 0 ||
        SafeRead(table + 1000, &current, 8) || current == 0) return;
    uintptr_t exe = (uintptr_t)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS64* nt = (IMAGE_NT_HEADERS64*)(exe + ((IMAGE_DOS_HEADER*)exe)->e_lfanew);
    size_t exeSize = nt->OptionalHeader.SizeOfImage;
    g_textHooked = 1;
    if (current == (uintptr_t)TextDetour) return;
    if (current < exe || current >= exe + exeSize) { Note("registry: the engine's text function is not the reviewed one; mod text not shown (%d)", g_textCount); return; }
    g_engineText = (EngineTextFn)current;
    void* detour = (void*)TextDetour;
    if (ApiWrite(table + 1000, &detour, 8)) { g_textHooked = 0; return; }
    Note("registry: engine text function hooked (%d mod text(s), exe+0x%llx)", g_textCount, (unsigned long long)(current - exe));
}

static uint32_t AddText(const char* text, const char* key, uint32_t salt)
{
    if (text == NULL || text[0] == 0 || g_textCount >= (int)(sizeof g_texts / sizeof g_texts[0])) return 0;
    size_t length = strlen(text);
    char* copy = (char*)malloc(length + 1);
    if (copy == NULL) return 0;
    memcpy(copy, text, length + 1);
    uint32_t id = (uint32_t)(Murmur64A(key, strlen(key)) >> 32) ^ salt;
    if (id == 0) id = salt;
    g_texts[g_textCount] = (ModText){id, copy};
    MemoryBarrier(); /* the game thread looks texts up while a live change adds them (TextDetour) */
    g_textCount++;
    return id;
}

/* Where RegisterWeapons (or a live change's registration: live_registry.c) adds weapons: the entity state, map and
 * loadout table in use (HellForge's copy), the short-id list being appended to (its 32-byte header first), its entries
 * and the last short id before this pass. late: the game runs meanwhile, so a row's key goes in after the rest of it. */
typedef struct RegCtx
{
    uint8_t *state, *map, *index, *records, *list;
    uint32_t recordCount, listCount, appended, lastShort;
    int late;
} RegCtx;

/* A row of the game's entity data (the key its first 8 bytes): at once while the entity data loads; for a live change
 * the rest first, then the key that makes it found. */
static void RegisterRow(const RegCtx* ctx, uint8_t* row, const uint8_t* bytes, size_t size)
{
    if (!ctx->late) { ApiWrite((uintptr_t)row, bytes, size); return; }
    ApiWrite((uintptr_t)row + 8, bytes + 8, size - 8);
    ApiWrite((uintptr_t)row, bytes, 8);
}

/* One weapon's registration as it goes: its base (loadout record, entity and map row, whose components it shares), the
 * rows it takes, and its own records the later steps fill. */
typedef struct RegJob
{
    Weapon* w;
    int baseRecord;
    uint64_t baseEntity;
    uint8_t *baseRow, *mapRow, *loadoutRow, *customization, *fireRecord;
    uint16_t* components;
    uint64_t componentCount;
} RegJob;

/* The base: its loadout record and the entity that owns it (an entity of add_entity names its base itself), and every
 * component checked first, so a weapon is added completely or not at all. 1 when it can be added. */
static int RegisterBase(RegCtx* ctx, RegJob* job)
{
    Weapon* w = job->w;
    job->baseRecord = w->plain ? 0 : -1;
    for (uint32_t r = 0; r < ctx->recordCount && !w->plain; r++)
    {
        uint32_t id;
        memcpy(&id, ctx->records + (size_t)r * REG_LOADOUT_STRIDE, 4);
        if (id == w->id) { ApiLog(w->mod, "%s: loadout id %08x is taken; not registered", w->name, w->id); job->baseRecord = -2; break; }
        if (id == w->base && job->baseRecord == -1) job->baseRecord = (int)r;
    }
    if (job->baseRecord < 0) { if (job->baseRecord == -1) ApiLog(w->mod, "%s: no loadout item %08x to copy; not registered", w->name, w->base); return 0; }
    job->baseEntity = w->plain ? w->baseEntity : LoadoutOwner(ctx->index, (uint32_t)job->baseRecord);
    job->baseRow = job->baseEntity ? FindRow(ctx->map, REG_MAP_SLOTS, 32, job->baseEntity) : NULL;
    if (job->baseRow == NULL) { ApiLog(w->mod, "%s: the base's entity was not found; not registered", w->name); return 0; }
    if (FindRow(ctx->map, REG_MAP_SLOTS, 32, w->entity) != NULL) { ApiLog(w->mod, "%s: entity %016llx exists already; not registered", w->name, (unsigned long long)w->entity); return 0; }
    memcpy(&job->components, job->baseRow + 8, 8);
    memcpy(&job->componentCount, job->baseRow + 16, 8);
    w->copies = job->baseEntity;
    for (uint64_t k = 0; k < job->componentCount; k++)
    {
        uint32_t c = job->components[k];
        uint8_t* t = c == REG_LOADOUT ? NULL : StatePointer(ctx->state, REG_TABLES + 8 * c);
        uint32_t slots = SlotsOf(c);
        if (t == NULL) continue; /* the loadout entry, or a component without data */
        if (slots != 0 && FindRow(t, slots, 16, job->baseEntity) != NULL && FindRow(t, slots, 16, w->entity) == NULL) continue;
        ApiLog(w->mod, "%s: component %u of the base is not understood (slots %u); not registered", w->name, c, slots);
        return 0;
    }
    job->mapRow = FreeRow(ctx->map, REG_MAP_SLOTS, 32, w->entity);
    job->loadoutRow = !w->plain ? FreeRow(ctx->index, REG_LOADOUT_SLOTS, 16, w->entity) : NULL;
    if (job->mapRow != NULL && (w->plain || job->loadoutRow != NULL) && ctx->recordCount < REG_LOADOUT_RECORDS + REG_MAX_WEAPONS) return 1;
    ApiLog(w->mod, "%s: no free row; not registered", w->name);
    return 0;
}

/* Its rows in the base's component tables, naming the base's records. */
static void RegisterShared(RegCtx* ctx, const RegJob* job)
{
    for (uint64_t k = 0; k < job->componentCount; k++)
    {
        uint32_t c = job->components[k];
        uint8_t* t = c == REG_LOADOUT ? NULL : StatePointer(ctx->state, REG_TABLES + 8 * c);
        if (t == NULL) continue;
        uint32_t slots = SlotsOf(c);
        uint8_t* row = FindRow(t, slots, 16, job->baseEntity);
        uint8_t* slot = FreeRow(t, slots, 16, job->w->entity);
        uint8_t entry[16];
        memcpy(entry, &job->w->entity, 8);
        memcpy(entry + 8, row + 8, 4);
        memset(entry + 12, 0, 4);
        RegisterRow(ctx, slot, entry, 16);
    }
}

/* Whether a loadout record has this id. */
static int LoadoutIdTaken(const RegCtx* ctx, uint32_t id)
{
    for (uint32_t r = 0; r < ctx->recordCount; r++)
        if (!memcmp(ctx->records + (size_t)r * REG_LOADOUT_STRIDE, &id, 4)) return 1;
    return 0;
}

/* An entity copy of a weapon (an EAT or a targeting remote a mod's hellpod rack delivers): a loadout record of its own,
 * its base's under the copy's id (its entity's top 32 bits), so the game resolves the item it picks up to the copy (the
 * armory object maps a loadout id to its one entity): with its base's shared record, a picked-up silo remote copy never
 * reached the player's hands (2026-10-18). */
static void RegisterPlainLoadout(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    uint8_t* baseLoadout = FindRow(ctx->index, REG_LOADOUT_SLOTS, 16, job->baseEntity);
    uint8_t* own = baseLoadout != NULL ? FreeRow(ctx->index, REG_LOADOUT_SLOTS, 16, w->entity) : NULL;
    if (own == NULL) return;
    int taken = LoadoutIdTaken(ctx, w->id);
    if (taken || ctx->recordCount >= REG_LOADOUT_RECORDS + REG_MAX_WEAPONS)
    {
        ApiLog(w->mod, "%s: no loadout record of its own (%s); the game may not hand it to a player", w->name, taken ? "its id is taken" : "no room");
        return;
    }
    uint32_t baseLoadoutRecord, mine = ctx->recordCount++;
    memcpy(&baseLoadoutRecord, baseLoadout + 8, 4);
    memcpy(ctx->records + (size_t)mine * REG_LOADOUT_STRIDE, ctx->records + (size_t)baseLoadoutRecord * REG_LOADOUT_STRIDE, REG_LOADOUT_STRIDE);
    memcpy(ctx->records + (size_t)mine * REG_LOADOUT_STRIDE, &w->id, 4);
    memcpy(own + 8, &mine, 4);
    memset(own + 12, 0, 4);
    MemoryBarrier();
    memcpy(own, &w->entity, 8);
    ApiLog(w->mod, "%s: a loadout record of its own (%u, id %08x; its base's is %u)", w->name, mine, w->id, baseLoadoutRecord);
}

/* Its own unit record: the base's, naming the weapon's unit (an entity of add_entity: only when given one). */
static void RegisterUnit(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    if (w->plain && w->unit == 0) return;
    uint8_t* unit = OwnRecord(ctx->state, REG_UNIT, REG_UNIT_STRIDE, job->baseEntity, w->entity);
    uint64_t model = w->plain ? w->unit : w->entity, vanilla;
    if (unit == NULL) { ApiLog(w->mod, "%s: could not give it its own unit record; it uses the base's model", w->name); return; }
    memcpy(&vanilla, unit, 8);
    memcpy(unit, &model, 8);
    ApiLog(w->mod, "%s: its own unit record (was %016llx, now %016llx)", w->name, (unsigned long long)vanilla, (unsigned long long)model);
    RememberUnitCopy(w->mod, w->name, vanilla, model);
}

/* Its own customization record, naming its own mastery package. */
static void RegisterCustomization(RegCtx* ctx, RegJob* job)
{
    Weapon* w = job->w;
    uint8_t* table = StatePointer(ctx->state, REG_TABLES + 8 * REG_CUSTOMIZATION);
    if (w->plain || table == NULL || FindRow(table, SlotsOf(REG_CUSTOMIZATION), 16, job->baseEntity) == NULL) return;
    job->customization = OwnRecord(ctx->state, REG_CUSTOMIZATION, REG_CUSTOMIZATION_STRIDE, job->baseEntity, w->entity);
    if (job->customization == NULL) { ApiLog(w->mod, "%s: could not give it its own customization record; customizing it may crash", w->name); return; }
    uint32_t package = 0;
    memcpy(&package, job->customization + REG_MASTERY_PACKAGE_AT, 4);
    if (package == 0) return;
    uint32_t own = Rehome(package, w->base, w->id);
    memcpy(job->customization + REG_MASTERY_PACKAGE_AT, &own, 4);
    ApiLog(w->mod, "%s: its own customization record (mastery package %u, the base's %u)", w->name, own, package);
}

/* The ProjectileWeapon record of the entity owning a loadout id, or NULL. */
static const uint8_t* FireRecordOf(const RegCtx* ctx, uint32_t loadoutId)
{
    uint8_t* weaponTable = StatePointer(ctx->state, REG_TABLES + 8 * REG_PROJECTILE_WEAPON);
    for (uint32_t r = 0; r < ctx->recordCount && weaponTable != NULL; r++)
    {
        if (memcmp(ctx->records + (size_t)r * REG_LOADOUT_STRIDE, &loadoutId, 4)) continue;
        uint64_t owner = LoadoutOwner(ctx->index, r);
        uint8_t* row = owner ? FindRow(weaponTable, SlotsOf(REG_PROJECTILE_WEAPON), 16, owner) : NULL;
        if (row == NULL) continue;
        uint32_t record;
        memcpy(&record, row + 8, 4);
        return weaponTable + (size_t)SlotsOf(REG_PROJECTILE_WEAPON) * 16 + (size_t)record * REG_PROJECTILE_WEAPON_STRIDE;
    }
    return NULL;
}

/* The ammunition slot out of its customization record's defaults, so no default ammunition names another projectile
 * (an ammunition option's delta sets ProjType again when the weapon is assembled). The pairs removed. */
static int RegisterNoAmmunition(uint8_t* customization)
{
    if (customization == NULL) return 0;
    uint32_t pairs[REG_DEFAULT_PAIRS * 2], kept = 0;
    int removed = 0;
    memcpy(pairs, customization, sizeof pairs);
    for (int k = 0; k < REG_DEFAULT_PAIRS; k++)
    {
        if (pairs[2 * k] == REG_AMMUNITION_SLOT) { removed++; continue; }
        memcpy(customization + 8 * kept++, pairs + 2 * k, 8);
    }
    memset(customization + 8 * kept, 0, 8 * (REG_DEFAULT_PAIRS - kept));
    return removed;
}

/* Its own ProjectileWeapon record, for its own projectile or another weapon's firing sounds. */
static void RegisterFiring(RegCtx* ctx, RegJob* job)
{
    Weapon* w = job->w;
    if (w->projectile == 0 && w->soundFrom == 0) return;
    uint8_t* weaponTable = StatePointer(ctx->state, REG_TABLES + 8 * REG_PROJECTILE_WEAPON);
    job->fireRecord = weaponTable != NULL && FindRow(weaponTable, SlotsOf(REG_PROJECTILE_WEAPON), 16, job->baseEntity) != NULL
        ? OwnRecord(ctx->state, REG_PROJECTILE_WEAPON, REG_PROJECTILE_WEAPON_STRIDE, job->baseEntity, w->entity) : NULL;
    if (job->fireRecord == NULL) { ApiLog(w->mod, "%s: its base fires no projectiles; its projectile and sounds stay the base's", w->name); return; }
    if (w->soundFrom != 0)
    {
        const uint8_t* donor = FireRecordOf(ctx, w->soundFrom);
        static const uint16_t blocks[4][2] = {{237, 1}, {240, 44}, {528, 12}, {556, 1}};
        uint32_t single = 0;
        for (int k = 0; k < 4 && donor != NULL; k++) memcpy(job->fireRecord + blocks[k][0], donor + blocks[k][0], blocks[k][1]);
        memcpy(&single, job->fireRecord + 260, 4);
        if (donor == NULL) ApiLog(w->mod, "%s: loadout item %08x fires no projectiles; its sounds stay the base's", w->name, w->soundFrom);
        else ApiLog(w->mod, "%s: makes the firing sounds of loadout item %08x (shot event %08x)", w->name, w->soundFrom, single);
    }
    if (w->projectile == 0) return;
    memcpy(&w->baseProjectile, job->fireRecord, 4);
    w->projectileRecord = job->fireRecord;
    if (ProjectileUsable(w->projectile)) memcpy(job->fireRecord, &w->projectile, 4);
    int removed = RegisterNoAmmunition(job->customization);
    ApiLog(w->mod, "%s: fires projectile %u (its base fires %u)%s", w->name, w->projectile, w->baseProjectile, removed ? "; its default ammunition is removed" : "");
}

/* Its own encyclopedia record when it has text of its own. */
static void RegisterText(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    if (w->text[0] == 0 && w->text[2] == 0 && w->text[3] == 0) return;
    uint8_t* text = OwnRecord(ctx->state, REG_TEXT_COMPONENT, REG_TEXT_STRIDE, job->baseEntity, w->entity);
    static const uint32_t offsets[4] = {0x00, 0x08, 0x14, 0x24};
    if (text == NULL) { ApiLog(w->mod, "%s: could not give it its own name record; it shows the base's name", w->name); return; }
    for (int k = 0; k < 4; k++) if (w->text[k]) memcpy(text + offsets[k], &w->text[k], 4);
}

/* Another entity's model: its records of every component tied to the model (generated_entities.dl_bin: the transport and
 * extraction Pelicans differ in these by the names of their models' nodes and actors). */
static void RegisterModel(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    if (w->modelFrom == 0) return;
    static const uint16_t model[][2] = {{88, 136}, {28, 32}, {299, 352}, {298, 152}, {223, 1544}, {104, 3912}, {91, 3704}, {159, 4836}, {225, 456}};
    int taken = 0, missing = 0;
    for (size_t k = 0; k < sizeof model / sizeof model[0]; k++)
    {
        uint32_t c = model[k][0], stride = model[k][1], slots = SlotsOf(c);
        uint8_t* t = StatePointer(ctx->state, REG_TABLES + 8 * c);
        if (t == NULL || slots == 0 || FindRow(t, slots, 16, job->baseEntity) == NULL) continue; /* the base has none */
        if (FindRow(t, slots, 16, w->modelFrom) == NULL) { missing++; continue; }
        uint8_t* own = OwnRecord(ctx->state, c, stride, job->baseEntity, w->entity);
        t = StatePointer(ctx->state, REG_TABLES + 8 * c); /* now HellForge's copy */
        uint8_t* donorRow = FindRow(t, slots, 16, w->modelFrom);
        if (own == NULL || donorRow == NULL) { missing++; continue; }
        uint32_t donor;
        memcpy(&donor, donorRow + 8, 4);
        memcpy(own, t + (size_t)slots * 16 + (size_t)donor * stride, stride);
        taken++;
    }
    ApiLog(w->mod, "%s: takes the model of %016llx (%d record(s)%s)", w->name, (unsigned long long)w->modelFrom, taken, missing ? "; some it lacks stay its base's" : "");
}

/* Its own behaviour: its own behaviour record naming it, when HellForge could install it. */
static void RegisterBehaviour(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    if (w->behaviour == 0) return;
    uint8_t* behaviour = OwnRecord(ctx->state, REG_BEHAVIOUR, REG_BEHAVIOUR_STRIDE, job->baseEntity, w->entity);
    uint32_t baseBehaviour = 0;
    if (behaviour == NULL) { ApiLog(w->mod, "%s: its base has no behaviour record; it keeps its base's behaviour", w->name); return; }
    memcpy(&baseBehaviour, behaviour, 4);
    if (!BehaviourUsable(w->behaviour)) { ApiLog(w->mod, "%s: behaviour %u is not installed; it runs its base's (%u)", w->name, w->behaviour, baseBehaviour); return; }
    memcpy(behaviour, &w->behaviour, 4);
    ApiLog(w->mod, "%s: runs behaviour %u (its base runs %u)", w->name, w->behaviour, baseBehaviour);
}

/* Records of its own the mod asked for, and with HfEntity.own_all one in every component: nothing it shares changes
 * with its base. */
static void RegisterOwn(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    for (int k = 0; k < w->ownCount; k++)
    {
        if (OwnRecord(ctx->state, w->own[k], w->ownStride[k], job->baseEntity, w->entity) != NULL) ApiLog(w->mod, "%s: its own record of component %u", w->name, w->own[k]);
        else ApiLog(w->mod, "%s: could not give it its own record of component %u (does its base have one?); it shares the base's", w->name, w->own[k]);
    }
    if (!w->ownAll) return;
    int own = 0, shared = 0;
    char sharedList[160] = "";
    for (uint64_t k = 0; k < job->componentCount; k++)
    {
        uint32_t c = job->components[k];
        if (c == REG_LOADOUT || StatePointer(ctx->state, REG_TABLES + 8 * c) == NULL) continue;
        uint32_t stride = ComponentStride(ctx->state, c);
        if (stride != 0 && OwnRecord(ctx->state, c, stride, job->baseEntity, w->entity) != NULL) { own++; continue; }
        size_t at = strlen(sharedList);
        if (at + 8 < sizeof sharedList) snprintf(sharedList + at, sizeof sharedList - at, "%s%u", shared ? ", " : "", c);
        shared++;
    }
    if (shared) ApiLog(w->mod, "%s: records of its own in %d component(s); it still shares %d with its base (record size unknown: %s)", w->name, own, shared, sharedList);
    else ApiLog(w->mod, "%s: records of its own in all %d component(s); it shares none with its base", w->name, own);
}

/* Its own loadout record (the base's with the new id), its short id and its map row, the key last. */
static void RegisterIds(RegCtx* ctx, const RegJob* job)
{
    Weapon* w = job->w;
    uint32_t record = 0;
    if (!w->plain)
    {
        record = ctx->recordCount++;
        memcpy(ctx->records + (size_t)record * REG_LOADOUT_STRIDE, ctx->records + (size_t)job->baseRecord * REG_LOADOUT_STRIDE, REG_LOADOUT_STRIDE);
        memcpy(ctx->records + (size_t)record * REG_LOADOUT_STRIDE, &w->id, 4);
        memcpy(job->loadoutRow + 8, &record, 4);
        memset(job->loadoutRow + 12, 0, 4);
        MemoryBarrier();
        memcpy(job->loadoutRow, &w->entity, 8);
    }
    uint32_t shortId = ctx->lastShort + 1 + ctx->appended;
    uint32_t mapIndex = (uint32_t)((job->mapRow - ctx->map) / 32);
    memcpy(ctx->list + 32 + (size_t)(ctx->listCount + ctx->appended) * 8, &shortId, 4);
    memcpy(ctx->list + 32 + (size_t)(ctx->listCount + ctx->appended) * 8 + 4, &mapIndex, 4);
    ctx->appended++;
    memcpy(&w->baseShortId, job->baseRow + 24, 4);
    w->shortId = shortId;
    uint8_t row[32] = {0};
    memcpy(row, &w->entity, 8);
    memcpy(row + 8, &job->components, 8);
    memcpy(row + 16, &job->componentCount, 8);
    memcpy(row + 24, &shortId, 4);
    RegisterRow(ctx, job->mapRow, row, 32);
    if (w->plain)
        ApiLog(w->mod, "%s registered: entity %016llx, short id %u, %llu components", w->name, (unsigned long long)w->entity, shortId,
            (unsigned long long)job->componentCount);
    else
        ApiLog(w->mod, "%s registered: entity %016llx, loadout record %u (id %08x), short id %u, %llu shared components", w->name,
            (unsigned long long)w->entity, record, w->id, shortId, (unsigned long long)job->componentCount - 1);
}

/* Adds one weapon (or entity) to the loaded entity data. 1 when it was registered (w->state 1), else 0 (-1, logged). */
static int RegisterOne(RegCtx* ctx, Weapon* w)
{
    RegJob job = {w};
    w->state = -1;
    if (!RegisterBase(ctx, &job)) return 0;
    RegisterShared(ctx, &job);
    if (w->plain) RegisterPlainLoadout(ctx, &job);
    RegisterUnit(ctx, &job);
    RegisterCustomization(ctx, &job);
    RegisterFiring(ctx, &job);
    RegisterText(ctx, &job);
    RegisterModel(ctx, &job);
    RegisterBehaviour(ctx, &job);
    RegisterOwn(ctx, &job);
    RegisterIds(ctx, &job);
    w->state = 1;
    return 1;
}

/* HellForge's copies of the loadout table (the same index, room for the new records: the vanilla record -> row array after
 * the records is only read by the armory builder, which HellForge replaces) and of the short-id list (room for
 * REG_MAX_WEAPONS more, appended after the largest vanilla short id in the entities' order, WeaponOrder: the same on
 * every machine), and a context adding to them. 0 when out of memory. */
static int RegistryCopies(RegCtx* ctx, uint8_t* state, uint8_t* map, uint8_t* loadout, uint8_t* list)
{
    size_t indexBytes = (size_t)REG_LOADOUT_SLOTS * 16;
    size_t copyBytes = 32 + indexBytes + (size_t)REG_LOADOUT_RECORDS * REG_LOADOUT_STRIDE;
    uint8_t* table = (uint8_t*)VirtualAlloc(NULL, copyBytes + (size_t)REG_MAX_WEAPONS * REG_LOADOUT_STRIDE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (table == NULL) return 0;
    memcpy(table, loadout - 32, copyBytes);
    uint32_t listBytes = 0, lastShort = 0;
    memcpy(&listBytes, list - 12, 4);
    uint32_t listCount = listBytes / 8;
    if (listCount > 0) memcpy(&lastShort, list + 8 * (listCount - 1), 4);
    uint8_t* newList = (uint8_t*)VirtualAlloc(NULL, 32 + (size_t)(listCount + REG_MAX_WEAPONS) * 8, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (newList == NULL) { VirtualFree(table, 0, MEM_RELEASE); return 0; }
    memcpy(newList, list - 32, 32 + (size_t)listCount * 8);
    *ctx = (RegCtx){state, map, table + 32, table + 32 + indexBytes, newList, REG_LOADOUT_RECORDS, listCount, 0, lastShort, 0};
    return 1;
}

/* After a pass added weapons: the list's new size (after its entries), and the copies in use. first: the game is
 * pointed at them (they were made by this pass). */
static void RegistryPublish(const RegCtx* ctx, int first)
{
    uint32_t newListBytes = (ctx->listCount + ctx->appended) * 8;
    MemoryBarrier();
    memcpy(ctx->list + 32 - 12, &newListBytes, 4);
    g_loadoutRecords = ctx->recordCount;
    g_regListCount = ctx->listCount + ctx->appended;
    if (!first) return;
    g_loadoutTable = ctx->index;
    g_regList = ctx->list;
    SetStatePointer(ctx->state, REG_TABLES + 8 * REG_LOADOUT, ctx->index);
    SetStatePointer(ctx->state, REG_LIST, ctx->list + 32);
}

/* Adds every pending weapon to the loaded entity data. Runs on the game's thread right after the entity loader. */
static void RegisterWeapons(void)
{
    uint8_t* state = EntityState();
    uint8_t* map = StatePointer(state, REG_MAP);
    uint8_t* list = StatePointer(state, REG_LIST);
    uint32_t vanillaRecords;
    uint8_t* loadout = LoadoutTable(state, &vanillaRecords);
    if (map == NULL || list == NULL || loadout == NULL) { Note("registry: the entity data is not loaded yet"); return; }
    if (loadout == g_loadoutTable) return; /* already done for this load */
    int pending = 0;
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].state == 0) pending++;
    if (pending == 0) return;

    RegCtx ctx;
    if (!RegistryCopies(&ctx, state, map, loadout, list)) { Note("registry: out of memory"); return; }
    int order[REG_MAX_WEAPONS];
    WeaponOrder(order);
    for (int k = 0; k < g_weaponCount; k++)
        if (g_weapons[order[k]].state == 0) RegisterOne(&ctx, &g_weapons[order[k]]);
    if (ctx.appended == 0) { VirtualFree(ctx.index - 32, 0, MEM_RELEASE); VirtualFree(ctx.list, 0, MEM_RELEASE); return; }
    RegistryPublish(&ctx, 1);
    Note("registry: %u weapon(s) added (loadout records %u, entities %u)", ctx.appended, ctx.recordCount, ctx.listCount + ctx.appended);
}

typedef void* (*EngineAllocateFn)(void* allocator, size_t size, uint32_t align);
static const uint8_t kAllocatePin[21] = {0x48, 0x83, 0xec, 0x38, 0x48, 0x8b, 0x01, 0x45, 0x8b, 0xc8, 0x4c, 0x8b, 0xc2, 0x48,
    0x8d, 0x54, 0x24, 0x20, 0xff, 0x50, 0x30};

/* A block from the allocator of the engine array whose header is at `array` ({u32 count, u32 capacity, data, allocator}),
 * so the engine can free it; NULL when that allocator is not the reviewed one. */
static uint8_t* EngineAllocate(uintptr_t array, size_t size)
{
    uintptr_t allocator = 0, vtable = 0, allocate = 0;
    uint8_t code[sizeof kAllocatePin];
    if (SafeRead(array + 0x10, &allocator, 8) || allocator == 0 || SafeRead(allocator, &vtable, 8) || vtable == 0 ||
        SafeRead(vtable + 7 * 8, &allocate, 8) || allocate == 0 || SafeRead(allocate, code, sizeof code) || memcmp(code, kAllocatePin, sizeof code) != 0)
        return NULL;
    uint8_t* block = (uint8_t*)((EngineAllocateFn)allocate)((void*)allocator, size, 16);
    if (block != NULL) memset(block, 0, size);
    return block;
}

/* The entry of a short id among the first count of the configuration's 80-byte entries (binary search by short id, as the
 * engine does), or -1. */
static int NetworkEntry(const uint8_t* entries, uint32_t count, uint32_t shortId)
{
    uint32_t lo = 0, hi = count;
    while (lo < hi)
    {
        uint32_t mid = (lo + hi) / 2, key;
        memcpy(&key, entries + (size_t)mid * 80, 4);
        if (key < shortId) lo = mid + 1; else if (key > shortId) hi = mid; else return (int)mid;
    }
    return -1;
}

/* Appends the registered weapons' entity types to the engine's network configuration, once both are loaded, in short id
 * order (WeaponOrder). */
static void RegisterNetworkTypes(void)
{
    if (g_networkTypesAdded || g_loadoutTable == NULL) return;
    uintptr_t base = ApiModuleBase(g_networkModule, NULL), holder = 0, config = 0;
    if (base == 0 || SafeRead(base + g_networkRva, &holder, 8) || holder == 0 || SafeRead(holder, &config, 8) || config == 0) return;
    uint32_t count = 0, ownCount = 0, valueCount = 0;
    uint8_t *entries = NULL, *own = NULL, *values = NULL;
    if (SafeRead(config + 0x70, &count, 4) || SafeRead(config + 0x78, &entries, 8) || SafeRead(config + 0x28, &ownCount, 4) ||
        SafeRead(config + 0x30, &own, 8) || SafeRead(config + 0x40, &valueCount, 4) || SafeRead(config + 0x48, &values, 8)) return;
    if (count == 0 || entries == NULL || ownCount != count || valueCount != count || own == NULL || values == NULL) return;
    /* claimed once (the game thread and the watch loop both call this); 2 once g_networkTypes is complete, which is when
     * netapply.c reads it */
    if (InterlockedCompareExchange(&g_networkTypesAdded, 1, 0) != 0) return;
    /* A type index travels in 11 bits (the configuration's +0xC, and field type 431): 2,048 types at most. */
    if (count + REG_MAX_WEAPONS > 2048u)
    {
        Note("registry: the network configuration has %u types; with %u more they would not fit the 11-bit type index; none added", count, REG_MAX_WEAPONS);
        goto done;
    }
    uint32_t added = 0, lastKey;
    memcpy(&lastKey, entries + (size_t)(count - 1) * 80, 4);
    uint8_t* newEntries = EngineAllocate(config + 0x70, (size_t)(count + REG_MAX_WEAPONS) * 80);
    uint8_t* newOwn = EngineAllocate(config + 0x28, (size_t)(count + REG_MAX_WEAPONS) * 4);
    uint8_t* newValues = EngineAllocate(config + 0x40, (size_t)(count + REG_MAX_WEAPONS));
    if (newEntries == NULL || newOwn == NULL || newValues == NULL)
    {
        Note("registry: the engine's allocator is not the reviewed one; the weapons have no network types (equipping them crashes)");
        goto done;
    }
    memcpy(newEntries, entries, (size_t)count * 80);
    memcpy(newOwn, own, (size_t)count * 4);
    memcpy(newValues, values, count);
    int order[REG_MAX_WEAPONS];
    WeaponOrder(order);
    for (int k = 0; k < g_weaponCount; k++)
    {
        Weapon* w = &g_weapons[order[k]];
        if (w->state != 1) continue;
        int found = NetworkEntry(entries, count, w->baseShortId);
        if (found < 0 || w->shortId <= lastKey) { ApiLog(w->mod, "%s: no network type to copy (base short id %u); joining games with it may fail", w->name, w->baseShortId); continue; }
        uint32_t index = count + added++;
        memcpy(newEntries + (size_t)index * 80, entries + (size_t)found * 80, 80);
        memcpy(newEntries + (size_t)index * 80, &w->shortId, 4);
        memcpy(newOwn + (size_t)index * 4, &index, 4);
        newValues[index] = values[found];
        lastKey = w->shortId;
        g_networkTypes[g_networkTypeCount++] = (NetworkType){index, (uint32_t)found, w->entity, w->copies, w->name, w->mod};
        ApiLog(w->mod, "%s: network entity type %u (a copy of type %d)", w->name, index, found);
    }
    if (added == 0) goto done;
    g_networkVanillaTypes = count;
    uint32_t total = count + added;
    uint32_t pair[2] = {total, total};
    ApiWrite(config + 0x30, &newOwn, 8);
    ApiWrite(config + 0x48, &newValues, 8);
    ApiWrite(config + 0x78, &newEntries, 8);
    g_netEntries = newEntries;
    g_netOwn = newOwn;
    g_netValues = newValues;
    ApiWrite(config + 0x28, pair, 8);
    ApiWrite(config + 0x40, pair, 8);
    ApiWrite(config + 0x70, pair, 8);
    Note("registry: %u network entity type(s) added (%u in all)", added, total);
done:
    InterlockedExchange(&g_networkTypesAdded, 2);
}

#define REG_ARMORY_BYTES ((size_t)(REG_ARMORY_FIRST + REG_LOADOUT_RECORDS + REG_MAX_WEAPONS + 8) * 32)
static void ArmoryFill(uint8_t* armory, uint8_t* table, uint32_t count);

/* The armory's list, built from the loadout table in use into HellForge's larger object. */
static void BuildArmory(void)
{
    uint8_t* state = EntityState();
    uint32_t count;
    uint8_t* table = LoadoutTable(state, &count);
    if (g_armory == NULL) g_armory = (uint8_t*)VirtualAlloc(NULL, REG_ARMORY_BYTES, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (g_armory == NULL || table == NULL) return;
    ArmoryFill(g_armory, table, count);
    ApiWrite(g_game + REG_ARMORY_GLOBAL, &g_armory, 8);
}

/* The armory object's entries and per-type starts and counts, from the loadout table's records. */
static void ArmoryFill(uint8_t* armory, uint8_t* table, uint32_t count)
{
    memset(armory, 0, REG_ARMORY_BYTES);
    uint8_t* records = table + (size_t)REG_LOADOUT_SLOTS * 16;
    uint32_t counts[REG_ARMORY_TYPES] = {0}, starts[REG_ARMORY_TYPES], placed[REG_ARMORY_TYPES] = {0};
    for (uint32_t r = 0; r < count; r++)
    {
        uint32_t type;
        memcpy(&type, records + (size_t)r * REG_LOADOUT_STRIDE + 4, 4);
        if (type < REG_ARMORY_TYPES) counts[type]++;
    }
    uint32_t next = REG_ARMORY_FIRST;
    for (int t = 0; t < REG_ARMORY_TYPES; t++) { starts[t] = next; next += counts[t]; }
    for (uint32_t r = 0; r < count; r++)
    {
        const uint8_t* rec = records + (size_t)r * REG_LOADOUT_STRIDE;
        uint32_t type;
        memcpy(&type, rec + 4, 4);
        if (type >= REG_ARMORY_TYPES) continue;
        uint8_t* entry = armory + (size_t)(starts[type] + placed[type]++) * 32;
        uint64_t owner = LoadoutOwner(table, r);
        memcpy(entry, rec, 4);          /* loadout id */
        memcpy(entry + 8, &owner, 8);   /* entity */
        memcpy(entry + 16, &type, 4);
        memcpy(entry + 20, rec + 8, 4);
    }
    memcpy(armory + 0x1B24, starts, sizeof starts);
    memcpy(armory + 0x1B40, counts, sizeof counts);
}

typedef uint8_t* (*EncyclopediaFn)(uint64_t entity);
typedef uint8_t* (*ArmoryEntryFn)(uint32_t id);
#define REG_ENCYCLOPEDIA_LOOKUP 0x4F8A50u /* component 76 record of an entity */
#define REG_ARMORY_LOOKUP 0x11E7C60u      /* armory entry of a loadout id (walks the per-type ranges) */

/* Asks the game's own lookups about every registered weapon and logs the answers (the armory's name path). */
static void SelfCheck(const char* when)
{
    for (int i = 0; i < g_weaponCount; i++)
    {
        Weapon* w = &g_weapons[i];
        if (w->state != 1 || w->plain) continue;
        uint8_t* record = ((EncyclopediaFn)(void*)(g_game + REG_ENCYCLOPEDIA_LOOKUP))(w->entity);
        uint32_t ids[4] = {0};
        if (record != NULL) { memcpy(&ids[0], record, 4); memcpy(&ids[1], record + 8, 4); memcpy(&ids[2], record + 0x14, 4); memcpy(&ids[3], record + 0x24, 4); }
        uint8_t* entry = g_armory != NULL ? ((ArmoryEntryFn)(void*)(g_game + REG_ARMORY_LOOKUP))(w->id) : NULL;
        uint64_t entryEntity = 0;
        if (entry != NULL) memcpy(&entryEntity, entry + 8, 8);
        ApiLog(w->mod, "self-check (%s): name record %p ids %08x %08x %08x %08x; armory entry %p entity %016llx; text \"%s\" \"%s\"",
            when, (void*)record, ids[0], ids[1], ids[2], ids[3], (void*)entry, (unsigned long long)entryEntity,
            ids[3] ? TextDetour(ids[3], NULL, NULL, NULL) : "-", ids[1] ? TextDetour(ids[1], NULL, NULL, NULL) : "-");
    }
}

static void EntityLoaderDetour(void* state)
{
    HookText();
    StatsBeginComponents();
    g_entityLoader(state);
    RegisterWeapons();
    StatsFinishComponents(EntityState());
    RegisterNetworkTypes();
    SelfCheck("after loading");
}

/* <session>/arsenal.tsv: every armory item with its loadout id, armory type, entity and text, as the game names it (for
 * mod authors choosing a base). */
static void WriteArsenal(void)
{
    if (g_logs[0] == 0 || g_armory == NULL) return;
    wchar_t path[MAX_PATH];
    swprintf(path, MAX_PATH, L"%ls\\arsenal.tsv", g_logs);
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    char line[512];
    DWORD written;
    int n = snprintf(line, sizeof line, "loadout_id\ttype\tentity\tdesignation\tname\n");
    WriteFile(file, line, (DWORD)n, &written, NULL);
    uint32_t starts[REG_ARMORY_TYPES], counts[REG_ARMORY_TYPES];
    memcpy(starts, g_armory + 0x1B24, sizeof starts);
    memcpy(counts, g_armory + 0x1B40, sizeof counts);
    for (int t = 0; t < REG_ARMORY_TYPES; t++)
        for (uint32_t k = 0; k < counts[t]; k++)
        {
            const uint8_t* entry = g_armory + (size_t)(starts[t] + k) * 32;
            uint32_t id;
            uint64_t entity;
            memcpy(&id, entry, 4);
            memcpy(&entity, entry + 8, 8);
            uint8_t* record = entity ? ((EncyclopediaFn)(void*)(g_game + REG_ENCYCLOPEDIA_LOOKUP))(entity) : NULL;
            uint32_t designation = 0, name = 0;
            if (record != NULL) { memcpy(&name, record, 4); memcpy(&designation, record + 0x24, 4); }
            const char* d = designation ? TextDetour(designation, NULL, NULL, NULL) : "";
            const char* m = name ? TextDetour(name, NULL, NULL, NULL) : "";
            n = snprintf(line, sizeof line, "%08x\t%d\t%016llx\t%s\t%s\n", id, t, (unsigned long long)entity, d ? d : "", m ? m : "");
            if (n > 0) WriteFile(file, line, (DWORD)(n < (int)sizeof line ? n : (int)sizeof line - 1), &written, NULL);
        }
    CloseHandle(file);
}

static void ArmoryBuilderDetour(void* list)
{
    (void)list;
    HookText();
    RegisterNetworkTypes();
    BuildArmory();
    static int checked;
    if (!checked++) { SelfCheck("armory built"); WriteArsenal(); }
}

/* A 14-byte absolute jump (jmp [rip+0]; dq target). */
static int PatchJump(uintptr_t at, const void* target)
{
    uint8_t jump[14] = {0xFF, 0x25, 0, 0, 0, 0};
    memcpy(jump + 6, &target, 8);
    return ApiWrite(at, jump, sizeof jump);
}

/* Hooks without MinHook, for the two pinned functions when MinHook declines: the entity loader's first 16 bytes are
 * position-independent pushes and moves (kLoaderPin), so they run from a trampoline that jumps back; the armory builder
 * is replaced, never called, so it only needs the jump. */
static int PatchHooks(void)
{
    uint8_t* trampoline = (uint8_t*)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (trampoline == NULL) return 1;
    memcpy(trampoline, kLoaderPin, 16);
    uint8_t back[14] = {0xFF, 0x25, 0, 0, 0, 0};
    uintptr_t resume = g_game + REG_ENTITY_LOADER + 16;
    memcpy(back + 6, &resume, 8);
    memcpy(trampoline + 16, back, sizeof back);
    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);
    g_entityLoader = (EntityLoaderFn)(void*)trampoline;
    if (PatchJump(g_game + REG_ARMORY_BUILDER, (void*)ArmoryBuilderDetour)) return 1;
    return PatchJump(g_game + REG_ENTITY_LOADER, (void*)EntityLoaderDetour);
}

/* The projectiles mods added were refused: weapons pointed at one fire their base's projectile again. */
static void RevertWeaponProjectiles(void)
{
    for (int i = 0; i < g_weaponCount; i++)
    {
        Weapon* w = &g_weapons[i];
        if (w->projectileRecord == NULL || ProjectileUsable(w->projectile)) continue;
        memcpy(w->projectileRecord, &w->baseProjectile, 4);
        ApiLog(w->mod, "%s: projectile %u was refused; it fires its base's %u", w->name, w->projectile, w->baseProjectile);
    }
}

/* Units a registered entity was given a copy of (its own unit record names a unit other than its base's). The game keys
 * some per-unit settings by the unit's name; a copy is entered in those as its base is, so it is set up as its base.
 * The destructible index (game.dll 25480438): [[game + 0x3326550]] + 0x2C60 is {+8 table, +0x10 capacity}, filled
 * from a baked resource (type 0xB8FD4D2CEDE20ED7) when it loads (0xF6EC10): 16-byte entries {u64 unit name, u64 the
 * unit's destruction settings}, probed from slot (i + 2 * name) & (capacity - 1) to the first empty name (lookup
 * 0xFC45C0). A unit missing from it is never registered for destruction (0x883200: "Could not find any destructible
 * index data"), which also sets up some of its havok actors; a sentry so left had no aim and a field around it that
 * blocked players, likely for that reason. */
#define REG_MAX_UNIT_COPIES 64
#define DESTR_MANAGER 0x3326550u
#define DESTR_INDEX 0x2C60u
static struct { uint64_t base, copy; const char* name; HfMod* mod; } g_unitCopies[REG_MAX_UNIT_COPIES];
static int g_unitCopyCount;
static uintptr_t g_destructibleTable; /* the table the copies are in, and how many */
static int g_destructibleEntered;

static void RememberUnitCopy(HfMod* mod, const char* name, uint64_t base, uint64_t copy)
{
    if (base == 0 || copy == 0 || base == copy) return;
    for (int i = 0; i < g_unitCopyCount; i++) if (g_unitCopies[i].copy == copy) return;
    if (g_unitCopyCount == REG_MAX_UNIT_COPIES) { ApiLog(mod, "%s: too many unit copies to set up as their bases", name); return; }
    g_unitCopies[g_unitCopyCount].base = base;
    g_unitCopies[g_unitCopyCount].copy = copy;
    g_unitCopies[g_unitCopyCount].name = name;
    g_unitCopies[g_unitCopyCount].mod = mod;
    g_unitCopyCount++;
}

/* The slot holding `name` (1), or the empty slot its lookup would stop at (0), or none (-1). */
static int DestructibleSlot(uintptr_t table, uint32_t capacity, uint64_t name, uintptr_t* slot)
{
    for (uint32_t i = 0; i < capacity; i++)
    {
        uintptr_t at = table + (uintptr_t)(((uint64_t)i + 2 * name) & (capacity - 1)) * 16;
        uint64_t key = 0;
        if (SafeRead(at, &key, 8)) return -1;
        if (key == name || key == 0) { *slot = at; return key == name; }
    }
    return -1;
}

/* Polled once the game is loaded: enters the unit copies in the destructible index once it is loaded (again if the game
 * loads it anew); a copy whose base is not in it (a weapon's unit, say) is left out, as its base is. */
static void UnitCopiesTick(uintptr_t game)
{
    if (g_unitCopyCount == 0 || game == 0 || strcmp(g_build, "25480438")) return;
    uintptr_t manager = 0, index = 0, table = 0;
    uint32_t capacity = 0;
    if (SafeRead(game + DESTR_MANAGER, &manager, 8) || manager == 0 || SafeRead(manager, &index, 8) || index == 0 ||
        SafeRead(index + DESTR_INDEX + 8, &table, 8) || table == 0 || SafeRead(index + DESTR_INDEX + 0x10, &capacity, 4)) return;
    if (table == g_destructibleTable && g_destructibleEntered == g_unitCopyCount) return;
    g_destructibleTable = table;
    g_destructibleEntered = g_unitCopyCount;
    if (capacity == 0 || (capacity & (capacity - 1)) != 0) { Note("units: the destructible index is not the reviewed one (capacity %u); unit copies are left out", capacity); return; }
    int entered = 0;
    for (int i = 0; i < g_unitCopyCount; i++)
    {
        uintptr_t baseSlot = 0, copySlot = 0;
        uint64_t settings = 0;
        if (DestructibleSlot(table, capacity, g_unitCopies[i].base, &baseSlot) != 1 || SafeRead(baseSlot + 8, &settings, 8)) continue; /* its base is not destructible */
        int found = DestructibleSlot(table, capacity, g_unitCopies[i].copy, &copySlot);
        if (found == 1) continue;
        /* the settings first, then the name that makes the entry found */
        if (found != 0 || ApiWrite(copySlot + 8, &settings, 8) || ApiWrite(copySlot, &g_unitCopies[i].copy, 8))
        {
            ApiLog(g_unitCopies[i].mod, "%s: its unit %016llx could not be entered in the destructible index (the table is full)", g_unitCopies[i].name,
                (unsigned long long)g_unitCopies[i].copy);
            continue;
        }
        ApiLog(g_unitCopies[i].mod, "%s: its unit %016llx is destructible as its base's (%016llx, settings %llu)", g_unitCopies[i].name,
            (unsigned long long)g_unitCopies[i].copy, (unsigned long long)g_unitCopies[i].base, (unsigned long long)settings);
        entered++;
    }
    Note("units: %d unit cop%s entered in the destructible index", entered, entered == 1 ? "y" : "ies");
}

/* Whether the Watch thread should hold HF_EVENT_ENTITIES_READY back: weapons are waiting for the entity loader. */
static int RegistryPending(void)
{
    if (!g_registryActive) return 0;
    for (int i = 0; i < g_weaponCount; i++) if (g_weapons[i].state == 0) return 1;
    return 0;
}

/* The entity loader and the armory builder hooked when the build matches (once). 1 when they are. */
static int RegistryHook(void)
{
    if (g_registryHooked) return g_registryActive;
    g_registryHooked = 1;
    g_game = ApiModuleBase("game.dll", NULL);
    uint8_t loader[16], builder[16];
    if (g_game == 0 || HookedCode(g_game + REG_ENTITY_LOADER, loader, 16) || HookedCode(g_game + REG_ARMORY_BUILDER, builder, 16) ||
        memcmp(loader, kLoaderPin, 16) != 0 || memcmp(builder, kBuilderPin, 16) != 0)
    {
        Note("registry: game.dll is not the reviewed build; %d weapon(s) not registered", g_weaponCount);
        return 0;
    }
    MEMORY_BASIC_INFORMATION page;
    if (VirtualQuery((void*)(g_game + REG_ENTITY_LOADER), &page, sizeof page))
        Note("registry: entity loader page 0x%llx protect 0x%lx state 0x%lx type 0x%lx", (unsigned long long)(uintptr_t)page.BaseAddress,
            page.Protect, page.State, page.Type);
    /* through the hook chain (hooks.c): mods may hook these too. The armory builder only once the entity loader is in, so
     * the fallback (both jumps patched directly) never patches over a chain. */
    int loaderFailed = LoaderHook("weapon registry: entity loader", g_game + REG_ENTITY_LOADER, (void*)EntityLoaderDetour, (void**)&g_entityLoader);
    int armoryFailed = loaderFailed ? 1 : LoaderHook("weapon registry: armory builder", g_game + REG_ARMORY_BUILDER, (void*)ArmoryBuilderDetour, (void**)&g_armoryBuilder);
    int hooked = !loaderFailed && !armoryFailed;
    if (loaderFailed)
    {
        Note("registry: the entity loader could not be hooked; patching the two jumps directly");
        hooked = !PatchHooks();
    }
    else if (armoryFailed) Note("registry: the armory builder could not be hooked; added weapons load without their armory entries");
    Note(hooked ? "registry: hooked the entity loader and the armory builder for %d weapon(s)" : "registry: could not hook (%d weapon(s))", g_weaponCount);
    g_registryActive = hooked;
    if (hooked) HookText();
    return hooked;
}

/* At GAME_LOADED: hook the entity loader and the armory builder when weapons are registered and the build matches. */
static void StartRegistry(void)
{
    if ((g_weaponCount == 0 && !StatsHasComponents()) || g_registryHooked) return;
    /* Entity data that loaded before the hook still gets the weapons. */
    if (RegistryHook() && StatePointer(EntityState(), REG_MAP) != NULL)
    {
        RegisterWeapons();
        StatsApplyComponents(EntityState());
    }
}
