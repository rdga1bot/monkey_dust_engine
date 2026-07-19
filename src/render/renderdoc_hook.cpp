#include <monkey_dust/render/renderdoc_hook.h>
#include <renderdoc_app.h>
#include <dlfcn.h>
#include <cstdio>

static RENDERDOC_API_1_1_2* s_api = nullptr;
static bool s_tried = false;
static bool s_armed = false;
static bool s_capturing = false;

bool RenderDocHook_Init() {
    if (s_tried) return s_api != nullptr;
    s_tried = true;

    void* mod = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
    if (!mod) {
        fprintf(stderr, "[RenderDocHook] librenderdoc.so not loaded in this process (not running under renderdoccmd)\n");
        return false;
    }
    pRENDERDOC_GetAPI get_api = (pRENDERDOC_GetAPI)dlsym(mod, "RENDERDOC_GetAPI");
    if (!get_api) {
        fprintf(stderr, "[RenderDocHook] RENDERDOC_GetAPI symbol not found\n");
        return false;
    }
    int ok = get_api(eRENDERDOC_API_Version_1_1_2, (void**)&s_api);
    if (!ok || !s_api) {
        fprintf(stderr, "[RenderDocHook] RENDERDOC_GetAPI call failed\n");
        s_api = nullptr;
        return false;
    }
    fprintf(stdout, "[RenderDocHook] connected to RenderDoc API\n");
    return true;
}

void RenderDocHook_ArmCapture() {
    if (!RenderDocHook_Init()) return;
    s_armed = true;
    fprintf(stdout, "[RenderDocHook] capture armed for next frame\n");
}

void RenderDocHook_BeginFrame() {
    if (!s_armed || !s_api) return;
    s_armed = false;
    s_api->StartFrameCapture(nullptr, nullptr);
    s_capturing = true;
    fprintf(stdout, "[RenderDocHook] StartFrameCapture() called\n");
}

void RenderDocHook_EndFrame() {
    if (!s_capturing || !s_api) return;
    s_capturing = false;
    uint32_t ok = s_api->EndFrameCapture(nullptr, nullptr);
    fprintf(stdout, "[RenderDocHook] EndFrameCapture() -> %s\n", ok ? "OK (.rdc written)" : "FAILED");
}
