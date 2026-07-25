#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::phase_occq()
{
    if (!RImplementation.o.msaa)
        // [DA_PORT] scene-space pass: viewport follows the scene targets, not the window
        u_setrt(RCache, Device.dwRenderWidth, Device.dwRenderHeight, get_base_rt(), 0, 0, rt_MSAADepth);
    else
        // [DA_PORT] scene-space pass: viewport follows the scene targets, not the window
        u_setrt(RCache, Device.dwRenderWidth, Device.dwRenderHeight, 0, 0, 0, rt_MSAADepth);
    RCache.set_Shader(s_occq);
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
    RCache.set_ColorWriteEnable(FALSE);
}
} // namespace xray::render::RENDER_NAMESPACE
