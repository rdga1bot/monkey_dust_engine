#include <monkey_dust/combat/power_manager.h>
#include <monkey_dust/platform/md_log.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

const PowerDef* PowerManager::Find(int id) const {
    for (int i = 0; i < count_; ++i)
        if (powers_[i].id == id) return &powers_[i];
    return nullptr;
}

bool PowerManager::LoadFromJson(const char* path) {
    count_ = 0;

    FILE* f = fopen(path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "PowerManager: cannot open '%s'", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) {
        fclose(f);
        MD_LOG(MD_LOG_WARNING, "PowerManager: '%s' too large or empty", path);
        return false;
    }
    char* buf = static_cast<char*>(malloc(static_cast<size_t>(sz) + 1));
    if (!buf) { fclose(f); return false; }
    fread(buf, 1, static_cast<size_t>(sz), f);
    buf[sz] = '\0';
    fclose(f);

    const char* p = buf;
    while (count_ < 256) {
        const char* id_key = strstr(p, "\"id\"");
        if (!id_key) break;
        const char* obj_start = id_key;
        while (obj_start > buf && *obj_start != '{') --obj_start;
        const char* obj_end = strstr(id_key, "}");
        if (!obj_end) obj_end = buf + sz;

        size_t blen = static_cast<size_t>(obj_end - obj_start);
        if (blen > 256) blen = 256;
        char block[256] = {};
        memcpy(block, obj_start, blen);

        PowerDef& pd = powers_[count_];
        memset(&pd, 0, sizeof(pd));

        const char* pid = strstr(block, "\"id\"");
        if (pid) { pid += 4; while (*pid==':'||*pid==' ') ++pid; pd.id = atoi(pid); }

        const char* pn = strstr(block, "\"name\"");
        if (pn) {
            pn += 6; while (*pn==':'||*pn==' '||*pn=='"') ++pn;
            int i = 0;
            while (*pn && *pn != '"' && i < (int)sizeof(pd.name) - 1) pd.name[i++] = *pn++;
            pd.name[i] = '\0';
        }

        const char* pdt = strstr(block, "\"dmg_type\"");
        if (pdt) {
            pdt += 10; while (*pdt==':'||*pdt==' '||*pdt=='"') ++pdt;
            int i = 0;
            while (*pdt && *pdt != '"' && i < (int)sizeof(pd.dmg_type) - 1) pd.dmg_type[i++] = *pdt++;
            pd.dmg_type[i] = '\0';
        }

        const char* pr = strstr(block, "\"radius\"");
        if (pr) { pr += 8; while (*pr==':'||*pr==' ') ++pr; pd.radius = (float)atof(pr); }

        const char* pc = strstr(block, "\"cooldown_ms\"");
        if (pc) { pc += 13; while (*pc==':'||*pc==' ') ++pc; pd.cooldown_ms = atoi(pc); }

        if (pd.id != 0) ++count_;
        p = obj_end + 1;
    }

    free(buf);
    MD_LOG(MD_LOG_INFO, "PowerManager: loaded %d powers from '%s'", count_, path);
    return count_ > 0;
}
