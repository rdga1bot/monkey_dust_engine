#pragma once
#include <cstdint>

struct MdColor { uint8_t r, g, b, a; };

static constexpr MdColor MD_WHITE     = {255,255,255,255};
static constexpr MdColor MD_BLACK     = {  0,  0,  0,255};
static constexpr MdColor MD_GREEN     = {  0,228, 48,255};
static constexpr MdColor MD_RED       = {230, 41, 55,255};
static constexpr MdColor MD_YELLOW    = {253,249,  0,255};
static constexpr MdColor MD_GRAY      = {130,130,130,255};
static constexpr MdColor MD_DARKGRAY  = { 80, 80, 80,255};
static constexpr MdColor MD_LIME      = {  0,158, 47,255};
static constexpr MdColor MD_GOLD      = {255,203,  0,255};
static constexpr MdColor MD_ORANGE    = {255,161,  0,255};
static constexpr MdColor MD_LIGHTGRAY = {200,200,200,255};

// Call once after GL context is created. font_path may be nullptr for rect/line-only mode.
void MdDraw2DInit(const char* font_path);
void MdDraw2DShutdown();

// Bracket all 2D draw calls with Begin/End.
// Begin sets up ortho projection; End flushes the batch.
void MdDraw2DBegin(int screen_w, int screen_h);
void MdDraw2DEnd();

void MdDrawRect(int x, int y, int w, int h, MdColor c);
void MdDrawRectLines(int x, int y, int w, int h, int thick, MdColor c);
void MdDrawLine2D(int x0, int y0, int x1, int y1, MdColor c);
void MdDrawText(const char* text, int x, int y, int size, MdColor c);
int  MdMeasureText(const char* text, int size);
