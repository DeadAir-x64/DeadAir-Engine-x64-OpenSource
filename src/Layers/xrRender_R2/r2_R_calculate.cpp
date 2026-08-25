#include "stdafx.h"

#include "xrEngine/CustomHUD.h"
#include "xrCore/Threading/TaskManager.hpp"

namespace xray::render::RENDER_NAMESPACE
{
float g_fSCREEN;

extern float r_dtex_range;
extern float r_ssaDISCARD;
extern float r_ssaDONTSORT;
extern float r_ssaLOD_A;
extern float r_ssaLOD_B;
extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

extern int ps_r2_mt_calculate;
extern int ps_r2_mt_render;


//-----
// [DA_PORT] r__main_cull_mt 0 — считать видимость ОСНОВНОЙ сцены на главном потоке.
//
// Замер: ожидание расчёта 0.63 мс на кадр при собственной цене расчёта 0.34 (порталы 0.01, статика
// 0.24, динамика 0.09) — то есть около 0.29 мс уходит на то, что задача ждёт в очереди, пока её
// подхватит рабочий поток. При этом главный поток в ожидании ворует 2-3 задачи против тысяч
// холостых витков, то есть просто простаивает.
//
// ⚠️ Стоковая ручка r2_mt_calculate для этого НЕ ГОДИТСЯ: она выключает параллельность у ВСЕХ фаз
// разом, и замер это показал — кадр стал хуже (render 3.71 -> 3.96), потому что последовательными
// становятся ещё и каскады солнца с дождём. Здесь отключается ровно основная фаза; солнце и дождь
// продолжают считаться параллельно и перекрывают эту работу.
// ⭐ По умолчанию 0 (на главном потоке): замер дал render 3.77 -> 3.64 мс, работа кадра
// 4.79 -> 4.62, ожидание 0.63 -> 0.00, видеокарта не изменилась (3.38 -> 3.31).
//
// ⚠️ Замер на 8-ядерном процессоре. Перенос работы НА главный поток теоретически может быть хуже
// там, где он и так узкое место, а рабочие потоки простаивают — но здесь замер показал обратное:
// в ожидании главный поток воровал 2-3 задачи против ТЫСЯЧ холостых витков, то есть простаивал.
int ps_da_main_cull_mt = 0;

void render_main::init()
{
    o.mt_calc_enabled = ps_da_main_cull_mt && RImplementation.o.mt_calculate && !RImplementation.o.oldshadowcascades && !ps_r2_ls_flags.test(R2FLAG_ZFILL);
    o.mt_draw_enabled = false; // always on imm context
    o.active = true; // always active
}

void render_main::calculate()
{
    ZoneScoped;

    auto& dsgraph_main = RImplementation.get_imm_context();

    dsgraph_main.o.phase = CRender::PHASE_NORMAL;
    dsgraph_main.r_pmask(true, true, true); // enable priority "0,1",+ capture wmarks
    if (RImplementation.r_sun.o.active && RImplementation.o.oldshadowcascades)
        dsgraph_main.set_Recorder(&RImplementation.main_coarse_structure); // this is a show-stopper. Can't be paralleled with sun
    else
        dsgraph_main.set_Recorder(nullptr);
    dsgraph_main.o.use_hom = true;
    dsgraph_main.o.is_main_pass = true;
    dsgraph_main.o.sector_id = RImplementation.last_sector_id;
    dsgraph_main.o.portal_traverse_flags =
        CPortalTraverser::VQ_HOM | CPortalTraverser::VQ_SSA | CPortalTraverser::VQ_FADE;
    dsgraph_main.o.spatial_traverse_flags = ISpatial_DB::O_ORDERED;
    dsgraph_main.o.spatial_types = STYPE_RENDERABLE | STYPE_LIGHTSOURCE;
    dsgraph_main.o.view_pos = Device.vCameraPosition;
    dsgraph_main.o.xform = Device.mFullTransform;
    dsgraph_main.o.view_frustum = RImplementation.ViewBase;
    dsgraph_main.o.query_box_side = VIEWPORT_NEAR + EPS_L;
    dsgraph_main.o.precise_portals = true;
    dsgraph_main.o.mt_calculate = o.mt_calc_enabled;

    dsgraph_main.build_subspace();
}

void render_main::render()
{
    // TODO
}

//-----

void CRender::Calculate()
{
    ZoneScopedN("r2_calculate");

    // Transfer to global space to avoid deep pointer access
    float fov_factor = _sqr(90.f / Device.fFOV);
    // [DA_PORT] The OUTPUT size, not the render target's. Every level-of-detail threshold below is
    // divided by this area, so taking it from the scene target made them scale with r__render_scale:
    // at 67% the area is less than half, thresholds are more than twice as strict, and r_ssaDISCARD -
    // below which a visual is not drawn AT ALL - starts eating characters. Their shadows stayed,
    // because the shadow pass skips these tests, which is what made it look like the models had
    // vanished rather than been culled.
    //
    // Conceptually the output size is the right one regardless: an object covers the same fraction of
    // what the player sees no matter what resolution the scene was rendered at internally, so enabling
    // an upscaler must not quietly lower the level of detail everywhere.
    g_fSCREEN = float(Device.dwWidth * Device.dwHeight) * fov_factor * (EPS_S + ps_r__LOD);
    r_ssaDISCARD = _sqr(ps_r__ssaDISCARD) / g_fSCREEN;
    r_ssaDONTSORT = _sqr(ps_r__ssaDONTSORT / 3) / g_fSCREEN;
    r_ssaLOD_A = _sqr(ps_r2_ssaLOD_A / 3) / g_fSCREEN;
    r_ssaLOD_B = _sqr(ps_r2_ssaLOD_B / 3) / g_fSCREEN;
    r_ssaGLOD_start = _sqr(ps_r__GLOD_ssa_start / 3) / g_fSCREEN;
    r_ssaGLOD_end = _sqr(ps_r__GLOD_ssa_end / 3) / g_fSCREEN;
    r_ssaHZBvsTEX = _sqr(ps_r__ssaHZBvsTEX / 3) / g_fSCREEN;
    r_dtex_range = ps_r2_df_parallax_range * g_fSCREEN / (1024.f * 768.f);

    // Configure
    o.distortion    = o.distortion_enabled;
    o.mt_calculate  = ps_r2_mt_calculate > 0;
#ifdef USE_DX11
    o.mt_render     = ps_r2_mt_render > 0;
#else
    o.mt_render     = 0; // OpenGL does not support parallel draw calls
#endif

    if (m_bFirstFrameAfterReset)
        return;

    auto& dsgraph_main = get_imm_context();

    // Detect camera-sector
    if (!Device.vCameraDirectionSaved.similar(Device.vCameraPosition, EPS_L))
    {
        const auto sector_id = dsgraph_main.detect_sector(Device.vCameraPosition);
        if (sector_id != IRender_Sector::INVALID_SECTOR_ID)
        {
            if (sector_id != last_sector_id)
                g_pGamePersistent->OnSectorChanged(sector_id);

            last_sector_id = sector_id;
        }
    }

    //
    Lights.Update();

    // Check if we touch some light even trough portal
    static xr_vector<ISpatial*> spatial_lights;
    g_pGamePersistent->SpatialSpace.q_sphere(spatial_lights, 0, STYPE_LIGHTSOURCE, Device.vCameraPosition, EPS_L);
    for (auto spatial : spatial_lights)
    {
        const auto& entity_pos = spatial->spatial_sector_point();
        spatial->spatial_updatesector(dsgraph_main.detect_sector(entity_pos));
        const auto sector_id = spatial->GetSpatialData().sector_id;
        // [DA_PORT] Границу массива секторов проверяем наравне с «недействительным» номером —
        // см. разбор в r__dsgraph_build.cpp: номер приходит лучом по модели порталов, из данных.
        if (sector_id == IRender_Sector::INVALID_SECTOR_ID || sector_id >= dsgraph_main.Sectors.size())
            continue; // disassociated from S/P structure

        VERIFY(spatial->GetSpatialData().type & STYPE_LIGHTSOURCE);
        // lightsource
        light* L = (light*)spatial->dcast_Light();
        VERIFY(L);
        Lights.add_light(L);
    }

    // [DA_PORT] Ожидание отсечения (HOM) не покрыто НИ ОДНИМ счётчиком, а стоит на критическом
    // пути: расчёт видимости ставится в очередь только после него, потому что пользуется его
    // результатом. Пока оно идёт, главный поток выше занят светом — но хватает ли этой работы,
    // чтобы перекрыть ожидание, до сих пор никто не мерил. Замер под тем же флагом, что и разбор
    // ожидания видимости.
    {
        extern int ps_da_cull_prof;
        extern float g_da_ms_hom_wait;
        CTimer da_hom;
        const bool da_hp = ps_da_cull_prof != 0;
        if (da_hp)
            da_hom.Start();
        TaskScheduler->Wait(*ProcessHOMTask);
        if (da_hp)
            g_da_ms_hom_wait += da_hom.GetElapsed_sec() * 1000.f;
    }

    r_main.init();
    if (o.oldshadowcascades)
        r_sun_old.init();
    else
        r_sun.init();
#if RENDER != R_R2
    r_rain.init();
#endif

    // Main calc
    BasicStats.Culling.Begin();
    {
        r_main.run();
    }
    BasicStats.Culling.End();

    // Rain calc
#if RENDER != R_R2
    r_rain.run();
#endif

    // Sun calc
    if (o.oldshadowcascades)
        r_sun_old.run();
    else
        r_sun.run();
}
} // namespace xray::render::RENDER_NAMESPACE
