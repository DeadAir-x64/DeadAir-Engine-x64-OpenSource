#include "stdafx.h"

#include "da_fsr3.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API int ps_r__upscale_sharpness;
extern ENGINE_API int ps_r__foliage_tandc;
extern ENGINE_API int ps_r__fsr3_debug;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]

extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Shared with FSR 2 and XeSS on purpose: only one upscaler reconstructs a given frame, and
// the post-process pass only needs to know whether SOMETHING produced one. Reusing the stamp means
// postprocess.ps and its da_upscale.z need no third branch.
extern u32 g_da_fsr2_frame;

// Same slot in the frame as phase_fsr2, and the same reasoning throughout - see that file, which
// documents why the depth must be rt_Base_Depth rather than rt_Position, why the render targets are
// released first, and why the vertical field of view has to be derived from the horizontal one.
bool CRenderTarget::phase_fsr3()
{
    if (!ps_r__fsr3 || !g_da_fsr3.ready())
        return false;

    // [DA_PORT] r__fsr3_debug 1: everything exists, nothing runs. See xr_ioc_cmd.cpp for what it splits.
    if (ps_r__fsr3_debug == 1)
        return false;

    PIX_EVENT(DA_phase_fsr3);

    // Combine leaves rt_Color bound as the render target and rt_Base_Depth as the depth-stencil, i.e.
    // exactly the two resources handed to the upscaler below. D3D11 does not fault on a resource bound
    // for writing and read at once - it silently feeds zeros - so this must be released first.
    u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);

    ID3DBaseTexture* colour = rt_Color->pTexture->surface_get();
    ID3DBaseTexture* depth = rt_Base_Depth->pTexture->surface_get();
    ID3DBaseTexture* velocity = rt_Velocity->pTexture->surface_get();
    ID3DBaseTexture* output = rt_FSR2_out->pTexture->surface_get();
    ID3DBaseTexture* reactive = rt_Reactive ? rt_Reactive->pTexture->surface_get() : nullptr;

    bool ok = false;
    if (colour && depth && velocity && output)
    {
        da_fsr3::draw_params p;
        p.context = HW.get_context(CHW::IMM_CTX_ID);

        p.colour = colour;
        p.depth = depth;
        p.velocity = velocity;
        p.output = output;
        p.reactive = reactive;
        p.tandc = ps_r__foliage_tandc ? reactive : nullptr;

        p.render_width = Device.dwRenderWidth;
        p.render_height = Device.dwRenderHeight;
        // FSR 3 wants the output size explicitly; FSR 2 took it from the context alone.
        p.display_width = Device.dwWidth;
        p.display_height = Device.dwHeight;

        p.jitter_x = ::g_da_fsr2_jitter_px.x;
        p.jitter_y = ::g_da_fsr2_jitter_px.y;

        p.frame_time_ms = std::max(1.0f, float(Device.dwTimeDelta));

        p.near_plane = VIEWPORT_NEAR;
        p.far_plane = g_pGamePersistent->Environment().CurrentEnv.far_plane;

        const float aspect_hw = float(Device.dwRenderHeight) / float(Device.dwRenderWidth);
        p.fov_vertical = 2.f * atanf(tanf(deg2rad(Device.fFOV) * 0.5f) * aspect_hw);

        // [DA_PORT] Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
        // а прежнее `dwFrame < 3` покрывало только запуск сессии. См. xr_ioc_cmd.cpp.
        p.reset = ::da_upscaler_history_reset();

        p.sharpening = ::ps_r__upscale_sharpness > 0;
        p.sharpness = float(::ps_r__upscale_sharpness) / 100.f;

        ok = g_da_fsr3.draw(p);
        if (ok)
            g_da_fsr2_frame = Device.dwFrame;

        // [DA_PORT] Три провала подряд — гасим джиттер и объясняем в логе. Иначе на экран
        // идёт сдвинутая сцена, растянутая обычным фильтром, и это выглядит как тряска.
        ::da_upscaler_report_failure("FSR 3.0", !ok);

        // [DA_PORT] Put the pipeline back the way it was found.
        //
        // FSR 3 dispatches a chain of compute passes and leaves its own bindings in place - compute
        // shader, its SRVs, UAVs, samplers and constant buffers all stay attached to the immediate
        // context. D3D11 state is global and survives to the next frame, and a UAV still bound where
        // the scene expects a render target silently unbinds that target. The result is not a broken
        // upscaled frame but a broken NEXT frame: models and the weapon in hand lose their textures,
        // which looks nothing like an upscaler fault and sends the search in the wrong direction.
        //
        // FSR 2 got away without this because its backend binds a far smaller set. Cheap insurance:
        // a handful of null bindings once a frame, then the engine's own cache is told to forget what
        // it thinks is bound so the next pass re-establishes everything.
        ID3D11DeviceContext* ctx = p.context;
        ID3D11UnorderedAccessView* null_uav[8] = {};
        ID3D11ShaderResourceView* null_srv[16] = {};
        ID3D11Buffer* null_cb[4] = {};
        ID3D11SamplerState* null_smp[4] = {};
        ctx->CSSetUnorderedAccessViews(0, 8, null_uav, nullptr);
        ctx->CSSetShaderResources(0, 16, null_srv);
        ctx->CSSetConstantBuffers(0, 4, null_cb);
        ctx->CSSetSamplers(0, 4, null_smp);
        ctx->CSSetShader(nullptr, nullptr, 0);
        RCache.Invalidate();
    }

    _RELEASE(colour);
    _RELEASE(depth);
    _RELEASE(velocity);
    _RELEASE(output);
    _RELEASE(reactive);
    return ok;
}
} // namespace xray::render::RENDER_NAMESPACE
