#include "stdafx.h"

#include "da_fsr2.h"

namespace xray::render::RENDER_NAMESPACE
{
da_fsr2 g_da_fsr2;

// The library reports its own problems through this; without it a misconfiguration shows up only as a
// wrong picture.
static void fsr2_message(FfxFsr2MsgType type, const wchar_t* message)
{
    string512 text;
    const int written = WideCharToMultiByte(CP_ACP, 0, message, -1, text, sizeof(text) - 1, nullptr, nullptr);
    text[written > 0 ? written : 0] = 0;
    Msg("%s [FSR2] %s", type == FFX_FSR2_MESSAGE_TYPE_ERROR ? "!" : "~", text);
}

bool da_fsr2::create(const init_params& p)
{
    destroy();

    if (!p.device || !p.render_width || !p.display_width)
        return false;

    // The library wants a block of memory to work in; its size depends on the backend.
    const size_t scratch_size = ffxFsr2GetScratchMemorySizeDX11();
    m_scratch = xr_malloc(scratch_size);

    FfxFsr2ContextDescription desc{};
    FfxErrorCode code = ffxFsr2GetInterfaceDX11(&desc.callbacks, p.device, m_scratch, scratch_size);
    if (code != FFX_OK)
    {
        Msg("! [FSR2] cannot create the DX11 interface (%d)", code);
        xr_free(m_scratch);
        return false;
    }

    desc.device = ffxGetDeviceDX11(p.device);
    desc.maxRenderSize = { p.render_width, p.render_height };
    desc.displaySize = { p.display_width, p.display_height };

    // [DA_PORT] ⚠️ HIGH_DYNAMIC_RANGE снят: кадр к этому месту УЖЕ тонемаплен.
    //
    // Флаг ставился, когда апскейлер работал в другом месте кадра, и пережил переезд. Сейчас диспетч
    // стоит внутри phase_combine, после того как combine_1 (`tonemap(o.low, o.high, ...)`) свёл сцену
    // в отображаемый диапазон и обе половины сложились в rt_Color. То есть библиотеке говорили «это
    // линейный HDR», а давали картинку 0..1: её внутренняя работа с яркостью — веса, кламп соседства —
    // считалась в чужой шкале. Заметно это ровно там, где яркость экстремальная: светящаяся палочка на
    // тёмном фоне шла ступенчатой кромкой при ЛЮБОМ качестве и на всех трёх апскейлерах сразу, потому
    // что ошибка у всех трёх была одна и та же.
    //
    // INVERTED_DEPTH по-прежнему НЕ ставим — проекция X-Ray обычная.
    desc.flags = 0;
    // The library validates its own inputs far better than we can guess at them - resource sizes,
    // jitter against the phase count, depth convention - and says so through fpMessage. Worth turning
    // on in Release for a session whenever the upscaled picture looks wrong.
#ifdef DEBUG
    desc.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
    desc.fpMessage = fsr2_message;
#endif

    code = ffxFsr2ContextCreate(&m_context, &desc);
    if (code != FFX_OK)
    {
        Msg("! [FSR2] context creation failed (%d)", code);
        xr_free(m_scratch);
        return false;
    }

    m_created = true;
    Msg("* [FSR2] ready: %ux%u -> %ux%u", p.render_width, p.render_height, p.display_width, p.display_height);
    return true;
}

void da_fsr2::destroy()
{
    if (m_created)
    {
        ffxFsr2ContextDestroy(&m_context);
        m_created = false;
    }
    if (m_scratch)
    {
        xr_free(m_scratch);
        m_scratch = nullptr;
    }
}

bool da_fsr2::draw(const draw_params& p)
{
    if (!m_created)
        return false;

    FfxFsr2DispatchDescription d{};
    d.commandList = p.context;   // the DX11 backend takes the device context directly

    d.color = ffxGetResourceDX11(&m_context, p.colour, L"FSR2_Color");
    d.depth = ffxGetResourceDX11(&m_context, p.depth, L"FSR2_Depth");
    d.motionVectors = ffxGetResourceDX11(&m_context, p.velocity, L"FSR2_MotionVectors");
    d.exposure = ffxGetResourceDX11(&m_context, nullptr, L"FSR2_Exposure");
    // [DA_PORT] Reactive mask. Alpha-tested foliage is marked here (deffer_base_aref_*.ps): a branch
    // thinner than a pixel flips in and out with the jitter, so its history is worthless and blending
    // it produces either shimmer or smear. This is the only correct answer to that - the vectors are
    // not wrong, the pixel genuinely changes.
    d.reactive = ffxGetResourceDX11(&m_context, p.reactive, L"FSR2_Reactive");
    // [DA_PORT] Transparency-and-composition mask. Fed the SAME buffer as the reactive one, on a
    // switch, because the two masks answer different questions about the same pixels: reactive says
    // "this pixel legitimately changed", T&C says "this pixel was composed, treat its history with
    // suspicion". Alpha-tested foliage is arguably both. Sharing the buffer costs nothing and lets
    // the idea be judged before building a second render target for it.
    d.transparencyAndComposition = ffxGetResourceDX11(&m_context,
        p.tandc ? p.tandc : nullptr, L"FSR2_TransparencyAndComposition");
    d.output = ffxGetResourceDX11(&m_context, p.output, L"FSR2_Output", FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    d.jitterOffset.x = p.jitter_x;
    d.jitterOffset.y = p.jitter_y;

    // Our vectors are stored in NDC (the difference of two clip-space positions after the perspective
    // divide); FSR 2 wants them in pixels. Half the render size converts one to the other, and the sign
    // flip on x accounts for NDC growing to the right while the buffer's x grows the same way but the
    // library expects the opposite convention.
    d.motionVectorScale.x = float(p.render_width) * -0.5f;
    d.motionVectorScale.y = float(p.render_height) * 0.5f;

    d.renderSize.width = p.render_width;
    d.renderSize.height = p.render_height;

    d.frameTimeDelta = p.frame_time_ms;
    d.preExposure = 1.0f;
    d.reset = p.reset;

    d.enableSharpening = p.sharpening;
    d.sharpness = p.sharpness;

    d.cameraNear = p.near_plane;
    d.cameraFar = p.far_plane;
    d.cameraFovAngleVertical = p.fov_vertical;

    d.viewSpaceToMetersFactor = 1.0f;

    const FfxErrorCode code = ffxFsr2ContextDispatch(&m_context, &d);
    if (code != FFX_OK)
    {
        Msg("! [FSR2] dispatch failed (%d)", code);
        return false;
    }
    return true;
}

void da_fsr2::render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h)
{
    // The library's own ratios, so the reconstruction gets the sample density it was tuned for:
    // 1.5x, 1.7x, 2.0x and 3.0x per dimension respectively.
    //
    // [DA_PORT] Step 1 has no counterpart in the library - AMD's highest mode is 1.5x. It exists so
    // that all three upscalers answer to the same five-step quality control in the menu, and 1.3x is
    // simply computed here. Nothing in FSR 2 requires one of its named ratios: the context is built for
    // a maximum render size and any size up to it is valid.
    if (quality == 1)
    {
        out_w = u32(float(display_w) / 1.3f);
        out_h = u32(float(display_h) / 1.3f);
        return;
    }

    FfxFsr2QualityMode mode;
    switch (quality)
    {
    case 2: mode = FFX_FSR2_QUALITY_MODE_QUALITY; break;
    case 3: mode = FFX_FSR2_QUALITY_MODE_BALANCED; break;
    case 4: mode = FFX_FSR2_QUALITY_MODE_PERFORMANCE; break;
    case 5: mode = FFX_FSR2_QUALITY_MODE_ULTRA_PERFORMANCE; break;
    default: out_w = display_w; out_h = display_h; return;
    }

    ffxFsr2GetRenderResolutionFromQualityMode(&out_w, &out_h, display_w, display_h, mode);
}
} // namespace xray::render::RENDER_NAMESPACE
