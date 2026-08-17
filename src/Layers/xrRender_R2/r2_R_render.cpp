#include "stdafx.h"

// [DA_PORT] GPU timing per phase - see da_gpu_timer.h. R4 only; the GL branch has no D3D11 queries.
#if RENDER == R_R4
#   include "Layers/xrRenderPC_R4/da_gpu_timer.h"
#   define DA_GPU_ZONE_BEGIN(z) g_da_gpu_timer.zone_begin(da_gpu_timer::z)
#   define DA_GPU_ZONE_END(z)   g_da_gpu_timer.zone_end(da_gpu_timer::z)
#   define DA_GPU_FRAME_BEGIN()  g_da_gpu_timer.frame_begin()
#   define DA_GPU_FRAME_END()    g_da_gpu_timer.frame_end()
#   define DA_CULL_WAIT_STATS(exec, spins) da_gpu_set_cull_wait(exec, spins)
#   define DA_GBUF2_PARTS(h, l, d) da_gpu_set_gbuf2(h, l, d)
#else
#   define DA_GPU_ZONE_BEGIN(z)
#   define DA_GPU_ZONE_END(z)
#   define DA_GPU_FRAME_BEGIN()
#   define DA_GPU_FRAME_END()
#   define DA_CULL_WAIT_STATS(exec, spins)
#   define DA_GBUF2_PARTS(h, l, d)
#endif

#include "xrCore/Threading/TaskManager.hpp"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/CustomHUD.h"
#include "xrEngine/xr_object.h"

#include "Layers/xrRender/FBasicVisual.h"
#include "Layers/xrRender/FVisual.h"      // [DA_PORT] dwPrimitives для разбора очереди
#include "Layers/xrRender/FTreeVisual.h"
#include "Layers/xrRender/FProgressive.h" // [DA_PORT] окно LOD для оценки склейки

// [DA_PORT] Defined in the engine (device.cpp). Declared out here, not inside the function that uses it:
// an extern inside namespace xray::render::render_r4 would be looking for a symbol in that namespace.
extern ENGINE_API Fmatrix g_da_taa_unjittered_VP;

// [DA_PORT] Разовый дамп очередей, рисуемых после G-буфера, см. r__emissive_probe.
extern ENGINE_API int ps_r__emissive_probe;

// [DA_PORT] ВНЕ пространства имён: extern внутри xray::render искал бы символ в нём же — грабля
// уже описана у соседних глобалов в r4_rendertarget_phase_combine.cpp.
extern ENGINE_API int ps_r__light_map;

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

// [DA_PORT] Разбор очереди отрисовки. Сам он определён ниже, рядом с прямым проходом, а зовётся и
// отсюда, из основного -- поэтому объявлен заранее.
extern int ps_da_geom_dump;
extern std::atomic<u32> g_da_ssa_discarded;

// [DA_PORT] Приборы фазы света живут в r2_R_lights.cpp, отчёт печатается здесь.
extern int ps_da_light_prof;
extern double g_da_lp_prep;
extern double g_da_lp_vis;
extern double g_da_lp_pack;
extern double g_da_lp_wait;
extern double g_da_lp_smap;
extern double g_da_lp_accum;
extern u32 g_da_lp_lights;
extern u32 g_da_lp_accums;
extern u32 g_da_lp_waves;
extern u32 g_da_lp_starved;
extern u32 g_da_lp_free_ctx;
extern u32 g_da_lp_max_flight;
extern u32 g_da_lp_queued;
extern u32 g_da_lc_drawn, g_da_lc_empty;
extern u32 g_da_lc_drawn_size_min, g_da_lc_drawn_size_max;
extern u32 g_da_lc_empty_size_min, g_da_lc_empty_size_max;
extern double g_da_lc_drawn_dist, g_da_lc_empty_dist;
extern u32 g_da_lc_cached, g_da_lc_cached_special;
static void da_dump_pass(R_dsgraph_structure& dsgraph, u32 priority, pcstr tag);


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
    DA_GPU_ZONE_BEGIN(z_prepare);

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
        DA_GPU_ZONE_BEGIN(z_wait_fence);
        q_sync_point.Wait(ps_r2_wait_sleep, ps_r2_wait_timeout);
        DA_GPU_ZONE_END(z_wait_fence);
    }
    BasicStats.WaitS.End();
    q_sync_point.End();

    // [DA_PORT] Ожидание расчёта видимости: мерим не только СКОЛЬКО, но и ЧЕМ занят поток.
    //
    // Полный разбор -- у g_da_wait_executed в r2.h. Коротко: ожидание ворует чужие задачи, поэтому
    // одни и те же 1.63 мс означают либо настоящий пузырь (воровать было нечего), либо честную
    // работу кадра, просто записанную в эту зону. По времени эти случаи неразличимы, а лечение у них
    // противоположное, так что считаем выполненные задачи и холостые витки.
    //
    // Счётчики монотонные и общие для всех фаз, поэтому берём РАЗНОСТЬЮ вокруг нужного ожидания --
    // так в отчёт попадает только основная сцена, без каскадов солнца и дождя.
    //
    // Числа уезжают через ту же заглушку, что и зоны: копилка живёт в da_gpu_timer (только R4), а
    // этот файл идёт и в ветку GL.
    const u32 da_wait_exec_before = g_da_wait_executed;
    const u32 da_wait_spin_before = g_da_wait_idle_spins;

    DA_GPU_ZONE_BEGIN(z_wait_cull);
    r_main.sync();
    DA_GPU_ZONE_END(z_wait_cull);

    DA_CULL_WAIT_STATS(g_da_wait_executed - da_wait_exec_before, g_da_wait_idle_spins - da_wait_spin_before);

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

    DA_GPU_ZONE_END(z_prepare);

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
        // [DA_PORT] Зона G-буфера была только у НЕразделённой ветки выше, а работает эта. Отсюда и
        // пустое место в отчёте: фаза исполнялась каждый кадр, а в лог не попадала ни разу.
        DA_GPU_ZONE_BEGIN(z_gbuffer);
        // level, SPLIT
        Target->phase_scene_begin();
        // [DA_PORT] Разбор основного прохода -- ДО отрисовки: render_graph очищает списки.
        if (ps_da_geom_dump > 0)
        {
            da_dump_pass(dsgraph, 0, "DA_GEOM");
            --ps_da_geom_dump;
        }
        dsgraph.render_graph(0);
        Target->disable_aniso();
        // [DA_PORT] Снимок альбедо для карты кадра — ЗДЕСЬ, пока rt_Color ещё альбедо. Разбор у
        // da_map_capture_gbuffer: в phase_combine эта же цель занята под готовый кадр.
#if RENDER == R_R4
        if (::ps_r__light_map > 0)
            Target->da_map_capture_gbuffer();
#endif
        DA_GPU_ZONE_END(z_gbuffer);
    }
#ifdef USE_OGL
    if (psDeviceFlags.test(rsWireframe))
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif

    //******* Occlusion testing of volume-limited light-sources
    DA_GPU_ZONE_BEGIN(z_occq);
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

    DA_GPU_ZONE_END(z_occq);

    //******* Main render :: PART-1 (second)
    if (split_the_scene_to_minimize_wait)
    {
        PIX_EVENT(DEFER_PART1_SPLIT);
        // [DA_PORT] Вторая половина G-буфера -- отдельной зоной: между половинами идёт проверка
        // видимости источников света, и одна зона на обе показала бы её стоимость как свою.
        DA_GPU_ZONE_BEGIN(z_gbuffer2);
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
        // [DA_PORT] Вторая половина G-буфера стоит 0.90 мкс на вызов против 0.56 у первой -- на 60%
        // дороже за ту же по сути подачу геометрии. Половины делают РАЗНОЕ: первая гонит
        // render_graph(0), вторая -- интерфейс с оружием, импосторы и траву. Трава при плотности 0.3
        // и радиусе 200 м даёт много пачек, и хотя её построение вынесено на свой поток
        // (mt_detail_path), подача вызовов идёт здесь, на главном. Меряем три куска врозь: у них
        // разные хозяева и лечатся они по-разному.
        // Флаг живёт в da_gpu_timer.cpp того же пространства имён -- см. соседние externы наверху.
        extern int ps_da_gpu_log;
        const bool da_g2 = ps_da_gpu_log > 0;
        CTimer da_g2_timer;
        double da_g2_hud = 0.0, da_g2_lods = 0.0, da_g2_details = 0.0;

        Target->phase_scene_begin();

        if (da_g2)
            da_g2_timer.Start();
        dsgraph.render_hud();
        if (da_g2)
        {
            da_g2_hud = da_g2_timer.GetElapsed_sec() * 1000.0;
            da_g2_timer.Start();
        }

        dsgraph.render_lods(true, true);
        if (da_g2)
        {
            da_g2_lods = da_g2_timer.GetElapsed_sec() * 1000.0;
            da_g2_timer.Start();
        }

        if (Details)
            Details->Render(dsgraph.cmd_list);
        if (da_g2)
            da_g2_details = da_g2_timer.GetElapsed_sec() * 1000.0;

        Target->phase_scene_end();
        DA_GBUF2_PARTS(da_g2_hud, da_g2_lods, da_g2_details);
        DA_GPU_ZONE_END(z_gbuffer2);
    }

    DA_GPU_ZONE_BEGIN(z_wmarks);
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

    DA_GPU_ZONE_END(z_wmarks);

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

    // [DA_PORT] Отчёт приборов фазы света. Печатается ПОСЛЕ обоих заходов render_lights (обычного
    // и по видимости), иначе половина работы осталась бы за кадром отчёта.
    if (ps_da_light_prof > 0)
    {
        // [DA_PORT] Три первых числа -- разметка кусков, не покрытых прежними счётчиками: зона
        // lights стоила 0.59 мс, а объяснено было 0.26. Разбор -- у g_da_lp_prep в r2_R_lights.cpp.
        Msg("~ [DA_LIGHT] ламп со тенью %u, накоплений %u | выборка динамики %5.2f мс | видимость ламп %5.2f мс | "
            "раскладка атласа %5.2f мс | ожидание списков %5.2f мс | "
            "теневые карты %5.2f мс | накопление %5.2f мс | всего %5.2f мс | волн %u, нехваток %u, свободно контекстов %u, макс. в работе %u | обходов запущено %u",
            g_da_lp_lights, g_da_lp_accums, g_da_lp_prep, g_da_lp_vis, g_da_lp_pack,
            g_da_lp_wait, g_da_lp_smap, g_da_lp_accum,
            g_da_lp_prep + g_da_lp_vis + g_da_lp_pack + g_da_lp_wait + g_da_lp_smap + g_da_lp_accum,
            g_da_lp_waves, g_da_lp_starved, g_da_lp_free_ctx, g_da_lp_max_flight, g_da_lp_queued);
        Msg("~ [DA_LIGHT] рисуются: %u шт, ячейка %u..%u пикс, среднее расстояние %.0f м | "
            "пустые: %u шт, ячейка %u..%u пикс, среднее расстояние %.0f м",
            g_da_lc_drawn, g_da_lc_drawn == 0 ? 0 : g_da_lc_drawn_size_min, g_da_lc_drawn_size_max,
            g_da_lc_drawn ? g_da_lc_drawn_dist / g_da_lc_drawn : 0.0,
            g_da_lc_empty, g_da_lc_empty == 0 ? 0 : g_da_lc_empty_size_min, g_da_lc_empty_size_max,
            g_da_lc_empty ? g_da_lc_empty_dist / g_da_lc_empty : 0.0);
        Msg("~ [DA_LIGHT] кэш статики годен у %u ламп, из них с содержимым мимо кэша: %u", g_da_lc_cached,
            g_da_lc_cached_special);
        g_da_lc_cached = g_da_lc_cached_special = 0;
        g_da_lc_drawn = g_da_lc_empty = 0;
        g_da_lc_drawn_size_min = g_da_lc_empty_size_min = 0xFFFFFFFF;
        g_da_lc_drawn_size_max = g_da_lc_empty_size_max = 0;
        g_da_lc_drawn_dist = g_da_lc_empty_dist = 0.0;

        g_da_lp_prep = g_da_lp_vis = g_da_lp_pack = 0.0;
        g_da_lp_wait = g_da_lp_smap = g_da_lp_accum = 0.0;
        g_da_lp_lights = g_da_lp_accums = g_da_lp_waves = g_da_lp_starved = g_da_lp_free_ctx = g_da_lp_max_flight = g_da_lp_queued = 0;
        --ps_da_light_prof;
    }

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
    DA_GPU_ZONE_BEGIN(z_tail);
    extern Fmatrix g_da_prev_VP;
    g_da_prev_VP = ::g_da_taa_unjittered_VP;

    DA_GPU_ZONE_END(z_tail);
    DA_GPU_FRAME_END();

    VERIFY(dsgraph.mapDistort.empty());
}

// [DA_PORT] Разовый разбор прямого прохода: da_forward_dump 1.
//
// Повод: замер показал у прямого прохода 509 вызовов отрисовки на 11 тысяч треугольников -- по два
// десятка треугольников на вызов, 0.79 мс процессора и НОЛЬ на видеокарте. То есть работа, которой
// видеокарта не замечает, а процессор на неё тратит шестую часть своего времени в рендере. Прежде
// чем что-то с этим делать, надо знать, ЧТО там рисуется, а по числу вызовов этого не видно.
//
// Печатается гистограмма по пиксельным шейдерам: у каждого -- сколько элементов и пример визуала.
// Имя шейдера названо первым потому, что вызов рвётся именно на смене прохода: элементы одного и
// того же шейдера уже сгруппированы, и если их много при малом числе треугольников -- это кандидат
// на объединение, а если шейдеров много по одному элементу -- объединять нечего.
int ps_da_forward_dump = 0;

// [DA_PORT] То же самое для основного прохода: da_geom_dump 1.
int ps_da_geom_dump = 0;

// [DA_PORT] Приборы частиц живут в ParticleEffect.cpp -- в ЭТОМ же пространстве имён, поэтому
// extern объявлен здесь, а не в глобальной области: та же грабля, что с ps_da_gpu_log.
extern int ps_da_particle_prof;
extern double g_da_pp_lock;
extern double g_da_pp_fill;
extern double g_da_pp_draw;
extern u32 g_da_pp_calls;
extern u32 g_da_pp_parts;
extern u32 g_da_pp_far[4];
extern u32 g_da_pp_far_p[4];
extern float g_da_pp_dmin;
extern float g_da_pp_dmax;
extern u32 g_da_pp_xform;
extern int ps_da_particle_dist;
extern u32 g_da_pp_skipped;

// [DA_PORT] Имя визуала (dbg_name) живёт под #ifdef DEBUG и в отгружаемой сборке его нет, поэтому
// в отчёте стоит ТИП визуала: он есть всегда и для нашей задачи говорит больше имени -- частицы,
// деревья и скелеты объединяются по-разному, а имя одного примера про весь список не скажет.
static pcstr da_visual_type_name(u32 t)
{
    switch (t)
    {
    case MT_NORMAL: return "статика";
    case MT_HIERRARHY: return "иерархия";
    case MT_PROGRESSIVE: return "прогрессивная";
    case MT_SKELETON_ANIM: return "скелет анимир.";
    case MT_SKELETON_RIGID: return "скелет жёсткий";
    case MT_SKELETON_GEOMDEF_PM: return "скелет geom PM";
    case MT_SKELETON_GEOMDEF_ST: return "скелет geom ST";
    case MT_PARTICLE_EFFECT: return "частицы (эффект)";
    case MT_PARTICLE_GROUP: return "частицы (группа)";
    case MT_LOD: return "LOD";
    case MT_TREE_ST: return "дерево ST";
    case MT_TREE_PM: return "дерево PM";
    default: return "?";
    }
}

// [DA_PORT] Число треугольников визуала -- только для тех типов, у которых оно лежит на виду.
//
// Нужно, чтобы отличить «много мелочи» от «мало, но крупное»: и то и другое даёт одинаковое число
// вызовов отрисовки, а лечится противоположно. Скелеты и частицы считают иначе, для них ноль --
// и это честнее, чем подставить неверное число.
// [DA_PORT] Тот же расчёт LOD, что в r__dsgraph_render.cpp: там он ICF и наружу не виден, а
// разбору очереди нужен ровно он, иначе окно прогрессивной геометрии выберется не то.
extern float r_ssaGLOD_start, r_ssaGLOD_end;

static float da_calc_lod(float ssa)
{
    return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

static IRender_Mesh* da_visual_mesh(dxRender_Visual* V)
{
    if (!V)
        return nullptr;
    switch (V->Type)
    {
    case MT_NORMAL:
    case MT_PROGRESSIVE: return static_cast<Fvisual*>(V);
    case MT_TREE_ST:
    case MT_TREE_PM: return static_cast<FTreeVisual*>(V);
    // Скелеты сюда НЕ идут: они рисуются со своей матрицей и набором костей, склеивать их
    // диапазонами нельзя. Частицы и LOD'ы считают геометрию иначе.
    default: return nullptr;
    }
}

static u32 da_visual_tris(dxRender_Visual* V)
{
    IRender_Mesh* M = da_visual_mesh(V);
    return M ? M->dwPrimitives : 0;
}

// [DA_PORT] Разбор очереди отрисовки по шейдерам. priority 0 -- основной проход (G-буфер),
// priority 1 -- прямой, тот, что рисуется внутри combine.
//
// Что именно надо увидеть: вызов отрисовки рвётся на смене прохода, поэтому важно не только
// сколько элементов, но и по скольким проходам они разложены. Много элементов в НЕМНОГИХ проходах
// -- состояние уже общее, и остаётся только склеить отрисовку. Много проходов по паре элементов --
// склеивать нечего, там платят за переключение состояний, и лечение другое.
static void da_dump_pass(R_dsgraph_structure& dsgraph, u32 priority, pcstr tag)
{
    struct entry
    {
        u32 items = 0;   // всего элементов
        u32 stat = 0;    // из них статика (рисуется без своей матрицы)
        u32 runs = 0;    // во сколько вызовов статика сложилась бы объединением диапазонов
        u32 passes = 0;
        u32 tris = 0;
        pcstr sample = "";
    };
    xr_map<shared_str, entry> hist;
    u32 total_items = 0, total_passes = 0, total_tris = 0, total_stat = 0, total_runs = 0;

    // [DA_PORT] Сколько вызовов останется, если склеить соседние диапазоны индексов.
    //
    // Считаем ВОЗМОЖНОЕ, а не желаемое: два элемента сливаются в один вызов только если у них
    // общая геометрия (один вершинный и индексный буфер, одна разметка) и их диапазоны индексов
    // идут встык. Ни инстансинга, ни правки шейдеров это не требует -- потому и меряем первым
    // делом: если склейка даёт мало, браться за более дорогие приёмы незачем.
    //
    // Скелеты в счёт не идут: у каждого своя матрица и свои кости.
    xr_vector<std::pair<const void*, std::pair<u32, u32>>> ranges; // геометрия -> (iBase, iCount)

    const auto account = [&](SPass* P, auto& items, bool is_static)
    {
        const shared_str name = (P && P->ps) ? P->ps->cName : shared_str("(без шейдера)");
        entry& e = hist[name];
        e.items += u32(items.size());
        ++e.passes;
        for (const auto& it : items)
            e.tris += da_visual_tris(it.pVisual);
        if (!items.empty() && items[0].pVisual)
            e.sample = da_visual_type_name(items[0].pVisual->Type);
        total_items += u32(items.size());
        ++total_passes;

        if (!is_static)
            return;

        ranges.clear();
        for (const auto& it : items)
        {
            IRender_Mesh* M = da_visual_mesh(it.pVisual);
            if (!M || !M->rm_geom)
                continue;

            // Диапазон берём тот, что рисуется НА САМОМ ДЕЛЕ. У прогрессивной геометрии он зависит
            // от LOD: постоянные iBase/iCount показали бы стык там, где его нет, и завысили склейку.
            u32 ibase = M->iBase;
            u32 tris = M->dwPrimitives;
            if (it.pVisual->Type == MT_PROGRESSIVE)
            {
                const float lod = da_calc_lod(it.ssa);
                static_cast<FProgressive*>(it.pVisual)->da_lod_window(lod, ibase, tris);
            }
            ranges.emplace_back(M->rm_geom._get(), std::make_pair(ibase, tris * 3));
        }
        if (ranges.empty())
            return;

        std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b)
            {
                if (a.first != b.first)
                    return std::less<const void*>{}(a.first, b.first);
                return a.second.first < b.second.first;
            });

        u32 runs = 1;
        for (size_t i = 1; i < ranges.size(); ++i)
        {
            const bool same_geom = ranges[i].first == ranges[i - 1].first;
            const bool adjacent = ranges[i].second.first == ranges[i - 1].second.first + ranges[i - 1].second.second;
            if (!same_geom || !adjacent)
                ++runs;
        }
        e.stat += u32(ranges.size());
        e.runs += runs;
        total_stat += u32(ranges.size());
        total_runs += runs;
    };

    for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
    {
        xr_vector<R_dsgraph::mapNormal_T::value_type*> passes;
        dsgraph.mapNormalPasses[priority][iPass].get_any_p(passes);
        for (const auto& it : passes)
            account(it->first, it->second, true);

        xr_vector<R_dsgraph::mapMatrix_T::value_type*> mpasses;
        dsgraph.mapMatrixPasses[priority][iPass].get_any_p(mpasses);
        for (const auto& it : mpasses)
            account(it->first, it->second, false);
    }

    for (const auto& kv : hist)
        total_tris += kv.second.tris;

    Msg("~ [%s] проходов %u, элементов %u, треугольников %u (верхняя оценка, без учёта LOD)", tag,
        total_passes, total_items, total_tris);
    Msg("~ [%s] статики %u -> склеилось бы в %u вызовов (динамика и скелеты в счёт не идут)", tag, total_stat,
        total_runs);

    // [DA_PORT] Разница с прошлым отчётом = снято за этот кадр. Счётчик растёт из рабочих потоков
    // ещё на построении списков, до нас, поэтому обнулять его отсюда было бы не к месту.
    if (priority == 0)
    {
        static u32 prev = 0;
        const u32 cur = g_da_ssa_discarded.load(std::memory_order_relaxed);
        Msg("~ [%s] снято по экранной площади: %u (порог %.1f пикс.)", tag, cur - prev, ps_r__ssaDISCARD);
        prev = cur;
    }

    // Порядок по числу элементов: интересен тот шейдер, что даёт больше всего вызовов, а не тот,
    // что оказался первым по алфавиту.
    xr_vector<std::pair<shared_str, entry>> rows(hist.begin(), hist.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second.items > b.second.items; });
    for (const auto& kv : rows)
        Msg("~ [%s]   %-34s проходов %3u  элементов %4u  статики %4u -> %4u  тр. %6u  %s", tag, kv.first.c_str(),
            kv.second.passes, kv.second.items, kv.second.stat, kv.second.runs, kv.second.tris, kv.second.sample);
}

static void da_dump_forward_pass(R_dsgraph_structure& dsgraph) { da_dump_pass(dsgraph, 1, "DA_FWD"); }

void CRender::render_forward()
{
    ZoneScoped;
    auto& dsgraph = get_imm_context();

    if (ps_da_forward_dump > 0)
    {
        // Печатаем ДО отрисовки: render_graph очищает списки, после него смотреть уже нечего.
        da_dump_forward_pass(dsgraph);
        --ps_da_forward_dump;
    }

    //******* Main render - second order geometry (the one, that doesn't support deffering)
    //.todo: should be done inside "combine" with estimation of of luminance, tone-mapping, etc.
    {
        //	Igor: we don't want to render old lods on next frame.
        dsgraph.mapLOD.clear();
        dsgraph.render_graph(1); // normal level, secondary priority
        dsgraph.PortalTraverser.fade_render(); // faded-portals
        // [DA_PORT] Строго сортированная геометрия -- отдельной зоной. Пакетировать её нельзя по
        // определению: порядок задан расстоянием, значит каждый объект идёт своим вызовом. Если
        // мелкие вызовы постобработки родом отсюда, это увидно будет прямо в отчёте.
        DA_GPU_ZONE_BEGIN(z_sorted);
        dsgraph.render_sorted(); // strict-sorted geoms
        DA_GPU_ZONE_END(z_sorted);
        g_pGamePersistent->Environment().RenderLast(); // rain/thunder-bolts
    }

    // [DA_PORT] Отчёт приборов частиц -- ПОСЛЕ отрисовки, иначе считать было бы нечего.
    if (ps_da_particle_prof > 0)
    {
        Msg("~ [DA_PART] эффектов %u, частиц %u | блокировка буфера %5.2f мс | сборка %5.2f мс | "
            "отрисовка %5.2f мс | всего %5.2f мс",
            g_da_pp_calls, g_da_pp_parts, g_da_pp_lock, g_da_pp_fill, g_da_pp_draw,
            g_da_pp_lock + g_da_pp_fill + g_da_pp_draw);
        Msg("~ [DA_PART] дальше 50 м: %u эфф / %u част | 100 м: %u / %u | 150 м: %u / %u | 200 м: %u / %u",
            g_da_pp_far[0], g_da_pp_far_p[0], g_da_pp_far[1], g_da_pp_far_p[1], g_da_pp_far[2],
            g_da_pp_far_p[2], g_da_pp_far[3], g_da_pp_far_p[3]);
        Msg("~ [DA_PART] расстояние: ближайший %.1f м, дальний %.1f м | с преобразованием %u из %u | "
            "порог %d м, отсечено %u",
            g_da_pp_dmin, g_da_pp_dmax, g_da_pp_xform, g_da_pp_calls, ps_da_particle_dist, g_da_pp_skipped);
        g_da_pp_lock = g_da_pp_fill = g_da_pp_draw = 0.0;
        g_da_pp_calls = g_da_pp_parts = g_da_pp_xform = g_da_pp_skipped = 0;
        g_da_pp_dmin = 1e9f;
        g_da_pp_dmax = 0.f;
        for (int b = 0; b < 4; ++b)
        {
            g_da_pp_far[b] = 0;
            g_da_pp_far_p[b] = 0;
        }
        --ps_da_particle_prof;
    }
}

// Перед началом рендера мира --#SM+#--
void CRender::BeforeWorldRender() {}

// После рендера мира и пост-эффектов --#SM+#--
void CRender::AfterWorldRender() {}
} // namespace xray::render::RENDER_NAMESPACE
