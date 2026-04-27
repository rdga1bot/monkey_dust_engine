// M7.1 smoke test — md::flare::IniParser
// Build: g++ -std=c++17 test_ini_parser.cpp ../../src/flare/ini_parser.cpp
//             -I../../include -o test_ini_parser && ./test_ini_parser

#include <monkey_dust/flare/ini_parser.h>
#include <cstdio>
#include <cstring>

using namespace md::flare;

static int g_total = 0;
static int g_pass  = 0;

static void check(bool ok, const char* label) {
    ++g_total;
    if (ok) { ++g_pass; printf("  PASS  %s\n", label); }
    else            printf("  FAIL  %s\n", label);
}

// ─── Test 1: ParseIniFile all-sections ───────────────────────────────────────

struct T1State { int item_count; int key_count; };

static void t1_cb(const IniLine& l, void* ud) {
    T1State* s = (T1State*)ud;
    if (strcmp(l.key, "id") == 0) ++s->item_count;
    ++s->key_count;
}

static void test_parse_all() {
    T1State s = {};
    bool ok = ParseIniFile("fixtures/sample_item.txt", t1_cb, &s, nullptr);
    check(ok,             "file opened");
    check(s.item_count == 3, "3 items (id= per section)");
    check(s.key_count  >= 15, "≥15 key-value pairs");
}

// ─── Test 2: section filter ──────────────────────────────────────────────────

struct T2State { char last_name[64]; int name_count; };

static void t2_cb(const IniLine& l, void* ud) {
    T2State* s = (T2State*)ud;
    if (strcmp(l.key, "name") == 0) {
        CopyString(s->last_name, l.value, (int)sizeof(s->last_name));
        ++s->name_count;
    }
}

static void test_section_filter() {
    T2State s = {};
    ParseIniFile("fixtures/sample_item.txt", t2_cb, &s, "item");
    check(s.name_count == 3,                       "3 names in [item] sections");
    check(strcmp(s.last_name, "Gold Coin") == 0,   "last name = Gold Coin");
}

// ─── Test 3: ParseIntList ─────────────────────────────────────────────────────

static void test_int_list() {
    int out[4] = {};
    int n = ParseIntList("10,20,30", out, 4);
    check(n == 3,       "ParseIntList count=3");
    check(out[0] == 10, "out[0]=10");
    check(out[1] == 20, "out[1]=20");
    check(out[2] == 30, "out[2]=30");
}

// ─── Test 4: ParseFloatList ───────────────────────────────────────────────────

static void test_float_list() {
    float out[3] = {};
    int n = ParseFloatList("1.5,2.25, 3.0", out, 3);
    check(n == 3,           "ParseFloatList count=3");
    check(out[0] > 1.49f && out[0] < 1.51f, "out[0]≈1.5");
    check(out[1] > 2.24f && out[1] < 2.26f, "out[1]≈2.25");
}

// ─── Test 5: CopyString bounds ───────────────────────────────────────────────

static void test_copy_string() {
    char buf[4] = {};
    CopyString(buf, "hello", (int)sizeof(buf));
    check(buf[3] == '\0',        "null-terminated on overflow");
    check(strncmp(buf,"hel",3)==0, "first 3 chars copied");
}

// ─── Test 6: repeated keys (bonus=hp,5 + bonus=physical,2) ──────────────────

struct T6State { int bonus_count; };

static void t6_cb(const IniLine& l, void* ud) {
    T6State* s = (T6State*)ud;
    if (strcmp(l.key, "bonus") == 0) ++s->bonus_count;
}

static void test_repeated_keys() {
    T6State s = {};
    ParseIniFile("fixtures/sample_item.txt", t6_cb, &s, nullptr);
    check(s.bonus_count == 3,   "3 bonus= entries across all items");
}

// ─────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== IniParser tests ===\n");
    test_parse_all();
    test_section_filter();
    test_int_list();
    test_float_list();
    test_copy_string();
    test_repeated_keys();
    printf("=== %d / %d passed ===\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
