#include "stdafx.h"

#include "da_xess.h"

namespace xray::render::RENDER_NAMESPACE
{
da_xess g_da_xess;

xess_quality_settings_t da_xess::quality_for(u32 quality)
{
    switch (quality)
    {
    case 1: return XESS_QUALITY_SETTING_ULTRA_QUALITY;  // 1.3x per dimension
    case 2: return XESS_QUALITY_SETTING_QUALITY;        // 1.5x
    case 3: return XESS_QUALITY_SETTING_BALANCED;       // 1.7x
    case 4: return XESS_QUALITY_SETTING_PERFORMANCE;    // 2.0x
    case 5: return XESS_QUALITY_SETTING_ULTRA_PERFORMANCE; // 3.0x
    default: return XESS_QUALITY_SETTING_AA;            // 1.0x - reconstruction at native size
    }
}

void da_xess::render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h)
{
    // Intel's documented scale factors. Taken from the header rather than from xessGetInputResolution
    // because that one needs a live context, and the render size has to be known before the context is
    // created - the scene targets are sized from it.
    float ratio;
    switch (quality)
    {
    case 1: ratio = 1.3f; break;
    case 2: ratio = 1.5f; break;
    case 3: ratio = 1.7f; break;
    case 4: ratio = 2.0f; break;
    case 5: ratio = 3.0f; break;
    default: ratio = 1.0f; break;
    }
    out_w = u32(float(display_w) / ratio) & ~1u;
    out_h = u32(float(display_h) / ratio) & ~1u;
}

bool da_xess::create(const init_params& p)
{
    destroy();

    if (!p.device || !p.display_width || !p.display_height)
        return false;

    xess_result_t r = xessD3D11CreateContext(p.device, &m_context);
    if (r != XESS_RESULT_SUCCESS)
    {
        // [DA_PORT] XESS_RESULT_ERROR_UNSUPPORTED_DEVICE (-1) is the expected answer on most machines,
        // and it is not a fault in this code. Intel's own guide, under "Initialization D3D11 specific":
        // XeSS-SR on D3D11 is limited to Intel Arc Graphics or later, and creating a context on any
        // other device fails with exactly this. The cross-vendor DP4a path exists only for D3D12 and
        // Vulkan, neither of which this renderer has.
        //
        // So this is a working feature for Arc owners and a no-op for everyone else. Said plainly
        // because the opposite was assumed when the integration was planned: XeSS being open and
        // cross-vendor, and XeSS having a D3D11 path, are both true and do not overlap.
        if (r == XESS_RESULT_ERROR_UNSUPPORTED_DEVICE)
            Msg("* [XESS] not available: the D3D11 path needs an Intel Arc GPU. FSR 2 is unaffected.");
        else
            Msg("! [XESS] cannot create the DX11 context (%d)", int(r));
        return false;
    }

    xess_d3d11_init_params_t init{};
    init.outputResolution = { p.display_width, p.display_height };
    init.qualitySetting = quality_for(p.quality);

    // USE_NDC_VELOCITY: our motion vectors are the difference of two clip-space positions after the
    // perspective divide, which is exactly what this flag means. FSR 2 had no such switch and wanted
    // pixels, so it needed a motionVectorScale whose sign took a night to settle; here the format is
    // declared once and there is nothing left to get backwards.
    //
    // INVERTED_DEPTH is deliberately NOT set - X-Ray's projection is the conventional way up, same
    // reasoning as in da_fsr2.
    // RESPONSIVE_PIXEL_MASK is Intel's name for the same thing FSR 2 calls the reactive mask, and it
    // takes the identical texture - one mask serves both, which is why it is worth building properly.
    init.initFlags = XESS_INIT_FLAG_USE_NDC_VELOCITY | XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK;

    r = xessD3D11Init(m_context, &init);
    if (r != XESS_RESULT_SUCCESS)
    {
        Msg("! [XESS] initialisation failed (%d)", int(r));
        xessDestroyContext(m_context);
        m_context = nullptr;
        return false;
    }

    m_created = true;

    u32 rw, rh;
    render_size_for(p.quality, p.display_width, p.display_height, rw, rh);
    Msg("* [XESS] ready: %ux%u -> %ux%u, quality mode %d", rw, rh, p.display_width, p.display_height,
        p.quality);
    return true;
}

void da_xess::destroy()
{
    if (m_created)
    {
        xessDestroyContext(m_context);
        m_context = nullptr;
        m_created = false;
    }
}

bool da_xess::draw(const draw_params& p)
{
    if (!m_created)
        return false;

    xess_d3d11_execute_params_t e{};
    e.pColorTexture = p.colour;
    e.pVelocityTexture = p.velocity;
    e.pDepthTexture = p.depth;
    e.pOutputTexture = p.output;
    e.pResponsivePixelMaskTexture = p.reactive;

    // Already in the [-0.5, 0.5] range XeSS asks for: CCameraManager generates the jitter in pixels
    // centred on the pixel. The scene shaders apply it themselves through m_taa_jitter - it must not
    // reach Device.mProject, or shadows, particles and the HUD dither with nothing to compensate them.
    e.jitterOffsetX = p.jitter_x;
    e.jitterOffsetY = p.jitter_y;

    e.exposureScale = 1.f;
    e.resetHistory = p.reset ? 1u : 0u;
    e.inputWidth = p.render_width;
    e.inputHeight = p.render_height;

    const xess_result_t r = xessD3D11Execute(m_context, &e);
    if (r != XESS_RESULT_SUCCESS)
    {
        Msg("! [XESS] execute failed (%d)", int(r));
        return false;
    }
    return true;
}
} // namespace xray::render::RENDER_NAMESPACE
