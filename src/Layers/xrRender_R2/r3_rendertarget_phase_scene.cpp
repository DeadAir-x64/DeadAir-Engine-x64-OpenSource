#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
// startup
void CRenderTarget::phase_scene_prepare()
{
    PIX_EVENT(phase_scene_prepare);
    // Clear depth & stencil
    // u_setrt	( Device.dwWidth,Device.dwHeight, get_base_rt(), nullptr, nullptr, get_base_zb() );
    // CHK_DX	( HW.pDevice->Clear	( 0L, NULL, D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL, 0x0, 1.0f, 0L) );
    //	Igor: soft particles

    const auto& env = g_pGamePersistent->Environment().CurrentEnv;
    const float fValue = env.m_fSunShaftsIntensity;
    //	TODO: add multiplication by sun color here
    // if (fValue<0.0001) FlagSunShafts = 0;

    //	TODO: DX11: Check if complete clear of _ALL_ rendertargets will increase
    //	FPS. Make check for SLI configuration.
    if (RImplementation.o.advancedpp && (ps_r2_ls_flags.test(R2FLAG_SOFT_PARTICLES | R2FLAG_DOF) ||
                                            ((ps_r_sun_shafts > 0) && (fValue >= 0.0001)) || (ps_r_ssao > 0)))
    {
        //	TODO: DX11: Check if we need to set RT here.
        // [DA_PORT] This is the G-buffer pass — the viewport has to match the scene targets, not the
        // window. With r__render_scale < 100 the old window-sized viewport rasterised the geometry at
        // full size into a smaller buffer, so only its top-left corner was captured and the final blit
        // then blew that corner up over the whole screen.
        u_setrt(RCache, Device.dwRenderWidth, Device.dwRenderHeight, rt_Position->pRT, 0, 0, rt_MSAADepth);

        const Fcolor color{}; // black
        RCache.ClearRT(rt_Position, color);
        // RCache.ClearRT(rt_Normal, color);
        // RCache.ClearRT(rt_Color, color);
        if (!RImplementation.o.msaa)
            RCache.ClearZB(get_base_zb(), 1.0f, 0);
        else
        {
            RCache.ClearRT(rt_Color, color);
            RCache.ClearRT(rt_Accumulator, color);
            RCache.ClearZB(rt_MSAADepth, 1.0f, 0);
            RCache.ClearZB(get_base_zb(), 1.0f, 0);
        }
    }
    else
    {
        //	TODO: DX11: Check if we need to set RT here.
        // [DA_PORT] same reasoning: depth here is the scene depth, so the viewport follows the scene
        u_setrt(RCache, Device.dwRenderWidth, Device.dwRenderHeight, get_base_rt(), 0, 0, rt_MSAADepth);
        RCache.ClearZB(rt_MSAADepth, 1.0f, 0);
    }

    //	Igor: for volumetric lights
    m_bHasActiveVolumetric = false;
    //	Clear later if try to draw volumetric
}

// begin
void CRenderTarget::phase_scene_begin()
{
    // Targets, use accumulator for temporary storage
    // [DA_PORT] With motion vectors on, rt_Velocity is bound as the last colour target. Only the
    // compressed G-buffer layout is wired up: it is the one the port actually runs (R3FLAG_GBUFFER_OPT
    // is set by default), and binding four targets on the uncompressed path without a shader that
    // writes the fourth would leave it undefined rather than merely unused.
#if RENDER == R_R4
    const bool da_velocity = RImplementation.o.velocity && RImplementation.o.gbuffer_opt;
#else
    constexpr bool da_velocity = false;
#endif

    // [DA_PORT] Clear the position target too, once per frame, whatever else is switched on.
    //
    // The sky writes nothing into it, and stock X-Ray does not care: the deferred lighting skips those
    // pixels by depth and never reads what is there. Our temporal work does read it, and reads it as
    // the answer to "is this pixel sky" - da_taa.ps, da_sky_velocity.ps and da_reactive.ps all take a
    // zero eye-space z to mean nothing was drawn.
    //
    // Left uncleared it holds whatever the last frame that DID have geometry there wrote. So the moment
    // the camera turns and sky arrives where a wall used to be, the sky is reprojected as though it sat
    // twenty metres away instead of at infinity, its history is fetched from the wrong place, and the
    // horizon shimmers - visibly, and only while turning, which is exactly how it was reported.
    //
    // Same once-per-frame guard as the velocity clear below, and for the same reason: with the scene
    // split in two this function runs twice, and a second clear would wipe the first half's work.
// [DA_PORT] REVERTED - clearing it here made every model vanish: NPCs, the weapon in hand, the actor.
// The reasoning above still stands (nothing writes the sky's position, and our temporal work reads a
// zero there as "sky"), but this is the wrong place or the wrong way to do it, and it did not fix the
// shimmer either. Left as a note rather than deleted, so the next attempt starts from what is known:
// find out what else reads this target between here and the lighting before clearing it again.
#if 0
    if (rt_Position && rt_Position->pRT && da_position_cleared_frame != Device.dwFrame)
    {
        constexpr float zero[4] = { 0.f, 0.f, 0.f, 0.f };
        HW.get_context(RCache.context_id)->ClearRenderTargetView(rt_Position->pRT, zero);
        da_position_cleared_frame = Device.dwFrame;
    }
#endif

    if (!RImplementation.o.gbuffer_opt)
    {
        if (RImplementation.o.albedo_wo)
            u_setrt(RCache, rt_Position, rt_Normal, rt_Accumulator, rt_MSAADepth);
        else
            u_setrt(RCache, rt_Position, rt_Normal, rt_Color, rt_MSAADepth);
    }
    else if (da_velocity)
    {
        // [DA_PORT] Clear the velocity target ONCE PER FRAME. Pixels the G-buffer never covers — the sky
        // above all — are written by nothing, so without a clear they keep whatever previous frames left
        // there, and an upscaler trusts this buffer everywhere.
        //
        // The once-per-frame guard is essential, not tidiness: with the scene split in two (see
        // render_forward / the split path in r2_R_render.cpp) this function runs TWICE per frame, and
        // clearing on the second call wipes everything the first part drew. That left only what the
        // second part draws — the HUD weapon and the detail grass — with vectors, while the whole world
        // and every NPC came out empty. Cost me an evening.
#if defined(USE_DX11)
        if (rt_Velocity && rt_Velocity->pRT && da_velocity_cleared_frame != Device.dwFrame)
        {
            constexpr float zero[4] = { 0.f, 0.f, 0.f, 0.f };
            HW.get_context(RCache.context_id)->ClearRenderTargetView(rt_Velocity->pRT, zero);
            // The reactive mask rides the same guard: zero means "trust the history here", which is the
            // right answer for every pixel no shader marks, including the sky.
            if (rt_Reactive && rt_Reactive->pRT)
                HW.get_context(RCache.context_id)->ClearRenderTargetView(rt_Reactive->pRT, zero);
            da_velocity_cleared_frame = Device.dwFrame;
        }
#endif

        if (RImplementation.o.albedo_wo)
            u_setrt(RCache, rt_Position, rt_Accumulator, rt_Velocity, rt_Reactive, rt_MSAADepth);
        else
            u_setrt(RCache, rt_Position, rt_Color, rt_Velocity, rt_Reactive, rt_MSAADepth);
    }
    else
    {
        if (RImplementation.o.albedo_wo)
            u_setrt(RCache, rt_Position, rt_Accumulator, rt_MSAADepth);
        else
            u_setrt(RCache, rt_Position, rt_Color, rt_MSAADepth);
        // else								u_setrt		(rt_Position,	rt_Color, rt_Normal,		rt_MSAADepth);
    }

    // Stencil - write 0x1 at pixel pos
    RCache.set_Stencil(
        TRUE, D3DCMP_ALWAYS, 0x01, 0xff, 0x7f, D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);

    // Misc		- draw only front-faces
    //	TODO: DX11: siable two-sided stencil here
    // CHK_DX(HW.pDevice->SetRenderState	( D3DRS_TWOSIDEDSTENCILMODE,FALSE				));
    RCache.set_CullMode(CULL_CCW);
    RCache.set_ColorWriteEnable();
}

void CRenderTarget::disable_aniso()
{
    // Disable ANISO
    //	TODO: DX11: disable aniso here
    // for (u32 i=0; i<HW.Caps.raster.dwStages; i++)
    //	CHK_DX(HW.pDevice->SetSamplerState( i, D3DSAMP_MAXANISOTROPY, 1	));
}

// end
void CRenderTarget::phase_scene_end()
{
    disable_aniso();

    if (!RImplementation.o.albedo_wo)
        return;

    // transfer from "rt_Accumulator" into "rt_Color"
    u_setrt(RCache, rt_Color, nullptr, nullptr, rt_MSAADepth);
    RCache.set_CullMode(CULL_NONE);
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00); // stencil should be >= 1
    if (RImplementation.o.nvstencil)
        u_stencil_optimize(RCache, CRenderTarget::SO_Combine);
    RCache.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00); // stencil should be >= 1
    RCache.set_ColorWriteEnable();

    // common calc for quad-rendering
    u32 Offset;
    u32 C = color_rgba(255, 255, 255, 255);
    float _w = float(Device.dwWidth);
    float _h = float(Device.dwHeight);
    Fvector2 p0, p1;
    p0.set(.5f / _w, .5f / _h);
    p1.set((_w + .5f) / _w, (_h + .5f) / _h);
    float d_Z = EPS_S, d_W = 1.f;

    // Fill vertex buffer
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
    pv++;
    pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
    pv++;
    pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
    pv++;
    pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
    pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    // if (stencil>=1 && aref_pass)	stencil = light_id
    RCache.set_Element(s_accum_mask->E[SE_MASK_ALBEDO]); // masker
    RCache.set_Geometry(g_combine);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
} // namespace xray::render::RENDER_NAMESPACE
