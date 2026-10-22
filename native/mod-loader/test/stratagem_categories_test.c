/* Included by stats_test.c: real registration/grouping/header code with synthetic rows and native UI callbacks. */
static uint32_t g_categoryTestText, g_categoryTestAvailable, g_categoryTestTotal, g_categoryTestHeaders;
static void CategoryTestHeader(void* widget, uint32_t key) { g_categoryTestHeaders++; }
static void CategoryTestText(void* widget, uint32_t text) { g_categoryTestText = text; }
static void CategoryTestNumber(void* widget, uint32_t key, uint32_t value)
{
    if (key == 0xEB646556u) g_categoryTestAvailable = value;
    else { assert(key == 0x4442EB89u); g_categoryTestTotal = value; }
}
static void* CategoryTestItem(void* account, uint32_t type) { return type == 3 ? NULL : account; }
static uint8_t CategoryTestAvailable(void* account, uint32_t type) { return type != STR_VANILLA + 1; }
static int CategoryTestCompare(const void* a, const void* b)
{
    const PickerCandidate* x = a; const PickerCandidate* y = b;
    if (x->section != y->section) return x->section < y->section ? -1 : 1;
    if (x->order != y->order) return x->order < y->order ? -1 : 1;
    return (x->type > y->type) - (x->type < y->type);
}

static void StratagemCategoriesFixture(void)
{
    int oldTextCount = g_textCount;
    HfMod a = {0}, b = {0}; strcpy(a.id, "test.a"); strcpy(b.id, "test.b");
    g_stratagemCount = g_stratagemCategoryCount = 0;
    g_stratagemState = 0; g_fired[HF_EVENT_GAME_LOADED] = 0;
    HfStratagemCategory cat = {sizeof cat, "air", "Air Support", 10};
    uint32_t air = ApiAddStratagemCategory(&a, &cat);
    assert(air == 1 && !strcmp(g_stratagemCategories[0].name, "test.a:air"));
    uint32_t other = ApiAddStratagemCategory(&b, &cat);
    assert(other == 2); /* equal short names in different mods do not collide */
    cat.name = "test.a:air";
    assert(ApiAddStratagemCategory(&b, &cat) == air); /* explicit sharing */
    cat.title = "Conflicting title";
    assert(!ApiAddStratagemCategory(&b, &cat) && g_stratagemCategoryCount == 2);
    cat.title = ""; cat.name = "bad";
    assert(!ApiAddStratagemCategory(&a, &cat));
    cat.title = "Robotics"; cat.name = "robots"; cat.sort_order = -5;
    uint32_t robots = ApiAddStratagemCategory(&a, &cat);
    cat.size = 0; assert(!ApiAddStratagemCategory(&a, &cat)); cat.size = sizeof cat;
    g_fired[HF_EVENT_GAME_LOADED] = 1;
    assert(!ApiAddStratagemCategory(&a, &cat)); g_fired[HF_EVENT_GAME_LOADED] = 0;
    OrderStratagemCategories();
    assert(g_stratagemCategories[robots - 1].section == 5 && g_stratagemCategories[air - 1].section == 6 &&
        g_stratagemCategories[other - 1].section == 7);

    HfStratagem strat = {sizeof strat, .base = 1, .name = "first", .selection_category = air};
    assert(ApiAddStratagem(&a, &strat) == STR_VANILLA);
    strat.name = "second"; strat.selection_order = -3;
    assert(ApiAddStratagem(&a, &strat) == STR_VANILLA + 1 && g_stratagems[1].selectionOrder == -3);
    strat.name = "robot"; strat.selection_order = 0; strat.selection_category = robots;
    assert(ApiAddStratagem(&a, &strat) == STR_VANILLA + 2);
    strat.name = "invalid"; strat.selection_category = 99;
    assert(!ApiAddStratagem(&a, &strat) && g_stratagemCount == 3);
    strat.name = "old-abi"; strat.size = offsetof(HfStratagem, selection_category);
    assert(ApiAddStratagem(&a, &strat) == STR_VANILLA + 3 && g_stratagems[3].selectionCategory == 0);

    uint8_t rows[6][STR_ROW] = {{0}};
    uint8_t* table[STR_VANILLA + 4] = {0};
    for (int i = 0; i < 6; i++) { rows[i][0x80] = 2; table[i < 2 ? i + 1 : STR_VANILLA + i - 2] = rows[i]; }
    *(uint32_t*)(rows[1] + 0xB8) = 3; /* vanilla defensive */
    *(uint32_t*)(rows[3] + 0xB8) = 3; /* Air Support can mix native categories */
    g_stratagemTable = (uint8_t*)table;
    g_pickerSort = qsort; g_pickerHeader = CategoryTestHeader; g_pickerText = CategoryTestText;
    g_pickerNumber = CategoryTestNumber; g_pickerItem = CategoryTestItem; g_pickerAvailable = CategoryTestAvailable;
    void* account = rows; g_pickerAccountSlot = (uintptr_t)&account;
    g_stratagemCategoriesActive = 1;
    PickerCandidate candidates[] = {
        {100,0,STR_VANILLA,8,0,1,{0}}, {10,0,1,0,1,0,{0}}, {102,0,STR_VANILLA+2,0,1,0,{0}},
        {103,0,STR_VANILLA+3,1,1,0,{0}}, {20,3,2,0,1,0,{0}}
    };
    PickerSortCategories(candidates, 5, sizeof candidates[0], CategoryTestCompare);
    assert(candidates[0].key == 10 && candidates[1].key == 103 && candidates[2].key == 20 &&
        candidates[3].key == 102 && candidates[4].key == 100);
    assert(candidates[4].enabled == 0 && candidates[4].blocked == 1 && candidates[4].type == STR_VANILLA);
    assert(*(uint32_t*)(rows[2] + 0xB8) == 0 && *(uint32_t*)(rows[3] + 0xB8) == 3); /* UI only */
    /* selection_order: lower first, ties in call order; the base row's order (+0x188) is ignored under a custom heading */
    PickerCandidate ordered[] = {{100,0,STR_VANILLA,1,1,0,{0}}, {101,0,STR_VANILLA+1,9,1,0,{0}}, {11,0,1,7,1,0,{0}}, {12,0,2,3,1,0,{0}}};
    PickerSortCategories(ordered, 4, sizeof ordered[0], CategoryTestCompare);
    assert(ordered[0].key == 12 && ordered[1].key == 11 && ordered[2].key == 101 && ordered[3].key == 100); /* -3 before 0 */
    g_stratagems[1].selectionOrder = 0;
    PickerCandidate tied[] = {{101,0,STR_VANILLA+1,1,1,0,{0}}, {100,0,STR_VANILLA,9,1,0,{0}}};
    PickerSortCategories(tied, 2, sizeof tied[0], CategoryTestCompare);
    assert(tied[0].key == 100 && tied[1].key == 101);
    g_stratagems[0].selectionOrder = INT32_MIN; g_stratagems[1].selectionOrder = INT32_MAX;
    PickerSortCategories(tied, 2, sizeof tied[0], CategoryTestCompare);
    assert(tied[0].key == 100 && tied[1].key == 101); /* the whole signed range */
    g_stratagems[0].selectionOrder = 1; g_stratagems[1].selectionOrder = -1;
    PickerSortCategories(tied, 2, sizeof tied[0], CategoryTestCompare);
    assert(tied[0].key == 101 && tied[1].key == 100);
    g_stratagems[0].selectionOrder = g_stratagems[1].selectionOrder = 0;
    PickerSortCategories(candidates, 5, sizeof candidates[0], CategoryTestCompare); /* the header checks' list */
    uint8_t widget[0x688] = {0};
    uint8_t* list = calloc(1, 0x93000); assert(list);
    *(uint32_t*)(list + 0x92984) = 5;
    uint32_t groups = 0;
    for (uint32_t i = 0; i < 5; i++)
    {
        *(uint32_t*)(list + 0x92990 + i * 4) = candidates[i].key;
        if (i == 0 || candidates[i].section != candidates[i - 1].section)
        {
            *(uint32_t*)(list + 0x927D0 + groups * 4) = i;
            *(uint32_t*)(list + 0x92854 + groups * 4) = candidates[i].section;
            groups++;
        }
    }
    *(uint32_t*)(list + 0x92748) = groups;
    assert(PickerCategoryListMatches(list));
    PickerHeaderCategory(widget, 100, list);
    assert(g_categoryTestText == g_stratagemCategories[air - 1].text && g_categoryTestAvailable == 1 && g_categoryTestTotal == 2);
    g_categoryTestText = 0;
    PickerHeaderCategory(widget, 10, list);
    assert(g_categoryTestText == 0 && g_categoryTestAvailable == 2 && g_categoryTestTotal == 2); /* no custom double-count */
    PickerHeaderCategory(widget, 20, list);
    assert(g_categoryTestAvailable == 1 && g_categoryTestTotal == 1);
    list[0x92854] ^= 1; /* shared renderer, different screen/grouping */
    PickerHeaderCategory(widget, 100, list);
    assert(!g_categoryTestText && !PickerCategoryListMatches(list));
    list[0x92854] ^= 1;
    PickerSortCategories(candidates, 0, sizeof candidates[0], CategoryTestCompare);
    assert(g_pickerCandidateCount == 0); /* reopening an empty picker cannot reuse previous keys */
    g_stratagemCategoriesActive = 0;
    uint32_t calls = g_categoryTestHeaders;
    PickerHeaderCategory(widget, 100, NULL);
    assert(g_categoryTestHeaders == calls + 1 && !g_categoryTestText); /* fallback calls native header */

    for (uint32_t i = g_stratagemCategoryCount; i < STR_CATEGORY_MAX; i++)
    {
        char name[32]; snprintf(name, sizeof name, "group%u", i);
        cat.name = name; assert(ApiAddStratagemCategory(&a, &cat));
    }
    cat.name = "too-many"; assert(!ApiAddStratagemCategory(&a, &cat));
    OrderStratagemCategories();
    for (uint32_t i = 0; i < STR_CATEGORY_MAX; i++) assert(g_stratagemCategories[i].section < 33);

    uint8_t* image = VirtualAlloc(NULL, 0x2100000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(image && !StratagemCategoryPinsMatch((uintptr_t)image));
    for (size_t i = 0; i < sizeof kCategoryPins / sizeof kCategoryPins[0]; i++)
        memcpy(image + kCategoryPins[i].rva, kCategoryPins[i].bytes, kCategoryPins[i].size);
    assert(StratagemCategoryPinsMatch((uintptr_t)image));
    image[STR_CATEGORY_SORT_CALL] ^= 1;
    InstallStratagemCategories((uintptr_t)image);
    assert(!g_stratagemCategoriesActive && image[STR_CATEGORY_HEADER_CALL] == 0xE8);
    image[STR_CATEGORY_SORT_CALL] ^= 1;
    InstallStratagemCategories((uintptr_t)image);
    assert(g_stratagemCategoriesActive);
    int32_t sortDisp, headerDisp;
    memcpy(&sortDisp, image + STR_CATEGORY_SORT_CALL + 1, 4);
    memcpy(&headerDisp, image + STR_CATEGORY_HEADER_CALL + 1, 4);
    uint8_t* sortThunk = image + STR_CATEGORY_SORT_CALL + 5 + sortDisp;
    uint8_t* headerThunk = image + STR_CATEGORY_HEADER_CALL + 5 + headerDisp;
    g_pickerSort = qsort; g_pickerHeader = CategoryTestHeader; g_pickerText = CategoryTestText;
    g_pickerNumber = CategoryTestNumber; g_pickerItem = CategoryTestItem; g_pickerAvailable = CategoryTestAvailable;
    g_pickerAccountSlot = (uintptr_t)&account;
    ((void (*)(void*, size_t, size_t, PickerCompare))sortThunk)(candidates, 5, sizeof candidates[0], CategoryTestCompare);
    assert(g_pickerCandidateCount == 5 && candidates[4].key == 100);
    groups = 0;
    for (uint32_t i = 0; i < 5; i++)
        if (i == 0 || candidates[i].section != candidates[i - 1].section)
            *(uint32_t*)(list + 0x92854 + groups++ * 4) = candidates[i].section;
    /* Stand in for the native call site: preserve RSI, put the list there, and call the actual generated thunk.
     * Windows x64: keep stack alignment and the 32-byte shadow space across the call. */
    uint8_t caller[] = {0x56, 0x48,0x83,0xec,0x20, 0x4c,0x89,0xc6, 0x48,0xb8,
        0,0,0,0,0,0,0,0, 0xff,0xd0, 0x48,0x83,0xc4,0x20, 0x5e,0xc3};
    memcpy(caller + 10, &headerThunk, 8);
    uint8_t* entry = VirtualAlloc(NULL, sizeof caller, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    assert(entry); memcpy(entry, caller, sizeof caller);
    DWORD protect; assert(VirtualProtect(entry, sizeof caller, PAGE_EXECUTE_READ, &protect));
    FlushInstructionCache(GetCurrentProcess(), entry, sizeof caller);
    g_categoryTestText = 0;
    ((void (*)(void*, uint32_t, const uint8_t*))entry)(widget, 100, list);
    assert(g_categoryTestText == g_stratagemCategories[air - 1].text && g_categoryTestTotal == 2 && g_categoryTestAvailable == 1);
    VirtualFree(entry, 0, MEM_RELEASE);
    VirtualFree(sortThunk, 0, MEM_RELEASE);
    g_stratagemCategoriesActive = 0;
    free(list);
    VirtualFree(image, 0, MEM_RELEASE);
    for (int i = oldTextCount; i < g_textCount; i++) free((void*)g_texts[i].text);
    g_textCount = oldTextCount;
    g_stratagemCount = g_stratagemCategoryCount = 0; g_stratagemTable = NULL;
    puts("PASS stratagem categories: namespaces, sharing, conflicts, late/invalid registration, old ABI, grouping, selection order, flags, counts, empty picker, limits, pin refusal, executable call thunks");
}
