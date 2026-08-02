#include "stdafx.h"

// [DA_PORT] GPU timing per phase - see da_gpu_timer.h. R4 only; the GL branch has no D3D11 queries.
#if RENDER == R_R4
#   include "Layers/xrRenderPC_R4/da_gpu_timer.h"
#   define DA_GPU_ZONE_BEGIN(z) g_da_gpu_timer.zone_begin(da_gpu_timer::z)
#   define DA_GPU_ZONE_END(z)   g_da_gpu_timer.zone_end(da_gpu_timer::z)
#   define DA_GPU_FRAME_BEGIN()  g_da_gpu_timer.frame_begin()
#   define DA_GPU_FRAME_END()    g_da_gpu_timer.frame_end()
#else
#   define DA_GPU_ZONE_BEGIN(z)
#   define DA_GPU_ZONE_END(z)
#   define DA_GPU_FRAME_BEGIN()
#   define DA_GPU_FRAME_END()
#endif

#include "xrCore/Threading/TaskManager.hpp"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/CustomHUD.h"
#include "xrEngine/xr_object.h"

#include "Layers/xrRender/FBasicVisual.h"

// [DA_PORT] Defined in the engine (device.cpp). Declared out here, not inside the function that uses it:
// an extern inside namespace xray::render::render_r4 would be looking for a symbol in that namespace.
extern ENGINE_API Fmatrix g_da_taa_unjittered_VP;

// [DA_PORT] Разовый дамп очередей, рисуемых после G-буфера, см. r__emissive_probe.
extern ENGINE_API int ps_r__emissive_probe;

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Лампы, у которых бюджет теней снял флаг bShadow на этот кадр. Флаг - постоянное свойство
// источника (его ставит уровень), поэтому он возвращается сразу после прохода света. Одалживаем на
// время кадра, а не отбираем насовсем.
static xr_vector<light*> s_demoted;

static void da_restore_demoted_lights()
{
    for (light* L : s_demoted)
        L->flags.bShadow = true;
    s_demoted.clear();
}


void CRender::RenderMenu()
{
#if defined(USE_DX11)
    TracyD3D11Zone(HW.profiler_ctx, "render_menu");
#endif
    PIX_EVENT(render_menu);
    //	Globals
    RCache.set_CullMode(CULL_CCW);
    RCache.set_Stencil(FALSE);
    RCache.set_ColorWriteEnable();

    // [DA_PORT] The composite path below draws the menu into rt_Generic_0 — but the UI lays itself out in
    // screen coordinates, and with r__render_scale < 100 that target is smaller than the screen. Only the
    // top-left corner of the menu would land in it, then get stretched back over the whole screen. The
    // render scale is meant for the 3D scene, not the menu, so when it is active the menu goes straight to
    // the back buffer at native size. The only thing lost is the menu distortion overlay.
    if (Device.dwRenderWidth != Device.dwWidth || Device.dwRenderHeight != Device.dwHeight)
    {
        Target->u_setrt(RCache, Device.dwWidth, Device.dwHeight, Target->get_base_rt(), 0, 0, nullptr);
        RCache.ClearRT(Target->get_base_rt(), {});
        g_pGamePersistent->OnRenderPPUI_main();
        return;
    }

    // Main Render
    {
        Target->u_setrt(RCache, Target->rt_Generic_0, nullptr, nullptr, Target->rt_Base_Depth); // LDR RT
        g_pGamePersistent->OnRenderPPUI_main(); // PP-UI
    }
    // Distort
    {
        Target->u_setrt(RCache, Target->rt_Generic_1, nullptr, nullptr, Target->rt_Base_Depth); // Now RT is a distortion mask
        RCache.ClearRT(Target->rt_Generic_1, color_rgba(127, 127, 0, 127));
        g_pGamePersistent->OnRenderPPUI_PP(); // PP-UI
    }

    // Actual Display
    Target->u_setrt(RCache, Device.dwWidth, Device.dwHeight, Target->get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
    RCache.set_Shader(Target->s_menu);
    RCache.set_Geometry(Target->g_menu);

    Fvector2 p0, p1;
    u32 Offset;
    u32 C = color_rgba(255, 255, 255, 255);
    float _w = float(Device.dwWidth);
    float _h = float(Device.dwHeight);
    float d_Z = EPS_S;
    float d_W = 1.f;
    p0.set(.5f / _w, .5f / _h);
    p1.set((_w + .5f) / _w, (_h + .5f) / _h);

    FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, Target->g_menu->vb_stride, Offset);
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
    RImplementation.Vertex.Unlock(4, Target->g_menu->vb_stride);
    RCache.Render(D3DPT_TRIANGLELIST, Offset, 0, 4, 0, 2);
}

extern u32 g_r;
void CRender::Render()
{
    ZoneScoped;
#if defined(USE_DX11)
    TracyD3D11Zone(HW.profiler_ctx, "Render");
#endif
    PIX_EVENT(CRender_Render);

    g_r = 1;

    rmNormal(RCache);

    IMainMenu* pMainMenu = g_pGamePersistent ? g_pGamePersistent->m_pMainMenu : 0;
    bool bMenu = pMainMenu ? pMainMenu->CanSkipSceneRendering() : false;

    // XXX: do we need to handle case when there is level, but HUD isn't loaded yet?
    // if (!(g_pGameLevel && g_hud) || bMenu)
    if (!g_pGameLevel || bMenu)
    {
        Target->u_setrt(RCache, Device.dwWidth, Device.dwHeight, Target->get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
        return;
    }

    if (m_bFirstFrameAfterReset)
    {
        m_bFirstFrameAfterReset = false;
        return;
    }

    //.	VERIFY					(g_pGameLevel && g_pGameLevel->pHUD);
    auto& dsgraph = get_imm_context();
    DA_GPU_FRAME_BEGIN();

    //******* Z-prefill calc - DEFERRER RENDERER
    if (ps_r2_ls_flags.test(R2FLAG_ZFILL))
    {
        PIX_EVENT(DEFER_Z_FILL);
        BasicStats.Culling.Begin();
        float z_distance = ps_r2_zfill;
        Fmatrix m_zfill, m_project;
        m_project.build_projection(deg2rad(Device.fFOV /* *Device.fASPECT*/), Device.fASPECT, VIEWPORT_NEAR,
            z_distance * g_pGamePersistent->Environment().CurrentEnv.far_plane);
        m_zfill.mul(m_project, Device.mView);

        if (last_sector_id != IRender_Sector::INVALID_SECTOR_ID)
        {
            dsgraph.o.phase = PHASE_SMAP;
            dsgraph.r_pmask(true, false); // enable priority "0"
            dsgraph.set_Recorder(nullptr);
            dsgraph.o.use_hom = true;
            dsgraph.o.is_main_pass = true;
            dsgraph.o.sector_id = last_sector_id;
            dsgraph.o.portal_traverse_flags = CPortalTraverser::VQ_HOM | CPortalTraverser::VQ_SSA | CPortalTraverser::VQ_FADE;
            dsgraph.o.spatial_traverse_flags = ISpatial_DB::O_ORDERED;
            dsgraph.o.spatial_types = STYPE_RENDERABLE | STYPE_LIGHTSOURCE;
            dsgraph.o.view_pos = Device.vCameraPosition;
            dsgraph.o.xform = m_zfill;
            dsgraph.o.view_frustum = ViewBase;
            dsgraph.o.query_box_side = VIEWPORT_NEAR + EPS_L;
            dsgraph.o.precise_portals = true;

            dsgraph.build_subspace();
        }
        BasicStats.Culling.End();
    }

    //*******
    // Sync point
    BasicStats.WaitS.Begin();
    {
        q_sync_point.Wait(ps_r2_wait_sleep, ps_r2_wait_timeout);
    }
    BasicStats.WaitS.End();
    q_sync_point.End();

    r_main.sync();

    if (ps_r2_ls_flags.test(R2FLAG_ZFILL))
    {
        // flush
        Target->phase_scene_prepare();
        dsgraph.cmd_list.set_ColorWriteEnable(FALSE);
        dsgraph.render_graph(0);
        dsgraph.cmd_list.set_ColorWriteEnable();
    }
    else
    {
        Target->phase_scene_prepare();
    }

    BOOL split_the_scene_to_minimize_wait = FALSE;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_SPLIT_SCENE))
        split_the_scene_to_minimize_wait = TRUE;

    //******* Main render :: PART-0	-- first
#ifdef USE_OGL
    if (psDeviceFlags.test(rsWireframe))
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif
    if (!split_the_scene_to_minimize_wait)
    {
        PIX_EVENT(DEFER_PART0_NO_SPLIT);
        DA_GPU_ZONE_BEGIN(z_gbuffer);
        // level, DO NOT SPLIT
        Target->phase_scene_begin();
        dsgraph.render_hud();
        dsgraph.render_graph(0);
        dsgraph.render_lods(true, true);
        if (Details)
            Details->Render(dsgraph.cmd_list);
        Target->phase_scene_end();
        DA_GPU_ZONE_END(z_gbuffer);
    }
    else
    {
        PIX_EVENT(DEFER_PART0_SPLIT);
        // level, SPLIT
        Target->phase_scene_begin();
        dsgraph.render_graph(0);
        Target->disable_aniso();
    }
#ifdef USE_OGL
    if (psDeviceFlags.test(rsWireframe))
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    //******* Occlusion testing of volume-limited light-sources
    Target->phase_occq();
    LP_normal.clear();
    LP_pending.clear();
    if (o.msaa)
    {
#if defined(USE_DX11)
        dsgraph.cmd_list.set_ZB(Target->rt_MSAADepth->pZRT[dsgraph.cmd_list.context_id]);
#elif defined(USE_OGL)
        dsgraph.cmd_list.set_ZB(Target->rt_MSAADepth->pZRT);
#endif
    }
    {
        PIX_EVENT(DEFER_TEST_LIGHT_VIS);
        light_Package& LP = Lights.package;

        // [DA_PORT] Keep only the most prominent lights casting shadows; demote the rest.
        //
        // A shadowed light costs a full re-submission of the scene geometry into its own shadow map, so
        // sixty lamps mean sixty extra scene passes. Demoted lights are moved to the unshadowed vectors:
        // they still light the room, they simply stop casting - which on a distant lamp is invisible and
        // on the frame time is everything. accum_point/accum_spot never consult flags.bShadow, so the
        // flag is left alone and nothing else in the light's state changes.
        //
        // Ranking is apparent size: range divided by distance to the eye. Cheap, and it keeps the lamp
        // you are standing under while dropping the ones across the map.
        if (ps_r__light_shadow_budget > 0 && LP.v_shadowed.size() > size_t(ps_r__light_shadow_budget))
        {
            const Fvector eye = Device.vCameraPosition;

            // [DA_PORT] Привилегированные источники (сейчас это фонарь в руках игрока, см.
            // IRender_Light::set_never_demote) выносятся в начало и бюджет НЕ расходуют - им
            // выделяется место сверх него. При значении по умолчанию слот всего один, и без этого
            // он доставался ближайшей лампе на стене, а свет из рук игрока шёл сквозь стены.
            //
            // stable_partition, а не сортировка: порядок остальных обязан сохраниться, иначе
            // гистерезис ниже сравнивал бы каждый кадр с другим набором.
            const auto first_ordinary = std::stable_partition(LP.v_shadowed.begin(), LP.v_shadowed.end(),
                [](const light* L) { return !!L->flags.bNeverDemote; });
            const size_t privileged = size_t(first_ordinary - LP.v_shadowed.begin());
            const size_t budget = privileged + size_t(ps_r__light_shadow_budget);

            // [DA_PORT] Бюджет забивается БЛИЖАЙШИМИ источниками, и меряется расстояние не до самой
            // лампы, а до края её освещённой области: `дистанция - радиус`. Лампа, внутри которой
            // стоит игрок, даёт отрицательное значение и попадает в отбор первой; дальше идут те, чей
            // свет ближе всего к камере. Прежняя оценка «видимый размер» (радиус / дистанция) в
            // помещении вела себя хуже: далёкий большой фонарь на улице обгонял лампу в двух метрах
            // над головой, потому что радиус у него больше.
            //
            // + Гистерезис, без которого любой отбор мигает. На базе ламп заметно больше бюджета, и
            // соседние по оценке стоят вплотную: камера смещается на сантиметр - две лампы меняются
            // местами. А разница между «с тенью» и «без тени» на глаз огромная: без теневой карты свет
            // проходит СКВОЗЬ стену и освещает соседний коридор. Со стороны - лампа, мигающая без
            // остановки; ровно это и было видно на базе.
            //
            // Поблажка односторонняя и в тех же единицах, что и оценка: действующей теневой лампе
            // прощается полтора метра (или 15% её радиуса, смотря что больше). Чтобы отобрать у неё
            // место, новая должна подойти заметно ближе, а не на волос.
            //
            // Держится на указателях - они сравниваются, но никогда не разыменовываются, поэтому
            // исчезнувший между кадрами источник безопасен.
            static xr_vector<const light*> s_prev_shadowed;

            const auto kept_last_frame = [](const light* L)
            {
                return std::find(s_prev_shadowed.begin(), s_prev_shadowed.end(), L) != s_prev_shadowed.end();
            };
            const auto score = [&](const light* L) // меньше - важнее
            {
                const float base = L->position.distance_to(eye) - L->range;
                return kept_last_frame(L) ? base - _max(1.5f, L->range * 0.15f) : base;
            };

            // Сортируется только ОБЫЧНАЯ часть: привилегированные уже стоят в начале и остаются
            // там. Проверка на размер обязательна - привилегированных могло оказаться столько,
            // что вытеснять уже нечего, и тогда `begin() + budget` ушёл бы за конец.
            const bool over_budget = LP.v_shadowed.size() > budget;
            if (over_budget)
            {
                std::partial_sort(first_ordinary, LP.v_shadowed.begin() + budget, LP.v_shadowed.end(),
                    [&score](const light* a, const light* b) { return score(a) < score(b); });
            }

            for (size_t i = over_budget ? budget : LP.v_shadowed.size(); i < LP.v_shadowed.size(); ++i)
            {
                light* L = LP.v_shadowed[i];

                // ⚠️ [DA_PORT] Флаг ОБЯЗАН сняться. Прежний комментарий здесь утверждал обратное -
                // «accum_point/accum_spot не читают flags.bShadow» - и это была неправда:
                // `accum_spot` его читает и по нему уходит в теневую ветку. А там берутся координаты
                // слота в атласе теней (`X.S.size/posX/posY/view/project`), которые считает
                // `compute_xf_spot` - и только для тех ламп, что В СПИСКЕ ОСТАЛИСЬ. У выброшенной
                // там лежит мусор от прошлого раза, выборка из атласа уходит в никуда, и свет
                // гасится почти везде. Граница получается ровной - это край конуса лампы; в игре
                // выглядело так, будто мир поделили надвое.
                //
                // Флаг возвращается в конце кадра (см. da_restore_demoted_lights ниже): он часть
                // постоянного состояния источника, а не наше свойство.
                L->flags.bShadow = false;
                s_demoted.push_back(L);

                // ⚠️ ВСЕ выброшенные идут в v_spot, включая секторы точечной лампы (OMNIPART).
                //
                // Точечная лампа с тенями разбирается движком на ШЕСТЬ 90-градусных секторов
                // (`light::Export`), поэтому в списке лежат OMNIPART, а не POINT. Соблазн отправить
                // их в v_point велик - по имени похоже, - но это ошибка, и она стоила второго
                // захода на «мир поделили надвое»:
                //
                //   • `accum_spot` ЗНАЕТ про OMNIPART - там стоит явная ветка, берущая точечный
                //     шейдер, но всю остальную обвязку сектора;
                //   • `accum_point` про него не знает и трактует объём-клин как замкнутую сферу.
                //     Разметка трафаретом («обратный приём Кармака») на незамкнутом объёме врёт, и
                //     свет ложится клином во весь экран - ровная диагональная граница;
                //   • для v_spot движок сам зовёт `compute_xf_spot` перед отрисовкой (см.
                //     r2_R_lights.cpp), а для v_point не зовёт. Без него у лампы остаются ЧУЖИЕ
                //     матрицы проекции с прошлого кадра - вторая причина той же полосы.
                //
                // Настоящих POINT в v_shadowed не бывает вовсе: теневые точечные уже разобраны на
                // секторы, а нетеневые сюда не попадают. Ветка на POINT оставлена лишь на случай,
                // если движок когда-нибудь начнёт класть их сюда напрямую.
                if (L->flags.type == IRender_Light::POINT)
                    LP.v_point.push_back(L);
                else
                    LP.v_spot.push_back(L);
            }
            if (over_budget)
                LP.v_shadowed.resize(budget);

            s_prev_shadowed.assign(LP.v_shadowed.begin(), LP.v_shadowed.end());
        }

        // stats
        Stats.l_shadowed = LP.v_shadowed.size();
        Stats.l_unshadowed = LP.v_point.size() + LP.v_spot.size();
        Stats.l_total = Stats.l_shadowed + Stats.l_unshadowed;

        // perform tests
        size_t count = 0;
        count = _max(count, LP.v_point.size());
        count = _max(count, LP.v_spot.size());
        count = _max(count, LP.v_shadowed.size());
        for (size_t it = 0; it < count; it++)
        {
            if (it < LP.v_point.size())
            {
                light* L = LP.v_point[it];
                L->vis_prepare(dsgraph.cmd_list);
                if (L->vis.pending)
                    LP_pending.v_point.push_back(L);
                else
                    LP_normal.v_point.push_back(L);
            }
            if (it < LP.v_spot.size())
            {
                light* L = LP.v_spot[it];
                L->vis_prepare(dsgraph.cmd_list);
                if (L->vis.pending)
                    LP_pending.v_spot.push_back(L);
                else
                    LP_normal.v_spot.push_back(L);
            }
            if (it < LP.v_shadowed.size())
            {
                light* L = LP.v_shadowed[it];
                L->vis_prepare(dsgraph.cmd_list);
                if (L->vis.pending)
                    LP_pending.v_shadowed.push_back(L);
                else
                    LP_normal.v_shadowed.push_back(L);
            }
        }
    }
    LP_normal.sort();
    LP_pending.sort();

    //******* Main render :: PART-1 (second)
    if (split_the_scene_to_minimize_wait)
    {
        PIX_EVENT(DEFER_PART1_SPLIT);
        // skybox can be drawn here
        if (false)
        {
            Target->u_setrt(dsgraph.cmd_list, Target->rt_Generic_0_r, Target->rt_Generic_1_r, nullptr, Target->rt_MSAADepth);
            dsgraph.cmd_list.set_CullMode(CULL_NONE);
            dsgraph.cmd_list.set_Stencil(FALSE);

            // draw skybox
            dsgraph.cmd_list.set_ColorWriteEnable();
            dsgraph.cmd_list.set_Z(false);
            g_pGamePersistent->Environment().RenderSky();
            dsgraph.cmd_list.set_Z(true);
        }

        // level
        Target->phase_scene_begin();
        dsgraph.render_hud();
        dsgraph.render_lods(true, true);
        if (Details)
            Details->Render(dsgraph.cmd_list);
        Target->phase_scene_end();
    }

    if (g_pGameLevel->pHUD && g_pGameLevel->pHUD->RenderActiveItemUIQuery())
    {
        Target->phase_wallmarks();
        dsgraph.render_hud_ui();
    }

    // Wall marks
    if (Wallmarks)
    {
        PIX_EVENT(DEFER_WALLMARKS);
        Target->phase_wallmarks();
        g_r = 0;
        Wallmarks->Render(); // wallmarks has priority as normal geometry
    }

    // Update incremental shadowmap-visibility solver
    {
        PIX_EVENT(DEFER_FLUSH_OCCLUSION);
        u32 it = 0;
        for (it = 0; it < Lights_LastFrame.size(); it++)
        {
            if (0 == Lights_LastFrame[it])
                continue;
            try
            {
                for (int id = 0; id < 3; ++id)
                    Lights_LastFrame[it]->svis[id].flushoccq();
            }
            catch (...)
            {
                Msg("! Failed to flush-OCCq on light [%d] %X", it, *(u32*)(&Lights_LastFrame[it]));
            }
        }
        Lights_LastFrame.clear();
    }

    // full screen pass to mark msaa-edge pixels in highest stencil bit
    if (o.msaa)
    {
        PIX_EVENT(MARK_MSAA_EDGES);
        Target->mark_msaa_edges();
    }

    r_rain.sync();

    // Directional light - fucking sun
    {
        PIX_EVENT(DEFER_SUN);
        Stats.l_visible++;
        DA_GPU_ZONE_BEGIN(z_sun_smap);
        if (!RImplementation.o.oldshadowcascades)
            r_sun.sync();
        else
            r_sun_old.sync();
        DA_GPU_ZONE_END(z_sun_smap);
        DA_GPU_ZONE_BEGIN(z_sun_apply);
        Target->accum_direct_blend(dsgraph.cmd_list);
        DA_GPU_ZONE_END(z_sun_apply);
    }

    {
        PIX_EVENT(DEFER_SELF_ILLUM);
        DA_GPU_ZONE_BEGIN(z_selfillum);
        Target->phase_accumulator(dsgraph.cmd_list);
        // Render emissive geometry, stencil - write 0x0 at pixel pos
        dsgraph.cmd_list.set_xform_project(Device.mProject);
        dsgraph.cmd_list.set_xform_view(Device.mView);
        // Stencil - write 0x1 at pixel pos -
        //
        // [DA_PORT] К общему биту 0x01 добавляется свой, 0x02, когда включена метка свечения. Иначе
        // отличить эти пиксели потом нечем: 0x01 стоит у ВСЕЙ непрозрачной геометрии кадра.
        // Бит гасится сразу же, в phase_reactive_emissive: свет и отражения сравнивают трафарет с
        // 0x01, в том числе на равенство, и оставленная отметка сломала бы их на этих пикселях.
        // Маска записи 0x7f под MSAA не мешает - 0x02 в неё входит, старший бит там за кромками.
#if RENDER == R_R4
        const u32 emissive_ref = Target->da_emissive_mark_ready() ? 0x03 : 0x01;
#else
        const u32 emissive_ref = 0x01;
#endif
        if (!o.msaa)
        {
            dsgraph.cmd_list.set_Stencil(TRUE, D3DCMP_ALWAYS, emissive_ref, 0xff, 0xff,
                D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        }
        else
        {
            dsgraph.cmd_list.set_Stencil(TRUE, D3DCMP_ALWAYS, emissive_ref, 0xff, 0x7f,
                D3DSTENCILOP_KEEP, D3DSTENCILOP_REPLACE, D3DSTENCILOP_KEEP);
        }
        dsgraph.cmd_list.set_CullMode(CULL_CCW);
        dsgraph.cmd_list.set_ColorWriteEnable();

        // [DA_PORT] Разовый дамп: в какой очереди лежит то, что мерцает. Всё перечисленное рисуется
        // ПОСЛЕ G-буфера и потому не пишет ни векторов движения, ни реактивности - но лечится это в
        // разных местах кадра, и гадать, в какую именно очередь попал предмет, дороже, чем спросить.
        // Считать надо здесь: render_emissive и render_sorted очищают свои очереди по ходу отрисовки.
        // Считается КАДРАМИ, а не одним снимком: мигание - это разница МЕЖДУ кадрами, и один снимок
        // про него не скажет ничего. Строка на кадр, `r__emissive_probe 120` - две секунды.
        if (::ps_r__emissive_probe > 0)
        {
            if (::ps_r__emissive_probe == 1)
                Msg("~ [DA_EMIS] ---- конец ----");

            Msg("~ [DA_EMIS] кадр %6u | свечение: мир %u, руки %u | прозрачное: мир %u, руки %u | "
                "искажение %u | света: всего %u, видимо %u, с тенью %u, без тени %u",
                Device.dwFrame, dsgraph.mapEmissive.size(), dsgraph.mapHUDEmissive.size(),
                dsgraph.mapSorted.size(), dsgraph.mapHUDSorted.size(), dsgraph.mapDistort.size(),
                Stats.l_total, Stats.l_visible, Stats.l_shadowed, Stats.l_unshadowed);

            --::ps_r__emissive_probe;
        }

        dsgraph.render_emissive();

#if RENDER == R_R4
        // [DA_PORT] Пока трафарет ещё помнит, где легло свечение: дальше идёт свет, а он переписывает
        // трафарет маркерами источников. Позже этой отметки уже не существует.
        Target->phase_reactive_emissive();
#endif
    }

    // Lighting, non dependant on OCCQ
    {
        DA_GPU_ZONE_END(z_selfillum);
        PIX_EVENT(DEFER_LIGHT_NO_OCCQ);
        DA_GPU_ZONE_BEGIN(z_lights);
        render_lights(LP_normal);
    }

    // Lighting, dependant on OCCQ
    {
        PIX_EVENT(DEFER_LIGHT_OCCQ);
        render_lights(LP_pending);
    }

    // [DA_PORT] Свет отрисован - возвращаем флаг тем, у кого его занял бюджет теней.
    da_restore_demoted_lights();

    // Postprocess
    {
        DA_GPU_ZONE_END(z_lights);
        PIX_EVENT(DEFER_LIGHT_COMBINE);
        DA_GPU_ZONE_BEGIN(z_combine);
        Target->phase_combine();
        DA_GPU_ZONE_END(z_combine);
    }

    // [DA_PORT] FSR 2 used to be dispatched here, which looked like "after the frame is assembled but
    // before post-process". It is not: phase_pp runs inside phase_combine above, so this was a pass too
    // late and its output was never displayed. Moved into phase_combine, immediately ahead of phase_pp.

    // [DA_PORT] Remember this frame's camera for the next one. Temporal effects reproject a pixel into
    // the previous frame from its depth and this matrix.
    extern Fmatrix g_da_prev_VP;
    g_da_prev_VP = ::g_da_taa_unjittered_VP;

    DA_GPU_FRAME_END();

    VERIFY(dsgraph.mapDistort.empty());
}

void CRender::render_forward()
{
    ZoneScoped;
    auto& dsgraph = get_imm_context();

    //******* Main render - second order geometry (the one, that doesn't support deffering)
    //.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
    {
        //	Igor: we don't want to render old lods on next frame.
        dsgraph.mapLOD.clear();
        dsgraph.render_graph(1); // normal level, secondary priority
        dsgraph.PortalTraverser.fade_render(); // faded-portals
        dsgraph.render_sorted(); // strict-sorted geoms
        g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
    }
}

// Перед началом рендера мира --#SM+#--
void CRender::BeforeWorldRender() {}

// После рендера мира и пост-эффектов --#SM+#--
void CRender::AfterWorldRender() {}
} // namespace xray::render::RENDER_NAMESPACE
