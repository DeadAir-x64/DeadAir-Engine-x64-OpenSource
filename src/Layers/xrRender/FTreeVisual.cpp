#include "stdafx.h"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/IGame_Level.h"
#include "xrEngine/Environment.h"
#include "xrCore/FMesh.hpp"
#include "FTreeVisual.h"
#include "Common/OGF_GContainer_Vertices.hpp"

namespace xray::render::RENDER_NAMESPACE
{
shared_str m_xform;
shared_str m_xform_v;
shared_str c_consts;
shared_str c_wave;
shared_str c_wind;
shared_str c_c_bias;
shared_str c_c_scale;
shared_str c_c_sun;

FTreeVisual::FTreeVisual(void) {}
FTreeVisual::~FTreeVisual(void) {}
void FTreeVisual::Release() { dxRender_Visual::Release(); }
void FTreeVisual::Load(const char* N, IReader* data, u32 dwFlags)
{
    dxRender_Visual::Load(N, data, dwFlags);

    const VertexElement* vFormat = nullptr;

    // read vertices
    R_ASSERT(data->find_chunk(OGF_GCONTAINER));
    {
        // verts
        u32 ID = data->r_u32();
        vBase = data->r_u32();
        vCount = data->r_u32();
        vFormat = RImplementation.getVB_Format(ID);

        VERIFY(nullptr == p_rm_Vertices);
        p_rm_Vertices = RImplementation.getVB(ID);
        p_rm_Vertices->AddRef();

        // indices
        dwPrimitives = 0;
        ID = data->r_u32();
        iBase = data->r_u32();
        iCount = data->r_u32();
        dwPrimitives = iCount / 3;

        VERIFY(nullptr == p_rm_Indices);
        p_rm_Indices = RImplementation.getIB(ID);
        p_rm_Indices->AddRef();
    }

    // load tree-def
    R_ASSERT(data->find_chunk(OGF_TREEDEF2));
    {
        data->r(&xform, sizeof(xform));
        data->r(&c_scale, sizeof(c_scale));
        c_scale.rgb.mul(.5f);
        c_scale.hemi *= .5f;
        c_scale.sun *= .5f;
        data->r(&c_bias, sizeof(c_bias));
        c_bias.rgb.mul(.5f);
        c_bias.hemi *= .5f;
        c_bias.sun *= .5f;
        // Msg				("hemi[%f / %f], sun[%f / %f]",c_scale.hemi,c_bias.hemi,c_scale.sun,c_bias.sun);
    }

    /*if (RImplementation.o.ffp && dcl_equal(vFormat, mu_model_decl_unpacked))
    {
        const size_t vertices_size = vCount * sizeof(mu_model_vert_unpacked);

        const auto new_buffer = xr_new<VertexStagingBuffer>();
        new_buffer->Create(vertices_size);

        auto vert_new = static_cast<mu_model_vert_unpacked*>(new_buffer->Map());
        const auto vert_orig = static_cast<mu_model_vert_unpacked*>(p_rm_Vertices->Map(vBase, vertices_size, true)); // read-back
        CopyMemory(vert_new, vert_orig, vertices_size);

        for (size_t i = 0; i < vCount; ++i)
        {
            //vert_new->P.mul(xform.j);
            ++vert_new;
        }

        new_buffer->Unmap(true);
        p_rm_Vertices->Unmap(false);
        _RELEASE(p_rm_Vertices);
        p_rm_Vertices = new_buffer;
        vBase = 0;
    }*/

    // Geom
    rm_geom.create(vFormat, *p_rm_Vertices, *p_rm_Indices);

    // Get constants
    m_xform = "m_xform";
    m_xform_v = "m_xform_v";
    c_consts = "consts";
    c_wave = "wave";
    c_wind = "wind";
    c_c_bias = "c_bias";
    c_c_scale = "c_scale";
    c_c_sun = "c_sun";
}

struct FTreeVisual_setup
{
    u32 dwFrame;
    float scale;
    Fvector4 wave;
    Fvector4 wind;

    // [DA_PORT] The same two as they were on the previous frame, for motion vectors. Without them the
    // sway is invisible to the upscaler: the previous position is rebuilt from the vertex as it stands
    // NOW, already displaced, so foliage reports "I did not move" while it visibly does. The history is
    // then fetched by the camera offset alone, lands on a different leaf, and gets accepted or rejected
    // depending on how well it happens to match - which reads as trees and bushes blinking.
    Fvector4 wave_old;
    Fvector4 wind_old;
    bool seeded;

    // [DA_PORT] Sway phase, accumulated rather than derived from the clock. See calculate().
    float phase_pos, phase_rot, time_old;

    FTreeVisual_setup(): dwFrame(0), scale(0), seeded(false), phase_pos(0), phase_rot(0), time_old(0) {}


    void calculate()
    {
        // Both are functions of global time and are recomputed once per frame, so remembering the
        // previous frame's pair costs one copy.
        if (seeded)
        {
            wave_old.set(wave);
            wind_old.set(wind);
        }
        dwFrame = Device.dwFrame;
        CEnvDescriptor& desc = g_pGamePersistent->Environment().CurrentEnv;

        // [DA_PORT] The phase is ACCUMULATED, not computed as time * speed.
        //
        // Both m_fTreeSpeed and m_fTreeRotation come from the weather descriptor and are interpolated
        // between keyframes, so they drift continuously through the day. Multiplying them by
        // fTimeGlobal - which is thousands of seconds by then - turns a drift of a thousandth into a
        // phase jump of many whole turns: the tree is instantly somewhere else in its swing, which
        // looks exactly like it snapping back to its rest position. Visible at native resolution too,
        // so it was never an upscaler problem; a temporal filter merely makes it more obvious.
        //
        // CDetailManager already does it this way for grass (m_time_pos += dt * speed), which is why
        // grass never showed the artefact. Same treatment here.
        float dt = Device.fTimeGlobal - time_old;
        if (dt < 0.f || dt > 1.f) // first frame, or a pause / level load
            dt = 0.03f;
        time_old = Device.fTimeGlobal;

        phase_rot += PI_MUL_2 * dt / desc.m_fTreeRotation;
        phase_pos += dt * desc.m_fTreeSpeed;

        // [DA_PORT] Wrapped to one turn. The phase is fed to periodic functions (calc_cyclic in the
        // shader, sin/cos here), so a full turn is worth nothing to them - but it is worth a great deal
        // to a float. Left to grow, the phase reaches thousands within minutes of play, where float32
        // resolves about a thousandth - the same size as one frame's increment at 300 FPS. The phase
        // then advances in visible steps instead of smoothly, which is the jumping that appears "after
        // a while" and never at the start. Wrapping keeps every value small and exact.
        // Safe for the previous-frame copies too: they feed the same periodic functions.
        phase_rot = fmodf(phase_rot, PI_MUL_2);
        phase_pos = fmodf(phase_pos, PI_MUL_2);

        // Calc wind-vector3, scale
        float tm_rot = phase_rot;

        wind.set(_sin(tm_rot), 0, _cos(tm_rot), 0);
        wind.normalize();
        wind.mul(desc.m_fTreeAmplitude); // dir1*amplitude

        scale = 1.f / float(FTreeVisual_quant);

        // setup constants
        wave.set(desc.m_fTreeWave.x, desc.m_fTreeWave.y, desc.m_fTreeWave.z, phase_pos); // wave
        wave.div(PI_MUL_2);

        if (!seeded)
        {
            // First frame: no previous sway to speak of. Seeding with the current one reports no motion,
            // which beats reporting the whole displacement as if it happened in a single frame.
            wave_old.set(wave);
            wind_old.set(wind);
            seeded = true;
        }
    }
};

void FTreeVisual::Render(CBackend& cmd_list, float /*LOD*/, bool use_fast_geo)
{
    static FTreeVisual_setup tvs;
    // [DA_PORT] Exactly once per frame, and provably so.
    //
    // The plain "if (dwFrame != current) calculate()" this replaces was safe only while calculate() was
    // a pure function of the clock: several command lists render trees in parallel (scene plus shadow
    // cascades), two of them can pass that test in the same frame, and recomputing the same numbers
    // twice cost nothing. Once the sway phase became ACCUMULATED and the previous frame's wind started
    // being remembered here, a second entry stopped being harmless: it advances the phase twice in one
    // frame, and it overwrites wave_old/wind_old with the values just computed - so the previous frame's
    // wind equals the current one and every tree reports zero motion for that frame.
    //
    // Which is why the artefact was intermittent (the race is not won every frame), grew more frequent
    // the more trees were on screen (more draws, more chances), and never appeared in the original: the
    // race was always there, it just had nothing to corrupt.
    static std::atomic<u32> s_frame{ 0 };
    u32 seen = s_frame.load(std::memory_order_relaxed);
    if (seen != Device.dwFrame && s_frame.compare_exchange_strong(seen, Device.dwFrame))
        tvs.calculate();
// setup constants
#if RENDER != R_R1
    Fmatrix xform_v;
    xform_v.mul_43(cmd_list.get_xform_view(), xform);
    cmd_list.tree.set_m_xform_v(xform_v); // matrix
#endif
    float s = ps_r__Tree_SBC;
    cmd_list.tree.set_m_xform(xform); // matrix
    cmd_list.tree.set_consts(tvs.scale, tvs.scale, 0, 0); // consts/scale
    cmd_list.tree.set_wave(tvs.wave); // wave
    cmd_list.tree.set_wind(tvs.wind); // wind
    cmd_list.tree.set_wave_old(tvs.wave_old); // [DA_PORT] motion vectors
    cmd_list.tree.set_wind_old(tvs.wind_old); // [DA_PORT] motion vectors
#if RENDER != R_R1
    s *= 1.3333f;
    cmd_list.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
    cmd_list.tree.set_c_bias(s * c_bias.rgb.x, s * c_bias.rgb.y, s * c_bias.rgb.z, s * c_bias.hemi); // bias
#else
    const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
    cmd_list.tree.set_c_scale(s * c_scale.rgb.x, s * c_scale.rgb.y, s * c_scale.rgb.z, s * c_scale.hemi); // scale
    cmd_list.tree.set_c_bias(s * c_bias.rgb.x + desc.ambient.x, s * c_bias.rgb.y + desc.ambient.y,
        s * c_bias.rgb.z + desc.ambient.z, s * c_bias.hemi); // bias
#endif
    cmd_list.tree.set_c_sun(s * c_scale.sun, s * c_bias.sun, 0, 0); // sun
}

#define PCOPY(a) a = pFrom->a
void FTreeVisual::Copy(dxRender_Visual* pSrc)
{
    dxRender_Visual::Copy(pSrc);

    FTreeVisual* pFrom = dynamic_cast<FTreeVisual*>(pSrc);

    PCOPY(rm_geom);
    PCOPY(p_rm_Vertices);
    if (p_rm_Vertices)
        p_rm_Vertices->AddRef();
    PCOPY(vBase);
    PCOPY(vCount);
    PCOPY(vStride);
    PCOPY(p_rm_Indices);
    if (p_rm_Indices)
        p_rm_Indices->AddRef();
    PCOPY(iBase);
    PCOPY(iCount);
    PCOPY(dwPrimitives);

    PCOPY(xform);
    PCOPY(c_scale);
    PCOPY(c_bias);
}

//-----------------------------------------------------------------------------------
// Stripified Tree
//-----------------------------------------------------------------------------------
FTreeVisual_ST::FTreeVisual_ST(void) {}
FTreeVisual_ST::~FTreeVisual_ST(void) {}
void FTreeVisual_ST::Release() { inherited::Release(); }
void FTreeVisual_ST::Load(const char* N, IReader* data, u32 dwFlags) { inherited::Load(N, data, dwFlags); }
void FTreeVisual_ST::Render(CBackend& cmd_list, float LOD, bool use_fast_geo)
{
    inherited::Render(cmd_list, LOD, use_fast_geo);
    cmd_list.set_Geometry(rm_geom);
    cmd_list.Render(D3DPT_TRIANGLELIST, vBase, 0, vCount, iBase, dwPrimitives);
    cmd_list.stat.r.s_flora.add(vCount);
}
void FTreeVisual_ST::Copy(dxRender_Visual* pSrc) { inherited::Copy(pSrc); }
//-----------------------------------------------------------------------------------
// Progressive Tree
//-----------------------------------------------------------------------------------
FTreeVisual_PM::FTreeVisual_PM(void)
{
    pSWI = nullptr;
    last_lod = 0;
}
FTreeVisual_PM::~FTreeVisual_PM(void) {}
void FTreeVisual_PM::Release() { inherited::Release(); }
void FTreeVisual_PM::Load(const char* N, IReader* data, u32 dwFlags)
{
    inherited::Load(N, data, dwFlags);
    R_ASSERT(data->find_chunk(OGF_SWICONTAINER));
    {
        u32 ID = data->r_u32();
        pSWI = RImplementation.getSWI(ID);
    }
}
void FTreeVisual_PM::Render(CBackend& cmd_list, float LOD, bool use_fast_geo)
{
    inherited::Render(cmd_list, LOD, use_fast_geo);
    int lod_id = last_lod;
    if (LOD >= 0.f)
    {
        lod_id = iFloor((1.f - LOD) * float(pSWI->count - 1) + 0.5f);
        last_lod = lod_id;
    }
    VERIFY(lod_id >= 0 && lod_id < int(pSWI->count));
    FSlideWindow& SW = pSWI->sw[lod_id];
    cmd_list.set_Geometry(rm_geom);
    cmd_list.Render(D3DPT_TRIANGLELIST, vBase, 0, SW.num_verts, iBase + SW.offset, SW.num_tris);
    cmd_list.stat.r.s_flora.add(SW.num_verts);
}
void FTreeVisual_PM::Copy(dxRender_Visual* pSrc)
{
    inherited::Copy(pSrc);
    FTreeVisual_PM* pFrom = dynamic_cast<FTreeVisual_PM*>(pSrc);
    PCOPY(pSWI);
}
} // namespace xray::render::RENDER_NAMESPACE
