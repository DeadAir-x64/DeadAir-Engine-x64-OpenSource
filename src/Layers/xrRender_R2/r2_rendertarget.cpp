#include "stdafx.h"
#include "Layers/xrRender/ResourceManager.h"
#include "Layers/xrRender/blenders/blender_light_occq.h"
#include "Layers/xrRender/blenders/blender_light_mask.h"
#include "Layers/xrRender/blenders/blender_light_direct.h"
#include "Layers/xrRender/blenders/blender_light_point.h"
#include "Layers/xrRender/blenders/blender_light_spot.h"
#include "Layers/xrRender/blenders/blender_light_reflected.h"
#include "Layers/xrRender/blenders/blender_combine.h"
#include "Layers/xrRender/blenders/blender_bloom_build.h"
#include "Layers/xrRender/blenders/blender_luminance.h"
#include "Layers/xrRender/blenders/blender_ssao.h"
#include "Layers/xrRender/blenders/blender_taa.h" // DA: temporal AA
#if RENDER == R_R4
#include "Layers/xrRenderPC_R4/da_fsr2.h" // DA: FSR 2
#include "Layers/xrRenderPC_R4/da_xess.h"
#include "Layers/xrRenderPC_R4/da_dlss.h" // [DA_PORT] NVIDIA DLSS
#include "Layers/xrRenderPC_R4/da_fsr3_api.h" // DA: Intel XeSS
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API void da_upscaler_set_available(u32 mask, pcstr why_hidden); // [DA_PORT]
extern ENGINE_API int ps_r__xess;
extern ENGINE_API int ps_r__dlss; // [DA_PORT]
extern ENGINE_API int ps_r__fsr3;
#endif

#include "Layers/xrRender/blenders/dx11MSAABlender.h"
#include "Layers/xrRender/blenders/dx11RainBlender.h"

#include "Layers/xrRender/blenders/dx11MinMaxSMBlender.h"
#if defined(USE_DX11)
#    include "Layers/xrRender/blenders/dx11HDAOCSBlender.h"
#endif

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::u_stencil_optimize(CBackend& cmd_list, eStencilOptimizeMode eSOM)
{
    PIX_EVENT(stencil_optimize);

#if defined(USE_DX11)
    // TODO: DX11: remove half pixel offset?
    VERIFY(RImplementation.o.nvstencil);
    u32 Offset;
    float _w = float(Device.dwRenderWidth);
    float _h = float(Device.dwRenderHeight);
    u32 C = color_rgba(255, 255, 255, 255);
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    float eps = 0;
    float _dw = 0.5f;
    float _dh = 0.5f;
    pv->set(-_dw, _h - _dh, eps, 1.f, C, 0, 0);
    pv++;
    pv->set(-_dw, -_dh, eps, 1.f, C, 0, 0);
    pv++;
    pv->set(_w - _dw, _h - _dh, eps, 1.f, C, 0, 0);
    pv++;
    pv->set(_w - _dw, -_dh, eps, 1.f, C, 0, 0);
    pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    cmd_list.set_Element(s_occq->E[1]);

    switch (eSOM)
    {
    case SO_Light: cmd_list.StateManager.SetStencilRef(dwLightMarkerID); break;
    case SO_Combine: cmd_list.StateManager.SetStencilRef(0x01); break;
    default: VERIFY(!"CRenderTarget::u_stencil_optimize. switch no default!");
    }

    cmd_list.set_Geometry(g_combine);
    cmd_list.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
#elif defined(USE_OGL)
    //	TODO: OGL: should we implement stencil optimization?
    VERIFY(RImplementation.o.nvstencil);
    VERIFY(!"CRenderTarget::u_stencil_optimize no implemented");
    UNUSED(eSOM);
#else
#   error No graphics API selected or enabled!
#endif // USE_DX11
}

// 2D texgen (texture adjustment matrix)
void CRenderTarget::u_compute_texgen_screen(CBackend& cmd_list, Fmatrix& m_Texgen)
{
#if defined(USE_DX11)
    Fmatrix m_TexelAdjust =
    {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
};
#elif defined(USE_OGL)
    Fmatrix m_TexelAdjust =
    {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };
#else
#   error No graphics API selected or enabled!
#endif

    m_Texgen.mul(m_TexelAdjust, cmd_list.xforms.m_wvp);
}

// 2D texgen for jitter (texture adjustment matrix)
void CRenderTarget::u_compute_texgen_jitter(CBackend& cmd_list, Fmatrix& m_Texgen_J)
{
    // place into 0..1 space
    Fmatrix m_TexelAdjust =
    {
        0.5f, 0.0f, 0.0f, 0.0f,
#if defined(USE_DX11)
        0.0f, -0.5f, 0.0f, 0.0f,
#elif defined(USE_OGL)
        0.0f, 0.5f, 0.0f, 0.0f,
#else
#   error No graphics API selected or enabled!
#endif
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };
    m_Texgen_J.mul(m_TexelAdjust, cmd_list.xforms.m_wvp);

    // rescale - tile it
    float scale_X = float(Device.dwRenderWidth) / float(TEX_jitter);
    float scale_Y = float(Device.dwRenderHeight) / float(TEX_jitter);
    m_TexelAdjust.scale(scale_X, scale_Y, 1.f);
    m_Texgen_J.mulA_44(m_TexelAdjust);
}

u8 fpack(float v)
{
    s32 _v = iFloor(((v + 1) * .5f) * 255.f + .5f);
    clamp(_v, 0, 255);
    return u8(_v);
}

u8 fpackZ(float v)
{
    s32 _v = iFloor(_abs(v) * 255.f + .5f);
    clamp(_v, 0, 255);
    return u8(_v);
}

Fvector vunpack(s32 x, s32 y, s32 z)
{
    Fvector pck;
    pck.x = (float(x) / 255.f - .5f) * 2.f;
    pck.y = (float(y) / 255.f - .5f) * 2.f;
    pck.z = -float(z) / 255.f;
    return pck;
}

Fvector vunpack(const Ivector& src)
{
    return vunpack(src.x, src.y, src.z);
}

Ivector vpack(const Fvector& src)
{
    Fvector _v;
    int bx = fpack(src.x);
    int by = fpack(src.y);
    int bz = fpackZ(src.z);
    // dumb test
    float e_best = flt_max;
    int r = bx, g = by, b = bz;
#ifdef DEBUG
    int d = 0;
#else
    int d = 3;
#endif
    for (int x = _max(bx - d, 0); x <= _min(bx + d, 255); x++)
        for (int y = _max(by - d, 0); y <= _min(by + d, 255); y++)
            for (int z = _max(bz - d, 0); z <= _min(bz + d, 255); z++)
            {
                _v = vunpack(x, y, z);
                float m = _v.magnitude();
                float me = _abs(m - 1.f);
                if (me > 0.03f)
                    continue;
                _v.div(m);
                float e = _abs(src.dotproduct(_v) - 1.f);
                if (e < e_best)
                {
                    e_best = e;
                    r = x, g = y, b = z;
                }
            }
    Ivector ipck;
    ipck.set(r, g, b);
    return ipck;
}

void manually_assign_texture(ref_shader& shader, pcstr textureName, pcstr rendertargetTextureName)
{
    SPass& pass = *shader->E[0]->passes[0];
    if (!pass.constants)
        return;

    const ref_constant constant = pass.constants->get(textureName);
    if (!constant)
        return;

    const auto index = constant->samp.index;
    pass.T->create_texture(index, rendertargetTextureName, false);
}

CRenderTarget::CRenderTarget()
{
    ZoneScoped;

    // [DA_PORT] Make sure the internal render resolution is current before any target is sized from it.
    // Targets are also rebuilt on device reset, which is how a render-scale change takes effect.
    Device.UpdateRenderResolution();

    // Belt and braces: a zero here would create 0x0 scene targets, i.e. a black screen with no D3D error.
    if (Device.dwRenderWidth == 0 || Device.dwRenderHeight == 0)
    {
        Device.dwRenderWidth = Device.dwWidth;
        Device.dwRenderHeight = Device.dwHeight;
    }
    Msg("* [DA_PORT] render targets: output %ux%u, scene %ux%u", Device.dwWidth, Device.dwHeight,
        Device.dwRenderWidth, Device.dwRenderHeight);

    static constexpr pcstr SAMPLE_DEFS[] = { "0", "1", "2", "3", "4", "5", "6", "7" };

    if (!strstr(Core.Params, "-smap"))
        RImplementation.o.smapsize = ps_r2_smapsize;

    RImplementation.m_SMAPSize = RImplementation.o.smapsize;
    RImplementation.o.rain_smapsize = ps_r3_dyn_wet_surf_sm_res;

    const auto& options = RImplementation.o;

    const u32 SampleCount  = options.msaa ? options.msaa_samples : 1u;
    const u32 BoundSamples = options.msaa_opt ? 1u : options.msaa_samples;

    // [DA_PORT] The stock report just below is #ifdef DEBUG, so a release build never says whether
    // multisampling actually came on - and "is this setting doing anything at all" is a question that
    // has cost this port whole evenings. The scene targets are built from these two numbers, so one
    // line here answers it for good. Cheap: once per renderer create.
    if (options.msaa)
        Msg("* [DA_PORT] MSAA: %u samples, per-sample lighting on edges only: %s", SampleCount,
            options.msaa_opt ? "yes" : "no");
    else
        Msg("* [DA_PORT] MSAA: off");

#ifdef DEBUG
    Msg("MSAA samples = %d", SampleCount);
    if (options.msaa_opt)
        Msg("MSAA_opt = on");
    if (options.gbuffer_opt)
        Msg("gbuffer_opt = on");
#endif

    param_blur = 0.f;
    param_gray = 0.f;
    param_noise = 0.f;
    param_duality_h = 0.f;
    param_duality_v = 0.f;
    param_noise_fps = 25.f;
    param_noise_scale = 1.f;

    im_noise_time = 1.0f / 100.0f;
    im_noise_shift_w = 0;
    im_noise_shift_h = 0;

    param_color_base = color_rgba(127, 127, 127, 0);
    param_color_gray = color_rgba(85, 85, 85, 0);
    param_color_add.set(0.0f, 0.0f, 0.0f);

    dwAccumulatorClearMark = 0;
    RImplementation.Resources->Evict();

    // Blenders
    b_accum_spot = xr_new<CBlender_accum_spot>();

    if (options.msaa)
    {
        for (u32 i = 0; i < BoundSamples; ++i)
        {
            b_accum_spot_msaa[i] = xr_new<CBlender_accum_spot_msaa>("ISAMPLE", SAMPLE_DEFS[i]);
            b_accum_volumetric_msaa[i] = xr_new<CBlender_accum_volumetric_msaa>("ISAMPLE", SAMPLE_DEFS[i]);
        }
    }

    // NORMAL
    {
        // [DA_PORT] Scene targets follow the internal render resolution ("r__render_scale"); only the
        // back buffer itself stays at the output size. rt_Base comes straight out of the swap chain, so
        // its dimensions are fixed by the swap chain regardless of what is passed here — but the scene
        // depth buffer is an ordinary texture and has to match the colour targets, or D3D rejects the
        // pairing. When the scale is 100% both are identical and nothing changes.
        u32 w = Device.dwRenderWidth, h = Device.dwRenderHeight;

        rt_Base.resize(HW.BackBufferCount);
        for (u32 i = 0; i < HW.BackBufferCount; i++)
        {
            string32 temp;
            xr_sprintf(temp, "%s%u", r2_RT_base, i);
            rt_Base[i].create(temp, Device.dwWidth, Device.dwHeight, HW.Caps.fTarget, 1, { CRT::CreateBase });
        }
        rt_Base_Depth.create(r2_RT_base_depth, w, h, HW.Caps.fDepth, 1, { CRT::CreateBase });

        if (!options.msaa)
            rt_MSAADepth = rt_Base_Depth;
        else
            rt_MSAADepth.create(r2_RT_MSAAdepth, w, h, D3DFMT_D24S8, SampleCount);

        rt_Position.create(r2_RT_P, w, h, D3DFMT_A16B16G16R16F, SampleCount);
#if RENDER == R_R4
        // DA: scene-grab target for water SSLR ("$user$ssr", bound to s_image by r3\effects_water.s).
        // ALWAYS single-sampled: with MSAA off we CopyResource rt_Generic_0 into it, with MSAA on we
        // ResolveSubresource into it — both require a non-MSAA destination.
        rt_SSR.create(r2_RT_SSR, w, h, D3DFMT_A8R8G8B8, 1);

#endif
        if (!options.gbuffer_opt)
            rt_Normal.create(r2_RT_N, w, h, D3DFMT_A16B16G16R16F, SampleCount);

        // select albedo & accum
        if (options.mrtmixdepth)
        {
            // NV50
            rt_Color.create(r2_RT_albedo, w, h, D3DFMT_A8R8G8B8, SampleCount);
            rt_Accumulator.create(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
        }
        else
        {
            // can't - mix-depth
            if (options.fp16_blend)
            {
                // NV40
                if (!options.gbuffer_opt)
                {
                    rt_Color.create(r2_RT_albedo, w, h, D3DFMT_A16B16G16R16F, SampleCount); // expand to full
                    rt_Accumulator.create(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
                }
                else
                {
                    rt_Color.create(r2_RT_albedo, w, h, D3DFMT_A8R8G8B8, SampleCount); // expand to full
                    rt_Accumulator.create(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
                }
            }
            else
            {
                // R4xx, no-fp-blend,-> albedo_wo
                VERIFY(options.albedo_wo);
                rt_Color.create(r2_RT_albedo, w, h, D3DFMT_A8R8G8B8, SampleCount); // normal
                rt_Accumulator.create(r2_RT_accum, w, h, D3DFMT_A16B16G16R16F, SampleCount);
                rt_Accumulator_temp.create(r2_RT_accum_temp, w, h, D3DFMT_A16B16G16R16F, SampleCount);
            }
        }

        // generic(LDR) RTs
        rt_Generic_0.create(r2_RT_generic0, w, h, D3DFMT_A8R8G8B8, 1);
        rt_Generic_1.create(r2_RT_generic1, w, h, D3DFMT_A8R8G8B8, 1);
        rt_Generic.create(r2_RT_generic, w, h, D3DFMT_A8R8G8B8, 1);

#if RENDER == R_R4
        // DA: temporal AA buffers. They live on rt_Color, which is where the finished frame lands after
        // the final combine, so they must match its format exactly — CopyResource refuses otherwise, and
        // rt_Color is fp16 on some option combinations. Always single-sampled: the resolve is skipped
        // under MSAA anyway.
        //
        // Three of them because the resolve has two outputs and neither can be its own input: rt_TAA_out
        // takes the sharpened frame that goes back into rt_Color, rt_TAA_scratch the plain one bound for
        // the history, and rt_TAA_history is what the next frame reads.
        {
            const D3DFORMAT taa_fmt = rt_Color->fmt;
            rt_TAA_out.create(r2_RT_taa_out, w, h, taa_fmt, 1);
            rt_TAA_scratch.create(r2_RT_taa_scratch, w, h, taa_fmt, 1);
            rt_TAA_history.create(r2_RT_taa_history, w, h, taa_fmt, 1);
        }

        // [DA_PORT] Motion vectors: where each pixel was on the previous frame, in screen space.
        // RG16F is the format FSR 2 and every other temporal upscaler expects — two signed channels
        // with enough precision for sub-pixel movement, at half the bandwidth of a full fp16 target.
        //
        // Deliberately NOT a fourth G-buffer target: that would mean changing the shared output
        // structure and touching all 48 blenders at once. Static geometry's motion is recovered
        // analytically from depth plus the previous camera matrix (one full-screen pass, no blender
        // changes), and only genuinely moving things need to draw into this on top.
        rt_Velocity.create(r2_RT_velocity, w, h, D3DFMT_G16R16F, 1);

        // [DA_PORT] Reactive mask: one channel, 1 where the pixel belongs to something a temporal
        // upscaler must not trust its history on. Alpha-tested foliage is the case that forced it -
        // a branch thinner than a pixel passes or fails the alpha test depending on where the jitter
        // put the sample, so the pixel legitimately changes every frame and blending it with history
        // produces either shimmer or smear, with nothing in between. Marking it reactive lets the
        // upscaler lean on the current frame there and keep accumulating everywhere else.
        //
        // Render resolution, like the rest of the G-buffer. Both FSR 2 and XeSS take it as a separate
        // single-channel texture, which is why it cannot simply be a spare channel of rt_Velocity.
        // R16F rather than an 8-bit format: the engine's D3DFORMAT list has no single-channel 8-bit
        // entry, and the extra byte per pixel is not worth adding one for.
        // [DA_PORT] Same format and size as the velocity buffer: the guard pass writes here and the
        // upscaler reads here, because a pass cannot read and write one target at once.
        rt_Velocity_guard.create(r2_RT_velocity_guard, w, h, D3DFMT_G16R16F, 1);

        rt_Reactive.create(r2_RT_reactive, w, h, D3DFMT_R16F, 1);

        // [DA_PORT] Working pair for phase_reactive. Widening happens one axis at a time - a maximum
        // over a square is the maximum of maxima over its rows, so a band of radius r costs 2r+1 reads
        // twice instead of the (2r+1) squared a single square pass would - and each axis needs a
        // destination that is not also its source. scratch2 carries the motion in and the result out,
        // scratch the half-finished pass between them.
        rt_Reactive_scratch.create(r2_RT_reactive_scratch, w, h, D3DFMT_R16F, 1);
        rt_Reactive_scratch2.create(r2_RT_reactive_scratch2, w, h, D3DFMT_R16F, 1);

        // [DA_PORT] FSR 2 writes its result here. Two things set it apart from every other target:
        // it is at the OUTPUT resolution rather than the scene's (that is the whole point of an
        // upscaler), and it needs unordered access, because FSR 2 is a compute shader and writes
        // through a UAV rather than as a render target.
        rt_FSR2_out.create(r2_RT_fsr2_out, Device.dwWidth, Device.dwHeight, D3DFMT_A16B16G16R16F, 1,
            { CRT::CreateUAV });

        // [DA_PORT] ---- Подрезаем список апскейлеров под железо -------------------------------
        //
        // Один раз за запуск, здесь: устройство уже создано, вендор видеокарты известен, библиотеки
        // можно спросить напрямую. Меню строится из таблицы токенов движка, поэтому вычеркнутый
        // пункт просто исчезает из списка — игрок не сможет выбрать то, что на его карте не
        // заработает. Подробности и мотив — в da_upscaler_set_available (xr_ioc_cmd.cpp).
        //
        // FSR 3 проверяется настоящей пробой: заранее по железу это не определить, а его отказ у
        // тестера на R9 290 выглядел как поломка игры. Проба стоит одного создания контекста и
        // делается только если FSR 3 сейчас не выбран — выбранный и так создаётся ниже, и его
        // результат учтёт da_upscaler_mark_failed.
        {
            static bool probed = false;
            if (!probed)
            {
                probed = true;

                u32 mask = 0;
                if (da_dlss::supported(HW.pDevice))
                    mask |= 1u << 6; // DLSS
                if (HW.Caps.id_vendor == 0x8086)
                    mask |= 1u << 5; // XeSS: путь D3D11 только на Intel

                bool fsr3_ok = !!::ps_r__fsr3;
                if (!fsr3_ok)
                {
                    fsr3_ok = da_fsr3_create(Device.dwRenderWidth, Device.dwRenderHeight,
                        Device.dwWidth, Device.dwHeight, HW.pDevice);
                    da_fsr3_destroy();
                }
                if (fsr3_ok)
                    mask |= 1u << 4; // FSR 3

                da_upscaler_set_available(mask, "видеокарта не поддерживает");
            }
        }

        // The upscaler is told both resolutions once, at creation: it allocates its history buffers for
        // them. Changing either means recreating the context, which is why r__fsr2 needs a restart.
        if (::ps_r__fsr2)
        {
            da_fsr2::init_params fsr;
            // maxRenderSize, not the current one: the library sizes its history buffers by this, and
            // anything rendered larger than it was told is undefined. Declaring the full output size
            // means r__render_scale can be changed at will without recreating the context — worth the
            // extra memory, since otherwise every change of the slider needs a restart to take effect.
            fsr.render_width = Device.dwWidth;
            fsr.render_height = Device.dwHeight;
            fsr.display_width = Device.dwWidth;
            fsr.display_height = Device.dwHeight;
            fsr.device = HW.pDevice;
            g_da_fsr2.create(fsr);

            // [DA_PORT] Intel XeSS, the alternative. Created alongside rather than instead: both are
            // cheap while idle, and having them both live means switching upscaler needs one restart
            // rather than two. Only whichever is enabled ever dispatches.
        }

        // [DA_PORT] Intel XeSS. Was nested inside the FSR 2 branch above, which meant it could only
        // ever be created while FSR 2 was ALSO enabled - and since selecting one upscaler now switches
        // the others off, that made XeSS impossible to create at all. Independent, like the others.
        if (::ps_r__xess)
        {
            da_xess::init_params xe;
            xe.display_width = Device.dwWidth;
            xe.display_height = Device.dwHeight;
            xe.quality = u32(::ps_r__xess);
            xe.device = HW.pDevice;
            g_da_xess.create(xe);
        }

        // [DA_PORT] NVIDIA DLSS. Независимо от остальных, по той же причине, что и XeSS: выбор одного
        // апскейлера гасит другие, и вложенное условие сделало бы его несоздаваемым.
        if (::ps_r__dlss)
        {
            da_dlss::init_params dl;
            // Ровно те же числа, которыми созданы цели сцены выше (w, h) и которые phase_dlss отдаёт
            // в evaluate. Здесь нельзя подставить Device.dwWidth, как это сделано у FSR 2: тот
            // объявляет ПРЕДЕЛ размера рендера и допускает движение масштаба без пересоздания, а
            // DLSS создаётся под конкретный размер.
            dl.render_width = Device.dwRenderWidth;
            dl.render_height = Device.dwRenderHeight;
            dl.display_width = Device.dwWidth;
            dl.display_height = Device.dwHeight;
            dl.quality = u32(::ps_r__dlss);
            dl.device = HW.pDevice;
            g_da_dlss.create(dl);
        }

        // [DA_PORT] FSR 3 создаётся под КОНКРЕТНЫЙ размер рендера, как DLSS выше, а не под предел,
        // как FSR 2.
        //
        // Так пришлось из-за общих ресурсов, которых у FSR 2 нет: da_fsr3::create зовёт
        // create_shared(render_w, render_h) и делает три текстуры (dilatedDepth,
        // dilatedMotionVectors, reconstructedPrevNearestDepth) ровно этого размера. А вызов потом
        // объявляет renderSize = Device.dwRenderWidth/Height, то есть УМЕНЬШЕННЫЙ масштабом. Пока
        // здесь стояло Device.dwWidth, ресурсы получались 1920x1080 против объявленных 1478x830 —
        // библиотека сверяет их с renderSize, не сходится, и ffxFsr3UpscalerContextDispatch падает
        // исключением. Проявлялось только при масштабе рендера меньше 100%: на 100% размеры
        // случайно совпадали, поэтому дефект и жил незамеченным.
        //
        // Пересоздания это не стоит: смена r__render_scale и так перестраивает цели сцены, а вместе
        // с ними проходит и этот код.
        if (::ps_r__fsr3)
        {
            da_fsr3_create(Device.dwRenderWidth, Device.dwRenderHeight, Device.dwWidth, Device.dwHeight,
                HW.pDevice);
        }
#endif

        if (!options.msaa)
        {
            rt_Generic_0_r = rt_Generic_0;
            rt_Generic_1_r = rt_Generic_1;
        }
        else
        {
            rt_Generic_0_r.create(r2_RT_generic0_r, w, h, D3DFMT_A8R8G8B8, SampleCount);
            rt_Generic_1_r.create(r2_RT_generic1_r, w, h, D3DFMT_A8R8G8B8, SampleCount);
        }
        //	Igor: for volumetric lights
        // rt_Generic_2.create			(r2_RT_generic2,w,h,D3DFMT_A8R8G8B8		);
        //	temp: for higher quality blends
        if (options.advancedpp)
            rt_Generic_2.create(r2_RT_generic2, w, h, D3DFMT_A16B16G16R16F, SampleCount);
    }

    // OCCLUSION
    {
        CBlender_light_occq b_occq;
        s_occq.create(&b_occq, "r2" DELIMITER "occq");
    }

    // DIRECT (spot)
    pcstr smapTarget = r2_RT_smap_depth;
    {
        const u32 smapsize = options.smapsize;

        D3DFORMAT depth_format = D3DFMT_D24X8;
        D3DFORMAT surf_format = D3DFMT_R32F;

        Flags32 flags{};
        if (!options.HW_smap)
        {
            flags.flags = CRT::CreateSurface;
            smapTarget = r2_RT_smap_surf;
        }
        else
        {
            depth_format = (D3DFORMAT)options.HW_smap_FORMAT;
            if (options.nullrt) // use nullrt if possible
                surf_format = (D3DFORMAT)MAKEFOURCC('N', 'U', 'L', 'L');
            else
                surf_format = D3DFMT_R5G6B5;
        }

        // We only need to create rt_smap_surf on DX9, on DX10+ it's always a NULL render target
        // TODO: OGL: Don't create a color buffer for the shadow map.
#if defined(USE_OGL)
        rt_smap_surf.create(r2_RT_smap_surf, smapsize, smapsize, surf_format);
#endif

        // Create D3DFMT_D24X8 depth-stencil surface if HW smap is not supported,
        // otherwise - create texture with specified HW_smap_FORMAT
        const auto num_slices = RImplementation.o.support_rt_arrays ? R__NUM_SUN_CASCADES : 1;
        rt_smap_depth.create(r2_RT_smap_depth, smapsize, smapsize, depth_format, 1, num_slices, flags);
        rt_smap_rain.create(r2_RT_smap_rain, options.rain_smapsize, options.rain_smapsize, depth_format);
        if (options.minmax_sm)
        {
            rt_smap_depth_minmax.create(r2_RT_smap_depth_minmax, smapsize / 4, smapsize / 4, D3DFMT_R32F);
            CBlender_createminmax b_create_minmax;
            s_create_minmax_sm.create(&b_create_minmax, "null");
        }

        // Accum mask
        {
            CBlender_accum_direct_mask b_accum_mask;
            s_accum_mask.create(&b_accum_mask, "r2" DELIMITER "accum_mask");
        }

        // Accum direct
        {
#if RENDER == R_R2
            if (options.oldshadowcascades)
            {
                CBlender_accum_direct b_accum_direct;
                s_accum_direct.create(&b_accum_direct, "r2" DELIMITER "accum_direct");
            }
            else
            {
                CBlender_accum_direct_cascade b_accum_direct;
                s_accum_direct.create(&b_accum_direct, "r2" DELIMITER "accum_direct_cascade");
            }
#else
            CBlender_accum_direct b_accum_direct;
            s_accum_direct.create(&b_accum_direct, "r2" DELIMITER "accum_direct");
#endif // RENDER == R_R2
        }

        // Accum direct/mask MSAA
        if (options.msaa)
        {
            for (u32 i = 0; i < BoundSamples; ++i)
            {
                CBlender_accum_direct_msaa b_accum_direct_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_accum_direct_msaa[i].create(&b_accum_direct_msaa, "r2" DELIMITER "accum_direct");
                CBlender_accum_direct_mask_msaa b_accum_mask_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_accum_mask_msaa[i].create(&b_accum_mask_msaa, "r2" DELIMITER "accum_direct");
            }
        }

        // Accum volumetric
        if (options.advancedpp)
        {
            s_accum_direct_volumetric.create("accum_volumetric_sun_nomsaa");
            manually_assign_texture(s_accum_direct_volumetric, "s_smap", smapTarget);

            if (options.minmax_sm)
            {
                s_accum_direct_volumetric_minmax.create("accum_volumetric_sun_nomsaa_minmax");
                manually_assign_texture(s_accum_direct_volumetric_minmax, "s_smap", smapTarget);
            }

            if (options.msaa)
            {
                static constexpr pcstr snames[] =
                {
                    "accum_volumetric_sun_msaa0", "accum_volumetric_sun_msaa1",
                    "accum_volumetric_sun_msaa2", "accum_volumetric_sun_msaa3",
                    "accum_volumetric_sun_msaa4", "accum_volumetric_sun_msaa5",
                    "accum_volumetric_sun_msaa6", "accum_volumetric_sun_msaa7"
                };

                for (u32 i = 0; i < BoundSamples; ++i)
                {
                    // CBlender_accum_direct_volumetric_sun_msaa b_accum_direct_volumetric_sun_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                    // s_accum_direct_volumetric_msaa[i].create(&b_accum_direct_volumetric_sun_msaa, "r2" DELIMITER "accum_direct");
                    s_accum_direct_volumetric_msaa[i].create(snames[i]);
                    manually_assign_texture(s_accum_direct_volumetric_msaa[i], "s_smap", smapTarget);
                }
            }
        }
    }

    // RAIN
    // TODO: DX11: Create resources only when DX11 rain is enabled.
    // Or make DX11 rain switch dynamic?
    {
        CBlender_rain b_rain;
        s_rain.create(&b_rain, "null");

        if (options.msaa)
        {
            for (u32 i = 0; i < BoundSamples; ++i)
            {
                CBlender_combine_msaa b_combine_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_combine_msaa[i].create(&b_combine_msaa, "r2" DELIMITER "combine");

                CBlender_rain_msaa b_rain_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_rain_msaa[i].create(&b_rain_msaa, "null");

                CBlender_accum_point_msaa b_accum_point_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_accum_point_msaa[i].create(&b_accum_point_msaa, "r2" DELIMITER "accum_point_s");

                s_accum_spot_msaa[i].create(b_accum_spot_msaa[i], "r2" DELIMITER "accum_spot_s", "lights" DELIMITER "lights_spot01");

                // CBlender_accum_direct_volumetric_msaa b_accum_direct_volumetric_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                // s_accum_volume_msaa[i].create(&b_accum_direct_volumetric_msaa, "lights" DELIMITER "lights_spot01");
                s_accum_volume_msaa[i].create(b_accum_volumetric_msaa[i], "lights" DELIMITER "lights_spot01");
            }
        }
    }

    if (options.msaa)
    {
        CBlender_msaa b_msaa;
        s_mark_msaa_edges.create(&b_msaa, "null");
    }

    // POINT
    {
        CBlender_accum_point b_accum_point;
        s_accum_point.create(&b_accum_point, "r2" DELIMITER "accum_point_s");
        accum_point_geom_create();
        g_accum_point.create(D3DFVF_XYZ, g_accum_point_vb, g_accum_point_ib);
        accum_omnip_geom_create();
        g_accum_omnipart.create(D3DFVF_XYZ, g_accum_omnip_vb, g_accum_omnip_ib);
    }

    // SPOT
    {
        s_accum_spot.create(b_accum_spot, "r2" DELIMITER "accum_spot_s", "lights" DELIMITER "lights_spot01");
        accum_spot_geom_create();
        g_accum_spot.create(D3DFVF_XYZ, g_accum_spot_vb, g_accum_spot_ib);
    }

    // SPOT VOLUMETRIC
    if (options.advancedpp)
    {
        s_accum_volume.create("accum_volumetric", "lights" DELIMITER "lights_spot01");
        manually_assign_texture(s_accum_volume, "s_smap", smapTarget);
        accum_volumetric_geom_create();
        g_accum_volumetric.create(D3DFVF_XYZ, g_accum_volumetric_vb, g_accum_volumetric_ib);
    }

    // REFLECTED
    {
        CBlender_accum_reflected b_accum_reflected;
        s_accum_reflected.create(&b_accum_reflected, "r2" DELIMITER "accum_refl");
        if (options.msaa)
        {
            for (u32 i = 0; i < BoundSamples; ++i)
            {
                CBlender_accum_reflected_msaa b_accum_reflected_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                s_accum_reflected_msaa[i].create(&b_accum_reflected_msaa, "null");
            }
        }
    }

    // BLOOM
    {
        D3DFORMAT fmt = D3DFMT_A8R8G8B8; // D3DFMT_X8R8G8B8;
        u32 w = BLOOM_size_X, h = BLOOM_size_Y;
        constexpr u32 fvf_build = D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
            D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3);
        constexpr u32 fvf_filter = (u32)D3DFVF_XYZRHW | D3DFVF_TEX8 | D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1) |
            D3DFVF_TEXCOORDSIZE4(2) | D3DFVF_TEXCOORDSIZE4(3) | D3DFVF_TEXCOORDSIZE4(4) | D3DFVF_TEXCOORDSIZE4(5) |
            D3DFVF_TEXCOORDSIZE4(6) | D3DFVF_TEXCOORDSIZE4(7);
        rt_Bloom_1.create(r2_RT_bloom1, w, h, fmt);
        rt_Bloom_2.create(r2_RT_bloom2, w, h, fmt);
        g_bloom_build.create(fvf_build, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
        g_bloom_filter.create(fvf_filter, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
        s_bloom_dbg_1.create("effects" DELIMITER "screen_set", r2_RT_bloom1);
        s_bloom_dbg_2.create("effects" DELIMITER "screen_set", r2_RT_bloom2);

        CBlender_bloom_build b_bloom;
        s_bloom.create(&b_bloom, "r2" DELIMITER "bloom");
        if (!options.msaa)
            s_bloom_msaa = s_bloom;
        else
        {
            CBlender_bloom_build_msaa b_bloom_msaa;
            s_bloom_msaa.create(&b_bloom_msaa, "r2" DELIMITER "bloom");
        }
        f_bloom_factor = 0.5f;
    }

    // Check if SSAO Ultra is allowed
    if (ps_r_ssao_mode != ssao_mode_hdao || !options.ssao_ultra)
        ps_r_ssao = _min(ps_r_ssao, 3);

    // HBAO
    if (options.ssao_opt_data)
    {
        u32 w = 0;
        u32 h = 0;
        if (options.ssao_half_data)
        {
            w = Device.dwRenderWidth / 2;
            h = Device.dwRenderHeight / 2;
        }
        else
        {
            w = Device.dwRenderWidth;
            h = Device.dwRenderHeight;
        }

        D3DFORMAT fmt = HW.Caps.id_vendor == 0x10DE ? D3DFMT_R32F : D3DFMT_R16F;
        rt_half_depth.create(r2_RT_half_depth, w, h, fmt);

        CBlender_SSAO_noMSAA b_ssao;
        s_ssao.create(&b_ssao, "r2" DELIMITER "ssao");
#if RENDER == R_R4
        // DA: temporal AA resolve (R4-only). Skipped under MSAA: common.h then types s_position as
        // Texture2DMS, which da_taa.ps does not sample, so the shader would only fail to compile.
        if (!options.msaa)
        {
            CBlender_TAA b_taa;
            s_taa.create(&b_taa, "r2" DELIMITER "taa");
            // [DA_PORT] Script blender, unlike the TAA one - it needs no textures beyond two
            // render targets and is simpler to keep in the shader tree.
            s_velocity_guard.create("da_velocity_guard");
            // [DA_PORT] Знаковая копия векторов для XeSS: у него, в отличие от FSR и DLSS, нет
            // параметра масштаба, и знак поправить больше негде. См. da_xess_mv.s.
            s_xess_mv.create("da_xess_mv");
            // [DA_PORT] Отражения в лужах: читает копию освещённого кадра, пишет поверх сцены.
            s_puddle_refl.create("da_puddle_refl");
            // [DA_PORT] Object-motion reactivity, see phase_reactive. Two blenders share one pixel
            // shader and differ only in which buffer they read - that is what lets the widening run
            // along one axis and then the other without a target ever being its own source.
            s_reactive.create("da_reactive");
            s_reactive_dilate_h.create("da_reactive_dilate_h");
            s_reactive_dilate_v.create("da_reactive_dilate_v");
            // [DA_PORT] Метка свечения: не читает вообще ничего, пишет константу по трафарету.
            s_reactive_emissive.create("da_reactive_emissive");
            s_sky_velocity.create("da_sky_velocity");
        }
#endif
    }

    // HDAO/SSAO
    const bool ssao_blur_on = options.ssao_blur_on;
    const bool ssao_hdao_ultra = options.ssao_hdao && options.ssao_ultra && ps_r_ssao > 3;

    if (ssao_blur_on || ssao_hdao_ultra)
    {
        const u32 w = Device.dwRenderWidth, h = Device.dwRenderHeight;

        if (ssao_hdao_ultra)
        {
#if defined(USE_DX11) // XXX: support compute shaders for OpenGL
            if (options.msaa)
            {
                CBlender_CS_HDAO_MSAA b_hdao_msaa_cs;
                s_hdao_cs.create(&b_hdao_msaa_cs, "r2" DELIMITER "ssao");
            }
            else
            {
                CBlender_CS_HDAO b_hdao_cs;
                s_hdao_cs.create(&b_hdao_cs, "r2" DELIMITER "ssao");
            }
            rt_ssao_temp.create(r2_RT_ssao_temp, w, h, D3DFMT_R16F, 1, { CRT::CreateUAV });
#endif
        }
        else if (ssao_blur_on)
        {
            CBlender_SSAO_noMSAA b_ssao;
            s_ssao.create(&b_ssao, "r2" DELIMITER "ssao");

            // Should be used in r*_rendertarget_phase_ssao.cpp but it's commented there.
            /*if (options.msaa)
            {
                for (u32 i = 0; i < BoundSamples; ++i)
                {
                    CBlender_SSAO_MSAA b_ssao_msaa{ "ISAMPLE", SAMPLE_DEFS[i] };
                    s_ssao_msaa[i].create(&b_ssao_msaa, "null");
                }
            }*/
            rt_ssao_temp.create(r2_RT_ssao_temp, w, h, D3DFMT_G16R16F, SampleCount);
        }
    }

    // TONEMAP
    {
        rt_LUM_64.create(r2_RT_luminance_t64, 64, 64, D3DFMT_A16B16G16R16F);
        rt_LUM_8.create(r2_RT_luminance_t8, 8, 8, D3DFMT_A16B16G16R16F);

        CBlender_luminance b_luminance;
        s_luminance.create(&b_luminance, "r2" DELIMITER "luminance");
        f_luminance_adapt = 0.5f;

        t_LUM_src.create(r2_RT_luminance_src);
        t_LUM_dest.create(r2_RT_luminance_cur);

        // create pool
        for (u32 it = 0; it < HW.Caps.iGPUNum * 2; it++)
        {
            string256 name;
            xr_sprintf(name, "%s_%d", r2_RT_luminance_pool, it);
            rt_LUM_pool[it].create(name, 1, 1, D3DFMT_R32F);
            RCache.ClearRT(rt_LUM_pool[it], 0x7f7f7f7f);
        }
        // [DA_PORT] see phase_pp: no depth when targeting the back buffer (sizes may differ)
        u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);
    }

    // COMBINE
    {
        static D3DVERTEXELEMENT9 dwDecl[] =
        {
            { 0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 }, // pos+uv
            D3DDECL_END()
        };

        CBlender_combine b_combine;
        s_combine.create(&b_combine, "r2" DELIMITER "combine");
        s_combine_volumetric.create("combine_volumetric");
        s_combine_dbg_0.create("effects" DELIMITER "screen_set", r2_RT_smap_surf);
        s_combine_dbg_1.create("effects" DELIMITER "screen_set", r2_RT_luminance_t8);
        s_combine_dbg_Accumulator.create("effects" DELIMITER "screen_set", r2_RT_accum);
        g_combine_VP.create(dwDecl, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
        g_combine.create(FVF::F_TL, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
        g_combine_2UV.create(FVF::F_TL2uv, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
        g_combine_cuboid.create(dwDecl, RImplementation.Vertex.Buffer(), RImplementation.Index.Buffer());

        constexpr u32 fvf_aa_blur = D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
            D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3);
        g_aa_blur.create(fvf_aa_blur, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);

        constexpr u32 fvf_aa_AA = D3DFVF_XYZRHW | D3DFVF_TEX7 | D3DFVF_TEXCOORDSIZE2(0) | D3DFVF_TEXCOORDSIZE2(1) |
            D3DFVF_TEXCOORDSIZE2(2) | D3DFVF_TEXCOORDSIZE2(3) | D3DFVF_TEXCOORDSIZE2(4) | D3DFVF_TEXCOORDSIZE4(5) |
            D3DFVF_TEXCOORDSIZE4(6);
        g_aa_AA.create(fvf_aa_AA, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
    }

    // Build textures
    build_textures();

    // PP
    s_postprocess.create("postprocess");
    g_postprocess.create(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX3,
        RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
    if (!options.msaa)
        s_postprocess_msaa = s_postprocess;
    else
    {
        CBlender_postprocess_msaa b_postprocess_msaa;
        s_postprocess_msaa.create(&b_postprocess_msaa, "r2" DELIMITER "post");
    }

    // Menu
    s_menu.create("distort");
    g_menu.create(FVF::F_TL, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);

#if 0 // OpenGL: kept for historical reasons
    // Flip
    t_base = RImplementation.Resources->_CreateTexture(r2_base);
    t_base->surface_set(GL_TEXTURE_2D, get_base_rt());
    s_flip.create("effects" DELIMITER "screen_set", r2_base);
    g_flip.create(FVF::F_TL, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
#endif

    //
    dwWidth[RCache.context_id] = Device.dwRenderWidth;
    dwHeight[RCache.context_id] = Device.dwRenderHeight;
}

CRenderTarget::~CRenderTarget()
{
#if defined(USE_DX11)
    // [DA_PORT] ⭐ Контексты апскейлеров уничтожаются ЗДЕСЬ. Раньше их не уничтожал никто.
    //
    // Создаются все четыре в конструкторе этого же класса, а парного вызова не было ни у одного:
    // da_fsr3_destroy() существовал, но его не звали, у FSR 2 и XeSS методы destroy() тоже стояли
    // без единого вызывающего, а DLSS убирался только деструктором своего глобального объекта — то
    // есть при выгрузке библиотеки, когда устройство D3D уже уничтожено.
    //
    // Стоило это двух вещей сразу.
    //
    // Во-первых, СБРОС УСТРОЙСТВА. Он пересоздаёт цели рендера, значит на каждую смену разрешения,
    // масштаба или апскейлера рождался новый контекст поверх живого старого. Ресурсы прежнего
    // (у DLSS это ещё и тензорные буферы NGX в десятки мегабайт) оставались висеть до конца сеанса.
    //
    // Во-вторых, ВЫХОД ИЗ ИГРЫ. Перепись живых объектов при уничтожении устройства (r__d3d_debug 1)
    // показала больше тысячи объектов: 309 буферов, 295 представлений, 263 текстуры, 7 UAV. Мы
    // отпускали устройство, пока их держали чужие контексты, — и следом падал уже сам драйвер
    // NVIDIA, в своём потоке, внутри освобождения кучи. Отчёт об этом падении в стеке нашего кода
    // не содержал вообще ничего, поэтому искать причину по нему было бесполезно.
    //
    // Порядок важен: сначала контексты, потом наши цели. Контексты держат ссылки на текстуры, и
    // освобождать те, пока библиотека ещё жива, значит уничтожать ресурс из-под её носа.
    g_da_fsr2.destroy();
    g_da_xess.destroy();
    da_fsr3_destroy();
    g_da_dlss.destroy();

    _RELEASE(t_ss_async);
#elif defined(USE_OGL)
    // Textures
    t_material->surface_set(GL_TEXTURE_3D, 0);
    glDeleteTextures(1, &t_material_surf);
    t_material.destroy();

    t_LUM_src->surface_set(GL_TEXTURE_2D, 0);
    t_LUM_dest->surface_set(GL_TEXTURE_2D, 0);
    t_LUM_src.destroy();
    t_LUM_dest.destroy();

    // Jitter
    for (u32 it = 0; it < TEX_jitter_count; it++)
    {
        t_noise[it]->surface_set(GL_TEXTURE_2D, 0);
    }
    glDeleteTextures(TEX_jitter_count, t_noise_surf);

    t_noise_mipped->surface_set(GL_TEXTURE_2D, 0);
    glDeleteTextures(1, &t_noise_surf_mipped);
#else
#   error No graphics API selected or enabled!
#endif
    //
    accum_spot_geom_destroy();
    accum_omnip_geom_destroy();
    accum_point_geom_destroy();
    accum_volumetric_geom_destroy();

    // Blenders
    xr_delete(b_accum_spot);
    if (RImplementation.o.msaa)
    {
        const u32 bound = RImplementation.o.msaa_opt ? 1 : RImplementation.o.msaa_samples;

        for (u32 i = 0; i < bound; ++i)
        {
            xr_delete(b_accum_spot_msaa[i]);
            xr_delete(b_accum_volumetric_msaa[i]);
        }
    }
}

void CRenderTarget::reset_light_marker(CBackend& cmd_list, bool bResetStencil)
{
    dwLightMarkerID = 5;
    if (bResetStencil)
    {
        u32 Offset;
        float _w = float(Device.dwRenderWidth);
        float _h = float(Device.dwRenderHeight);
        u32 C = color_rgba(255, 255, 255, 255);
        float eps = 0;
        float _dw = 0.5f;
        float _dh = 0.5f;
        FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-_dw, _h - _dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(-_dw, -_dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(_w - _dw, _h - _dh, eps, 1.f, C, 0, 0);
        pv++;
        pv->set(_w - _dw, -_dh, eps, 1.f, C, 0, 0);
        pv++;
        RImplementation.Vertex.Unlock(4, g_combine->vb_stride);
        cmd_list.set_Element(s_occq->E[2]);
        cmd_list.set_Geometry(g_combine);
        cmd_list.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }
}

void CRenderTarget::increment_light_marker(CBackend& cmd_list)
{
    dwLightMarkerID += 2;

    const u32 iMaxMarkerValue = RImplementation.o.msaa ? 127 : 255;

    if (dwLightMarkerID > iMaxMarkerValue)
        reset_light_marker(cmd_list, true);
}

bool CRenderTarget::need_to_render_sunshafts()
{
    if (!(RImplementation.o.advancedpp && ps_r_sun_shafts))
        return false;

    {
        const auto& env = g_pGamePersistent->Environment().CurrentEnv;
        const float fValue = env.m_fSunShaftsIntensity;
        // TODO: add multiplication by sun color here
        if (fValue < 0.0001)
            return false;
    }

    return true;
}

bool CRenderTarget::use_minmax_sm_this_frame()
{
    switch (RImplementation.o.minmax_sm)
    {
    case CRender::MMSM_ON: return true;
    case CRender::MMSM_AUTO: return need_to_render_sunshafts();
    case CRender::MMSM_AUTODETECT:
    {
        const auto& [width, height] = HW.GetSurfaceSize();
        u32 dwScreenArea = width * height;

        if (dwScreenArea >= RImplementation.o.minmax_sm_screenarea_threshold)
            return need_to_render_sunshafts();
        return false;
    }

    default: return false;
    }
}
} // namespace xray::render::RENDER_NAMESPACE
