#include "stdafx.h"
#include "blender_taa.h"

namespace xray::render::RENDER_NAMESPACE
{
CBlender_TAA::CBlender_TAA() {}
CBlender_TAA::~CBlender_TAA() {}

void CBlender_TAA::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

    switch (C.iElement)
    {
    case 0:
        // Opaque full-screen resolve: the shader returns the finished blend of current frame and
        // history, so no alpha blending here. VS is the engine's built-in full-screen "combine_1".
        C.r_Pass("combine_1", "da_taa", FALSE, FALSE, FALSE, FALSE);
        C.r_CullMode(D3DCULL_NONE);

        // r_dx11Texture, not r_Sampler_*. Under DX11 a texture and a sampler are separate objects with
        // separate names, and r_Sampler_* looks the NAME up as a sampler: it only finds one for shaders
        // the engine had to recompile with D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY, where fxc emits a
        // DX9-style texture+sampler pair sharing one name. Every stock blender happens to be in that
        // bucket; da_taa.ps compiles clean, so asking for a sampler called "s_position" found the
        // texture instead and tripped the R_ASSERT(C->type == RC_sampler) in i_Sampler.
        // r_dx11Texture is the path that works either way — it falls back to r_Sampler itself when the
        // shader did come out DX9-compatible.
        C.r_dx11Texture("s_position", r2_RT_P);          // eye-space position: depth to reproject from
        C.r_dx11Texture("s_image", r2_RT_albedo);        // current frame — the assembled one, see phase_taa
        C.r_dx11Texture("s_history", r2_RT_taa_history); // previous resolved frame
        // combine_1.vs Loads the tonemap scale out of s_tonemap and passes it on in tc0.zw; da_taa.ps
        // ignores it, but the slot still has to be filled.
        C.r_dx11Texture("s_tonemap", r2_RT_luminance_cur);
        // [DA_PORT] Маска реактивности - «здесь истории верить нельзя». До 01.08 наша темпоралка её не
        // читала вовсе, хотя листва в неё пишется (r__reactive_foliage), и именно этим FSR 2 отличался
        // от TAA на дрожащей листве: у одного сигнал был, у другого нет. См. da_taa.ps.
        C.r_dx11Texture("s_reactive", r2_RT_reactive);

        C.r_dx11Sampler("smp_nofilter");
        C.r_dx11Sampler("smp_rtlinear");
        C.r_End();
        break;
    }
}
} // namespace xray::render::RENDER_NAMESPACE
