#include "stdafx.h"

// [DA_PORT] Ручки живут в движке (xr_ioc_cmd.cpp), объявлять их внутри namespace нельзя — имя
// сманглится в render_r4:: и линковка упадёт.
extern ENGINE_API int ps_r__puddles;
extern ENGINE_API int ps_r__puddles_refl;
extern ENGINE_API float ps_r__puddles_refl_power;
extern ENGINE_API float ps_r__puddles_facing;
extern ENGINE_API float ps_r__puddles_sky;

namespace xray::render::RENDER_NAMESPACE
{
// Отражения в лужах: один полноэкранный проход поверх освещённого кадра.
//
// Место в кадре выбрано, а не подвернулось. Проход стоит СРАЗУ ПОСЛЕ копии кадра для водяного SSR и
// ДО прямого прохода, в котором рисуется вода:
//   * после копии — потому что отражать надо освещённую картинку, а копия и есть она;
//   * до воды — потому что иначе лужи отражали бы воду, а вода потом рисовалась бы поверх, и на
//     берегу озера получилось бы отражение отражения.
//
// Читаем копию (rt_SSR), пишем в сцену (rt_Generic_0_r). Читать и писать одну цель нельзя — DirectX
// в лучшем случае вернёт мусор, и молча; здесь источник и приёмник разные по построению.
void CRenderTarget::phase_da_puddle_refl()
{
    if (!ps_r__puddles || !ps_r__puddles_refl || ps_r__puddles_refl_power <= 0.f)
        return;
    if (!s_puddle_refl || !rt_SSR)
        return;

    PIX_EVENT(DA_phase_puddle_reflections);

    u_setrt(RCache, rt_Generic_0_r, nullptr, nullptr, rt_MSAADepth);
    RCache.set_Stencil(FALSE);
    RCache.set_Z(FALSE);
    RCache.set_CullMode(CULL_NONE);

    // FVF::TL и та же геометрия, что у остальных наших полноэкранных проходов: несовпадение
    // раскладки вершин с вершинным шейдером DirectX отбрасывает БЕЗ единого сообщения.
    u32 Offset = 0;
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    RCache.set_Element(s_puddle_refl->E[0]);
    RCache.set_Geometry(g_combine);
    // [DA_PORT] z — нижняя граница френеля (r__puddles_facing), см. xr_ioc_cmd.cpp
    RCache.set_c("da_puddle_refl", ps_r__puddles_refl_power, 1.f, ps_r__puddles_facing, ps_r__puddles_sky);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}
} // namespace xray::render::RENDER_NAMESPACE
