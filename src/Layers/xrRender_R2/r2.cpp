#include "stdafx.h"
#if RENDER == R_R4
#   include "Layers/xrRenderPC_R4/da_gpu_timer.h"
#endif

#include "xrCore/PostProcess/PPInfo.hpp"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/GameFont.h"
#include "xrEngine/PerformanceAlert.hpp"

#include "Layers/xrRender/FBasicVisual.h"
#include "Layers/xrRender/SkeletonCustom.h"
#include "Layers/xrRender/dxWallMarkArray.h"
#include "Layers/xrRender/dxUIShader.h"

#if defined(USE_DX11)
#include "Layers/xrRenderDX11/3DFluid/dx113DFluidManager.h"
#endif

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared outside the namespace on purpose: an
// extern written inside xray::render::render_r4 would look for the symbol in that namespace instead.
extern ENGINE_API int ps_r__motion_vectors;
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API float ps_r__reactive_foliage;
extern ENGINE_API float ps_r__reactive_motion;
// [DA_PORT] "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
extern ENGINE_API bool da_upscaler_active();
extern ENGINE_API float ps_r__detail_gloss_fix;
extern ENGINE_API float ps_r__detail_normal_fix;
extern ENGINE_API float ps_r__detail_albedo_fix;
extern ENGINE_API int ps_r__detail_debug;
extern ENGINE_API float ps_r__reactive_gloss;
extern ENGINE_API float ps_r__reactive_gloss_min;
extern ENGINE_API float ps_r__foliage_velocity;
extern ENGINE_API float ps_r__grass_velocity;
extern ENGINE_API Fvector2 g_da_taa_jitter;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;
extern ENGINE_API Fmatrix g_da_taa_unjittered_VP;

namespace xray::render::RENDER_NAMESPACE
{
CRender RImplementation;

//////////////////////////////////////////////////////////////////////////
class CGlow : public IRender_Glow
{
public:
    bool bActive;

public:
    CGlow() : bActive(false) {}
    virtual void set_active(bool b) { bActive = b; }
    virtual bool get_active() { return bActive; }
    virtual void set_position(const Fvector& P) {}
    virtual void set_direction(const Fvector& D) {}
    virtual void set_radius(float R) {}
    virtual void set_texture(LPCSTR name) {}
    virtual void set_color(const Fcolor& C) {}
    virtual void set_color(float r, float g, float b) {}
};

float r_dtex_range = 50.f;
//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_dynamic(dxRender_Visual* pVisual, float cdist_sq, u32 phase)
{
    int id = SE_R2_SHADOW;
    if (CRender::PHASE_NORMAL == phase)
    {
        id = ((_sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range) ? SE_R2_NORMAL_HQ : SE_R2_NORMAL_LQ;
    }
    return pVisual->shader->E[id]._get();
}
//////////////////////////////////////////////////////////////////////////
ShaderElement* CRender::rimp_select_sh_static(dxRender_Visual* pVisual, float cdist_sq, u32 phase)
{
    if (!pVisual->shader)
        return nullptr;
    int id = SE_R2_SHADOW;
    if (CRender::PHASE_NORMAL == phase)
    {
        id = ((_sqrt(cdist_sq) - pVisual->vis.sphere.R) < r_dtex_range) ? SE_R2_NORMAL_HQ : SE_R2_NORMAL_LQ;
    }
    return pVisual->shader->E[id]._get();
}
static class cl_parallax : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        float h = ps_r2_df_parallax_h;
        cmd_list.set_c(C, h, -h / 2.f, 1.f / r_dtex_range, 1.f / r_dtex_range);
    }
} binder_parallax;

#if defined(USE_DX11)
static class cl_LOD : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.LOD.set_LOD(C); }
} binder_LOD;
#endif

static class cl_pos_decompress_params : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
#if defined(USE_DX11)
        const float VertTan = -1.0f * tanf(deg2rad(Device.fFOV / 2.0f));
        const float HorzTan = -VertTan / Device.fASPECT;
#elif defined(USE_OGL)
        const float VertTan = tanf(deg2rad(Device.fFOV / 2.0f));
        const float HorzTan = VertTan / Device.fASPECT;
#else
#   error No graphics API selected or enabled!
#endif
        // [DA_PORT] These describe the G-BUFFER, not the window: the deferred shaders rebuild eye-space
        // position from it by stepping one texel at a time. With r__render_scale < 100 the G-buffer is
        // smaller, so dividing the view angle by the output width gave every pixel a position that was
        // wrong by exactly the scale factor — which reads as broken depth, and the whole scene drowned
        // in fog. Must follow the render resolution.
        cmd_list.set_c(C, HorzTan, VertTan, (2.0f * HorzTan) / (float)Device.dwRenderWidth,
            (2.0f * VertTan) / (float)Device.dwRenderHeight);
    }
} binder_pos_decompress_params;

// [DA_PORT] Previous frame's view-projection, for temporal reprojection (TAA, temporal SSR).
// Anomaly's engine has no velocity buffer at all — its temporal effects reproject through depth plus
// the previous camera matrix and hide the resulting ghosts on moving objects by clipping the history
// against the current neighbourhood. Same approach here: this constant is the only engine-side input
// that approach needs, and it costs nothing when unused.
Fmatrix g_da_prev_VP = Fidentity;

static class cl_prev_vp : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // The shader feeds this an EYE-space position straight out of the G-buffer, so the matrix has to
        // undo the current view before applying the previous frame's view-projection. Same composition
        // the motion-blur code in phase_combine builds for m_previous.
        Fmatrix eye_to_prev_clip;
        eye_to_prev_clip.mul(g_da_prev_VP, Device.mInvView);
        cmd_list.set_c(C, eye_to_prev_clip);
    }
} binder_prev_vp;

// [DA_PORT] World-view-projection of the PREVIOUS frame, for motion vectors. Static level geometry is
// authored in world space, so its world matrix is identity and this is simply the previous frame's
// view-projection — the same matrix TAA reprojects through, kept un-jittered on purpose: the vectors
// must describe where the surface went, not where the sub-pixel offset moved the sample.
//
// Objects with a world matrix of their own (models, NPCs, doors) need it multiplied in here; that is
// the next step and needs the engine to remember each object's previous transform. Until then they are
// treated as static, i.e. their vectors account for camera movement but not for their own.

// [DA_PORT] Current frame's view-projection WITHOUT the temporal-AA jitter. Motion vectors have to be
// computed between two un-jittered positions: m_WVP carries the sub-pixel offset TAA adds every frame,
// and subtracting an un-jittered previous position from a jittered current one yields the jitter itself
// as a phantom motion — every pixel appears to move while the camera stands still. Upscalers are told
// the jitter separately, so leaving it in the vectors would also count it twice.
// NB despite the name this is the full world-view-projection, just without the jitter — it has to
// mirror m_WVP_old exactly, and that one carries the object's world matrix too. For static geometry the
// world part is identity, so the two readings coincide.
// [DA_PORT] The projection jitter, for shaders to apply themselves.
//
// With FSR 2 the jitter must NOT go into Device.mProject: that matrix feeds everything — shadow
// cascades, particles, the HUD — while the upscaler only ever compensates the scene. Everything else
// then dithers uncompensated, and the whole picture reads as shaking no matter how the offset is
// reported. So under FSR 2 the matrix stays clean and scene geometry shifts itself by this constant,
// exactly the way IX-Ray does it.
//
// Zero when the jitter is already baked into the projection (our own temporal AA), so the same shader
// line is correct in both modes.
static class cl_taa_jitter : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // [DA_PORT] Any temporal upscaler, not FSR 2 alone - see da_upscaler_active(). While this named
        // FSR 2 only, selecting FSR 3 or XeSS handed the scene shaders a shift of zero, so nothing was
        // jittered and the upscaler had no sub-pixel samples to work from.
        // [DA_PORT] Сдвиг выдаётся ВСЕГДА, когда джиттер вообще включён — и для апскейлеров, и для
        // нашей темпоралки. Раньше под TAA здесь возвращался ноль, потому что джиттер шёл в матрицу
        // проекции; теперь он в матрицу не идёт ни в одном режиме (см. CCameraManager::ApplyDevice).
        // Ноль тут означал бы, что геометрию никто не сдвигает, а снятие сдвига при восстановлении
        // позиции всё равно вычтет ноль — то есть путь без апскейлера остался бы несогласованным.
        {
            // [DA_PORT] Converted to clip space HERE, from the pixel offset the upscaler is handed, so
            // the two can never describe different shifts. Device.dwRenderWidth is the scene size at
            // this point in the frame — the same reason cl_pos_decompress_params reads it from here.
            //
            // Y is negated: AMD's sample builds the jittered projection with +2*jx/width and MINUS
            // 2*jy/height, because clip space points up and their pixel offset points down. X keeps its
            // sign. Only the relative sign against the reported offset matters, and this is the pairing
            // FSR 2 expects.
            const float jx = ::g_da_fsr2_jitter_px.x * 2.f / float(Device.dwRenderWidth);
            const float jy = ::g_da_fsr2_jitter_px.y * -2.f / float(Device.dwRenderHeight);

            // [DA_PORT] Вес растительности растёт вместе с длиной кадра.
            //
            // Значение подбиралось на высокой частоте кадров, а на 30 трава начинает смазываться:
            // за кадр колышущийся стебель проходит вдвое больший путь, история промахивается вдвое
            // сильнее, а недоверие к ней остаётся прежним. Реактивность от движения (da_motion_
            // reactive рядом) этой болезни не знает, потому что считается ОТ САМОГО СМЕЩЕНИЯ и
            // потому масштабируется с частотой сама; здесь же вес плоский, и масштабировать его
            // приходится руками.
            //
            // Растим только вниз по частоте: множитель не опускается ниже единицы, поэтому на 60 и
            // выше остаётся ровно то значение, которым сегодня лечили дрожь травы, - эта правка не
            // может её вернуть. Потолок в четыре раза: на 15 кадрах и ниже смазывает уже всё, и
            // добавлять реактивности дальше значит менять смаз на рябь.
            //
            // Берём сглаженную длину кадра, а не мгновенную: скачок веса от кадра к кадру сам по
            // себе выглядел бы мерцанием - ровно тем, от чего вес и заведён.
            const float dt = Device.fTimeDelta;
            const float fps_scale = std::clamp(dt * 60.f, 1.f, 4.f);

            // z carries the foliage reactive weight - the aref shaders read it from here rather
            // than through a constant of their own, so it costs no extra binding.
            cmd_list.set_c(C, jx, jy, ::ps_r__reactive_foliage * fps_scale, 0.f);
        }
    }
} binder_taa_jitter;


// [DA_PORT] Detail-bump damping weights, see xr_ioc_cmd.cpp for what they are for. An ordinary pass
// binder is correct here: both values are global settings with nothing object-specific in them, so the
// once-per-pass evaluation that broke the motion-vector matrices is harmless.
// [DA_PORT] Gloss-driven reactive mask, see xr_ioc_cmd.cpp. x = weight, y = gloss threshold.
static class cl_gloss_reactive : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        cmd_list.set_c(C, ::ps_r__reactive_gloss, ::ps_r__reactive_gloss_min,
            1.f - ::ps_r__foliage_velocity, 1.f - ::ps_r__grass_velocity);
    }
} binder_gloss_reactive;

// [DA_PORT] Motion-driven reactivity, see da_motion_reactive in common_functions.h.
static class cl_reactive_motion : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        cmd_list.set_c(C, ::ps_r__reactive_motion, 0.f, 0.f, 0.f);
    }
} binder_reactive_motion;

static class cl_detail_fix : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        cmd_list.set_c(C, ::ps_r__detail_gloss_fix, ::ps_r__detail_normal_fix, float(::ps_r__detail_debug),
            ::ps_r__detail_albedo_fix);
    }
} binder_detail_fix;

// [DA_PORT] Motion-vector camera matrices WITHOUT any world part, for geometry that is already in world
// space by the time the vertex shader has it. Trees are the case: they carry their own transform in
// m_xform and draw as mul(m_VP, f_pos), never setting a world matrix of their own — so they inherited
// whichever one the last model left behind, and their vectors described a stranger's movement.
//
// These are per-frame camera matrices with nothing object-specific in them, so unlike m_WVP_old and
// m_VP_nojit they are safe as ordinary pass binders.
static class cl_vp_nojit_ws : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, ::g_da_taa_unjittered_VP); }
} binder_vp_nojit_ws;

static class cl_vp_old_ws : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, g_da_prev_VP); }
} binder_vp_old_ws;

static class cl_pos_decompress_params2 : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // [DA_PORT] Same reasoning: this is the G-buffer's size, used for texel-exact fetches.
        cmd_list.set_c(C, (float)Device.dwRenderWidth, (float)Device.dwRenderHeight,
            1.0f / (float)Device.dwRenderWidth, 1.0f / (float)Device.dwRenderHeight);
    }
} binder_pos_decompress_params2;

static class cl_water_intensity : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        const auto& env = g_pGamePersistent->Environment().CurrentEnv;
        const float fValue = env.m_fWaterIntensity;
        cmd_list.set_c(C, fValue, fValue, fValue, 0.f);
    }
} binder_water_intensity;

static class cl_sun_shafts_intensity : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        const auto& env = g_pGamePersistent->Environment().CurrentEnv;
        const float fValue = env.m_fSunShaftsIntensity;
        cmd_list.set_c(C, fValue, fValue, fValue, 0.f);
    }
} binder_sun_shafts_intensity;

static class cl_alpha_ref : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // TODO: OGL: Implement AlphaRef.
#   if defined(USE_DX11)
        cmd_list.StateManager.BindAlphaRef(C);
#   endif
    }
} binder_alpha_ref;

// Defined in ResourceManager.cpp
IReader* open_shader(pcstr shader);

// Check shadow cascades type (old SOC/CS or new COP)
static bool must_enable_old_cascades()
{
    bool oldCascades = false;
#if RENDER != R_R1
    {
        IReader* accumSunNear = open_shader("accum_sun_near.ps");
        R_ASSERT3(accumSunNear, "Can't open shader", "accum_sun_near.ps");
        do
        {
            xr_string str(static_cast<cpcstr>(accumSunNear->pointer()), accumSunNear->length());

            pcstr begin = strstr(str.c_str(), "float4");
            if (!begin)
                break;

            begin = strstr(begin, "main");
            if (!begin)
                break;

            cpcstr end = strstr(begin, "SV_Target");
            if (!end)
                break;

            str.assign(begin, end);
            cpcstr ptr = str.data();

            if (strstr(ptr, "v2p_TL2uv"))
            {
                oldCascades = true;
            }
            else if (strstr(ptr, "v2p_volume"))
            {
                oldCascades = false;
            }
        } while (false);
        FS.r_close(accumSunNear);
    }
#endif
    return oldCascades;
}

// Returns true if compute shaders for HDAO Ultra exist
[[maybe_unused]] static bool ssao_hdao_cs_shaders_exist()
{
    IReader* hdao_cs      = open_shader("ssao_hdao.cs");
    IReader* hdao_cs_msaa = open_shader("ssao_hdao_msaa.cs");

    const bool exist      = hdao_cs && hdao_cs_msaa;

    FS.r_close(hdao_cs);
    FS.r_close(hdao_cs_msaa);

    return exist;
}

//////////////////////////////////////////////////////////////////////////
// Just two static storage
void CRender::create()
{
    ZoneScoped;

    Device.seqFrame.Add(this, REG_PRIORITY_HIGH + 0x12345678);

    m_skinning = -1;
    m_MSAASample = -1;

    // hardware
    o.mrt = (HW.Caps.raster.dwMRT_count >= 3);
    o.mrtmixdepth = (HW.Caps.raster.b_MRT_mixdepth);

    // Check for NULL render target support
    o.nullrt = false;

    /*
    if (o.nullrt)		{
    Msg				("* NULLRT supported and used");
    };
    */
    if (o.nullrt)
    {
        Msg("* NULLRT supported");

        //.	    _tzset			();
        //.		??? _strdate	( date, 128 );	???
        //.		??? if (date < 22-march-07)
        if (0)
        {
            u32 device_id = HW.Caps.id_device;
            bool disable_nullrt = false;
            switch (device_id)
            {
            case 0x190:
            case 0x191:
            case 0x192:
            case 0x193:
            case 0x194:
            case 0x197:
            case 0x19D:
            case 0x19E:
            {
                disable_nullrt = true; // G80
                break;
            }
            case 0x400:
            case 0x401:
            case 0x402:
            case 0x403:
            case 0x404:
            case 0x405:
            case 0x40E:
            case 0x40F:
            {
                disable_nullrt = true; // G84
                break;
            }
            case 0x420:
            case 0x421:
            case 0x422:
            case 0x423:
            case 0x424:
            case 0x42D:
            case 0x42E:
            case 0x42F:
            {
                disable_nullrt = true; // G86
                break;
            }
            }
            if (disable_nullrt)
                o.nullrt = false;
        }
        if (o.nullrt)
            Msg("* ...and used");
    }

    // SMAP / DST
    o.HW_smap_FETCH4 = FALSE;
    o.HW_smap = true;
    o.HW_smap_PCF = o.HW_smap;

    if (o.HW_smap)
    {
#if defined(USE_DX11)
        //	For ATI it's much faster on DX11 to use D32F format
        if (HW.Caps.id_vendor == 0x1002)
            o.HW_smap_FORMAT = D3DFMT_D32F_LOCKABLE;
        else
#endif
        {
            o.HW_smap_FORMAT = D3DFMT_D24X8;
        }
        Msg("* HWDST/PCF supported and used");
    }

    o.fp16_filter = true;
    o.fp16_blend = true;

    // emulate ATI-R4xx series
    if (strstr(Core.Params, "-r4xx"))
    {
        o.mrtmixdepth = FALSE;
        o.HW_smap = FALSE;
        o.HW_smap_PCF = FALSE;
        o.fp16_filter = FALSE;
        o.fp16_blend = FALSE;
    }

    VERIFY2(o.mrt && (HW.Caps.raster.dwInstructions >= 256), "Hardware doesn't meet minimum feature-level");
    if (o.mrtmixdepth)
        o.albedo_wo = FALSE;
    else if (o.fp16_blend)
        o.albedo_wo = FALSE;
    else
        o.albedo_wo = TRUE;

    // nvstencil on NV40 and up
    // nvstencil should be enabled only for GF 6xxx and GF 7xxx
    // if hardware support early stencil (>= GF 8xxx) stencil reset trick only
    // slows down.
    o.nvstencil = FALSE;
    if (strstr(Core.Params, "-nonvs"))
        o.nvstencil = FALSE;

    // nv-dbt
    o.nvdbt = false;

    if (o.nvdbt)
        Msg("* NV-DBT supported and used");

    o.ffp = false;

    // options (smap-pool-size)
    if (strstr(Core.Params, "-smap1024"))
        o.smapsize = 1024;
    else if (strstr(Core.Params, "-smap1536"))
        o.smapsize = 1536;
    else if (strstr(Core.Params, "-smap2048"))
        o.smapsize = 2048;
    else if (strstr(Core.Params, "-smap2560"))
        o.smapsize = 2560;
    else if (strstr(Core.Params, "-smap3072"))
        o.smapsize = 3072;
    else if (strstr(Core.Params, "-smap4096"))
        o.smapsize = 4096;
    else if (strstr(Core.Params, "-smap8192"))
        o.smapsize = 8192;
    else
        o.smapsize = ps_r2_smapsize;

    // gloss
    cpcstr g = strstr(Core.Params, "-gloss ");
    o.forcegloss = g ? TRUE : FALSE;
    if (g)
    {
        o.forcegloss_v = float(atoi(g + xr_strlen("-gloss "))) / 255.f;
    }

    // options
    o.bug = (strstr(Core.Params, "-bug")) ? TRUE : FALSE;
    o.sunfilter = (strstr(Core.Params, "-sunfilter")) ? TRUE : FALSE;
    //.	o.sunstatic			= (strstr(Core.Params,"-sunstatic"))?	TRUE	:FALSE	;
    o.sunstatic = ps_r2_sun_static;
    o.advancedpp = ps_r2_advanced_pp;
#if defined(USE_DX11)
    o.volumetricfog = ps_r2_ls_flags.test(R3FLAG_VOLUMETRIC_SMOKE);
#elif defined(USE_OGL)
    // TODO: OGL: temporary disabled, need to fix it
    o.volumetricfog = false;
#endif
    o.sjitter = (strstr(Core.Params, "-sjitter")) ? TRUE : FALSE;
    o.depth16 = (strstr(Core.Params, "-depth16")) ? TRUE : FALSE;
    o.noshadows = (strstr(Core.Params, "-noshadows")) ? TRUE : FALSE;
    o.Tshadows = (strstr(Core.Params, "-tsh")) ? TRUE : FALSE;
    o.oldshadowcascades = must_enable_old_cascades() || ps_r2_ls_flags_ext.test(R2FLAGEXT_SUN_OLD);
    o.mblur = (strstr(Core.Params, "-mblur")) ? TRUE : FALSE;
    o.distortion_enabled = (strstr(Core.Params, "-nodistort")) ? FALSE : TRUE;
    o.distortion = o.distortion_enabled;
    o.disasm = (strstr(Core.Params, "-disasm")) ? TRUE : FALSE;
    o.forceskinw = (strstr(Core.Params, "-skinw")) ? TRUE : FALSE;

    o.ssao_blur_on = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_BLUR) && (ps_r_ssao != 0);
    o.ssao_opt_data = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_OPT_DATA) && (ps_r_ssao != 0);
    o.ssao_half_data = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HALF_DATA) && o.ssao_opt_data && (ps_r_ssao != 0);
#if defined(USE_DX11)
    o.ssao_hdao = ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HDAO) && (ps_r_ssao != 0);
    o.ssao_ultra = HW.ComputeShadersSupported && ssao_hdao_cs_shaders_exist();
    o.ssao_hbao = !o.ssao_hdao && ps_r2_ls_flags_ext.test(R2FLAGEXT_SSAO_HBAO) && (ps_r_ssao != 0);
#elif defined(USE_OGL)
    // TODO: OGL: temporary disabled HBAO/HDAO, need to fix it
    o.ssao_hbao = false;
    o.ssao_hdao = false;
#else
#   error No graphics API selected or enabled!
#endif

    //	TODO: fix hbao shader to allow to perform per-subsample effect!
    if (o.ssao_hbao && HW.Caps.id_vendor == 0x1002)
        o.hbao_vectorized = true;
    else
        o.hbao_vectorized = false;

#if defined(USE_DX11)
    o.dx11_sm4_1 = ps_r2_ls_flags.test((u32)R3FLAG_USE_DX10_1);
    o.dx11_sm4_1 = o.dx11_sm4_1 && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1);
#elif defined(USE_OGL)
    o.dx11_sm4_1 = true;
#else
#   error No graphics API selected or enabled!
#endif

    //	MSAA option dependencies
#if defined(USE_DX11)
    o.msaa = !!ps_r3_msaa;
    o.msaa_samples = (1 << ps_r3_msaa);

    o.msaa_opt = ps_r2_ls_flags.test(R3FLAG_MSAA_OPT);
    o.msaa_opt = o.msaa_opt && o.msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1) ||
        o.msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0);

    // o.msaa_hybrid	= ps_r2_ls_flags.test(R3FLAG_MSAA_HYBRID);
    o.msaa_hybrid = ps_r2_ls_flags.test((u32)R3FLAG_USE_DX10_1);
    o.msaa_hybrid &= !o.msaa_opt && o.msaa && (HW.FeatureLevel >= D3D_FEATURE_LEVEL_10_1);
#elif defined(USE_OGL)
    // TODO: OGL: temporary disabled, need to fix it
    o.msaa = false;
    o.msaa_samples = 0;
    o.msaa_opt = o.msaa;
    o.msaa_hybrid = false;
#else
#   error No graphics API selected or enabled!
#endif
    //	Allow alpha test MSAA for DX10.0

    // o.msaa_alphatest= ps_r2_ls_flags.test((u32)R3FLAG_MSAA_ALPHATEST);
    // o.msaa_alphatest= o.msaa_alphatest && o.msaa;

    // o.msaa_alphatest_atoc= (o.msaa_alphatest && !o.msaa_opt && !o.msaa_hybrid);

    o.msaa_alphatest = 0;
    if (o.msaa)
    {
        if (o.msaa_opt || o.msaa_hybrid)
        {
            if (ps_r3_msaa_atest == 1)
                o.msaa_alphatest = MSAA_ATEST_DX10_1_ATOC;
            else if (ps_r3_msaa_atest == 2)
                o.msaa_alphatest = MSAA_ATEST_DX10_1_NATIVE;
        }
        else
        {
            if (ps_r3_msaa_atest)
                o.msaa_alphatest = MSAA_ATEST_DX10_0_ATOC;
        }
    }

    o.gbuffer_opt = ps_r2_ls_flags.test(R3FLAG_GBUFFER_OPT);

    // [DA_PORT] Motion vectors. R4 only — the extra target and the shader option are DX11-side, and R2
    // has no upscaler to consume them. Latched here so a mid-game toggle cannot desynchronise the bound
    // target count from what the shaders were compiled for.
#if RENDER == R_R4
    // FSR 2 cannot work without motion vectors, so switching it on switches them on too.
    // [DA_PORT] Every consumer of the velocity buffer has to be listed here, FSR 3 included. Missing
    // one does not disable a feature quietly - it decides whether the G-buffer shaders are compiled
    // with DA_VELOCITY at all, i.e. whether the vertex stage emits the interpolators the pixel stage
    // declares. Get it wrong and the two disagree; D3D11 reports "Signatures between stages are
    // incompatible" and draws garbage, which on screen looks like models losing their textures and
    // standing in a T-pose - nothing that points back at an upscaler.
    // [DA_PORT] Через da_upscaler_active(), а не перечислением. Перечисление тут и подвело: строка
    // называла FSR 2 и FSR 3, а XeSS и DLSS пропускала — причём молча, потому что их имён в ней нет
    // вовсе, и поиском по «xess» такое не находится.
    //
    // Цена промаха здесь выше, чем в остальных местах со списком: этот признак решает, собираются ли
    // шейдеры геометрии с DA_VELOCITY. При выбранном DLSS они собирались БЕЗ него — то есть трава,
    // земля и модели не писали вектора вообще, а апскейлер реконструировал кадр по пустому буферу.
    // Ноль для него значит «пиксель стоял на месте», поэтому в движении картинка разваливалась, а
    // стоя выглядела нормально. Замер это и показал: низ кадра, где травы больше всего, заполнен на
    // 28-43%, верх (его пишет отдельный проход неба) — на 100%.
    o.velocity = !!::ps_r__motion_vectors || da_upscaler_active();
    // [DA_PORT] Mode 3 turns the velocity buffer into a map of WHICH SHADER drew each pixel: every
    // G-buffer shader writes a fixed identifier instead of a motion vector. Answers "what actually
    // draws this object" directly, instead of guessing from pass names in the log.
    o.velocity_debug_ids = (::ps_r__motion_vectors == 3);
    // [DA_PORT] The debug map and an upscaler must not run together: FSR 2 reads the very buffer the
    // stamps overwrite, so it receives identifiers where vectors should be - about fifty pixels per
    // frame - and reconstructs every pixel from the wrong place. The picture then shakes and smears,
    // which is easy to mistake for a fault in whatever is being investigated. It cost one wasted
    // measurement, hence the warning rather than a silent override: the mode is still useful with the
    // upscaler off, and the choice stays with whoever is debugging.
    if (o.velocity_debug_ids && da_upscaler_active()) // [DA_PORT] любой апскейлер, не FSR 2 один
        Msg("! [DA_PORT] r__motion_vectors 3 with an upscaler enabled: it is being fed shader "
            "identifiers instead of motion vectors. Expect the whole image to shake - switch the "
            "upscaler off while mapping shaders, and remember both are latched at renderer start.");
#else
    o.velocity = 0;
    o.velocity_debug_ids = 0;
#endif

    o.minmax_sm = ps_r3_minmax_sm;
    o.minmax_sm_screenarea_threshold = 1600 * 1200;

#if defined(USE_DX11)
    o.tessellation =
        HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0 && ps_r2_ls_flags_ext.test(R2FLAGEXT_ENABLE_TESSELLATION);
    o.support_rt_arrays = true;
#else
    o.support_rt_arrays = false;
#endif

    if (o.minmax_sm == MMSM_AUTODETECT)
    {
        o.minmax_sm = MMSM_OFF;

        //	AMD device
        if (HW.Caps.id_vendor == 0x1002)
        {
            if (ps_r_sun_quality >= 3)
                o.minmax_sm = MMSM_AUTO;
            else if (ps_r_sun_shafts >= 2)
            {
                o.minmax_sm = MMSM_AUTODETECT;
                //	Check resolution in runtime in use_minmax_sm_this_frame
                o.minmax_sm_screenarea_threshold = 1600 * 1200;
            }
        }

        //	NVidia boards
        if (HW.Caps.id_vendor == 0x10DE)
        {
            if (ps_r_sun_shafts >= 2)
            {
                o.minmax_sm = MMSM_AUTODETECT;
                //	Check resolution in runtime in use_minmax_sm_this_frame
                o.minmax_sm_screenarea_threshold = 1280 * 1024;
            }
        }
    }

    // constants
    Resources->RegisterConstantSetup("parallax", &binder_parallax);
    Resources->RegisterConstantSetup("water_intensity", &binder_water_intensity);
    Resources->RegisterConstantSetup("sun_shafts_intensity", &binder_sun_shafts_intensity);
    Resources->RegisterConstantSetup("pos_decompression_params", &binder_pos_decompress_params);
    Resources->RegisterConstantSetup("pos_decompression_params2", &binder_pos_decompress_params2);
    Resources->RegisterConstantSetup("m_AlphaRef", &binder_alpha_ref);
    Resources->RegisterConstantSetup("m_prev_VP", &binder_prev_vp); // [DA_PORT] temporal reprojection
    Resources->RegisterConstantSetup("m_taa_jitter", &binder_taa_jitter); // [DA_PORT] jitter for shaders
    Resources->RegisterConstantSetup("m_VP_nojit_ws", &binder_vp_nojit_ws); // [DA_PORT] world-space geometry
    Resources->RegisterConstantSetup("m_VP_old_ws", &binder_vp_old_ws); // [DA_PORT] world-space geometry
    Resources->RegisterConstantSetup("da_detail_fix", &binder_detail_fix); // [DA_PORT] detail-bump damping
    Resources->RegisterConstantSetup("da_reactive_motion", &binder_reactive_motion); // [DA_PORT]
    Resources->RegisterConstantSetup("da_gloss_reactive", &binder_gloss_reactive); // [DA_PORT] gloss opts out of history
#if defined(USE_DX11)
    Resources->RegisterConstantSetup("triLOD", &binder_LOD);
#endif

    Msg("* [DA_PORT] create: before CRenderTarget"); FlushLog();
    Target = xr_new<CRenderTarget>(); // Main target
    Msg("* [DA_PORT] create: after CRenderTarget"); FlushLog();

    Models = xr_new<CModelPool>();
    PSLibrary.OnCreate();
    HWOCC.occq_create(occq_size);
    Msg("* [DA_PORT] create: after Models/PSLibrary/HWOCC"); FlushLog();

    rmNormal(RCache);
    q_sync_point.Create();
    Msg("* [DA_PORT] create: after q_sync_point"); FlushLog();

    //	TODO: OGL: Implement FluidManager.
#if defined(USE_DX11)
    Msg("* [DA_PORT] create: before FluidManager.Initialize"); FlushLog();
    FluidManager.Initialize(70, 70, 70);
    //	FluidManager.Initialize( 100, 100, 100 );
    FluidManager.SetScreenSize(Device.dwWidth, Device.dwHeight);
    Msg("* [DA_PORT] create: after FluidManager"); FlushLog();
#endif
#if RENDER == R_R4
    g_da_gpu_timer.create(); // [DA_PORT] per-phase GPU timing, see da_gpu_timer.h
#endif
    Msg("* [DA_PORT] create: DONE"); FlushLog();
}

void CRender::destroy()
{
#if defined(USE_DX11)
    FluidManager.Destroy();
#endif
    q_sync_point.Destroy();
    HWOCC.occq_destroy();
    xr_delete(Models);
    xr_delete(Target);
    PSLibrary.OnDestroy();
    Device.seqFrame.Remove(this);
}

void CRender::reset_begin()
{
    ZoneScoped;
    // Wait for tasks to be done
    r_main.sync();
    r_sun.sync();
    r_sun_old.sync();
#if RENDER != R_R2
    r_rain.sync();
#endif

    Resources->reset_begin();

    // Update incremental shadowmap-visibility solver
    // BUG-ID: 10646
    {
        u32 it = 0;
        for (it = 0; it < Lights_LastFrame.size(); it++)
        {
            if (0 == Lights_LastFrame[it])
                continue;
            try
            {
                for (int id = 0; id < 3; ++id)
                    Lights_LastFrame[it]->svis[id].resetoccq();
            }
            catch (...)
            {
                Msg("! Failed to flush-OCCq on light [%d] %X", it, *(u32*)(&Lights_LastFrame[it]));
            }
        }
        Lights_LastFrame.clear();
    }

    //AVO: let's reload details while changed details options on vid_restart
    if (b_loaded && (dm_current_size != dm_size ||
        !fsimilar(ps_r__Detail_density, ps_current_detail_density) ||
        !fsimilar(ps_r__Detail_height, ps_current_detail_height)))
    {
        Details->Unload();
        xr_delete(Details);
    }
    //-AVO

    xr_delete(Target);
    HWOCC.occq_destroy();
    q_sync_point.Destroy();
}

void CRender::reset_end()
{
    ZoneScoped;
    q_sync_point.Create();
    HWOCC.occq_create(occq_size);

    Target = xr_new<CRenderTarget>();

    //AVO: let's reload details while changed details options on vid_restart
    if (b_loaded && (dm_current_size != dm_size ||
        !fsimilar(ps_r__Detail_density, ps_current_detail_density) ||
        !fsimilar(ps_r__Detail_height, ps_current_detail_height)))
    {
        Details = xr_new<CDetailManager>();
        Details->Load();
    }
    //-AVO

#if defined(USE_DX11)
    FluidManager.SetScreenSize(Device.dwWidth, Device.dwHeight);
#endif

    cleanup_contexts();

    // Set this flag true to skip the first render frame,
    // that some data is not ready in the first frame (for example device camera position)
    m_bFirstFrameAfterReset = true;
}

void CRender::OnCameraUpdated()
{
    ZoneScoped;

    // Frustum
    ViewBase.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    if (g_pGamePersistent->MainMenuActiveOrLevelNotExist())
        return;

    ProcessHOMTask = &HOM.DispatchMTRender();
    if (Details)
        Details->DispatchMTCalc();
}

void CRender::OnFrame()
{
    ZoneScoped;

    Models->DeleteQueue();

    if (g_pGamePersistent->MainMenuActiveOrLevelNotExist())
        return;
}

#ifdef USE_OGL
IRender::RenderContext CRender::GetCurrentContext() const
{
    return HW.GetCurrentContext();
}

void CRender::MakeContextCurrent(RenderContext context)
{
    R_ASSERT3(HW.MakeContextCurrent(context) == 0,
        "Failed to switch OpenGL context", SDL_GetError());
}
#endif

// Implementation
IRender_ObjectSpecific* CRender::ros_create(IRenderable* parent) { return xr_new<CROS_impl>(); }
void CRender::ros_destroy(IRender_ObjectSpecific*& p) { xr_delete(p); }
IRenderVisual* CRender::model_Create(LPCSTR name, IReader* data) { return Models->Create(name, data); }
IRenderVisual* CRender::model_CreateChild(LPCSTR name, IReader* data) { return Models->CreateChild(name, data); }
IRenderVisual* CRender::model_Duplicate(IRenderVisual* V) { return Models->Instance_Duplicate((dxRender_Visual*)V); }

void CRender::model_Delete(IRenderVisual*& V, bool bDiscard)
{
    dxRender_Visual* pVisual = (dxRender_Visual*)V;
    Models->Delete(pVisual, bDiscard);
    V = nullptr;
}

IRender_DetailModel* CRender::model_CreateDM(IReader* F)
{
    CDetail* D = xr_new<CDetail>();
    D->Load(F);
    return D;
}

void CRender::model_Delete(IRender_DetailModel*& F)
{
    if (F)
    {
        CDetail* D = (CDetail*)F;
        D->Unload();
        xr_delete(D);
        F = nullptr;
    }
}

IRenderVisual* CRender::model_CreatePE(LPCSTR name)
{
    PS::CPEDef* SE = PSLibrary.FindPED(name);
    R_ASSERT3(SE, "Particle effect doesn't exist", name);
    return Models->CreatePE(SE);
}

IRenderVisual* CRender::model_CreateParticles(LPCSTR name)
{
    PS::CPEDef* SE = PSLibrary.FindPED(name);
    if (SE)
        return Models->CreatePE(SE);

    PS::CPGDef* SG = PSLibrary.FindPGD(name);
    R_ASSERT3(SG, "Particle effect or group doesn't exist", name);
    return Models->CreatePG(SG);
}
void CRender::models_Prefetch() { Models->Prefetch(); }
void CRender::models_Clear(bool b_complete) { Models->ClearPool(b_complete); }
ref_shader CRender::getShader(int id)
{
    VERIFY(id < int(Shaders.size()));
    return Shaders[id];
}
IRenderVisual* CRender::getVisual(int id)
{
    VERIFY(id < int(Visuals.size()));
    return Visuals[id];
}

VertexElement* CRender::getVB_Format(int id, bool alternative)
{
    if (alternative)
    {
        VERIFY(id < int(xDC.size()));
        return xDC[id].begin();
    }
    VERIFY(id < int(nDC.size()));
    return nDC[id].begin();
}

VertexStagingBuffer* CRender::getVB(int id, bool alternative)
{
    if (alternative)
    {
        VERIFY(id<int(xVB.size()));
        return &xVB[id];
    }
    VERIFY(id < int(nVB.size()));
    return &nVB[id];
}

IndexStagingBuffer* CRender::getIB(int id, bool alternative)
{
    if (alternative)
    {
        VERIFY(id < int(xIB.size()));
        return &xIB[id];
    }
    VERIFY(id < int(nIB.size()));
    return &nIB[id];
}

FSlideWindowItem* CRender::getSWI(int id)
{
    VERIFY(id < int(SWIs.size()));
    return &SWIs[id];
}

IRender_Light* CRender::light_create() { return Lights.Create(); }
IRender_Glow* CRender::glow_create() { return xr_new<CGlow>(); }
bool CRender::occ_visible(vis_data& P) { return HOM.visible(P); }
bool CRender::occ_visible(sPoly& P) { return HOM.visible(P); }
bool CRender::occ_visible(Fbox& P) { return HOM.visible(P); }
void CRender::add_Visual(u32 context_id, IRenderable* root, IRenderVisual* V, Fmatrix& m)
{
    // TODO: this whole function should be replaced by a list of renderables+xforms returned from `renderable_Render` call
    auto& dsgraph = get_context(context_id);
    dsgraph.add_leafs_dynamic(root, (dxRender_Visual*)V, m);
}
void CRender::add_StaticWallmark(ref_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* verts)
{
    VERIFY2(T, "Invalid static wallmark triangle");
    if (T->suppress_wm)
        return;
    VERIFY2(_valid(P) && _valid(s) && verts && (s > EPS_L), "Invalid static wallmark params");
    Wallmarks->AddStaticWallmark(T, verts, P, &*S, s);
}

void CRender::add_StaticWallmark(IWallMarkArray* pArray, const Fvector& P, float s, CDB::TRI* T, Fvector* V)
{
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader)
        add_StaticWallmark(*pShader, P, s, T, V);
}

void CRender::add_StaticWallmark(const wm_shader& S, const Fvector& P, float s, CDB::TRI* T, Fvector* V)
{
    dxUIShader* pShader = (dxUIShader*)&*S;
    add_StaticWallmark(pShader->hShader, P, s, T, V);
}

void CRender::clear_static_wallmarks() { Wallmarks->clear(); }
void CRender::add_SkeletonWallmark(intrusive_ptr<CSkeletonWallmark> wm) { Wallmarks->AddSkeletonWallmark(wm); }
void CRender::add_SkeletonWallmark(
    const Fmatrix* xf, CKinematics* obj, ref_shader& sh, const Fvector& start, const Fvector& dir, float size)
{
    Wallmarks->AddSkeletonWallmark(xf, obj, sh, start, dir, size);
}
void CRender::add_SkeletonWallmark(
    const Fmatrix* xf, IKinematics* obj, IWallMarkArray* pArray, const Fvector& start, const Fvector& dir, float size)
{
    dxWallMarkArray* pWMA = (dxWallMarkArray*)pArray;
    ref_shader* pShader = pWMA->dxGenerateWallmark();
    if (pShader)
        add_SkeletonWallmark(xf, (CKinematics*)obj, *pShader, start, dir, size);
}

void CRender::rmNear(CBackend& cmd_list)
{
    const D3D_VIEWPORT viewport = { 0, 0, Target->get_width(cmd_list), Target->get_height(cmd_list), 0.f, 0.02f };
    cmd_list.SetViewport(viewport);
}

void CRender::rmFar(CBackend& cmd_list)
{
    const D3D_VIEWPORT viewport = { 0, 0, Target->get_width(cmd_list), Target->get_height(cmd_list), 0.99999f, 1.f };
    cmd_list.SetViewport(viewport);
}

void CRender::rmNormal(CBackend& cmd_list)
{
    const D3D_VIEWPORT viewport = { 0, 0, Target->get_width(cmd_list), Target->get_height(cmd_list), 0.f, 1.f };
    cmd_list.SetViewport(viewport);
}

void CRender::SetPostProcessParams(const SPPInfo& ppi)
{
    Target->set_blur(ppi.blur);
    Target->set_gray(ppi.gray);

    Target->set_duality_h(ppi.duality.h);
    Target->set_duality_v(ppi.duality.v);

    Target->set_noise(ppi.noise.intensity);
    Target->set_noise_scale(ppi.noise.grain);
    Target->set_noise_fps(ppi.noise.fps);

    Target->set_color_base(ppi.color_base);
    Target->set_color_gray(ppi.color_gray);
    Target->set_color_add(ppi.color_add);

    Target->set_cm_imfluence(ppi.cm_influence);
    Target->set_cm_interpolate(ppi.cm_interpolate);
    Target->set_cm_textures(ppi.cm_tex1, ppi.cm_tex2);
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CRender::CRender()
    : Sectors_xrc("render")
{
}

CRender::~CRender() {}

void CRender::DumpStatistics(IGameFont& font, IPerformanceAlert* alert)
{
    D3DXRenderBase::DumpStatistics(font, alert);
    Stats.FrameEnd();
    font.OutNext("Lights:");
    font.OutNext("- total:      %u", Stats.l_total);
    font.OutNext("- visible:    %u", Stats.l_visible);
    font.OutNext("- shadowed:   %u", Stats.l_shadowed);
    font.OutNext("- unshadowed: %u", Stats.l_unshadowed);
    font.OutNext("Shadow maps:");
    font.OutNext("- used:       %d", Stats.s_used);
    font.OutNext("- merged:     %d", Stats.s_merged - Stats.s_used);
    font.OutNext("- finalclip:  %d", Stats.s_finalclip);
    u32 ict = Stats.ic_total + Stats.ic_culled;
    font.OutNext("ICULL:        %03.1f", 100.f * f32(Stats.ic_culled) / f32(ict ? ict : 1));
    font.OutNext("- visible:    %u", Stats.ic_total);
    font.OutNext("- culled:     %u", Stats.ic_culled);
    Stats.FrameStart();
    HOM.DumpStatistics(font, alert);
    Sectors_xrc.DumpStatistics(font, alert);
}
} // namespace xray::render::RENDER_NAMESPACE
