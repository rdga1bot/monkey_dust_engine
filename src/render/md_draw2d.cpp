#include <monkey_dust/render/md_draw2d.h>
#include <monkey_dust/platform/md_log.h>
#include "glad.h"
#include <cstdio>
#include <cstring>
#include <cmath>

#undef  STBTT_DEF
#define STBTT_DEF static          // give all stbtt symbols internal linkage — avoids conflict with Raylib
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

static constexpr int   FONT_ATLAS   = 512;
static constexpr int   FONT_FIRST   = 32;
static constexpr int   FONT_COUNT   = 96;
static constexpr float FONT_BAKE_SZ = 20.0f;
static constexpr int   MAX_VERTS    = 8192 * 6;

struct HudVert { float x, y, u, v; uint8_t r, g, b, a; };

static GLuint s_vao       = 0;
static GLuint s_vbo       = 0;
static GLuint s_prog      = 0;
static GLuint s_font_tex  = 0;
static int    s_loc_proj  = -1;
static int    s_loc_font  = -1;

static stbtt_bakedchar s_cdata[FONT_COUNT];
static float           s_ascent_px   = 15.0f;
static bool            s_font_loaded = false;

static HudVert s_verts[MAX_VERTS];
static int     s_vert_count = 0;

// -- inline shader sources --------------------------------------------------

static const char* VERT_SRC = R"(#version 430 core
layout(location=0) in vec2 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_color;
uniform mat4 u_proj;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_uv    = a_uv;
    v_color = a_color;
})";

static const char* FRAG_SRC = R"(#version 430 core
uniform sampler2D u_font;
in vec2 v_uv;
in vec4 v_color;
out vec4 frag_color;
void main() {
    if (v_uv.x < 0.0) {
        frag_color = v_color;
    } else {
        float a = texture(u_font, v_uv).r;
        frag_color = vec4(v_color.rgb, v_color.a * a);
    }
})";

// --------------------------------------------------------------------------

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[256]; glGetShaderInfoLog(s, 256, nullptr, log);
        MD_LOG(MD_LOG_WARNING, "[MdDraw2D] shader error: %s", log);
    }
    return s;
}

static bool LoadFont(const char* path) {
    static unsigned char font_buf[1 << 20]; // 1 MB
    static unsigned char bitmap[FONT_ATLAS * FONT_ATLAS];

    FILE* f = fopen(path, "rb");
    if (!f) {
        MD_LOG(MD_LOG_WARNING, "[MdDraw2D] font not found: %s", path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz > (long)sizeof(font_buf)) { fclose(f); return false; }
    fread(font_buf, 1, (size_t)fsz, f);
    fclose(f);

    int ret = stbtt_BakeFontBitmap(font_buf, 0, FONT_BAKE_SZ,
                                   bitmap, FONT_ATLAS, FONT_ATLAS,
                                   FONT_FIRST, FONT_COUNT, s_cdata);
    if (ret <= 0) {
        MD_LOG(MD_LOG_WARNING, "[MdDraw2D] font bake failed: %s", path);
        return false;
    }

    s_ascent_px = 0.0f;
    for (int i = 0; i < FONT_COUNT; ++i)
        if (-s_cdata[i].yoff > s_ascent_px) s_ascent_px = -s_cdata[i].yoff;

    glGenTextures(1, &s_font_tex);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED,
                 FONT_ATLAS, FONT_ATLAS, 0,
                 GL_RED, GL_UNSIGNED_BYTE, bitmap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

static void Flush() {
    if (s_vert_count == 0) return;
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, s_vert_count * (int)sizeof(HudVert), s_verts);
    glDrawArrays(GL_TRIANGLES, 0, s_vert_count);
    glBindVertexArray(0);
    s_vert_count = 0;
}

static void PushVert(float x, float y, float u, float v, MdColor c) {
    if (s_vert_count >= MAX_VERTS) Flush();
    HudVert& hv = s_verts[s_vert_count++];
    hv.x = x; hv.y = y; hv.u = u; hv.v = v;
    hv.r = c.r; hv.g = c.g; hv.b = c.b; hv.a = c.a;
}

static void PushSolidQuad(float x, float y, float w, float h, MdColor c) {
    constexpr float S = -1.0f; // UV sentinel for solid color
    PushVert(x,   y,   S,S, c); PushVert(x+w, y,   S,S, c); PushVert(x+w, y+h, S,S, c);
    PushVert(x,   y,   S,S, c); PushVert(x+w, y+h, S,S, c); PushVert(x,   y+h, S,S, c);
}

static void PushTextQuad(float x0, float y0, float x1, float y1,
                          float s0, float t0, float s1, float t1, MdColor c) {
    // stbtt UV y is top-down; flip for GL (bottom-up)
    float ft0 = 1.0f - t0, ft1 = 1.0f - t1;
    PushVert(x0, y0, s0,ft0, c); PushVert(x1, y0, s1,ft0, c); PushVert(x1, y1, s1,ft1, c);
    PushVert(x0, y0, s0,ft0, c); PushVert(x1, y1, s1,ft1, c); PushVert(x0, y1, s0,ft1, c);
}

// --------------------------------------------------------------------------

void MdDraw2DInit(const char* font_path) {
    GLuint vs = CompileShader(GL_VERTEX_SHADER,   VERT_SRC);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, FRAG_SRC);
    s_prog = glCreateProgram();
    glAttachShader(s_prog, vs); glAttachShader(s_prog, fs);
    glLinkProgram(s_prog);
    glDeleteShader(vs); glDeleteShader(fs);

    s_loc_proj = glGetUniformLocation(s_prog, "u_proj");
    s_loc_font = glGetUniformLocation(s_prog, "u_font");

    glGenVertexArrays(1, &s_vao);
    glGenBuffers(1, &s_vbo);
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTS * (int)sizeof(HudVert), nullptr, GL_DYNAMIC_DRAW);
    constexpr int S = (int)sizeof(HudVert);
    glVertexAttribPointer(0, 2, GL_FLOAT,         GL_FALSE, S, (void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT,         GL_FALSE, S, (void*)(2*sizeof(float)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE,  S, (void*)(4*sizeof(float)));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    if (font_path) s_font_loaded = LoadFont(font_path);
}

void MdDraw2DShutdown() {
    if (s_font_tex) { glDeleteTextures(1, &s_font_tex); s_font_tex = 0; }
    if (s_vbo)      { glDeleteBuffers(1, &s_vbo);       s_vbo = 0; }
    if (s_vao)      { glDeleteVertexArrays(1, &s_vao);  s_vao = 0; }
    if (s_prog)     { glDeleteProgram(s_prog);           s_prog = 0; }
    s_font_loaded = false;
}

void MdDraw2DBegin(int sw, int sh) {
    s_vert_count = 0;

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(s_prog);

    // Ortho: top-left = (0,0), bottom-right = (sw,sh)
    float proj[16] = {
         2.0f/(float)sw,  0,  0,  0,
         0, -2.0f/(float)sh,  0,  0,
         0,  0, -1,  0,
        -1,  1,  0,  1
    };
    glUniformMatrix4fv(s_loc_proj, 1, GL_FALSE, proj);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_font_tex);
    glUniform1i(s_loc_font, 0);
}

void MdDraw2DEnd() {
    Flush();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void MdDrawRect(int x, int y, int w, int h, MdColor c) {
    PushSolidQuad((float)x, (float)y, (float)w, (float)h, c);
}

void MdDrawRectLines(int x, int y, int w, int h, int thick, MdColor c) {
    if (thick < 1) thick = 1;
    PushSolidQuad((float)x,         (float)y,          (float)w,     (float)thick, c);
    PushSolidQuad((float)x,         (float)(y+h-thick),(float)w,     (float)thick, c);
    PushSolidQuad((float)x,         (float)(y+thick),  (float)thick, (float)(h-2*thick), c);
    PushSolidQuad((float)(x+w-thick),(float)(y+thick), (float)thick, (float)(h-2*thick), c);
}

void MdDrawLine2D(int x0, int y0, int x1, int y1, MdColor c) {
    float dx = (float)(x1-x0), dy = (float)(y1-y0);
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 0.5f) return;
    float px = -dy/len * 0.5f, py = dx/len * 0.5f;
    float ax=(float)x0+px, ay=(float)y0+py;
    float bx=(float)x1+px, by=(float)y1+py;
    float cx=(float)x1-px, cy=(float)y1-py;
    float ddx=(float)x0-px, ddy=(float)y0-py;
    constexpr float S = -1.0f;
    PushVert(ax,ay,S,S,c); PushVert(bx,by,S,S,c); PushVert(cx,cy,S,S,c);
    PushVert(ax,ay,S,S,c); PushVert(cx,cy,S,S,c); PushVert(ddx,ddy,S,S,c);
}

void MdDrawText(const char* text, int x, int y, int size, MdColor c) {
    if (!s_font_loaded || !text || size <= 0) return;
    float scale    = (float)size / FONT_BAKE_SZ;
    float baseline = (float)y + s_ascent_px * scale;
    float bx = 0.0f, by_acc = 0.0f;

    while (*text) {
        int ch = (unsigned char)*text++;
        if (ch < FONT_FIRST || ch >= FONT_FIRST + FONT_COUNT) {
            if (ch == ' ') bx += s_cdata[0].xadvance; // use space char advance
            continue;
        }
        stbtt_aligned_quad q;
        float tmp_bx = bx;
        stbtt_GetBakedQuad(s_cdata, FONT_ATLAS, FONT_ATLAS,
                           ch - FONT_FIRST, &bx, &by_acc, &q, 1);
        float sx0 = (float)x + q.x0 * scale;
        float sy0 = baseline + q.y0 * scale;
        float sx1 = (float)x + q.x1 * scale;
        float sy1 = baseline + q.y1 * scale;
        (void)tmp_bx;
        PushTextQuad(sx0, sy0, sx1, sy1, q.s0, q.t0, q.s1, q.t1, c);
    }
}

int MdMeasureText(const char* text, int size) {
    if (!s_font_loaded || !text || size <= 0) return 0;
    float scale = (float)size / FONT_BAKE_SZ;
    float w = 0.0f;
    while (*text) {
        int ch = (unsigned char)*text++;
        if (ch < FONT_FIRST || ch >= FONT_FIRST + FONT_COUNT) continue;
        w += s_cdata[ch - FONT_FIRST].xadvance;
    }
    return (int)(w * scale);
}
