#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::phase_smap_spot_clear(CBackend& cmd_list)
{
    rt_smap_depth->set_slice_write(cmd_list.context_id, 0);
    cmd_list.set_pass_targets(
        rt_smap_surf,
        nullptr,
        nullptr,
        rt_smap_depth
    );
    cmd_list.ClearZB(rt_smap_depth, 1.0f);

#if defined(USE_DX11)
    HW.get_context(CHW::IMM_CTX_ID)->ClearState();
#endif
}

// [DA_PORT] Очистка ОДНОЙ ячейки теневого атласа.
//
// Зачем: phase_smap_spot_clear стирает всю текстуру перед каждой пачкой ламп, и поэтому карта,
// нарисованная в прошлом кадре, до этого кадра не доживает. Пока это так, кэш теней ламп
// невозможен в принципе — не «дорого перерисовывать», а «нечего переиспользовать».
//
// Как: область просмотра ставится в прямоугольник лампы, поверх рисуется четырёхугольник с
// глубиной 1.0 и функцией сравнения «всегда». Прямоугольник задаётся в полный размер экрана
// НАРОЧНО: преобразование области просмотра само сожмёт его до ячейки лампы, и одна и та же
// геометрия годится для ячейки любого размера.
//
// ⚠️ Проверять глазами обязательно. Ошибка здесь молчаливая: не зальётся — в тени останется
// мусор от прошлого кадра, зальётся мимо — пропадут чужие тени. Ни то, ни другое не сообщит о
// себе ни строкой. Поэтому по умолчанию путь выключен (r__smap_clear_rect).
void CRenderTarget::phase_smap_spot_clear_rect(CBackend& cmd_list, light* L, bool to_static)
{
    ref_rt& target = to_static ? rt_da_smap_static : rt_smap_depth;
    target->set_slice_write(cmd_list.context_id, 0);
    cmd_list.set_pass_targets(rt_smap_surf, nullptr, nullptr, target);

    // В атласе статики у лампы СВОЁ, постоянное место — рабочую раскладку сюда переносить нельзя.
    const D3D_VIEWPORT viewport = to_static
        ? D3D_VIEWPORT{ float(L->da_cache.st_posX), float(L->da_cache.st_posY),
              float(L->da_cache.st_size), float(L->da_cache.st_size), 0.f, 1.f }
        : D3D_VIEWPORT{ float(L->X.S.posX), float(L->X.S.posY), float(L->X.S.size), float(L->X.S.size), 0.f, 1.f };
    cmd_list.SetViewport(viewport);

    u32 Offset;
    const u32 C = color_rgba(255, 255, 255, 255);
    const float far_z = 1.f; // то же значение, что кладёт ClearZB для всего атласа

    // [DA_PORT] Четырёхугольник ЗАВЕДОМО больше порта просмотра, и это принципиально.
    //
    // Вершины здесь в экранных пикселях, а в отсечённые координаты их переводит шейдер
    // stub_notransform_t по глобальной константе screen_res:
    //     HPos.x = (P.x + 0.5) * screen_res.z * 2 - 1
    // Значение screen_res на теневом проходе задаётся отдельным связывателем, и полагаться на то,
    // что там окажется именно размер кадра, нельзя: стоит ему разойтись с нашим — и заливка
    // покрывает не весь порт, а часть. В ячейке остаётся хвост прошлого кадра.
    //
    // Ошибка при этом МОЛЧАЛИВАЯ и зависит от того, что рисовали до этого, то есть от направления
    // взгляда игрока. Именно так она себя и вела: «ходишь туда-сюда — свет по арке меняется».
    //
    // Берём с большим запасом: при любом разумном screen_res (от 512 до 8К) это гарантированно
    // накрывает отсечённые координаты [-1..1] целиком. Лишнее отрежет растеризатор по порту
    // просмотра — это бесплатно и надёжнее любой договорённости о размерах.
    const float big = 16384.f;

    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    pv->set(-big,  big, far_z, 1.f, C, 0, 0); pv++;
    pv->set(-big, -big, far_z, 1.f, C, 0, 0); pv++;
    pv->set( big,  big, far_z, 1.f, C, 0, 0); pv++;
    pv->set( big, -big, far_z, 1.f, C, 0, 0); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    cmd_list.set_Element(s_occq->E[3]);
    cmd_list.set_Geometry(g_combine);
    cmd_list.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

// [DA_PORT] Восстановить статику ламп: атлас статики -> рабочий атлас, ОДИН вызов на кадр.
//
// ПОЧЕМУ ЦЕЛИКОМ, А НЕ ПО ЯЧЕЙКАМ. Копировать прямоугольник глубины в DX11 запрещено: по
// документации Microsoft, при копировании ресурсов с D3D11_BIND_DEPTH_STENCIL субресурс должен
// копироваться целиком — DstX/Y/Z обязаны быть нулями, а pSrcBox обязан быть NULL. Отказ здесь был
// бы МОЛЧАЛИВЫМ: CHK_DX в релизе пуст, и в тени оказался бы мусор без единой строки в логе.
//
// Но по кусочкам и не нужно: весь атлас — это одна текстура со всеми ячейками, поэтому одно
// копирование восстанавливает статику сразу всех кэшированных ламп. За кадр — ровно одно.
//
// ⚠️ CopyResource НЕ годится: у рабочего атласа может быть несколько срезов под каскады солнца,
// а у нашего один, и требование «одинаковое описание ресурса» не выполняется. Копируем СУБРЕСУРС
// среза 0 — прожекторы пишут именно в него, каскады солнца не затрагиваются.
//
// Копия делается на непосредственном контексте: так же поступает phase_smap_spot_clear, и это
// снимает вопрос о порядке между отложенными списками команд.
bool CRenderTarget::da_smap_restore_static()
{
#if defined(USE_DX11)
    if (!da_smap_static_ok() || !rt_smap_depth->valid())
        return false;

    // Размеры и формат обязаны совпадать: иначе копия субресурса недопустима. Проверяем сами —
    // в релизе об этом никто не сообщит.
    if (rt_da_smap_static->dwWidth != rt_smap_depth->dwWidth ||
        rt_da_smap_static->dwHeight != rt_smap_depth->dwHeight ||
        rt_da_smap_static->fmt != rt_smap_depth->fmt)
    {
        static bool reported = false;
        if (!reported)
        {
            reported = true;
            Msg("! [DA] атлас статики не совпадает с рабочим (%ux%u fmt %d против %ux%u fmt %d) — кэш ламп выключен",
                rt_da_smap_static->dwWidth, rt_da_smap_static->dwHeight, int(rt_da_smap_static->fmt),
                rt_smap_depth->dwWidth, rt_smap_depth->dwHeight, int(rt_smap_depth->fmt));
        }
        return false;
    }

    HW.get_context(CHW::IMM_CTX_ID)
        ->CopySubresourceRegion(rt_smap_depth->pSurface, 0, 0, 0, 0, rt_da_smap_static->pSurface, 0, nullptr);
    return true;
#else
    return false;
#endif
}

// [DA_PORT] Перенести статику лампы из атласа статики в её ячейку РАБОЧЕГО атласа.
//
// Раскладки двух атласов не совпадают и совпадать не могут: в атласе статики место за лампой
// закреплено, рабочий перепаковывается каждый кадр и делится на пачки. Поэтому перенос — полампово,
// как в HDRP («блит из кэшированного атласа в динамический»), а не копией всего атласа.
//
// ⚠️ Ошибка ориентации здесь была бы молчаливой (зеркальная тень), поэтому шейдер адресует источник
// ЦЕЛЫМИ текселями: адрес приёмника плюс постоянный сдвиг между местами лампы в двух атласах.
// Ни координат текстуры, ни фильтрации — угадывать нечего. См. da_smap_blit.ps.
bool CRenderTarget::da_smap_blit_static(CBackend& cmd_list, light* L)
{
    if (!da_smap_static_ok() || !s_da_smap_blit || !L->da_cache.st_owned)
        return false;

    rt_smap_depth->set_slice_write(cmd_list.context_id, 0);
    cmd_list.set_pass_targets(rt_smap_surf, nullptr, nullptr, rt_smap_depth);

    const D3D_VIEWPORT viewport = { L->X.S.posX, L->X.S.posY, L->X.S.size, L->X.S.size, 0.f, 1.f };
    cmd_list.SetViewport(viewport);

    u32 Offset;
    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
    // xy — уже отсечённые координаты, zw — координата текстуры: этого ждёт da_fullscreen.vs.
    // Координата текстуры здесь не используется (адресуем по текселям), но вершина её несёт.
    pv->set(-1.f,  1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
    pv->set( 1.f,  1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
    pv->set( 1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
    RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

    cmd_list.set_Element(s_da_smap_blit->E[0]);
    cmd_list.set_Geometry(g_combine);
    // Константу ставим ПОСЛЕ элемента: до него набор констант прохода ещё не привязан.
    cmd_list.set_c("da_smap_blit_ofs", float(L->da_cache.st_posX) - float(L->X.S.posX),
        float(L->da_cache.st_posY) - float(L->X.S.posY), 0.f, 0.f);
    cmd_list.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    return true;
}

// [DA_PORT] Залить атлас статики «далёкой» глубиной. Один раз после создания целей отрисовки.
//
// Без этого копия атласа статики в рабочий притащила бы мусор в каждую ячейку, где нет
// кэшированной лампы. Зато с ним копия делает сразу две вещи — очистку и восстановление статики, —
// и полная очистка рабочего атласа перед пачкой ламп становится не нужна.
void CRenderTarget::da_smap_static_clear(CBackend& cmd_list)
{
    if (!da_smap_static_ok())
        return;

    rt_da_smap_static->set_slice_write(cmd_list.context_id, 0);
    cmd_list.set_pass_targets(rt_smap_surf, nullptr, nullptr, rt_da_smap_static);
    cmd_list.ClearZB(rt_da_smap_static, 1.0f);

    // 🪤 Счётчик поколений — на весь процесс, а не на объект. Набор целей отрисовки пересоздаётся
    // при vid_restart, и поле в свежем объекте начиналось бы с той же единицы. Тогда кэш лампы,
    // переживший пересоздание, совпал бы по поколению с ЧУЖИМ атласом и показал бы его содержимое.
    static u32 s_gen = 0;
    da_static_gen = ++s_gen;
    da_static_ready = true;
    Msg("* [DA] атлас статики залит, поколение %u", da_static_gen);
}

void CRenderTarget::phase_smap_spot(CBackend& cmd_list, light* L, bool to_static)
{
    ref_rt& target = to_static ? rt_da_smap_static : rt_smap_depth;
    target->set_slice_write(cmd_list.context_id, 0); // TODO: it is possible to increase lights batch size
                                                     // by rendering into different smap array slices in parallel
    cmd_list.set_pass_targets(
        rt_smap_surf,
        nullptr,
        nullptr,
        target
    );
    const D3D_VIEWPORT viewport = to_static
        ? D3D_VIEWPORT{ float(L->da_cache.st_posX), float(L->da_cache.st_posY),
              float(L->da_cache.st_size), float(L->da_cache.st_size), 0.f, 1.f }
        : D3D_VIEWPORT{ float(L->X.S.posX), float(L->X.S.posY), float(L->X.S.size), float(L->X.S.size), 0.f, 1.f };
    cmd_list.SetViewport(viewport);

    // Misc		- draw only front-faces //back-faces
    cmd_list.set_CullMode(CULL_CCW);
    cmd_list.set_Stencil(FALSE);
    // no transparency
#pragma todo("can optimize for multi-lights covering more than say 50%...")
    if (RImplementation.o.HW_smap)
        cmd_list.set_ColorWriteEnable(FALSE);
}

void CRenderTarget::phase_smap_spot_tsh(CBackend& cmd_list, light* L)
{
    VERIFY(!"Implement clear of the buffer for tsh!");
    VERIFY(RImplementation.o.Tshadows);
    cmd_list.set_ColorWriteEnable();
    if (IRender_Light::OMNIPART == L->flags.type)
    {
        // omni-part
        cmd_list.ClearRT(cmd_list.get_RT(), { 1.0f, 1.0f, 1.0f, 1.0f });
    }
    else
    {
        // real-spot
        // Select color-mask
        ref_shader shader = L->s_spot;
        if (!shader)
            shader = s_accum_spot;
        cmd_list.set_Element(shader->E[SE_L_FILL]);

        // Fill vertex buffer
        Fvector2 p0, p1;
        u32 Offset;
        u32 C = color_rgba(255, 255, 255, 255);
        float _w = float(L->X.S.size);
        float _h = float(L->X.S.size);
        float d_Z = EPS_S;
        float d_W = 1.f;
        p0.set(.5f / _w, .5f / _h);
        p1.set((_w + .5f) / _w, (_h + .5f) / _h);

        FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
#if defined(USE_DX11)
        pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
        pv++;
        pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
        pv++;
        pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
        pv++;
        pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
        pv++;
#elif defined(USE_OGL)
        pv->set(EPS, EPS, d_Z, d_W, C, p0.x, p0.y);
        pv++;
        pv->set(EPS, float(_h + EPS), d_Z, d_W, C, p0.x, p1.y);
        pv++;
        pv->set(float(_w + EPS), EPS, d_Z, d_W, C, p1.x, p0.y);
        pv++;
        pv->set(float(_w + EPS), float(_h + EPS), d_Z, d_W, C, p1.x, p1.y);
        pv++;
#else
#   error No graphics API selected or enabled!
#endif
        RImplementation.Vertex.Unlock(4, g_combine->vb_stride);
        cmd_list.set_Geometry(g_combine);

        // draw
        cmd_list.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
    }
}
} // namespace xray::render::RENDER_NAMESPACE
