#pragma once
// MdShader — minimal vert+frag shader wrapper using direct glad (no Raylib).
// Rule M-A: only used under MD_OPENGL43_ENABLED.
// Under !MD_OPENGL43_ENABLED all functions are no-ops / return {}.

struct MdShader { unsigned int id = 0; };

#ifdef MD_OPENGL43_ENABLED

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

#else
// ── no-op stubs ──────────────────────────────────────────────────────────────
inline MdShader MdLoadShader(const char*, const char*) { return {}; }
inline void     MdUnloadShader(MdShader&)              {}
inline int      MdGetLoc(MdShader, const char*)        { return -1; }
inline void     MdSetFloat   (int,float)               {}
inline void     MdSetInt     (int,int)                 {}
inline void     MdSetVec3    (int,const float*)        {}
inline void     MdSetVec4    (int,const float*)        {}
inline void     MdSetMat4    (int,const float*)        {}
inline void     MdSetFloatArr(int,const float*,int)    {}
inline void     MdSetVec4Arr (int,const float*,int)    {}
inline void     MdUseShader  (MdShader)                {}
inline void     MdStopShader ()                        {}
#endif
