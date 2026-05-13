#include <monkey_dust/ai/director_system.h>
#include <monkey_dust/ai/fnv.h>
#include <monkey_dust/platform/md_hints.h>
#include <monkey_dust/ecs/registry.h>
#include <monkey_dust/components/agent_state.h>
#include <monkey_dust/components/sense_component.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// strstr-based JSON parser — no external JSON libs (project rule).

DirectorSystem& DirectorSystem::Get() {
    static DirectorSystem inst;
    return inst;
}

const DirectorProfile* DirectorSystem::GetCurrentProfile() const {
    if (profile_count_ == 0) return nullptr;
    return &profiles_[profile_idx_];
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

static float read_json_float(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0.f;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return static_cast<float>(atof(p));
}

static int read_json_int(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return atoi(p);
}

static const char* read_json_str(const char* block, const char* key, char* out, int max) {
    const char* p = strstr(block, key);
    if (!p) { out[0] = '\0'; return block; }
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    if (*p == '"') ++p;
    int i = 0;
    while (*p && *p != '"' && i < max - 1) out[i++] = *p++;
    out[i] = '\0';
    return p;
}

// ── Init / Load ───────────────────────────────────────────────────────────────

void DirectorSystem::Init(const char* json_path) {
    profile_count_ = 0;
    profile_idx_   = 0;
    menace_        = 0.f;
    stage_         = DirectorStage::Unaware;

    FILE* f = fopen(json_path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "DirectorSystem: cannot open '%s' — using defaults", json_path);
        // Fallback default profile so Tick() is safe even without a file.
        DirectorProfile& def = profiles_[0];
        memset(&def, 0, sizeof(def));
        strncpy(def.name, "default", sizeof(def.name) - 1);
        def.gauge_fill_rate    = 0.05f;
        def.hunt_timeout_min_s = 30.f;
        def.hunt_timeout_max_s = 90.f;
        def.ambush_wait_s      = 15.f;
        def.trap_trigger_s     = 5.f;
        def.sweep_radius_m     = 20.f;
        def.max_menaces        = 1;
        profile_count_         = 1;
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 32768) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "DirectorSystem: '%s' too large or empty", json_path);
        return;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    // Iterate "name" keys inside the profiles array
    const char* p = buf;
    while (profile_count_ < MAX_PROFILES) {
        const char* name_key = strstr(p, "\"name\"");
        if (!name_key) break;

        // Find the enclosing object: scan back to '{', forward to '}'
        const char* obj_start = name_key;
        while (obj_start > buf && *obj_start != '{') --obj_start;
        const char* obj_end = strstr(name_key, "}");
        if (!obj_end) obj_end = buf + sz;

        size_t block_len = static_cast<size_t>(obj_end - obj_start);
        if (block_len > 1024) block_len = 1024;
        char block[1024] = {};
        memcpy(block, obj_start, block_len);

        DirectorProfile& pr = profiles_[profile_count_];
        memset(&pr, 0, sizeof(pr));

        read_json_str(block, "\"name\"",            pr.name, sizeof(pr.name));
        pr.gauge_fill_rate    = read_json_float(block, "\"gauge_fill_rate\"");
        pr.hunt_timeout_min_s = read_json_float(block, "\"hunt_timeout_min_s\"");
        pr.hunt_timeout_max_s = read_json_float(block, "\"hunt_timeout_max_s\"");
        pr.ambush_wait_s      = read_json_float(block, "\"ambush_wait_s\"");
        pr.trap_trigger_s     = read_json_float(block, "\"trap_trigger_s\"");
        pr.sweep_radius_m     = read_json_float(block, "\"sweep_radius_m\"");
        pr.max_menaces        = read_json_int  (block, "\"max_menaces\"");

        if (pr.gauge_fill_rate <= 0.f) pr.gauge_fill_rate = 0.05f;

        ++profile_count_;
        p = obj_end + 1;
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "DirectorSystem: loaded %d profiles from '%s'", profile_count_, json_path);
}

// ── SetProfile ────────────────────────────────────────────────────────────────

void DirectorSystem::SetProfile(const char* name) {
    for (int i = 0; i < profile_count_; ++i) {
        if (strncmp(profiles_[i].name, name, sizeof(profiles_[0].name)) == 0) {
            profile_idx_ = i;
            return;
        }
    }
    MD_LOG(MD_LOG_WARNING, "DirectorSystem: profile '%s' not found", name);
}

// ── Tick ──────────────────────────────────────────────────────────────────────

static constexpr uint32_t K_MENACE = md::fnv1a("menace");
static constexpr uint32_t K_STAGE  = md::fnv1a("director_stage");

void DirectorSystem::Tick(float dt) {
    if (profile_count_ == 0) return;
    const DirectorProfile& pr = profiles_[profile_idx_];

    // 1. Find max visual activation across all sensed entities
    float max_activation = 0.f;
    auto& reg = Registry::Get();
    {
        auto view = reg.view<SenseComponent>();
        for (auto [e, sc] : view.each()) {
            if (sc.activation[0] > max_activation)
                max_activation = sc.activation[0];
        }
    }

    // 2. Accumulate/decay menace
    // Fill when any NPC perceives above their lo-threshold; decay otherwise.
    // Decay is half the fill rate so menace is "sticky" (CATHODE behavior).
    if (MD_UNLIKELY(max_activation > 0.3f)) {
        menace_ += pr.gauge_fill_rate * dt;
    } else {
        menace_ -= pr.gauge_fill_rate * 0.5f * dt;
    }
    menace_ = menace_ < 0.f ? 0.f : menace_ > 1.f ? 1.f : menace_;

    // 3. Update stage
    if      (menace_ < 0.25f) stage_ = DirectorStage::Unaware;
    else if (menace_ < 0.50f) stage_ = DirectorStage::Suspicious;
    else if (menace_ < 0.75f) stage_ = DirectorStage::Hunting;
    else                       stage_ = DirectorStage::Intense;

    // 4. Broadcast to all AgentBlackboards
    {
        auto view = reg.view<AgentState>();
        for (auto [e, as] : view.each()) {
            bb_set_float(as, K_MENACE, menace_);
            bb_set_int  (as, K_STAGE,  static_cast<int32_t>(stage_));
        }
    }
}
