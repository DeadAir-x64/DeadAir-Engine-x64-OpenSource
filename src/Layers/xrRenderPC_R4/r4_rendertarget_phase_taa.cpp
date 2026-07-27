#include "stdafx.h"

// DA: temporal anti-aliasing resolve. Blends the previous frame (rt_TAA_history) into the current one
// and keeps the result as the next frame's history. Enabled by the "r__taa" console setting; with it
// off the pass returns immediately and the frame is exactly what it was before TAA existed.
// Defined in the engine (xr_ioc_cmd.cpp): the camera code needs it as well, to decide whether to jitter
// the projection.
extern ENGINE_API int ps_r__taa;
extern ENGINE_API int ps_r__taa_sharp;
extern ENGINE_API int ps_r__taa_debug;
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API int ps_r__xess;

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::phase_taa()
{
    // MSAA: the final combine reads the multisampled rt_Generic_0_r, not the resolved copy this pass
    // writes, so the result would simply be discarded. s_taa is not created in that case either.
    if (!ps_r__taa || !s_taa || RImplementation.o.msaa)
        return;

    // [DA_PORT] Not alongside an upscaler. Each of them is a temporal resolve in its own right, working
    // from the same jittered samples and the same motion vectors, and each expects the raw frame:
    // accumulating here first hands it an image that has already been blended with history, which shows
    // up as extra softness and doubled edges on movement. Only one temporal filter may own the frame.
    //
    // Every upscaler has to be named here, and the list is the whole point. It read "FSR 2" alone while
    // FSR 3 and XeSS were added around it, so selecting either of those left two temporal filters
    // running one after the other - which is exactly the softness on moving figures it exists to
    // prevent. Same omission, and same symptom, as the velocity buffer's own consumer list in r2.cpp.
    if (ps_r__fsr2 || ps_r__fsr3 || ps_r__xess)
        return;

    // Not while the menu is up. The menu is drawn inside the combine pass, i.e. BEFORE this one, so it
    // lands in the temporal history; with the world paused behind it the same translucent text is
    // blended into the accumulation frame after frame and smears into stripes. The paused scene has
    // nothing to gain from accumulation anyway.
    if (g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive())
        return;

    PIX_EVENT(DA_phase_taa);

    // Two outputs: rt_TAA_out takes the sharpened frame that goes back on screen, rt_TAA_scratch the
    // plain one that becomes the next history. Sharpening the accumulated copy would feed its own output
    // back in and turn the jitter into visible shimmer, which is why SSFX keeps sharpening a separate
    // phase. Neither can be rt_Color itself: that is the source (s_image), and D3D11 will not bind one
    // resource for reading and writing at once.
    u_setrt(RCache, rt_TAA_out, rt_TAA_scratch, nullptr, nullptr);

    RCache.set_Stencil(FALSE);
    RCache.set_Z(FALSE);
    RCache.set_CullMode(CULL_NONE);

    // combine_1.vs takes the texcoord in position.zw and flips y, so this is the same quad the combine
    // pass builds; the jitter coords it also reads are unused by da_taa.ps.
    u32 Offset = 0;
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_taa->E[0]);
    RCache.set_Geometry(g_combine);
    // y carries the debug switch: with it on the pass draws where it fetches history from instead of
    // the resolved frame. See da_taa.ps.
    RCache.set_c("taa_params", float(ps_r__taa_sharp) / 100.f, float(ps_r__taa_debug), 0.f, 0.f);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // Put the resolved image back where the rest of the frame expects it, and keep the un-sharpened
    // version as history. Two full-screen copies is cheaper than wiring up a ping-pong would be, and it
    // leaves post-processing pointing at the same rt_Color it already used.
#if defined(USE_DX11)
    ID3DBaseTexture* sharpened = rt_TAA_out->pTexture->surface_get();
    ID3DBaseTexture* plain = rt_TAA_scratch->pTexture->surface_get();
    ID3DBaseTexture* scene = rt_Color->pTexture->surface_get();
    ID3DBaseTexture* history = rt_TAA_history->pTexture->surface_get();
    if (sharpened && plain && scene && history)
    {
        auto* context = HW.get_context(CHW::IMM_CTX_ID);
        context->CopyResource(scene, sharpened);
        context->CopyResource(history, plain);
    }
    _RELEASE(sharpened);
    _RELEASE(plain);
    _RELEASE(scene);
    _RELEASE(history);
#endif
}
} // namespace xray::render::RENDER_NAMESPACE
