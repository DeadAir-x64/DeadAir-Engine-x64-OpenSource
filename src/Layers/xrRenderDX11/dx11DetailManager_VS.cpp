#include "stdafx.h"
#include "Layers/xrRender/DetailManager.h"
#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/Environment.h"
#include "Layers/xrRender/BufferUtils.h"

// [DA_PORT] Vegetation sway scale, 0 = frozen. Defined in the engine; declared outside the namespace
// on purpose - written inside it, the extern would look for the symbol in that namespace instead.
extern ENGINE_API float ps_r__wind_scale;
extern ENGINE_API int ps_r__wind_shadow;

namespace xray::render::RENDER_NAMESPACE
{
namespace detail_manager
{
extern const int quant;
//extern const int c_hdr;
}

void CDetailManager::hw_Load_Shaders()
{
    // Create shader to access constant storage
    ref_shader S;
    S.create("details\\set");
    R_constant_table& T0 = *(S->E[0]->passes[0]->constants);
    R_constant_table& T1 = *(S->E[1]->passes[0]->constants);
    hwc_consts = T0.get("consts");
    hwc_wave = T0.get("wave");
    hwc_wind = T0.get("dir2D");
    hwc_array = T0.get("array");
    hwc_s_consts = T1.get("consts");
    hwc_s_xform = T1.get("xform");
    hwc_s_array = T1.get("array");
}

void CDetailManager::hw_Render(CBackend& cmd_list)
{
    ZoneScoped;
    using namespace detail_manager;

    // Render-prepare
    //	Update timer
    //	Can't use Device.fTimeDelta since it is smoothed! Don't know why, but smoothed value looks more choppy!
    float fDelta = Device.fTimeGlobal - m_global_time_old;
    if ((fDelta < 0) || (fDelta > 1))
        fDelta = 0.03f;
    m_global_time_old = Device.fTimeGlobal;

    // [DA_PORT] Remember where the sway was before this frame advances it - see DetailManager.h.
    if (m_swing_seeded)
    {
        m_time_rot_1_old = m_time_rot_1;
        m_time_rot_2_old = m_time_rot_2;
        m_time_pos_old = m_time_pos;
    }

    m_time_rot_1 += (PI_MUL_2 * fDelta / swing_current.rot1);
    m_time_rot_2 += (PI_MUL_2 * fDelta / swing_current.rot2);
    m_time_pos += fDelta * swing_current.speed;

    // [DA_PORT] Wrapped to one turn. The phase is fed to periodic functions (calc_cyclic in the
    // shader, sin/cos here), so a full turn is worth nothing to them - but it is worth a great deal
    // to a float. Left to grow, the phase reaches thousands within minutes of play, where float32
    // resolves about a thousandth - the same size as one frame's increment at 300 FPS. The phase
    // then advances in visible steps instead of smoothly, which is the jumping that appears "after
    // a while" and never at the start. Wrapping keeps every value small and exact.
    // Safe for the previous-frame copies too: they feed the same periodic functions.
    m_time_rot_1 = fmodf(m_time_rot_1, PI_MUL_2);
    m_time_rot_2 = fmodf(m_time_rot_2, PI_MUL_2);
    m_time_pos = fmodf(m_time_pos, PI_MUL_2);

    if (!m_swing_seeded)
    {
        // First frame: no previous sway. Seeding with the current one reports no movement, which beats
        // reporting the whole bend as if it had happened between two frames.
        m_time_rot_1_old = m_time_rot_1;
        m_time_rot_2_old = m_time_rot_2;
        m_time_pos_old = m_time_pos;
        m_swing_seeded = true;
    }

    // float		tm_rot1		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot1);
    // float		tm_rot2		= (PI_MUL_2*Device.fTimeGlobal/swing_current.rot2);
    float tm_rot1 = m_time_rot_1;
    float tm_rot2 = m_time_rot_2;

    // [DA_PORT] r__wind_scale: 0 freezes the grass. Applied to the previous-frame copies too, so the
    // motion vectors keep describing the same sway the current frame is drawn with.
    float da_wind = ps_r__wind_scale;

    // [DA_PORT] r__wind_shadow 0: grass stands still in the SHADOW pass while swaying on screen.
    //
    // Grass goes into the sun's shadow map (render_phase_sun.cpp, gated by r2_sun_details), and a
    // shadow map is a hard edge sitting on a texel boundary. A blade moving by a fraction of a texel
    // flips whole pixels of whatever it shades between lit and unlit every frame. Matte surfaces
    // absorb that; a narrow specular lobe answers to illumination sharply and turns it into the colour
    // noise seen on metal - which is why the artefact picks out barrels and cars and leaves the wooden
    // fence beside them alone.
    //
    // The tree half of this lives in FTreeVisual::Render. Freezing only the trees was not a test of the
    // idea: objects standing in grass are shadowed by the grass, not by the canopy.
    if (ps_r__wind_shadow == 0 &&
        RImplementation.get_context(cmd_list.context_id).o.phase == CRender::PHASE_SMAP)
        da_wind = 0.f;

    const float da_amp1 = swing_current.amp1 * da_wind;
    const float da_amp2 = swing_current.amp2 * da_wind;

    Fvector4 dir1, dir2;
    dir1.set(_sin(tm_rot1), 0, _cos(tm_rot1), 0).normalize().mul(da_amp1);
    dir2.set(_sin(tm_rot2), 0, _cos(tm_rot2), 0).normalize().mul(da_amp2);

    // [DA_PORT] The same directions one frame back, built exactly the same way.
    Fvector4 dir1_old, dir2_old;
    dir1_old.set(_sin(m_time_rot_1_old), 0, _cos(m_time_rot_1_old), 0).normalize().mul(da_amp1);
    dir2_old.set(_sin(m_time_rot_2_old), 0, _cos(m_time_rot_2_old), 0).normalize().mul(da_amp2);

    // Setup geometry and DMA
    cmd_list.set_Geometry(hw_Geom);

    // Wave0
    float scale = 1.f / float(quant);
    Fvector4 wave;
    Fvector4 consts;
    consts.set(scale, scale, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
    // wave.set				(1.f/5.f,		1.f/7.f,	1.f/3.f,	Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos);
    // RCache.set_c			(&*hwc_consts,	scale,		scale,		ps_r__Detail_l_aniso,	ps_r__Detail_l_ambient);
    // //
    // consts
    // RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir1); //
    // wind-dir
    // hw_Render_dump			(&*hwc_array,	1, 0, c_hdr );
    Fvector4 wave_old;
    wave_old.set(1.f / 5.f, 1.f / 7.f, 1.f / 3.f, m_time_pos_old);
    hw_Render_dump(cmd_list, consts, wave.div(PI_MUL_2), dir1, wave_old.div(PI_MUL_2), dir1_old, 1, 0);

    // Wave1
    // wave.set				(1.f/3.f,		1.f/7.f,	1.f/5.f,	Device.fTimeGlobal*swing_current.speed);
    wave.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos);
    // RCache.set_c			(&*hwc_wave,	wave.div(PI_MUL_2));	// wave
    // RCache.set_c			(&*hwc_wind,	dir2); //
    // wind-dir
    // hw_Render_dump			(&*hwc_array,	2, 0, c_hdr );
    wave_old.set(1.f / 3.f, 1.f / 7.f, 1.f / 5.f, m_time_pos_old);
    hw_Render_dump(cmd_list, consts, wave.div(PI_MUL_2), dir2, wave_old.div(PI_MUL_2), dir2_old, 2, 0);

    // Still
    consts.set(scale, scale, scale, 1.f);
    // RCache.set_c			(&*hwc_s_consts,scale,		scale,		scale,				1.f);
    // RCache.set_c			(&*hwc_s_xform,	Device.mFullTransform);
    // hw_Render_dump			(&*hwc_s_array,	0, 1, c_hdr );
    // The "still" batch does not sway at all, so its previous sway is its current one.
    hw_Render_dump(cmd_list, consts, wave.div(PI_MUL_2), dir2, wave.div(1.f), dir2, 0, 1);
}

void CDetailManager::hw_Render_dump(CBackend& cmd_list, const Fvector4& consts, const Fvector4& wave,
    const Fvector4& wind, const Fvector4& wave_old, const Fvector4& wind_old, u32 var_id, u32 lod_id)
{
    ZoneScoped;

    static shared_str strConsts("consts");
    static shared_str strWave("wave");
    static shared_str strDir2D("dir2D");
    static shared_str strWaveOld("wave_old"); // [DA_PORT] motion vectors
    static shared_str strDir2DOld("dir2D_old"); // [DA_PORT] motion vectors
    static shared_str strArray("array");
    static shared_str strXForm("xform");

    RImplementation.BasicStats.DetailCount = 0;

    // Matrices and offsets
    u32 vOffset = 0;
    u32 iOffset = 0;

    vis_list& list = m_visibles[var_id];

    const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
    Fvector c_sun, c_ambient, c_hemi;
    c_sun.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z);
    c_sun.mul(.5f);
    c_ambient.set(desc.ambient.x, desc.ambient.y, desc.ambient.z);
    c_hemi.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z);

    // Iterate
    for (u32 O = 0; O < objects.size(); O++)
    {
        CDetail& Object = *objects[O];
        xr_vector<SlotItemVec*>& vis = list[O];
        if (!vis.empty())
        {
            for (u32 iPass = 0; iPass < Object.shader->E[lod_id]->passes.size(); ++iPass)
            {
                // Setup matrices + colors (and flush it as necessary)
                // RCache.set_Element				(Object.shader->E[lod_id]);
                cmd_list.set_Element(Object.shader->E[lod_id], iPass);
                cmd_list.apply_lmaterial();

                //	This could be cached in the corresponding consatant buffer
                //	as it is done for DX9
                cmd_list.set_c(strConsts, consts);
                cmd_list.set_c(strWave, wave);
                cmd_list.set_c(strDir2D, wind);
                cmd_list.set_c(strWaveOld, wave_old); // [DA_PORT]
                cmd_list.set_c(strDir2DOld, wind_old); // [DA_PORT]
                cmd_list.set_c(strXForm, Device.mFullTransform);

                // ref_constant constArray = RCache.get_c(strArray);
                // VERIFY(constArray);

                // u32			c_base				= x_array->vs.index;
                // Fvector4*	c_storage			= RCache.get_ConstantCache_Vertex().get_array_f().access(c_base);
                Fvector4* c_storage = 0;
                //	Map constants to memory directly
                {
                    void* pVData;
                    cmd_list.get_ConstantDirect(strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                    c_storage = (Fvector4*)pVData;
                }
                VERIFY(c_storage);

                u32 dwBatch = 0;

                for (SlotItemVec* items : vis)
                {
                    for (SlotItem* item : *items)
                    {
                        SlotItem& Instance = *item;
                        u32 base = dwBatch * 4;

                        // Build matrix ( 3x4 matrix, last row - color )
                        float scale = Instance.scale_calculated;
                        Fmatrix& M = Instance.mRotY;
                        c_storage[base + 0].set(M._11 * scale, M._21 * scale, M._31 * scale, M._41);
                        c_storage[base + 1].set(M._12 * scale, M._22 * scale, M._32 * scale, M._42);
                        c_storage[base + 2].set(M._13 * scale, M._23 * scale, M._33 * scale, M._43);
                        // RCache.set_ca(&*constArray, base+0, M._11*scale,	M._21*scale,	M._31*scale,	M._41	);
                        // RCache.set_ca(&*constArray, base+1, M._12*scale,	M._22*scale,	M._32*scale,	M._42	);
                        // RCache.set_ca(&*constArray, base+2, M._13*scale,	M._23*scale,	M._33*scale,	M._43	);

                        // Build color
                        // R2 only needs hemisphere
                        float h = Instance.c_hemi;
                        float s = Instance.c_sun;
                        c_storage[base + 3].set(s, s, s, h);
                        // RCache.set_ca(&*constArray, base+3, s,				s,				s,				h
                        // );
                        dwBatch++;
                        if (dwBatch == hw_BatchSize)
                        {
                            // flush
                            RImplementation.BasicStats.DetailCount += dwBatch;
                            u32 dwCNT_verts = dwBatch * Object.number_vertices;
                            u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                            // RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
                            // RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
                            cmd_list.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                            cmd_list.stat.r.s_details.add(dwCNT_verts);

                            // restart
                            dwBatch = 0;

                            //	Remap constants to memory directly (just in case anything goes wrong)
                            {
                                void* pVData;
                                cmd_list.get_ConstantDirect(strArray, hw_BatchSize * sizeof(Fvector4) * 4, &pVData, 0, 0);
                                c_storage = (Fvector4*)pVData;
                            }
                            VERIFY(c_storage);
                        }
                    }
                }
                // flush if necessary
                if (dwBatch)
                {
                    RImplementation.BasicStats.DetailCount += dwBatch;
                    u32 dwCNT_verts = dwBatch * Object.number_vertices;
                    u32 dwCNT_prims = (dwBatch * Object.number_indices) / 3;
                    // RCache.get_ConstantCache_Vertex().b_dirty				=	TRUE;
                    // RCache.get_ConstantCache_Vertex().get_array_f().dirty	(c_base,c_base+dwBatch*4);
                    cmd_list.Render(D3DPT_TRIANGLELIST, vOffset, 0, dwCNT_verts, iOffset, dwCNT_prims);
                    cmd_list.stat.r.s_details.add(dwCNT_verts);
                }
            }
        }
        vOffset += hw_BatchSize * Object.number_vertices;
        iOffset += hw_BatchSize * Object.number_indices;
    }
}
} // namespace xray::render::RENDER_NAMESPACE
