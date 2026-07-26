#include "stdafx.h"

#include "da_fsr3.h"

namespace xray::render::RENDER_NAMESPACE
{
da_fsr3 g_da_fsr3;

// [DA_PORT] Type-free entry points, see da_fsr3_api.h for why they exist.
bool da_fsr3_create(u32 render_w, u32 render_h, u32 display_w, u32 display_h, ID3D11Device* device)
{
    da_fsr3::init_params p;
    p.render_width = render_w;
    p.render_height = render_h;
    p.display_width = display_w;
    p.display_height = display_h;
    p.device = device;
    return g_da_fsr3.create(p);
}
void da_fsr3_destroy() { g_da_fsr3.destroy(); }
bool da_fsr3_ready() { return g_da_fsr3.ready(); }

// The library reports its own problems through this; without it a misconfiguration shows up only as a
// wrong picture. Worth having in Release - it validates resource sizes, the depth convention and the
// jitter against the phase count far better than we can guess at them.
static void fsr3_message(FfxMsgType type, const wchar_t* message)
{
    string512 text;
    const int written = WideCharToMultiByte(CP_ACP, 0, message, -1, text, sizeof(text) - 1, nullptr, nullptr);
    text[written > 0 ? written : 0] = 0;
    Msg("%s [FSR3] %s", type == FFX_MESSAGE_TYPE_ERROR ? "!" : "~", text);
}

// [DA_PORT] The three buffers FSR 3 shares with whatever runs after it. Formats and usage are taken
// verbatim from ffxFsr3UpscalerGetSharedResourceDescriptions in the component source - getting them
// wrong is not caught at creation, only later as corrupt output.
bool da_fsr3::create_shared(u32 width, u32 height)
{
    ID3D11Device* device = HW.pDevice;
    if (!device)
        return false;

    auto make = [&](DXGI_FORMAT fmt, bool render_target, ID3D11Texture2D** out) -> bool
    {
        D3D11_TEXTURE2D_DESC d{};
        d.Width = width;
        d.Height = height;
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = fmt;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        if (render_target)
            d.BindFlags |= D3D11_BIND_RENDER_TARGET;
        return SUCCEEDED(device->CreateTexture2D(&d, nullptr, out));
    };

    if (!make(DXGI_FORMAT_R32_FLOAT, true, &m_dilated_depth) ||
        !make(DXGI_FORMAT_R16G16_FLOAT, true, &m_dilated_motion) ||
        !make(DXGI_FORMAT_R32_UINT, false, &m_prev_depth))
    {
        Msg("! [FSR3] cannot create the shared buffers (%ux%u)", width, height);
        destroy_shared();
        return false;
    }
    return true;
}

void da_fsr3::destroy_shared()
{
    _RELEASE(m_dilated_depth);
    _RELEASE(m_dilated_motion);
    _RELEASE(m_prev_depth);
}

bool da_fsr3::create(const init_params& p)
{
    destroy();

    if (!p.device || !p.render_width || !p.display_width)
        return false;

    // Unlike FSR 2, where the DX11 backend was handed straight to the context, FSR 3 goes through a
    // generic FfxInterface. One context is all we need.
    const size_t scratch_size = ffxGetScratchMemorySizeDX11(1);
    m_scratch = xr_malloc(scratch_size);

    FfxErrorCode code = ffxGetInterfaceDX11(&m_backend, ffxGetDeviceDX11_Fsr31(p.device), m_scratch, scratch_size, 1);
    if (code != FFX_OK)
    {
        Msg("! [FSR3] cannot create the DX11 interface (%d)", code);
        xr_free(m_scratch);
        m_scratch = nullptr;
        return false;
    }

    if (!create_shared(p.render_width, p.render_height))
    {
        xr_free(m_scratch);
        m_scratch = nullptr;
        return false;
    }

    FfxFsr3UpscalerContextDescription desc{};
    desc.maxRenderSize = { p.render_width, p.render_height };
    desc.maxUpscaleSize = { p.display_width, p.display_height };
    desc.backendInterface = m_backend;
    desc.fpMessage = fsr3_message;

    // HIGH_DYNAMIC_RANGE: the scene reaching the upscaler has not been tonemapped yet, so values go
    // above 1. DEPTH_INVERTED is deliberately NOT set - X-Ray's projection is the conventional way up,
    // the same reasoning as in da_fsr2.
    // [DA_PORT] DEBUG_CHECKING stays on in Release. The backend answers a failed context with a bare
    // FFX_ERROR_BACKEND_API_ERROR, which says only "something inside went wrong"; the message callback
    // is the only way to learn what. It costs a few validations at startup and nothing per frame.
    desc.flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3UPSCALER_ENABLE_DEBUG_CHECKING;

    code = ffxFsr3UpscalerContextCreate(&m_context, &desc);
    if (code != FFX_OK)
    {
        Msg("! [FSR3] context creation failed (%d)", code);
        destroy_shared();
        xr_free(m_scratch);
        m_scratch = nullptr;
        return false;
    }

    m_created = true;
    Msg("* [FSR3] ready: %ux%u -> %ux%u", p.render_width, p.render_height, p.display_width, p.display_height);
    return true;
}

void da_fsr3::destroy()
{
    if (m_created)
    {
        ffxFsr3UpscalerContextDestroy(&m_context);
        m_created = false;
    }
    destroy_shared();
    if (m_scratch)
    {
        xr_free(m_scratch);
        m_scratch = nullptr;
    }
}

bool da_fsr3::draw(const draw_params& p)
{
    if (!m_created)
        return false;

    FfxFsr3UpscalerDispatchDescription d{};
    d.commandList = ffxGetCommandListDX11(p.context);

    d.color = ffxGetResourceDX11_Fsr31(p.colour, GetFfxResourceDescriptionDX11(p.colour), nullptr);
    d.depth = ffxGetResourceDX11_Fsr31(p.depth, GetFfxResourceDescriptionDX11(p.depth), nullptr);
    d.motionVectors = ffxGetResourceDX11_Fsr31(p.velocity, GetFfxResourceDescriptionDX11(p.velocity), nullptr);
    d.exposure = ffxGetResourceDX11_Fsr31(nullptr, FfxResourceDescription{}, nullptr);
    d.reactive = p.reactive
        ? ffxGetResourceDX11_Fsr31(p.reactive, GetFfxResourceDescriptionDX11(p.reactive), nullptr)
        : ffxGetResourceDX11_Fsr31(nullptr, FfxResourceDescription{}, nullptr);
    d.transparencyAndComposition = p.tandc
        ? ffxGetResourceDX11_Fsr31(p.tandc, GetFfxResourceDescriptionDX11(p.tandc), nullptr)
        : ffxGetResourceDX11_Fsr31(nullptr, FfxResourceDescription{}, nullptr);

    d.dilatedDepth = ffxGetResourceDX11_Fsr31(m_dilated_depth, GetFfxResourceDescriptionDX11(m_dilated_depth),
        nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.dilatedMotionVectors = ffxGetResourceDX11_Fsr31(m_dilated_motion,
        GetFfxResourceDescriptionDX11(m_dilated_motion), nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    d.reconstructedPrevNearestDepth = ffxGetResourceDX11_Fsr31(m_prev_depth,
        GetFfxResourceDescriptionDX11(m_prev_depth), nullptr, FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    d.output = ffxGetResourceDX11_Fsr31(p.output, GetFfxResourceDescriptionDX11(p.output), nullptr,
        FFX_RESOURCE_STATE_UNORDERED_ACCESS);

    d.jitterOffset.x = p.jitter_x;
    d.jitterOffset.y = p.jitter_y;

    // Same convention as FSR 2: our vectors are the difference of two clip-space positions after the
    // perspective divide, the library wants pixels, and the x sign is flipped because NDC and the
    // buffer disagree about which way the axis runs. Settled by measurement, do not "fix" by reasoning.
    d.motionVectorScale.x = float(p.render_width) * -0.5f;
    d.motionVectorScale.y = float(p.render_height) * 0.5f;

    d.renderSize = { p.render_width, p.render_height };
    d.upscaleSize = { p.display_width, p.display_height };

    d.enableSharpening = p.sharpening;
    d.sharpness = p.sharpness;
    d.frameTimeDelta = p.frame_time_ms;
    d.preExposure = 1.0f;
    d.reset = p.reset;

    d.cameraNear = p.near_plane;
    d.cameraFar = p.far_plane;
    d.cameraFovAngleVertical = p.fov_vertical;
    d.viewSpaceToMetersFactor = 1.0f;

    const FfxErrorCode code = ffxFsr3UpscalerContextDispatch(&m_context, &d);
    if (code != FFX_OK)
    {
        Msg("! [FSR3] dispatch failed (%d)", code);
        return false;
    }
    return true;
}

void da_fsr3::render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h)
{
    // The same five steps every upscaler in the menu answers to: 1.3x, 1.5x, 1.7x, 2.0x, 3.0x per
    // dimension. FSR 3 exposes its own ratio helper, but keeping one table for all three backends is
    // what makes the quality control mean the same thing whichever is selected.
    static const float ratio[5] = { 1.3f, 1.5f, 1.7f, 2.0f, 3.0f };
    if (quality < 1 || quality > 5)
    {
        out_w = display_w;
        out_h = display_h;
        return;
    }
    const float r = ratio[quality - 1];
    out_w = u32(float(display_w) / r);
    out_h = u32(float(display_h) / r);
}
} // namespace xray::render::RENDER_NAMESPACE
