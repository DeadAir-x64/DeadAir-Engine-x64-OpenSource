#include "stdafx.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API float ps_r__vguard_gloss;
extern ENGINE_API float ps_r__vguard_strength;
extern ENGINE_API int ps_r__vguard_radius;
extern ENGINE_API float ps_r__vguard_still;

namespace xray::render::RENDER_NAMESPACE
{
// Filters the motion buffer before an upscaler reads it: vegetation motion is damped where a glossy,
// standing surface is close enough for FSR's velocity dilation to drag that motion onto it. The whole
// reasoning behind the idea lives in da_velocity_guard.ps - this is only the plumbing.
//
// One full-screen pass at render resolution over two buffers that are already in cache. Skipped at zero
// strength, and then the upscaler reads the unfiltered buffer and nothing about the frame changes.
void CRenderTarget::phase_velocity_guard()
{
    if (ps_r__vguard_strength <= 0.f || !s_velocity_guard || !rt_Velocity_guard || !rt_Velocity)
        return;

    // [DA_PORT] One decision for the whole frame: is the camera moving?
    //
    // The per-pixel version of this test - "is my glossy neighbour standing still" - had to go. Screen
    // velocity depends on depth, so under camera motion neighbouring pixels legitimately differ, and any
    // threshold across that difference puts some pixels on one side and some on the other. Worse, the
    // camera never moves at a constant rate, so pixels changed sides between frames and the picture went
    // jittery - a fault created entirely by the fix.
    //
    // The artefact this pass exists for only appears with the world still and the vegetation moving, so
    // there is nothing to lose by standing down whenever the camera travels. Decided once, on the CPU,
    // and applied to every pixel identically: no threshold anywhere in the image, nothing to flicker.
    static Fvector s_prev_pos{};
    static Fvector s_prev_dir{};
    static float s_settle = 0.f;

    const float moved = Device.vCameraPosition.distance_to(s_prev_pos);
    const float turned = 1.f - Device.vCameraDirection.dotproduct(s_prev_dir);
    s_prev_pos = Device.vCameraPosition;
    s_prev_dir = Device.vCameraDirection;

    // [DA_PORT] Recalibrated. These thresholds were driven down and down against jitter that this pass
    // was supposedly causing - while it was in fact drawing nothing at all, because its vertex layout
    // did not match the geometry it was drawn with and D3D11 discards such a draw in silence. Every
    // tightening was therefore measured against no effect whatsoever, and the end of that road was a
    // bar so low that standing still barely cleared it: the guard almost never ran even once the draw
    // was fixed. Set now to what "the player is standing still" actually looks like.
    const bool still = (moved < 0.002f) && (turned < 0.000002f);

    // Eased in and out over a few frames instead of switched: an abrupt change of the motion buffer is
    // itself visible as a lurch, and the whole point here is not to trade one artefact for another.
    // And still for a WHILE, not merely this frame - a single quiet frame happens in the middle of any
    // movement, between two mouse reports or at the top of a step.
    static u32 s_still_frames = 0;
    s_still_frames = still ? (s_still_frames + 1) : 0;

    const float target = (s_still_frames > 3) ? 1.f : 0.f;
    // Out quickly, in gently: dropping the guard the instant the camera moves is free, while
    // bringing it back abruptly would be seen.
    s_settle += (target - s_settle) * (target > s_settle ? 0.15f : 0.6f);
    if (s_settle < 0.01f)
        return;

    PIX_EVENT(DA_phase_velocity_guard);

    u_setrt(RCache, rt_Velocity_guard, nullptr, nullptr, nullptr);

    RCache.set_Stencil(FALSE);
    RCache.set_Z(FALSE);
    RCache.set_CullMode(CULL_NONE);

    // The same quad the combine and TAA passes build - stub_notransform_1uv takes the texcoord out of
    // position.zw and flips y for us.
    u32 Offset = 0;
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_velocity_guard->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.set_c("da_vguard", ps_r__vguard_gloss, ps_r__vguard_strength * s_settle,
        float(ps_r__vguard_radius), ps_r__vguard_still);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    // The filtered buffer replaces the original, so every reader downstream - both upscalers and the
    // debug view - keeps pointing at rt_Velocity and needs no knowledge of this pass at all.
    ID3DBaseTexture* src = rt_Velocity_guard->pTexture->surface_get();
    ID3DBaseTexture* dst = rt_Velocity->pTexture->surface_get();
    if (src && dst)
        HW.get_context(CHW::IMM_CTX_ID)->CopyResource(dst, src);
    _RELEASE(src);
    _RELEASE(dst);
}
} // namespace xray::render::RENDER_NAMESPACE
