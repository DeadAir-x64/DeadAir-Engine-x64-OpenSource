#pragma once

#include "Layers/xrRender/ColorMapManager.h"

namespace xray::render::RENDER_NAMESPACE
{
class light;

//#define DU_SPHERE_NUMVERTEX 92
//#define DU_SPHERE_NUMFACES	180
//#define DU_CONE_NUMVERTEX	18
//#define DU_CONE_NUMFACES	32
//	no less than 2
#define VOLUMETRIC_SLICES 100

class CRenderTarget
{
    u32 dwWidth[R__NUM_CONTEXTS];
    u32 dwHeight[R__NUM_CONTEXTS];
    u32 dwAccumulatorClearMark;

public:
    enum eStencilOptimizeMode
    {
        SO_Light = 0, //	Default
        SO_Combine, //	Default
    };

    u32 dwLightMarkerID;

    IBlender* b_accum_spot{};
    IBlender* b_accum_spot_msaa[8]{};
    IBlender* b_accum_volumetric_msaa[8]{};

#ifdef DEBUG
    struct dbg_line_t
    {
        Fvector P0, P1;
        u32 color;
    };
    xr_vector<std::pair<Fsphere, Fcolor>> dbg_spheres;
    xr_vector<dbg_line_t> dbg_lines;
    xr_vector<Fplane> dbg_planes;
#endif

    // Base targets
    xr_vector<ref_rt> rt_Base;
    ref_rt rt_Base_Depth;

    // MRT-path
    ref_rt rt_Depth; // Z-buffer like - initial depth
    ref_rt rt_MSAADepth; // z-buffer for MSAA deferred shading. If MSAA is disabled, points to rt_Base_Depth so we can reduce branching
    ref_rt rt_Generic_0_r; // MRT generic 0, if MSAA is disabled, just an alias of rt_Generic_0
    ref_rt rt_Generic_1_r; // MRT generic 1, if MSAA is disabled, just an alias of rt_Generic_1
    ref_rt rt_Generic;
    ref_rt rt_Position; // 64bit,	fat	(x,y,z,?)				(eye-space)
    ref_rt rt_Normal; // 64bit,	fat	(x,y,z,hemi)			(eye-space)
    ref_rt rt_Color; // 64/32bit,fat	(r,g,b,specular-gloss)	(or decompressed MET-8-8-8-8)
    ref_rt rt_SSR; // DA: screen-space reflections output (R4)
    ref_rt rt_TAA_history; // DA: previous frame, kept for temporal reprojection (R4)
    ref_rt rt_TAA_scratch; // DA: second output of the resolve — the copy that goes into the history
    ref_rt rt_TAA_out;     // DA: first output of the resolve — the copy that goes back on screen
    ref_rt rt_Velocity;    // DA: screen-space motion vectors, RG16F (R4). Groundwork for FSR 2.
    // [DA_PORT] Velocity after the guard pass - see phase_velocity_guard.
    ref_rt rt_Velocity_guard;
    ref_rt rt_Reactive;    // DA: reactive mask for the upscalers, R8 — 1 where history must not be trusted
    // [DA_PORT] Working pair for the reactive widening - see phase_reactive.
    ref_rt rt_Reactive_scratch;
    ref_rt rt_Reactive_scratch2;
    u32 da_velocity_cleared_frame{}; // DA: phase_scene_begin runs twice per frame when the scene is split
    // [DA_PORT] Same guard for the position clear - see phase_scene_begin.
    u32 da_position_cleared_frame{};
    ref_rt rt_FSR2_out;    // DA: FSR 2 output, at OUTPUT resolution and writable from a compute shader

    //
    ref_rt rt_Accumulator; // 64bit		(r,g,b,specular)
    ref_rt rt_Accumulator_temp; // only for HW which doesn't feature fp16 blend
    ref_rt rt_Generic_0; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.
    ref_rt rt_Generic_1; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.
    //	Igor: for volumetric lights
    ref_rt rt_Generic_2; // 32bit		(r,g,b,a)				// post-process, intermidiate results, etc.
    ref_rt rt_Bloom_1; // 32bit, dim/4	(r,g,b,?)
    ref_rt rt_Bloom_2; // 32bit, dim/4	(r,g,b,?)
    ref_rt rt_LUM_64; // 64bit, 64x64,	log-average in all components
    ref_rt rt_LUM_8; // 64bit, 8x8,		log-average in all components

    ref_rt rt_LUM_pool[CHWCaps::MAX_GPUS * 2]; // 1xfp32,1x1,		exp-result -> scaler
    ref_texture t_LUM_src; // source
    ref_texture t_LUM_dest; // destination & usage for current frame

    // smap
    ref_rt rt_smap_surf; // 32bit,		color
    ref_rt rt_smap_depth; // 24(32) bit,	depth
    ref_rt rt_smap_rain;
    ref_rt rt_smap_depth_minmax; //	is used for min/max sm

    //	Igor: for async screenshots
    ID3DTexture2D* t_ss_async; // 32bit		(r,g,b,a) is situated in the system memory

    // Textures
    ref_texture t_material;
    ref_texture t_noise[TEX_jitter_count];
    ref_texture t_noise_mipped;

private:
    // OCCq
    ref_shader s_occq;

    // Accum
    ref_shader s_accum_mask;
    ref_shader s_accum_mask_msaa[8];
    ref_shader s_accum_direct;
    ref_shader s_accum_direct_msaa[8];
    ref_shader s_accum_direct_volumetric;
    ref_shader s_accum_direct_volumetric_msaa[8];
    ref_shader s_accum_direct_volumetric_minmax;
    ref_shader s_accum_point;
    ref_shader s_accum_point_msaa[8];
    ref_shader s_accum_spot;
    ref_shader s_accum_spot_msaa[8];
    ref_shader s_accum_reflected;
    ref_shader s_accum_reflected_msaa[8];
    ref_shader s_accum_volume;
    ref_shader s_accum_volume_msaa[8];

    //	generate min/max
    ref_shader s_create_minmax_sm;

    //	DX11 Rain
    ref_shader s_rain;
    ref_shader s_rain_msaa[8]; // up to 8 shaders for DX10.0 support

    // Mark MSAA-edge pixels
    ref_shader s_mark_msaa_edges;

    ref_geom g_accum_point;
    ref_geom g_accum_spot;
    ref_geom g_accum_omnipart;
    ref_geom g_accum_volumetric;

    VertexStagingBuffer g_accum_point_vb;
    IndexStagingBuffer g_accum_point_ib;

    VertexStagingBuffer g_accum_omnip_vb;
    IndexStagingBuffer g_accum_omnip_ib;

    VertexStagingBuffer g_accum_spot_vb;
    IndexStagingBuffer g_accum_spot_ib;

    VertexStagingBuffer g_accum_volumetric_vb;
    IndexStagingBuffer g_accum_volumetric_ib;

    // SSAO
    ref_rt rt_ssao_temp;
    ref_rt rt_half_depth;
    ref_shader s_ssao;
    ref_shader s_taa; // DA: temporal AA resolve
    ref_shader s_velocity_guard; // DA: damps vegetation motion next to glossy surfaces
    ref_shader s_xess_mv; // [DA_PORT] знаковая копия векторов движения для XeSS, см. da_xess_mv.s
    ref_shader s_puddle_refl; // [DA_PORT] отражения в лужах, полноэкранный проход
    ref_shader s_reactive; // DA: marks pixels around genuinely moving objects as reactive
    ref_shader s_reactive_dilate_h; // DA: widens that mark, horizontally
    ref_shader s_reactive_dilate_v; // DA: and vertically, folding in the mask the G-buffer left
    ref_shader s_reactive_emissive; // [DA_PORT] метка самосветящейся геометрии, см. phase_reactive_emissive
    ref_shader s_sky_velocity; // DA: the sky writes no motion vectors of its own
    ref_shader s_ssao_msaa[8];
    ref_shader s_hdao_cs;      // HDAO compute shader

    // Bloom
    ref_geom g_bloom_build;
    ref_geom g_bloom_filter;
    ref_shader s_bloom_dbg_1;
    ref_shader s_bloom_dbg_2;
    ref_shader s_bloom;
    ref_shader s_bloom_msaa; // if MSAA is disabled, just an alias of s_bloom
    float f_bloom_factor;

    // Luminance
    ref_shader s_luminance;
    float f_luminance_adapt;

    // Combine
    ref_geom g_combine;
    ref_geom g_combine_VP; // xy=p,zw=tc
    ref_geom g_combine_2UV;
    ref_geom g_combine_cuboid;
    ref_geom g_aa_blur;
    ref_geom g_aa_AA;
    ref_shader s_combine_dbg_0;
    ref_shader s_combine_dbg_1;
    ref_shader s_combine_dbg_Accumulator;
    ref_shader s_combine;
    ref_shader s_combine_msaa[8];
    ref_shader s_combine_volumetric;

public:
    ref_shader s_postprocess;
    ref_shader s_postprocess_msaa; // if MSAA is disabled, just an alias of s_bloom
    ref_geom g_postprocess;
    ref_shader s_menu;
    ref_geom g_menu;
#if 0 // kept for historical reasons
    ref_shader s_flip;
    ref_geom g_flip;
#endif
private:
    float im_noise_time;
    u32 im_noise_shift_w;
    u32 im_noise_shift_h;

    float param_blur;
    float param_gray;
    float param_duality_h;
    float param_duality_v;
    float param_noise;
    float param_noise_scale;
    float param_noise_fps;
    u32 param_color_base;
    u32 param_color_gray;
    Fvector param_color_add;

    //	Color mapping
    float param_color_map_influence;
    float param_color_map_interpolate;
    ColorMapManager color_map_manager;

    //	Igor: used for volumetric lights
    bool m_bHasActiveVolumetric;

public:
    CRenderTarget();
    ~CRenderTarget();

    void build_textures();

    void accum_point_geom_create();
    void accum_point_geom_destroy();
    void accum_omnip_geom_create();
    void accum_omnip_geom_destroy();
    void accum_spot_geom_create();
    void accum_spot_geom_destroy();
    //	Igor: used for volumetric lights
    void accum_volumetric_geom_create();
    void accum_volumetric_geom_destroy();

    ID3DRenderTargetView* get_base_rt() { return rt_Base[HW.CurrentBackBuffer]->pRT; }
    ID3DDepthStencilView* get_base_zb() { return rt_Base_Depth->pZRT[CHW::IMM_CTX_ID]; }

    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb);
    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _4,
        ID3DDepthStencilView* zb); // [DA_PORT] reactive mask as a fourth target
    // [DA_PORT] Convenience form taking the depth buffer as a render target, mirroring the three-target
    // pair above - that is how every call site in the scene pass writes it.
    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _4,
        const ref_rt& _zb)
    {
        u_setrt(cmd_list, _1, _2, _3, _4, _zb ? _zb->pZRT[cmd_list.context_id] : nullptr);
    }
    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& _zb)
    {
        u_setrt(cmd_list, _1, _2, _3, _zb ? _zb->pZRT[cmd_list.context_id] : nullptr);
    }
    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, ID3DDepthStencilView* zb);
    void u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _zb)
    {
        u_setrt(cmd_list, _1, _2, _zb ? _zb->pZRT[cmd_list.context_id] : nullptr);
    }
    void u_setrt(CBackend& cmd_list, u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3,
        ID3DDepthStencilView* zb);
    void u_setrt(CBackend& cmd_list, u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3,
        const ref_rt& _zb)
    {
        u_setrt(cmd_list, W, H, _1, _2, _3, _zb ? _zb->pZRT[cmd_list.context_id] : nullptr);
    }

    void u_stencil_optimize(CBackend& cmd_list, eStencilOptimizeMode eSOM = SO_Light);
    void u_compute_texgen_screen(CBackend& cmd_list, Fmatrix& dest);
    void u_compute_texgen_jitter(CBackend& cmd_list, Fmatrix& dest);
    void u_calc_tc_noise(Fvector2& p0, Fvector2& p1);
    void u_calc_tc_duality_ss(Fvector2& r0, Fvector2& r1, Fvector2& l0, Fvector2& l1);
    bool u_need_PP();
    bool u_need_CM();
    bool u_DBT_enable(float zMin, float zMax);
    void u_DBT_disable();

    void phase_scene_prepare();
    void phase_scene_begin();
    void phase_scene_end();
    void phase_occq();
    void phase_ssao();
    void phase_hdao();
    void phase_taa(); // DA: temporal AA resolve
    bool phase_fsr2();
    void phase_velocity_guard();
    void phase_da_puddle_refl(); // [DA_PORT] // [DA_PORT] damp vegetation motion next to glossy surfaces
    void phase_reactive(); // [DA_PORT] widen the reactive mask around moving objects, against ghosting
    // [DA_PORT] Помечает свечение в маске реактивности. Зовётся сразу после отрисовки свечения, а
    // не рядом с phase_reactive: работает по трафарету, а свет затирает его своими маркерами.
    void phase_reactive_emissive();
    // [DA_PORT] То же для прозрачной геометрии, из середины phase_combine. Своя ручка, по умолчанию 0.
    void phase_reactive_transparent();
    // Условия включения меток. Спрашиваются и здесь, и по месту записи бита в трафарет - см. там.
    bool da_emissive_mark_ready() const;
    bool da_transparent_mark_ready() const;
    // Общая часть обеих: полноэкранный проход по трафаретному биту 0x02.
    void da_mark_reactive_from_stencil(float value);

    // [DA_PORT] Срез G-буфера по строке через прицел: глубина, цвет, вектор и маска ОДНОГО пикселя
    // рядом друг с другом. Средние по кадру дефект в десятке пикселей не видят — этот видит.
    void da_dump_gbuffer_row();
    void da_light_watch(); // [DA_PORT] покадровое наблюдение за светом в пикселе, см. .cpp
    void da_shift_watch(); // [DA_PORT] ездит ли ГОТОВЫЙ кадр относительно предыдущего, см. .cpp

    // [DA_PORT] Замер кэша теневых карт: фаза 1 — только время, фаза 2 — дрожание по всему экрану.
    void da_shadow_test_start();
    void da_shadow_test_frame(int phase);
    void da_shadow_test_report();
    void phase_sky_velocity(); // [DA_PORT] motion vectors for the sky, which no shader writes
    bool phase_fsr3(); // [DA_PORT] FSR 3 upscaler, same slot in the frame
    bool phase_dlss(); // [DA_PORT] NVIDIA DLSS, same slot in the frame
    bool phase_xess(); // [DA_PORT] Intel XeSS, same slot in the frame // DA: FSR 2 upscale; false when it did not run, so the caller can fall back
    // [DA_PORT] Знаковая копия векторов движения для XeSS; пустая ссылка = отдавать буфер как есть.
    ref_rt da_xess_signed_velocity();
    void phase_downsamp();
    void phase_wallmarks();

    void phase_smap_direct(CBackend& cmd_list, light *L, u32 sub_phase);
    void phase_smap_direct_tsh(CBackend& cmd_list, light *L, u32 sub_phase);
    void phase_smap_spot_clear(CBackend& cmd_list);
    void phase_smap_spot(CBackend& cmd_list, light* L);
    void phase_smap_spot_tsh(CBackend& cmd_list, light* L);
    void phase_accumulator(CBackend& cmd_list);
    void phase_vol_accumulator(CBackend& cmd_list);

    //	Generates min/max sm
    void create_minmax_SM(CBackend& cmd_list);

    void phase_rain(CBackend& cmd_list);
    void draw_rain(CBackend& cmd_list, light& RainSetup);

    void mark_msaa_edges();

    bool need_to_render_sunshafts();
    bool use_minmax_sm_this_frame();

    bool enable_scissor(light* L); // true if intersects near plane
    void enable_dbt_bounds(light* L);

    void disable_aniso();

    void draw_volume(CBackend& cmd_list, light* L);
    void accum_direct(CBackend& cmd_list, u32 sub_phase);
    void accum_direct_cascade(CBackend& cmd_list, u32 sub_phase, Fmatrix& xform, Fmatrix& xform_prev, float fBias);
    void accum_direct_f(CBackend& cmd_list, u32 sub_phase);
    void accum_direct_lum(CBackend& cmd_list);
    void accum_direct_blend(CBackend& cmd_list);
    void accum_direct_volumetric(CBackend& cmd_list, u32 sub_phase, const u32 Offset, const Fmatrix& mShadow, float zMin, float zMax);
    void accum_point(CBackend& cmd_list, light* L);
    void accum_spot(CBackend& cmd_list, light* L);
    void accum_reflected(CBackend& cmd_list, light* L);
    //	Igor: for volumetric lights
    void accum_volumetric(CBackend& cmd_list, light* L);

    void phase_bloom();
    void phase_luminance();
    void phase_combine();
    void phase_combine_volumetric();
    void phase_pp();
#if 0 // kept for historical reasons
    void phase_flip();
#endif

    u32 get_width(CBackend& cmd_list) { return dwWidth[cmd_list.context_id]; }
    u32 get_height(CBackend& cmd_list) { return dwHeight[cmd_list.context_id]; }

    void set_blur(float f) { param_blur = f; }
    void set_gray(float f) { param_gray = f; }
    void set_duality_h(float f) { param_duality_h = _abs(f); }
    void set_duality_v(float f) { param_duality_v = _abs(f); }
    void set_noise(float f) { param_noise = f; }
    void set_noise_scale(float f) { param_noise_scale = f; }
    void set_noise_fps(float f) { param_noise_fps = _abs(f) + EPS_S; }
    void set_color_base(u32 f) { param_color_base = f; }
    void set_color_gray(u32 f) { param_color_gray = f; }
    void set_color_add(const Fvector& f) { param_color_add = f; }
    void set_cm_imfluence(float f) { param_color_map_influence = f; }
    void set_cm_interpolate(float f) { param_color_map_interpolate = f; }
    void set_cm_textures(const shared_str& tex0, const shared_str& tex1)
    {
        color_map_manager.SetTextures(tex0, tex1);
    }

    //	Need to reset stencil only when marker overflows.
    //	Don't clear when render for the first time
    void reset_light_marker(CBackend& cmd_list, bool bResetStencil = false);
    void increment_light_marker(CBackend& cmd_list);

#ifdef DEBUG
    void dbg_addline(const Fvector& P0, const Fvector& P1, u32 c)
    {
        dbg_lines.emplace_back(dbg_line_t{ P0, P1, c });
    }

    void dbg_addbox(const Fbox& box, const u32& color)
    {
        Fvector c, r;
        box.getcenter(c);
        box.getradius(r);
        dbg_addbox(c, r.x, r.y, r.z, color);
    }

    void dbg_addbox(const Fvector& c, float rx, float ry, float rz, u32 color)
    {
        Fvector p1, p2, p3, p4, p5, p6, p7, p8;

        p1.set(c.x + rx, c.y + ry, c.z + rz);
        p2.set(c.x + rx, c.y - ry, c.z + rz);
        p3.set(c.x - rx, c.y - ry, c.z + rz);
        p4.set(c.x - rx, c.y + ry, c.z + rz);

        p5.set(c.x + rx, c.y + ry, c.z - rz);
        p6.set(c.x + rx, c.y - ry, c.z - rz);
        p7.set(c.x - rx, c.y - ry, c.z - rz);
        p8.set(c.x - rx, c.y + ry, c.z - rz);

        dbg_addline(p1, p2, color);
        dbg_addline(p2, p3, color);
        dbg_addline(p3, p4, color);
        dbg_addline(p4, p1, color);

        dbg_addline(p5, p6, color);
        dbg_addline(p6, p7, color);
        dbg_addline(p7, p8, color);
        dbg_addline(p8, p5, color);

        dbg_addline(p1, p5, color);
        dbg_addline(p2, p6, color);
        dbg_addline(p3, p7, color);
        dbg_addline(p4, p8, color);
    }
    void dbg_addplane(Fplane& P0, u32 /*c*/) { dbg_planes.emplace_back(P0); }
#else
    void dbg_addline(Fvector& /*P0*/, Fvector& /*P1*/, u32 /*c*/) {}
    void dbg_addplane(Fplane& /*P0*/, u32 /*c*/) {}
#endif
};
} // namespace xray::render::RENDER_NAMESPACE
