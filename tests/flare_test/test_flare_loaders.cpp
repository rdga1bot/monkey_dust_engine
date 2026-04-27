// M7.7 Flare data-loader tests — EnemyLoader, ItemLoader, PowerLoader, FactionLoader
//
// Build (from engine/tests/flare_test/):
//   g++ -std=c++17 test_flare_loaders.cpp \
//       ../../src/flare/enemy_loader.cpp \
//       ../../src/flare/item_loader.cpp  \
//       ../../src/flare/power_loader.cpp \
//       ../../src/flare/faction_loader.cpp \
//       -I../../include -o test_flare_loaders && ./test_flare_loaders

#include <monkey_dust/flare/enemy_loader.h>
#include <monkey_dust/flare/item_loader.h>
#include <monkey_dust/flare/power_loader.h>
#include <monkey_dust/flare/faction_loader.h>
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace md::flare;

static int g_total = 0;
static int g_pass  = 0;

static void chk(bool ok, const char* label) {
    ++g_total;
    if (ok) { ++g_pass; printf("  PASS  %s\n", label); }
    else               printf("  FAIL  %s\n", label);
}

// Mod paths relative to engine/tests/flare_test/
static const char* ALPHA     = "../../../third_party/flare-game/mods/alpha_demo";
static const char* FCORE     = "../../../third_party/flare-game/mods/fantasycore";
// Chain: game mod first, then base mod (for INCLUDE resolution)
static const char* CHAIN[2]  = {ALPHA, FCORE};

// ── EnemyLoader ───────────────────────────────────────────────────────────────

static EnemyRegistry g_enemies;

static void test_enemy_loader() {
    printf("\n[EnemyLoader]\n");
    memset(&g_enemies, 0, sizeof(g_enemies));

    bool ok = LoadEnemies(CHAIN, 2, g_enemies);
    chk(ok,                           "LoadEnemies returns true");
    chk(g_enemies.count > 0,          "at least one enemy loaded");

    const EnemyDef* antlion = FindEnemy(g_enemies, "antlion");
    chk(antlion != nullptr,           "FindEnemy(antlion) found");
    if (antlion) {
        chk(strcmp(antlion->name, "Antlion") == 0, "antlion name='Antlion'");
        chk(antlion->level == 4,                   "antlion level=4");
        chk(antlion->hp    == 70,                  "antlion hp=70");
        chk(antlion->xp    == 10,                  "antlion xp=10");
        chk(antlion->speed > 5.0f && antlion->speed < 6.0f, "antlion speed≈5.5");
        chk(antlion->dmg_melee_min == 10,          "antlion dmg_melee_min=10");
        chk(antlion->dmg_melee_max == 35,          "antlion dmg_melee_max=35");
        chk(antlion->absorb_min == 12,             "antlion absorb_min=12");
        chk(antlion->animations[0] != '\0',        "antlion has animations path");
        chk(antlion->valid,                        "antlion.valid");
    }

    const EnemyDef* goblin = FindEnemy(g_enemies, "goblin");
    chk(goblin != nullptr,            "FindEnemy(goblin) found");
    if (goblin) {
        chk(goblin->humanoid,         "goblin humanoid=true");
    }

    chk(FindEnemy(g_enemies, "nonexistent") == nullptr, "FindEnemy(nonexistent)=null");
}

// ── ItemLoader ────────────────────────────────────────────────────────────────

static ItemRegistry g_items;

static void test_item_loader() {
    printf("\n[ItemLoader]\n");
    memset(&g_items, 0, sizeof(g_items));

    bool ok = LoadItems(ALPHA, g_items);
    chk(ok,                     "LoadItems returns true");
    chk(g_items.count > 0,      "at least one item loaded");

    // Item 10 = Gold (currency)
    const ItemDef* gold = FindItem(g_items, 10);
    chk(gold != nullptr,        "FindItem(10)=Gold found");
    if (gold) {
        chk(strcmp(gold->name, "Gold") == 0,           "gold name='Gold'");
        chk(gold->type == FlareItemType::CURRENCY,     "gold type=CURRENCY");
        chk(gold->price == 1,                          "gold price=1");
        chk(gold->max_quantity == 5000,                "gold max_quantity=5000");
        chk(gold->icon == 88,                          "gold icon=88");
        chk(gold->valid,                               "gold.valid");
    }

    // Item 64 = Cloth Shirt (armor-ish, abs=0 so OTHER or ARMOR)
    const ItemDef* shirt = FindItem(g_items, 64);
    chk(shirt != nullptr,       "FindItem(64)=Cloth Shirt found");
    if (shirt) {
        chk(strcmp(shirt->name, "Cloth Shirt") == 0,  "shirt name='Cloth Shirt'");
    }

    chk(FindItem(g_items, 9999) == nullptr, "FindItem(9999)=null");
}

// ── PowerLoader ───────────────────────────────────────────────────────────────

static PowerRegistry g_powers;

static void test_power_loader() {
    printf("\n[PowerLoader]\n");
    memset(&g_powers, 0, sizeof(g_powers));

    bool ok = LoadPowers(ALPHA, g_powers);
    chk(ok,                      "LoadPowers returns true");
    chk(g_powers.count > 0,      "at least one power loaded");

    // Power 1 = Swing (basic melee)
    const PowerDef* swing = FindPower(g_powers, 1);
    chk(swing != nullptr,        "FindPower(1)=Swing found");
    if (swing) {
        chk(strcmp(swing->name, "Swing") == 0,         "swing name='Swing'");
        chk(strcmp(swing->type, "fixed") == 0,         "swing type='fixed'");
        chk(swing->icon == 1,                          "swing icon=1");
        chk(swing->use_hazard,                         "swing use_hazard=true");
        chk(swing->valid,                              "swing.valid");
    }

    // Power 3 = Block (block type)
    const PowerDef* block = FindPower(g_powers, 3);
    chk(block != nullptr,        "FindPower(3)=Block found");
    if (block) {
        chk(strcmp(block->type, "block") == 0,         "block type='block'");
    }

    // Power 9 = Warcry (has cooldown=15s)
    const PowerDef* warcry = FindPower(g_powers, 9);
    chk(warcry != nullptr,       "FindPower(9)=Warcry found");
    if (warcry) {
        chk(warcry->cooldown_s > 14.9f && warcry->cooldown_s < 15.1f,
                                 "warcry cooldown≈15.0s");
        chk(warcry->buff,        "warcry buff=true");
    }

    chk(FindPower(g_powers, 9999) == nullptr, "FindPower(9999)=null");
}

// ── FactionLoader ─────────────────────────────────────────────────────────────

static FlareFactionRegistry g_factions;

static void test_faction_loader() {
    printf("\n[FactionLoader]\n");
    // requires enemy registry from test_enemy_loader()
    BuildFactionsFromEnemies(g_enemies, g_factions);

    chk(g_factions.count > 0,   "at least one faction derived");

    // antlion's categories= starts with "antlion_normal" → that is the primary faction
    uint8_t ant_id = FindFactionId(g_factions, "antlion_normal");
    chk(ant_id != 0,            "FindFactionId(antlion_normal) != 0");

    // ids are 1-based
    chk(ant_id >= 1 && ant_id <= MAX_FLARE_FACTIONS,
                                "antlion_normal id in valid range");

    // same name → same id (idempotent)
    uint8_t ant_id2 = GetOrAddFaction(g_factions, "antlion_normal");
    chk(ant_id == ant_id2,      "GetOrAddFaction idempotent for antlion_normal");

    // add a new faction
    uint8_t new_id = GetOrAddFaction(g_factions, "test_faction_xyz");
    chk(new_id != 0,            "GetOrAddFaction registers new faction");
    chk(FindFactionId(g_factions, "test_faction_xyz") == new_id,
                                "FindFactionId finds newly added faction");

    chk(FindFactionId(g_factions, "nonexistent_faction_abc") == 0,
                                "FindFactionId(nonexistent)=0");
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    test_enemy_loader();
    test_item_loader();
    test_power_loader();
    test_faction_loader();   // depends on g_enemies from test_enemy_loader()

    printf("\nTests: %d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
