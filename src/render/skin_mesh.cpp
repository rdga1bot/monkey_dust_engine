// SkinMesh — GLB loader with skinning data + CPU animation playback.
#include <monkey_dust/render/skin_mesh.h>
#include "cgltf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ── Math helpers ─────────────────────────────────────────────────────────────

void SkinMesh::mat4_identity(float* m) {
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

void SkinMesh::mat4_from_tq(float* m, const float* t, const float* q) {
    float qx=q[0],qy=q[1],qz=q[2],qw=q[3];
    float x2=qx*2,y2=qy*2,z2=qz*2;
    float xx=qx*x2,yy=qy*y2,zz=qz*z2;
    float xy=qx*y2,xz=qx*z2,yz=qy*z2;
    float wx=qw*x2,wy=qw*y2,wz=qw*z2;
    m[0]=1-(yy+zz); m[1]=(xy+wz);   m[2]=(xz-wy);   m[3]=0;
    m[4]=(xy-wz);   m[5]=1-(xx+zz); m[6]=(yz+wx);   m[7]=0;
    m[8]=(xz+wy);   m[9]=(yz-wx);   m[10]=1-(xx+yy);m[11]=0;
    m[12]=t[0];     m[13]=t[1];     m[14]=t[2];     m[15]=1;
}

void SkinMesh::mat4_mul(float* out, const float* a, const float* b) {
    float tmp[16];
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[r*4+k] * b[k*4+c];
            tmp[r*4+c] = s;
        }
    memcpy(out, tmp, 64);
}

void SkinMesh::quat_lerp(float* out, const float* a, const float* b, float t) {
    float dot = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];
    float s   = (dot < 0.f) ? -1.f : 1.f;
    for (int i = 0; i < 4; ++i) out[i] = a[i]*(1.f-t) + b[i]*s*t;
    float len = sqrtf(out[0]*out[0]+out[1]*out[1]+out[2]*out[2]+out[3]*out[3]);
    if (len > 1e-9f) for (int i = 0; i < 4; ++i) out[i] /= len;
}

void SkinMesh::lerp3(float* out, const float* a, const float* b, float t) {
    for (int i = 0; i < 3; ++i) out[i] = a[i]*(1.f-t) + b[i]*t;
}

float SkinMesh::slerp_t(const SkinTrack& tr, float time_s,
                         float* out_t, float* out_q) {
    if (tr.count == 0) { out_t[0]=out_t[1]=out_t[2]=0; out_q[0]=out_q[1]=out_q[2]=0; out_q[3]=1; return 0; }
    if (tr.count == 1) {
        const auto& k = tr.kf[0];
        out_t[0]=k.tx; out_t[1]=k.ty; out_t[2]=k.tz;
        out_q[0]=k.qx; out_q[1]=k.qy; out_q[2]=k.qz; out_q[3]=k.qw;
        return 0;
    }
    // Find bracket
    int lo = 0;
    for (int i = 0; i < tr.count-1; ++i) { if (tr.kf[i+1].t > time_s) { lo = i; break; } lo = i; }
    int hi = lo + 1;
    if (hi >= tr.count) hi = tr.count - 1;
    float span = tr.kf[hi].t - tr.kf[lo].t;
    float f    = (span > 1e-6f) ? (time_s - tr.kf[lo].t) / span : 0.f;
    if (f < 0) f = 0; if (f > 1) f = 1;
    const float ta[3] = { tr.kf[lo].tx, tr.kf[lo].ty, tr.kf[lo].tz };
    const float tb[3] = { tr.kf[hi].tx, tr.kf[hi].ty, tr.kf[hi].tz };
    const float qa[4] = { tr.kf[lo].qx, tr.kf[lo].qy, tr.kf[lo].qz, tr.kf[lo].qw };
    const float qb[4] = { tr.kf[hi].qx, tr.kf[hi].qy, tr.kf[hi].qz, tr.kf[hi].qw };
    lerp3(out_t, ta, tb, f);
    quat_lerp(out_q, qa, qb, f);
    return f;
}

// ── GLB loading ──────────────────────────────────────────────────────────────

bool SkinMesh::LoadGLB(const char* path) {
    loaded = false;
    if (!path) return false;

    cgltf_options opts = {};
    cgltf_data*   data = nullptr;
    if (cgltf_parse_file(&opts, path, &data) != cgltf_result_success) {
        fprintf(stderr, "[SkinMesh] parse failed: %s\n", path);
        return false;
    }
    if (cgltf_load_buffers(&opts, data, path) != cgltf_result_success) {
        fprintf(stderr, "[SkinMesh] buffer load failed: %s\n", path);
        cgltf_free(data); return false;
    }

    // ── Mesh primitive ────────────────────────────────────────────────────
    cgltf_primitive* prim = nullptr;
    for (cgltf_size mi = 0; mi < data->meshes_count && !prim; ++mi)
        for (cgltf_size pi = 0; pi < data->meshes[mi].primitives_count && !prim; ++pi) {
            cgltf_primitive* p = &data->meshes[mi].primitives[pi];
            if (p->type != cgltf_primitive_type_triangles || !p->indices) continue;
            bool hp=false,hn=false,hj=false,hw=false;
            for (cgltf_size ai = 0; ai < p->attributes_count; ++ai) {
                auto t = p->attributes[ai].type;
                if (t==cgltf_attribute_type_position) hp=true;
                if (t==cgltf_attribute_type_normal)   hn=true;
                if (t==cgltf_attribute_type_joints)   hj=true;
                if (t==cgltf_attribute_type_weights)  hw=true;
            }
            if (hp && hn) prim = p;  // joints/weights optional but expected
        }
    if (!prim) { fprintf(stderr,"[SkinMesh] no valid prim: %s\n",path); cgltf_free(data); return false; }

    cgltf_accessor *pos_acc=nullptr,*nor_acc=nullptr,*uv_acc=nullptr,*jnt_acc=nullptr,*wgt_acc=nullptr;
    for (cgltf_size ai = 0; ai < prim->attributes_count; ++ai) {
        auto& a = prim->attributes[ai];
        if (a.type==cgltf_attribute_type_position)  pos_acc=a.data;
        if (a.type==cgltf_attribute_type_normal)    nor_acc=a.data;
        if (a.type==cgltf_attribute_type_texcoord)  uv_acc =a.data;
        if (a.type==cgltf_attribute_type_joints)    jnt_acc=a.data;
        if (a.type==cgltf_attribute_type_weights)   wgt_acc=a.data;
    }

    cgltf_size nv = pos_acc->count;
    if (nv == 0 || nv > 131072) { fprintf(stderr,"[SkinMesh] bad vert count %zu\n",nv); cgltf_free(data); return false; }

    static SkinVertex s_verts[131072];
    for (cgltf_size i = 0; i < nv; ++i) {
        float p[3]={0,0,0}, n[3]={0,1,0}, uv[2]={0,0}, w[4]={1,0,0,0};
        float ji[4]={0,0,0,0};
        cgltf_accessor_read_float(pos_acc, i, p, 3);
        cgltf_accessor_read_float(nor_acc, i, n, 3);
        if (uv_acc)  cgltf_accessor_read_float(uv_acc,  i, uv, 2);
        if (wgt_acc) cgltf_accessor_read_float(wgt_acc, i, w, 4);
        if (jnt_acc) cgltf_accessor_read_uint (jnt_acc, i, (unsigned*)ji, 4);
        s_verts[i].x=p[0]; s_verts[i].y=p[1]; s_verts[i].z=p[2];
        s_verts[i].nx=n[0]; s_verts[i].ny=n[1]; s_verts[i].nz=n[2];
        s_verts[i].u=uv[0]; s_verts[i].v=uv[1];
        s_verts[i].j[0]=(uint8_t)ji[0]; s_verts[i].j[1]=(uint8_t)ji[1];
        s_verts[i].j[2]=(uint8_t)ji[2]; s_verts[i].j[3]=(uint8_t)ji[3];
        s_verts[i].w[0]=w[0]; s_verts[i].w[1]=w[1];
        s_verts[i].w[2]=w[2]; s_verts[i].w[3]=w[3];
    }
    vbo.Init(0x8892u, s_verts, (uint32_t)(nv * sizeof(SkinVertex)));

    cgltf_size ni = prim->indices->count;
    indices_u16 = (nv <= 65535);
    index_count = (uint32_t)ni;
    if (indices_u16) {
        static uint16_t s_idx16[1<<20];
        for (cgltf_size i=0;i<ni;++i) s_idx16[i]=(uint16_t)cgltf_accessor_read_index(prim->indices,i);
        ibo.Init(0x8893u, s_idx16, (uint32_t)(ni*2));
    } else {
        static uint32_t s_idx32[1<<20];
        for (cgltf_size i=0;i<ni;++i) s_idx32[i]=(uint32_t)cgltf_accessor_read_index(prim->indices,i);
        ibo.Init(0x8893u, s_idx32, (uint32_t)(ni*4));
    }

    // ── Skin / inverse bind matrices ──────────────────────────────────────
    for (int i=0;i<MAX_SKIN_BONES;++i) { mat4_identity(inv_bind_[i]); parent_[i]=-1; }
    if (data->skins_count > 0) {
        cgltf_skin& sk = data->skins[0];
        bone_count = (int)sk.joints_count;
        if (bone_count > MAX_SKIN_BONES) bone_count = MAX_SKIN_BONES;

        // Inverse bind matrices from accessor
        if (sk.inverse_bind_matrices) {
            for (int i=0;i<bone_count;++i)
                cgltf_accessor_read_float(sk.inverse_bind_matrices,(cgltf_size)i,inv_bind_[i],16);
        }

        // Parent array and bind-pose local TRS
        for (int i=0;i<bone_count;++i) {
            cgltf_node* node = sk.joints[i];
            // Find parent index within joints array
            parent_[i] = -1;
            if (node->parent) {
                for (int j=0;j<bone_count;++j)
                    if (sk.joints[j] == node->parent) { parent_[i]=j; break; }
            }
            // Local TRS from node
            if (node->has_translation) { bind_t_[i][0]=node->translation[0]; bind_t_[i][1]=node->translation[1]; bind_t_[i][2]=node->translation[2]; }
            else { bind_t_[i][0]=bind_t_[i][1]=bind_t_[i][2]=0; }
            if (node->has_rotation) { bind_q_[i][0]=node->rotation[0]; bind_q_[i][1]=node->rotation[1]; bind_q_[i][2]=node->rotation[2]; bind_q_[i][3]=node->rotation[3]; }
            else { bind_q_[i][0]=bind_q_[i][1]=bind_q_[i][2]=0; bind_q_[i][3]=1; }
        }
    }

    // ── Animations ────────────────────────────────────────────────────────
    clip_count_ = 0;
    for (cgltf_size ai=0; ai<data->animations_count && clip_count_<MAX_SKIN_CLIPS; ++ai) {
        cgltf_animation& ca = data->animations[ai];
        SkinClip& cl = clips_[clip_count_];
        memset(&cl,0,sizeof(cl));
        if (ca.name) strncpy(cl.name, ca.name, sizeof(cl.name)-1);
        cl.bone_count = bone_count;

        for (cgltf_size chi=0; chi<ca.channels_count; ++chi) {
            cgltf_animation_channel& ch = ca.channels[chi];
            if (!ch.target_node || !ch.sampler) continue;
            if (ch.target_path != cgltf_animation_path_type_translation &&
                ch.target_path != cgltf_animation_path_type_rotation) continue;

            // Map node → bone index
            int bi = -1;
            if (data->skins_count > 0) {
                for (int j=0;j<bone_count;++j)
                    if (data->skins[0].joints[j] == ch.target_node) { bi=j; break; }
            }
            if (bi<0 || bi>=MAX_SKIN_BONES) continue;

            cgltf_accessor* times_acc = ch.sampler->input;
            cgltf_accessor* vals_acc  = ch.sampler->output;
            int nkf = (int)times_acc->count;
            if (nkf > MAX_SKIN_KF) nkf = MAX_SKIN_KF;

            SkinTrack& tr = cl.tracks[bi];

            for (int k=0;k<nkf;++k) {
                float t_k = 0;
                cgltf_accessor_read_float(times_acc,(cgltf_size)k,&t_k,1);
                if (t_k > cl.duration) cl.duration = t_k;

                if (ch.target_path == cgltf_animation_path_type_translation) {
                    float tv[3]={0,0,0};
                    cgltf_accessor_read_float(vals_acc,(cgltf_size)k,tv,3);
                    // Fill or extend keyframe slot
                    if (tr.count <= k) {
                        tr.kf[k] = {t_k, tv[0],tv[1],tv[2], 0,0,0,1};
                        tr.count = k+1;
                    } else {
                        tr.kf[k].t=t_k; tr.kf[k].tx=tv[0]; tr.kf[k].ty=tv[1]; tr.kf[k].tz=tv[2];
                    }
                } else { // rotation
                    float qv[4]={0,0,0,1};
                    cgltf_accessor_read_float(vals_acc,(cgltf_size)k,qv,4);
                    if (tr.count <= k) {
                        tr.kf[k] = {t_k, 0,0,0, qv[0],qv[1],qv[2],qv[3]};
                        tr.count = k+1;
                    } else {
                        tr.kf[k].t=t_k; tr.kf[k].qx=qv[0]; tr.kf[k].qy=qv[1]; tr.kf[k].qz=qv[2]; tr.kf[k].qw=qv[3];
                    }
                }
            }
        }
        ++clip_count_;
    }

    cgltf_free(data);
    loaded = true;
    fprintf(stdout,"[SkinMesh] %s  verts=%u idx=%u bones=%d clips=%d\n",
            path,(unsigned)nv,(unsigned)ni,bone_count,clip_count_);
    return true;
}

void SkinMesh::Shutdown() {
    vbo.Shutdown(); ibo.Shutdown();
    index_count=0; loaded=false; bone_count=0; clip_count_=0;
}

int SkinMesh::ClipIndexByName(const char* name) const {
    for (int i=0;i<clip_count_;++i)
        if (strcmp(clips_[i].name,name)==0) return i;
    return -1;
}

// ── GetFinalBones ────────────────────────────────────────────────────────────

void SkinMesh::GetFinalBones(int clip_idx, float time_s, float* out) const {
    // out: float[MAX_SKIN_BONES * 16], column-major mat4 per bone

    // Clamp / loop time
    float t = time_s;
    if (clip_idx >= 0 && clip_idx < clip_count_) {
        float dur = clips_[clip_idx].duration;
        if (dur > 1e-6f) t = fmodf(t, dur);
    }

    float world[MAX_SKIN_BONES][16];
    float local[16];

    for (int i = 0; i < bone_count; ++i) {
        float lt[3], lq[4];

        if (clip_idx >= 0 && clip_idx < clip_count_) {
            const SkinClip& cl = clips_[clip_idx];
            const SkinTrack& tr = cl.tracks[i];
            if (tr.count > 0) {
                slerp_t(tr, t, lt, lq);
            } else {
                lt[0]=bind_t_[i][0]; lt[1]=bind_t_[i][1]; lt[2]=bind_t_[i][2];
                lq[0]=bind_q_[i][0]; lq[1]=bind_q_[i][1]; lq[2]=bind_q_[i][2]; lq[3]=bind_q_[i][3];
            }
        } else {
            lt[0]=bind_t_[i][0]; lt[1]=bind_t_[i][1]; lt[2]=bind_t_[i][2];
            lq[0]=bind_q_[i][0]; lq[1]=bind_q_[i][1]; lq[2]=bind_q_[i][2]; lq[3]=bind_q_[i][3];
        }

        mat4_from_tq(local, lt, lq);

        int pi = parent_[i];
        if (pi < 0) memcpy(world[i], local, 64);
        else        mat4_mul(world[i], world[pi], local);

        // final = world[i] * inv_bind[i]
        mat4_mul(out + i*16, world[i], inv_bind_[i]);
    }
    // Identity for unused bone slots
    for (int i = bone_count; i < MAX_SKIN_BONES; ++i)
        mat4_identity(out + i*16);
}
