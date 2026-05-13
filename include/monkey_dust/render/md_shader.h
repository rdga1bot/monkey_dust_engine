#pragma once
// MdShader — minimal vert+frag shader wrapper using direct glad (no Raylib).
// Active under MD_OPENGL43_ENABLED or MD_SDL_GPU (GL fallback path for standalone editor).
// Under neither flag all functions are no-ops / return {}.

struct MdShader { unsigned int id = 0; };


MdShader MdLoadShader(const char* vert_path, const char* frag_path);
void     MdUnloadShader(MdShader& s);

int  MdGetLoc(MdShader s, const char* name);

void MdSetFloat   (int loc, float v);
void MdSetInt     (int loc, int v);
void MdSetVec3    (int loc, const float* v);
void MdSetVec4    (int loc, const float* v);
void MdSetMat4    (int loc, const float* m);
void MdSetFloatArr(int loc, const float* v, int n);
void MdSetVec4Arr (int loc, const float* v, int n);

void MdUseShader ();  // same as MdUseShader(s) but called after glUseProgram elsewhere
void MdUseShader (MdShader s);
void MdStopShader();

