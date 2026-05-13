#include <monkey_dust/scripting/flow_graph.h>
#include <monkey_dust/platform/md_log.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// ── Init ──────────────────────────────────────────────────────────────────────

void FlowGraph::Init() {
    memset(this, 0, sizeof(*this));
}

// ── Ring buffer ───────────────────────────────────────────────────────────────

bool FlowGraph::ring_push(const FlowPendingTrigger& t) {
    int next = (pending_tail + 1) & (MAX_PENDING - 1);
    if (next == pending_head) {
        MD_LOG(MD_LOG_WARNING, "FlowGraph: pending ring buffer full — trigger dropped");
        return false;
    }
    pending[pending_tail] = t;
    pending_tail = next;
    return true;
}

bool FlowGraph::ring_pop(FlowPendingTrigger& t) {
    if (pending_head == pending_tail) return false;
    t = pending[pending_head];
    pending_head = (pending_head + 1) & (MAX_PENDING - 1);
    return true;
}

bool FlowGraph::ring_peek(FlowPendingTrigger& t) const {
    if (pending_head == pending_tail) return false;
    t = pending[pending_head];
    return true;
}

// ── Node / action lookup ──────────────────────────────────────────────────────

FlowNode* FlowGraph::find_node(uint32_t id) {
    for (int i = 0; i < node_count; ++i)
        if (nodes[i].id == id) return &nodes[i];
    return nullptr;
}

FlowActionFunc FlowGraph::find_action(uint32_t node_id) const {
    for (int i = 0; i < action_count; ++i)
        if (actions[i].node_id == node_id) return actions[i].func;
    return nullptr;
}

// ── Variable accessors ────────────────────────────────────────────────────────

float FlowGraph::GetVar(uint32_t key, float def) const {
    for (int i = 0; i < var_count; ++i)
        if (vars[i].key == key) return vars[i].value;
    return def;
}

void FlowGraph::SetVar(uint32_t key, float value) {
    for (int i = 0; i < var_count; ++i) {
        if (vars[i].key == key) { vars[i].value = value; return; }
    }
    if (var_count < MAX_VARS) {
        vars[var_count++] = { key, value };
    } else {
        MD_LOG(MD_LOG_WARNING, "FlowGraph: var table full, key=0x%08X dropped", key);
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void FlowGraph::FireTrigger(uint32_t node_id, double fire_at_s) {
    ring_push({ node_id, FLOW_INVALID_CONN, fire_at_s });
}

void FlowGraph::RegisterAction(uint32_t node_id, FlowActionFunc func) {
    for (int i = 0; i < action_count; ++i) {
        if (actions[i].node_id == node_id) { actions[i].func = func; return; }
    }
    if (action_count < MAX_ACTIONS) {
        actions[action_count++] = { node_id, func };
    } else {
        MD_LOG(MD_LOG_WARNING, "FlowGraph: action table full, node=0x%08X dropped", node_id);
    }
}

// ── Propagate ─────────────────────────────────────────────────────────────────
// Fire all outgoing connections from from_node_id that match from_port.

void FlowGraph::propagate(uint32_t from_node_id, uint8_t from_port,
                          double now_s, entt::entity /*ctx*/, entt::registry& /*reg*/) {
    for (int i = 0; i < conn_count; ++i) {
        if (conns[i].from_node == from_node_id && conns[i].from_port == from_port) {
            ring_push({ conns[i].to_node, static_cast<uint32_t>(i), now_s });
        }
    }
}

// ── Tick ──────────────────────────────────────────────────────────────────────

void FlowGraph::Tick(double now_s, entt::entity ctx, entt::registry& reg) {
    // Drain ring buffer — process only triggers with fire_at_s <= now_s.
    // Stop at first future trigger to preserve ordering (ring is FIFO).
    // Limit iterations to MAX_PENDING to prevent infinite propagation loops.
    int iterations = 0;
    while (iterations++ < MAX_PENDING) {
        FlowPendingTrigger t;
        if (!ring_peek(t)) break;
        if (t.fire_at_s > now_s) break;
        ring_pop(t);

        FlowNode* nd = find_node(t.node_id);
        if (!nd) continue;

        switch (nd->type) {
        case 0: // Event — propagate all outgoing connections on port 0
            propagate(nd->id, 0, now_s, ctx, reg);
            break;

        case 1: { // Condition — check FlowVar; true→port0, false→port1
            uint32_t var_key = (nd->param_offset < MAX_PARAMS)
                               ? static_cast<uint32_t>(params[nd->param_offset])
                               : 0u;
            float val = GetVar(var_key);
            propagate(nd->id, val != 0.f ? 0u : 1u, now_s, ctx, reg);
            break;
        }

        case 2: { // Action — call registered callback then propagate port 0
            FlowActionFunc fn = find_action(nd->id);
            if (fn) fn(nd->id, now_s, ctx, reg);
            propagate(nd->id, 0, now_s, ctx, reg);
            break;
        }

        case 3: { // Delay — queue outgoing conns to fire at now_s + delay_s
            float  delay_s  = (nd->param_offset < MAX_PARAMS) ? params[nd->param_offset] : 0.f;
            double fire_at  = now_s + static_cast<double>(delay_s);
            for (int i = 0; i < conn_count; ++i) {
                if (conns[i].from_node == nd->id && conns[i].from_port == 0)
                    ring_push({ conns[i].to_node, static_cast<uint32_t>(i), fire_at });
            }
            break;
        }

        case 4: { // Variable — set FlowVar from params, propagate port 0
            if (nd->param_offset + 1 < MAX_PARAMS) {
                uint32_t key = static_cast<uint32_t>(params[nd->param_offset]);
                float    val = params[nd->param_offset + 1];
                SetVar(key, val);
            }
            propagate(nd->id, 0, now_s, ctx, reg);
            break;
        }

        default:
            break;
        }
    }
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

static const char* jstr(const char* block, const char* key, char* out, int max) {
    const char* p = strstr(block, key);
    if (!p) { if (out) out[0] = '\0'; return block; }
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    if (*p == '"') ++p;
    int i = 0;
    while (out && *p && *p != '"' && i < max - 1) out[i++] = *p++;
    if (out) out[i] = '\0';
    return p;
}

static uint32_t jstr_id(const char* block, const char* key) {
    char buf[64] = {};
    jstr(block, key, buf, sizeof(buf));
    return buf[0] ? md::fnv1a_rt(buf) : 0u;
}

static int jint(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return atoi(p);
}

static float jfloat(const char* block, const char* key) {
    const char* p = strstr(block, key);
    if (!p) return 0.f;
    p += strlen(key);
    while (*p == '"' || *p == ':' || *p == ' ') ++p;
    return static_cast<float>(atof(p));
}

static uint8_t jtype(const char* block) {
    const char* p = strstr(block, "\"type\"");
    if (!p) return 0;
    p += 6;
    while (*p == ':' || *p == ' ' || *p == '"') ++p;
    if (strncmp(p, "Event",     5) == 0) return 0;
    if (strncmp(p, "Condition", 9) == 0) return 1;
    if (strncmp(p, "Action",    6) == 0) return 2;
    if (strncmp(p, "Delay",     5) == 0) return 3;
    if (strncmp(p, "Variable",  8) == 0) return 4;
    return static_cast<uint8_t>(atoi(p));
}

// ── LoadFromJson ──────────────────────────────────────────────────────────────

bool FlowGraph::LoadFromJson(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "FlowGraph: cannot open '%s'", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "FlowGraph: '%s' too large or empty", path);
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    // Reset counts (keep registered actions)
    node_count = conn_count = var_count = params_count = 0;
    pending_head = pending_tail = 0;

    // Parse nodes array
    const char* p = buf;
    const char* nodes_arr = strstr(p, "\"nodes\"");
    if (nodes_arr) {
        const char* arr_end = strstr(nodes_arr, "]");
        const char* cursor = nodes_arr;
        while (node_count < MAX_NODES) {
            const char* obj = strstr(cursor, "\"name\"");
            if (!obj || (arr_end && obj > arr_end)) break;
            const char* obj_start = obj; while (obj_start > buf && *obj_start != '{') --obj_start;
            const char* obj_end   = strstr(obj, "}"); if (!obj_end) obj_end = buf + sz;
            size_t blen = static_cast<size_t>(obj_end - obj_start); if (blen > 512) blen = 512;
            char block[512] = {}; memcpy(block, obj_start, blen);

            FlowNode& nd    = nodes[node_count];
            nd.id           = jstr_id(block, "\"name\"");
            nd.type         = jtype(block);
            nd.conn_count   = 0;
            nd.param_offset = static_cast<uint16_t>(params_count);

            // Delay node: params[0] = delay_s
            if (nd.type == 3 && params_count < MAX_PARAMS)
                params[params_count++] = jfloat(block, "\"delay_s\"");
            // Variable node: params[0] = key (as float), params[1] = value
            if (nd.type == 4 && params_count + 1 < MAX_PARAMS) {
                char kbuf[64] = {};
                jstr(block, "\"var\"", kbuf, sizeof(kbuf));
                params[params_count++] = static_cast<float>(kbuf[0] ? md::fnv1a_rt(kbuf) : 0u);
                params[params_count++] = jfloat(block, "\"value\"");
            }
            // Condition node: params[0] = var key (as float)
            if (nd.type == 1 && params_count < MAX_PARAMS) {
                char kbuf[64] = {};
                jstr(block, "\"var\"", kbuf, sizeof(kbuf));
                params[params_count++] = static_cast<float>(kbuf[0] ? md::fnv1a_rt(kbuf) : 0u);
            }
            if (nd.id) ++node_count;
            cursor = obj_end + 1;
        }
    }

    // Parse connections array
    const char* conns_arr = strstr(buf, "\"connections\"");
    if (conns_arr) {
        const char* arr_end = strstr(conns_arr, "]");
        const char* cursor = conns_arr;
        while (conn_count < MAX_CONNS) {
            const char* obj = strstr(cursor, "\"from\"");
            if (!obj || (arr_end && obj > arr_end)) break;
            const char* obj_start = obj; while (obj_start > buf && *obj_start != '{') --obj_start;
            const char* obj_end   = strstr(obj, "}"); if (!obj_end) obj_end = buf + sz;
            size_t blen = static_cast<size_t>(obj_end - obj_start); if (blen > 512) blen = 512;
            char block[512] = {}; memcpy(block, obj_start, blen);

            FlowConn& c  = conns[conn_count];
            c.from_node  = jstr_id(block, "\"from\"");
            c.to_node    = jstr_id(block, "\"to\"");
            c.from_port  = static_cast<uint8_t>(jint(block, "\"from_port\""));
            c.to_port    = static_cast<uint8_t>(jint(block, "\"to_port\""));
            c._pad[0] = c._pad[1] = 0;

            // Update conn_count on source node
            FlowNode* src = find_node(c.from_node);
            if (src) src->conn_count++;

            if (c.from_node && c.to_node) ++conn_count;
            cursor = obj_end + 1;
        }
    }

    // Parse variables array
    const char* vars_arr = strstr(buf, "\"variables\"");
    if (vars_arr) {
        const char* arr_end = strstr(vars_arr, "]");
        const char* cursor = vars_arr;
        while (var_count < MAX_VARS) {
            const char* obj = strstr(cursor, "\"key\"");
            if (!obj || (arr_end && obj > arr_end)) break;
            const char* obj_start = obj; while (obj_start > buf && *obj_start != '{') --obj_start;
            const char* obj_end   = strstr(obj, "}"); if (!obj_end) obj_end = buf + sz;
            size_t blen = static_cast<size_t>(obj_end - obj_start); if (blen > 256) blen = 256;
            char block[256] = {}; memcpy(block, obj_start, blen);

            vars[var_count].key   = jstr_id(block, "\"key\"");
            vars[var_count].value = jfloat(block, "\"value\"");
            if (vars[var_count].key) ++var_count;
            cursor = obj_end + 1;
        }
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "FlowGraph: loaded '%s' — %d nodes, %d conns, %d vars",
           path, node_count, conn_count, var_count);
    return node_count > 0;
}
