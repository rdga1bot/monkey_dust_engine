#include <monkey_dust/render/imgui_impl_renderingserver.h>

#ifdef MD_USE_LIBGODOT

#include "imgui.h"
#include "servers/rendering/rendering_server.h"
#include "core/io/image.h"

#include <vector>

namespace {

struct State {
    RID canvas;
    RID viewport;
    std::vector<RID> item_pool; // grows, never shrinks across frames
};

State& S() {
    static State s;
    return s;
}

// Dear ImGui 1.92's texture API: ImTextureData::Status transitions,
// НЕ стара одна-TexID модель. Backend реагує на Status, ніколи сам не
// вирішує коли текстура потрібна -- та сама послідовність, що
// docs/BACKENDS.md's upgrade guide вимагає від усіх renderer backend-ів.
void UpdateTexture(RenderingServer* rs, ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates) {
        Image::Format fmt = (tex->Format == ImTextureFormat_RGBA32) ? Image::FORMAT_RGBA8 : Image::FORMAT_L8;
        Vector<uint8_t> data;
        data.resize(tex->GetSizeInBytes());
        memcpy(data.ptrw(), tex->GetPixels(), tex->GetSizeInBytes());
        Ref<Image> img = Image::create_from_data(tex->Width, tex->Height, false, fmt, data);
        if (tex->Status == ImTextureStatus_WantCreate) {
            RID rid = rs->texture_2d_create(img);
            tex->SetTexID((ImTextureID)rid.get_id());
        } else {
            RID rid = RID::from_uint64((uint64_t)tex->TexID);
            rs->texture_2d_update(rid, img);
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        RID rid = RID::from_uint64((uint64_t)tex->TexID);
        if (rid.is_valid()) rs->free_rid(rid);
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

// Пул canvas_item-ів -- один item на ImDrawCmd цього кадру. Росте за
// потреби, НІКОЛИ не звужується (уникає per-frame RID create/free
// churn -- той самий "не мутуй/не пересоздавай щокадру" принцип, що
// LibGodotBridge's MultiMesh-групування).
RID PoolItem(RenderingServer* rs, int idx) {
    State& s = S();
    while ((int)s.item_pool.size() <= idx) {
        RID item = rs->canvas_item_create();
        rs->canvas_item_set_parent(item, s.canvas);
        s.item_pool.push_back(item);
    }
    return s.item_pool[idx];
}

Color UnpackColor(ImU32 c) {
    return Color(
        (float)((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
        (float)((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
        (float)((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
        (float)((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f);
}

} // namespace

bool ImGui_ImplRenderingServer_Init(uint64_t viewport_id) {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return false;
    State& s = S();
    s.viewport = RID::from_uint64(viewport_id);
    s.canvas = rs->canvas_create();
    rs->viewport_attach_canvas(s.viewport, s.canvas);

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_renderingserver";
    // Дозволяє ImGui самому керувати font-atlas/user-texture life-
    // cycle через ImTextureData -- REQUIRED у 1.92, старий SetTexID()-
    // на-старті шлях більше не існує.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    return true;
}

void ImGui_ImplRenderingServer_Shutdown() {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs) return;
    State& s = S();

    // Знищити усі текстури, що ImGui ще відстежує через platform IO --
    // той самий підхід, що офіційні backend-и (imgui_impl_vulkan.cpp
    // тощо) роблять у своєму Shutdown().
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    for (ImTextureData* tex : pio.Textures) {
        if (tex->Status == ImTextureStatus_OK || tex->Status == ImTextureStatus_WantUpdates) {
            RID rid = RID::from_uint64((uint64_t)tex->TexID);
            if (rid.is_valid()) rs->free_rid(rid);
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    for (RID item : s.item_pool) rs->free_rid(item);
    s.item_pool.clear();
    rs->free_rid(s.canvas);
    s.canvas = RID();
    s.viewport = RID();
}

void ImGui_ImplRenderingServer_NewFrame() {
    // No-op: display size у 1.92's моделі -- відповідальність
    // platform backend-у (io.DisplaySize), не renderer backend-у.
    // Функція існує лише для симетрії виклику з imgui_platform.h's
    // imgui_new_frame() (той самий контракт, що ImGui_ImplSDLGPU3_
    // NewFrame()/ImGui_ImplOpenGL3_NewFrame()).
}

void ImGui_ImplRenderingServer_RenderDrawData(ImDrawData* draw_data) {
    RenderingServer* rs = RenderingServer::get_singleton();
    if (!rs || !draw_data) return;

    if (draw_data->Textures != nullptr) {
        for (ImTextureData* tex : *draw_data->Textures) {
            if (tex->Status != ImTextureStatus_OK) UpdateTexture(rs, tex);
        }
    }

    int item_idx = 0;
    for (int n = 0; n < draw_data->CmdListsCount; ++n) {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        // Конвертація ПОВНОГО vertex-буфера ОДИН раз на cmd_list --
        // ми не опт-інимо в ImGuiBackendFlags_RendererHasVtxOffset,
        // тож ImDrawCmd::VtxOffset завжди 0 і IdxBuffer-індекси вже
        // валідні як прямі індекси в ЦЕЙ повний масив, без ремапу.
        Vector<Point2> points; points.resize(cmd_list->VtxBuffer.Size);
        Vector<Color> colors; colors.resize(cmd_list->VtxBuffer.Size);
        Vector<Point2> uvs; uvs.resize(cmd_list->VtxBuffer.Size);
        for (int i = 0; i < cmd_list->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = cmd_list->VtxBuffer[i];
            points.write[i] = Point2(v.pos.x, v.pos.y);
            uvs.write[i] = Point2(v.uv.x, v.uv.y);
            colors.write[i] = UnpackColor(v.col);
        }

        for (int c = 0; c < cmd_list->CmdBuffer.Size; ++c) {
            const ImDrawCmd& cmd = cmd_list->CmdBuffer[c];
            if (cmd.UserCallback) {
                if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(cmd_list, &cmd);
                continue;
            }
            if (cmd.ElemCount == 0) continue;

            Vector<int> indices;
            indices.resize((int)cmd.ElemCount);
            for (unsigned int i = 0; i < cmd.ElemCount; ++i)
                indices.write[i] = (int)cmd_list->IdxBuffer[cmd.IdxOffset + i];

            RID tex = RID::from_uint64((uint64_t)cmd.GetTexID());
            RID item = PoolItem(rs, item_idx++);
            rs->canvas_item_clear(item);
            // ВІДОМЕ СПРОЩЕННЯ (Фаза E.1's заявлений бар: "компілюється
            // й відкриває вікно", не піксель-точний паритет): per-
            // команда clip rect (cmd.ClipRect) ще НЕ підключено --
            // canvas_item_set_clip()/set_custom_rect() діють на РІВНІ
            // цілого item-а, не окремого add_triangle_array()-виклику
            // всередині нього, тож потребує окремого design pass
            // (можливо один item на унікальний clip rect, не на
            // команду). Малює все без scissor -- коректно для одного
            // full-screen panel, панелі з overlap/scroll-регіонами
            // матимуть візуальні артефакти до цього фіксу.
            rs->canvas_item_add_triangle_array(item, indices, points, colors, uvs,
                                                Vector<int>(), Vector<float>(), tex);
        }
    }

    // Приховати/очистити item-и пулу, що лишились від кадру з більшою
    // кількістю draw-команд.
    State& s = S();
    for (int i = item_idx; i < (int)s.item_pool.size(); ++i)
        rs->canvas_item_clear(s.item_pool[i]);
}

#endif // MD_USE_LIBGODOT
