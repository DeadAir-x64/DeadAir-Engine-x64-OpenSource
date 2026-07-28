#include "stdafx.h"

#include "da_fsr2.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__upscale_sharpness;
extern ENGINE_API int ps_r__foliage_tandc;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Set only when the upscaler actually produced a frame. The post-process pass keys off
// this rather than off the console variable: switching r__fsr2 on mid-game leaves the context
// uncreated (it is built at renderer start, for a fixed pair of resolutions), and reading its empty
// output then gives a black screen instead of an ignored setting.
u32 g_da_fsr2_frame = 0;

// Reconstructs the frame at output resolution from the low-resolution render plus the history of
// previous frames. Called at the end of the frame, in place of the plain stretch that r__render_scale
// would otherwise do in the post-process pass.
bool CRenderTarget::phase_fsr2()
{
    if (!ps_r__fsr2 || !g_da_fsr2.ready())
        return false;

    PIX_EVENT(DA_phase_fsr2);

    // [DA_PORT] Release the outputs first. Combine leaves rt_Color bound as the render target and
    // rt_Base_Depth as the depth-stencil — precisely the two resources handed to the upscaler below.
    // D3D11 does not fault on a resource that is bound for writing and read at the same time: it
    // silently feeds the shader zeros. So FSR 2 reconstructed a frame from a black image and a blank
    // depth buffer, wrote a black result, and reported success. Same binding post-process establishes
    // for itself a moment later, so setting it here costs nothing.
    u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);

    ID3DBaseTexture* colour = rt_Color->pTexture->surface_get();
    // [DA_PORT] The real depth buffer, not rt_Position. That one holds eye-space POSITIONS: four
    // channels where the first is the X coordinate, so an upscaler reading channel zero as depth gets
    // nonsense. It reconstructs from garbage and the picture shakes — and no sign flip on the vectors
    // or the jitter changes anything, because neither is what it is actually going wrong with.
    ID3DBaseTexture* depth = rt_Base_Depth->pTexture->surface_get();
    ID3DBaseTexture* velocity = rt_Velocity->pTexture->surface_get();
    ID3DBaseTexture* output = rt_FSR2_out->pTexture->surface_get();
    ID3DBaseTexture* reactive = rt_Reactive ? rt_Reactive->pTexture->surface_get() : nullptr;

    bool ok = false;
    if (colour && depth && velocity && output)
    {
        da_fsr2::draw_params p;
        p.context = HW.get_context(CHW::IMM_CTX_ID);

        p.colour = colour;
        p.depth = depth;
        p.velocity = velocity;
        p.output = output;
        p.reactive = reactive;
        // [DA_PORT] Same buffer, second input - see da_fsr2.cpp.
        p.tandc = ps_r__foliage_tandc ? reactive : nullptr;

        p.render_width = Device.dwRenderWidth;
        p.render_height = Device.dwRenderHeight;

        // The very offset the scene was jittered by this frame, in pixels, exactly as generated.
        // FSR 2 undoes it itself, which is why the motion vectors must not contain it (see m_VP_nojit
        // in r2.cpp) and why the conversion to clip space happens in one place only, cl_taa_jitter -
        // doing it at both ends is how the two came to describe different shifts.
        p.jitter_x = ::g_da_fsr2_jitter_px.x;
        p.jitter_y = ::g_da_fsr2_jitter_px.y;

        // Milliseconds, and never zero: the library uses it to age the history, and a zero would make
        // it treat the frame as instantaneous.
        p.frame_time_ms = std::max(1.0f, float(Device.dwTimeDelta));

        p.near_plane = VIEWPORT_NEAR;
        p.far_plane = g_pGamePersistent->Environment().CurrentEnv.far_plane;
        // [DA_PORT] Device.fFOV is the HORIZONTAL angle — the projection is built from it together with
        // an inverted aspect (height/width) — while FSR 2 asks for the vertical one. At 16:9 the two
        // differ by about a third, and the library uses this angle to judge how far pixels may travel
        // between frames, so handing it the wrong one skews the whole reconstruction.
        const float aspect_hw = float(Device.dwRenderHeight) / float(Device.dwRenderWidth);
        p.fov_vertical = 2.f * atanf(tanf(deg2rad(Device.fFOV) * 0.5f) * aspect_hw);

        // History has to be discarded when the picture changes discontinuously — a level load or a
        // teleport — otherwise the upscaler blends two unrelated scenes for several frames.
        // [DA_PORT] Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
        // а прежнее `dwFrame < 3` покрывало только запуск сессии. См. xr_ioc_cmd.cpp.
        p.reset = ::da_upscaler_history_reset();

        // [DA_PORT] The library's own RCAS pass, on the same slider FSR 1.0 uses. Reconstruction from a
        // lower resolution is inherently softer, and foliage suffers most: once its motion vectors are
        // right the history is accepted rather than rejected, which removes the shimmer and adds blur in
        // its place. Sharpening here costs one pass inside the upscaler and works on the finished frame,
        // so it recovers that edge without re-introducing the shimmer.
        p.sharpening = ::ps_r__upscale_sharpness > 0;
        p.sharpness = float(::ps_r__upscale_sharpness) / 100.f;

        ok = g_da_fsr2.draw(p);
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
