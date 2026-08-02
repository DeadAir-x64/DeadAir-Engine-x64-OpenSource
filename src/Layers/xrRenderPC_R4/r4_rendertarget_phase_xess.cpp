#include "stdafx.h"

#include "da_xess.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API int ps_r__xess;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]

extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]

extern ENGINE_API int ps_r__xess_mv_sign; // [DA_PORT] см. da_xess_mv.s

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Знаковая копия буфера скоростей для XeSS.
//
// Возвращает цель с исправленными векторами, либо nullptr — тогда XeSS получит буфер как есть
// (r__xess_mv_sign 0, прежнее поведение).
//
// Пишем в rt_Velocity_guard: он уже создан под тот же формат и размер, а к этому моменту свободен —
// сторож скоростей, если он вообще включён, отработал раньше и вернул результат в rt_Velocity
// (см. phase_velocity_guard). Своей цели заводить не нужно.
ref_rt CRenderTarget::da_xess_signed_velocity()
{
    if (ps_r__xess_mv_sign <= 0 || !s_xess_mv || !rt_Velocity_guard || !rt_Velocity)
        return ref_rt{};

    // 1 - обе оси, 2 - только X, 3 - только Y.
    const float sx = (ps_r__xess_mv_sign == 1 || ps_r__xess_mv_sign == 2) ? -1.f : 1.f;
    const float sy = (ps_r__xess_mv_sign == 1 || ps_r__xess_mv_sign == 3) ? -1.f : 1.f;

    PIX_EVENT(DA_phase_xess_mv);

    u_setrt(RCache, rt_Velocity_guard, nullptr, nullptr, nullptr);
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

    RCache.set_Element(s_xess_mv->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.set_c("da_xess_sign", sx, sy, 0.f, 0.f);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

    return rt_Velocity_guard;
}

// Shared with FSR 2 on purpose: only one upscaler reconstructs a given frame, and the post-process
// pass only needs to know whether SOMETHING produced one. Reusing the stamp means postprocess.ps and
// its da_upscale.z need no second branch.
extern u32 g_da_fsr2_frame;

bool CRenderTarget::phase_xess()
{
    if (!ps_r__xess || !g_da_xess.ready())
        return false;

    PIX_EVENT(DA_phase_xess);

    // [DA_PORT] Знаковая копия векторов — ДО освобождения целей ниже: проход сам ставит свою цель
    // и читает rt_Velocity, а тот в этот момент никуда не привязан на запись.
    const ref_rt signed_mv = da_xess_signed_velocity();

    // [DA_PORT] Release the outputs first — same trap that made FSR 2 reconstruct a black frame and
    // report success. Combine leaves rt_Color bound as the render target and rt_Base_Depth as the
    // depth-stencil, which are exactly the two resources read below, and D3D11 silently feeds a shader
    // zeros for anything simultaneously bound for writing.
    u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);

    ID3DBaseTexture* colour = rt_Color->pTexture->surface_get();
    ID3DBaseTexture* depth = rt_Base_Depth->pTexture->surface_get();
    ID3DBaseTexture* velocity = (signed_mv ? signed_mv : rt_Velocity)->pTexture->surface_get();
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
        // CCameraManager produces.
        //
        // ⚠️ Прежде здесь стояло «и знак перепутать негде, вектора идут как NDC». Это оказалось
        // НЕВЕРНО и стоило тестеру мазни при каждом движении камеры: под NDC-флагом XeSS берёт
        // буфер как есть, а в нём лежит вектор, перевёрнутый относительно NDC (что видно по
        // множителям FSR и DLSS). Параметра масштаба у XeSS нет вовсе — поэтому знак правится
        // отдельным проходом выше, см. da_xess_signed_velocity и r__xess_mv_sign.
        p.jitter_x = ::g_da_fsr2_jitter_px.x;
        p.jitter_y = ::g_da_fsr2_jitter_px.y;

        // [DA_PORT] Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
        // а прежнее `dwFrame < 3` покрывало только запуск сессии. См. xr_ioc_cmd.cpp.
        p.reset = ::da_upscaler_history_reset();

        ok = g_da_xess.draw(p);
        if (ok)
            g_da_fsr2_frame = Device.dwFrame;

        // [DA_PORT] Три провала подряд — гасим джиттер и объясняем в логе. Иначе на экран
        // идёт сдвинутая сцена, растянутая обычным фильтром, и это выглядит как тряска.
        ::da_upscaler_report_failure("XeSS", !ok);
    }

    _RELEASE(colour);
    _RELEASE(depth);
    _RELEASE(velocity);
    _RELEASE(output);
    _RELEASE(reactive);
    return ok;
}
} // namespace xray::render::RENDER_NAMESPACE
