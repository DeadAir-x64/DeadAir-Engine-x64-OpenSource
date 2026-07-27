#include "stdafx.h"

extern ENGINE_API int ps_r__sky_velocity;
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API int ps_r__xess;

namespace xray::render::RENDER_NAMESPACE
{
// Fills in the motion vectors for the sky, which no shader writes - see da_sky_velocity.ps for why
// that is and why the answer needs no geometry.
//
// Draws straight into the velocity buffer and discards every pixel that is not sky, so it needs no
// second target and no copy back: what the G-buffer wrote stays exactly as it was.
void CRenderTarget::phase_sky_velocity()
{
    if (!ps_r__sky_velocity || !s_sky_velocity || !rt_Velocity)
        return;

    // Only an upscaler reads these vectors for the sky; without one the pass is pure cost.
    if (!ps_r__fsr2 && !ps_r__fsr3 && !ps_r__xess)
        return;

    PIX_EVENT(DA_phase_sky_velocity);

    u_setrt(RCache, rt_Velocity, nullptr, nullptr, nullptr);

    RCache.set_Stencil(FALSE);
    RCache.set_Z(FALSE);
    RCache.set_CullMode(CULL_NONE);

    u32 Offset = 0;
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_sky_velocity->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
} // namespace xray::render::RENDER_NAMESPACE
