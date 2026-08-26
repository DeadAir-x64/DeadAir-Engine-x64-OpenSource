#include "stdafx.h"

extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]
extern ENGINE_API int ps_da_frame_diff;      // [DA_PORT] 0 - выкл, 1 - серым, 2 - цветом
extern ENGINE_API float ps_da_frame_diff_gain;

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] ПРИБОР: что на экране меняется от кадра к кадру. Команда r__frame_diff.
//
// Зачем. Жалоба "земля вдали мерцает" не говорит, ЧТО именно мерцает: земля, импосторы кустов,
// тени или сам резолв. Перебор ручек по одному подозреваемому за заход трижды дал пустой результат,
// причём один раз потому, что выставленное значение уже стояло. Прибор отвечает на этот вопрос
// прямо: то, что дрожит, светится, остальное чёрное.
//
// ⭐ Место в кадре выбрано не случайно - СРАЗУ ПОСЛЕ АПСКЕЙЛЕРА и ДО постобработки:
//   • до апскейлера смотреть бесполезно: при рендере ниже ста процентов каждый пиксель меняется
//     каждый кадр, в этом смысл субпиксельного дрожания, и разница была бы сплошным шумом;
//   • после постобработки тоже: зерно и цветокоррекция меняются сами по себе и зашумили бы замер.
// Ровно по этой причине здесь же стоит da_shift_watch - см. комментарий у его вызова.
//
// Порядок трёх шагов важен и переставлять его нельзя: сперва считаем разницу (прошлый кадр ещё цел),
// потом запоминаем текущий, и только потом показываем разницу вместо кадра.
void CRenderTarget::phase_frame_diff()
{
    if (!ps_da_frame_diff)
        return;

    if (!s_frame_diff || !rt_Diff_prev || !rt_Diff_out || !rt_FSR2_out)
        return;

    // Прибор смотрит выход апскейлера. Без апскейлера кадр лежит не там, и показывать было бы нечего;
    // говорим об этом вслух, иначе молчание неотличимо от "ничего не дрожит".
    if (!da_upscaler_active())
    {
        if ((Device.dwFrame % 120) == 0)
            Msg("~ [DA_PORT] r__frame_diff: апскейлер выключен, прибор смотрит именно его выход");
        return;
    }

    PIX_EVENT(DA_phase_frame_diff);

    // 1. разница текущего кадра с прошлым -> во временную цель
    u_setrt(RCache, rt_Diff_out, nullptr, nullptr, nullptr);
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

    RCache.set_Element(s_frame_diff->E[0]);
    RCache.set_Geometry(g_combine);
    RCache.set_c("da_diff_params", ps_da_frame_diff_gain, float(ps_da_frame_diff), 0.f, 0.f);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);

#if defined(USE_DX11)
    // 2. запоминаем текущий кадр как прошлый для следующего
    {
        ID3DBaseTexture* cur = rt_FSR2_out->pTexture->surface_get();
        ID3DBaseTexture* prev = rt_Diff_prev->pTexture->surface_get();
        if (cur && prev)
            HW.get_context(CHW::IMM_CTX_ID)->CopyResource(prev, cur);
        _RELEASE(cur);
        _RELEASE(prev);
    }

    // 3. показываем разницу вместо кадра
    {
        ID3DBaseTexture* diff = rt_Diff_out->pTexture->surface_get();
        ID3DBaseTexture* cur = rt_FSR2_out->pTexture->surface_get();
        if (diff && cur)
            HW.get_context(CHW::IMM_CTX_ID)->CopyResource(cur, diff);
        _RELEASE(diff);
        _RELEASE(cur);
    }
#endif
}
} // namespace xray::render::RENDER_NAMESPACE
