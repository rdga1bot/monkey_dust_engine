#include <monkey_dust/platform/md_fs.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace md {

uint32_t fs_read_all(const char* path, char* buf, uint32_t buf_sz) noexcept {
    if (!path || !buf || buf_sz == 0) return 0;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t n = (uint32_t)fread(buf, 1, buf_sz - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

char* fs_read_alloc(const char* path, uint32_t* out_size) noexcept {
    if (!path) return nullptr;
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    if (len <= 0) { fclose(f); return nullptr; }
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return nullptr; }
    uint32_t n = (uint32_t)fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[n] = '\0';
    if (out_size) *out_size = n;
    return buf;
}

void fs_free(char* buf) noexcept {
    free(buf);
}

bool fs_exists(const char* path) noexcept {
    if (!path) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

} // namespace md
