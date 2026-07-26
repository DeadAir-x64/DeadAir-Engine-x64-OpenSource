#include "stdafx.h"

#include "da_xess.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API int ps_r__xess;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

namespace xray::render::RENDER_NAMESPACE
{
// Shared with FSR 2 on purpose: only one upscaler reconstructs a given frame, and the post-process
// pass only needs to know whether SOMETHING produced one. Reusing the stamp means postprocess.ps and
// its da_upscale.z need no second branch.
extern u32 g_da_fsr2_frame;

bool CRenderTarget::phase_xess()
{
    if (!ps_r__xess || !g_da_xess.ready())
        return false;

    PIX_EVENT(DA_phase_xess);

    // [DA_PORT] Release the outputs first — same trap that made FSR 2 reconstruct a black frame and
    // report success. Combine leaves rt_Color bound as the render target and rt_Base_Depth as the
    // depth-stencil, which are exactly the two resources read below, and D3D11 silently feeds a shader
    // zeros for anything simultaneously bound for writing.
    u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);

    ID3DBaseTexture* colour = rt_Color->pTexture->surface_get();
    ID3DBaseTexture* depth = rt_Base_Depth->pTexture->surface_get();
    ID3DBaseTexture* velocity = rt_Velocity->pTexture->surface_get();
    // The same target FSR 2 writes into: display resolution with unordered access, which is what both
    // libraries ask for. They never run in the same frame, so there is nothing to share badly.
    ID3DBaseTexture* output = rt_FSR2_out->pTexture->surface_get();
    ID3DBaseTexture* reactive = rt_Reactive ? rt_Reactive->pTexture->surface_get() : nullptr;

    bool ok = false;
    if (colour && depth && velocity && output)
    {
        da_xess::draw_params p;
        p.context = HW.get_context(CHW::IMM_CTX_ID);

        p.colour = colour;
        p.depth = depth;
        p.velocity = velocity;
        p.output = output;
        p.reactive = reactive;

        p.render_width = Device.dwRenderWidth;
        p.render_height = Device.dwRenderHeight;

        // In pixels, exactly as generated — XeSS wants [-0.5, 0.5] and that is the range
        // CCameraManager produces. No conversion, and no sign to get wrong: the vectors go in as NDC
        // under XESS_INIT_FLAG_USE_NDC_VELOCITY, so unlike FSR 2 there is no scale factor at all.
        p.jitter_x = ::g_da_fsr2_jitter_px.x;
        p.jitter_y = ::g_da_fsr2_jitter_px.y;

        p.reset = (Device.dwFrame < 3);

        ok = g_da_xess.draw(p);
        if (ok)
            g_da_fsr2_frame = Device.dwFrame;
    }

    _RELEASE(colour);
    _RELEASE(depth);
    _RELEASE(velocity);
    _RELEASE(output);
    _RELEASE(reactive);
    return ok;
}
} // namespace xray::render::RENDER_NAMESPACE
