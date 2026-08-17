#include "stdafx.h"
#include "da_gpu_timer.h"

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Switched on with "da_gpu_log <frames>".
//
// Defined INSIDE the namespace deliberately: the console registration writes "extern int ps_da_gpu_log"
// from within xray::render::RENDER_NAMESPACE, and an extern declared there names a symbol in THAT
// namespace. Defining it at global scope links against nothing - the same trap the TAA work hit.
int ps_da_gpu_log = 0;

// [DA_PORT] Копилка для замера кэша теневых карт (da_shadow_test).
//
// Время фазы солнца надо мерить ОТДЕЛЬНО от кадра: после наших оптимизаций кадр идёт за 2.4 мс, и
// доля теневых карт в нём тонет в шуме — по общему времени разницу между включённым и выключенным
// кэшем просто не увидеть. Здесь копится ровно z_sun_smap, а отчёт делит на число кадров.
double g_da_smap_gpu_ms = 0.0;
u32 g_da_smap_gpu_frames = 0;

da_gpu_timer g_da_gpu_timer;

// [DA_PORT] Метка последней фазы кадра -- «хлебные крошки» на случай потери устройства.
//
// Зачем. DXGI_ERROR_DEVICE_HUNG означает, что видеокарта не уложилась в сторожевой таймер Windows
// на НАШИХ командах и драйвер её перезапустил. Причина от драйвера (GetDeviceRemovedReason)
// говорит ЧТО случилось, но не ГДЕ. У игрока нет ни отладчика, ни профилировщика -- есть только
// лог, и по нему сейчас нельзя сказать даже, в какой части кадра всё встало.
//
// Как это решают в индустрии. Штатный механизм -- DRED, автоматические метки и отчёт о страничных
// отказах -- существует только в DirectX 12; у нас 11. NVIDIA Aftermath работает и с 11, но это
// сторонний набор и только для одного производителя. Метки в буфере ВИДЕОКАРТЫ (breadcrumbs у
// Unreal) дают точность до вызова отрисовки, и там же прямо сказано, что цена на видеокарте
// заметная -- их поэтому и не держат включёнными всегда.
//
// Отсюда наш выбор: метка ПРОЦЕССОРНАЯ. Два присваивания на фазу -- цены нет, можно держать
// включённой у всех. Точности до вызова она не даёт: видеокарта отстаёт от процессора, поэтому
// метка называет фазу, которую мы УСПЕЛИ отправить, а не ту, на которой видеокарта встала.
// Но сузить поиск с «где-то в рендере» до «свет» или «G-буфер» этого хватает, а номер кадра
// вместе с порядковым номером фазы показывает, стояла ли отправка вообще: если при потере
// устройства номер кадра сильно отстал от текущего, значит процессор давно ждёт видеокарту.
pcstr g_da_stage = "(кадр не начинался)";
u32 g_da_stage_frame = 0;
u32 g_da_stage_seq = 0;

static const char* zone_names[da_gpu_timer::z_count] ={ "sun_smap", "sun_apply", "selfillum", "gbuffer", "gbuffer2",
    "lights", "combine", "sky", "forward", "sorted", "prepare", "occq", "wmarks", "tail", "wait_fence", "wait_cull" };

// [DA_PORT] Копилка «чем занят поток в ожидании расчёта видимости» -- объявление в da_gpu_timer.h.
//
// Пишет её r2_R_render.cpp вокруг r_main.sync(), читает печать отчёта ниже. Обе стороны -- главный
// поток, поэтому без атомарности. Значение держится до следующего кадра: строка DA_GPU печатается
// не каждый кадр, и обнулять здесь было бы нечестно -- в отчёт попадали бы нули от кадров, которые
// просто не печатались.
static u32 s_cull_wait_executed = 0;
static u32 s_cull_wait_idle_spins = 0;

void da_gpu_set_cull_wait(u32 executed, u32 idle_spins)
{
    s_cull_wait_executed = executed;
    s_cull_wait_idle_spins = idle_spins;
}

// [DA_PORT] Состав самого расчёта видимости. Пишет build_subspace с РАБОЧЕГО потока, читает печать
// отчёта с главного -- но не одновременно: печать идёт после r_main.sync(), то есть после того, как
// расчёт этого кадра заведомо закончен, и ожидание задачи само по себе является барьером.
static double s_cull_portals_ms = 0.0;
static double s_cull_statics_ms = 0.0;
static double s_cull_dynamics_ms = 0.0;

void da_gpu_set_cull_parts(double portals_ms, double statics_ms, double dynamics_ms)
{
    s_cull_portals_ms = portals_ms;
    s_cull_statics_ms = statics_ms;
    s_cull_dynamics_ms = dynamics_ms;
}

static double s_dyn_collect_ms = 0.0;
static double s_dyn_sort_ms = 0.0;
static double s_dyn_sector_ms = 0.0;
static u32 s_dyn_count = 0;

// Сколько объектов реально пересчитали сектор -- см. da_dyn_sector_hits в r__dsgraph_build.cpp.
static u32 s_dyn_sector_hits = 0;

void da_gpu_set_cull_dyn(double collect_ms, double sort_ms, double sector_ms, u32 count, u32 sector_hits)
{
    s_dyn_collect_ms = collect_ms;
    s_dyn_sort_ms = sort_ms;
    s_dyn_sector_ms = sector_ms;
    s_dyn_count = count;
    s_dyn_sector_hits = sector_hits;
}

static double s_body_hom_ms = 0.0;
static double s_body_render_ms = 0.0;

void da_gpu_set_cull_body(double hom_ms, double render_ms)
{
    s_body_hom_ms = hom_ms;
    s_body_render_ms = render_ms;
}

static double s_skel_bones_ms = 0.0;
static double s_skel_wallmarks_ms = 0.0;
static u32 s_skel_count = 0;
static u32 s_skel_leafs = 0;

static double s_g2_hud_ms = 0.0;
static double s_g2_lods_ms = 0.0;
static double s_g2_details_ms = 0.0;

void da_gpu_set_gbuf2(double hud_ms, double lods_ms, double details_ms)
{
    s_g2_hud_ms = hud_ms;
    s_g2_lods_ms = lods_ms;
    s_g2_details_ms = details_ms;
}

static u32 s_skel_exact = 0;

void da_gpu_set_cull_skel(double bones_ms, double wallmarks_ms, u32 skeletons, u32 leafs, u32 exact)
{
    s_skel_bones_ms = bones_ms;
    s_skel_wallmarks_ms = wallmarks_ms;
    s_skel_count = skeletons;
    s_skel_leafs = leafs;
    s_skel_exact = exact;
}

void da_gpu_timer::create()
{
    if (m_created)
        return;

    D3D11_QUERY_DESC dj{};
    dj.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    D3D11_QUERY_DESC ts{};
    ts.Query = D3D11_QUERY_TIMESTAMP;

    for (auto& f : m_ring)
    {
        if (FAILED(HW.pDevice->CreateQuery(&dj, &f.disjoint)))
            return;
        for (u32 z = 0; z < z_count; ++z)
        {
            if (FAILED(HW.pDevice->CreateQuery(&ts, &f.begin[z])) ||
                FAILED(HW.pDevice->CreateQuery(&ts, &f.end[z])))
                return;
        }
    }
    m_created = true;
}

void da_gpu_timer::destroy()
{
    for (auto& f : m_ring)
    {
        _RELEASE(f.disjoint);
        for (u32 z = 0; z < z_count; ++z)
        {
            _RELEASE(f.begin[z]);
            _RELEASE(f.end[z]);
        }
        f.issued = false;
    }
    m_created = false;
}

void da_gpu_timer::frame_begin()
{
    if (!m_created || ps_da_gpu_log <= 0)
        return;

    // Read the OLDEST set first - it is RING frames old by now, so the results are already sitting in
    // the driver and asking for them costs nothing. Asking for this frame's would stall the pipeline.
    frame_queries& oldest = m_ring[(m_write + 1) % RING];
    if (oldest.issued)
        collect(oldest);

    frame_queries& f = m_ring[m_write];
    f.zone_used.fill(false);
    f.cpu_ms.fill(0.0); // [DA_PORT]
    f.calls.fill(0);    // [DA_PORT]
    f.polys.fill(0);    // [DA_PORT]
    HW.get_context(CHW::IMM_CTX_ID)->Begin(f.disjoint);
    f.issued = true;
}

void da_gpu_timer::frame_end()
{
    // [DA_PORT] Отдельная метка «кадр отправлен целиком». Без неё нельзя отличить зависание внутри
    // фазы от зависания на выводе кадра: в последнем случае метка так и осталась бы на хвосте.
    g_da_stage = "конец кадра (вывод)";
    g_da_stage_frame = Device.dwFrame;

    if (!m_created || ps_da_gpu_log <= 0)
        return;

    frame_queries& f = m_ring[m_write];
    if (!f.issued)
        return;

    // [DA_PORT] Снимаем итог по контекстам ЗДЕСЬ: к концу кадра списки команд рабочих потоков уже
    // исполнены, и счётчики окончательны. Обнуляются они все разом в D3DXRenderBase::Begin.
    for (u32 id = 0; id < R__NUM_CONTEXTS; ++id)
    {
        const auto& st = RImplementation.da_context_cmd_list(id).stat.render;
        f.ctx_calls[id] = st.calls;
        f.ctx_polys[id] = st.polys;
    }

    HW.get_context(CHW::IMM_CTX_ID)->End(f.disjoint);
    m_write = (m_write + 1) % RING;
}

void da_gpu_timer::zone_begin(zone z)
{
    // [DA_PORT] Метка ставится ДО всех проверок: она нужна всегда, а замер -- только по команде.
    // Разбор, почему метка процессорная и что она может, -- у объявления g_da_stage.
    g_da_stage = zone_names[z];
    g_da_stage_frame = Device.dwFrame;
    ++g_da_stage_seq;

    if (!m_created || ps_da_gpu_log <= 0)
        return;
    frame_queries& f = m_ring[m_write];
    if (!f.issued)
        return;
    HW.get_context(CHW::IMM_CTX_ID)->End(f.begin[z]); // timestamps use End() to record, not Begin()
    f.zone_used[z] = true;
    m_cpu_timer[z].Start(); // [DA_PORT] то же самое процессором

    // [DA_PORT] Счётчик тот же, что печатается как DIP в отчёте рендера, поэтому суммы сопоставимы.
    m_call_mark[z] = RCache.stat.render.calls;
    m_poly_mark[z] = RCache.stat.render.polys;
}

void da_gpu_timer::zone_end(zone z)
{
    if (!m_created || ps_da_gpu_log <= 0)
        return;
    frame_queries& f = m_ring[m_write];
    if (!f.issued || !f.zone_used[z])
        return;
    f.cpu_ms[z] += m_cpu_timer[z].GetElapsed_sec() * 1000.0; // [DA_PORT]

    // [DA_PORT] Разница, а не абсолют: зона может открываться дважды за кадр (свет идёт двумя
    // заходами, обычным и по видимости), и оба захода должны сложиться, а не затереть друг друга.
    f.calls[z] += RCache.stat.render.calls - m_call_mark[z];
    f.polys[z] += RCache.stat.render.polys - m_poly_mark[z];

    HW.get_context(CHW::IMM_CTX_ID)->End(f.end[z]);
}

void da_gpu_timer::collect(frame_queries& f)
{
    auto* ctx = HW.get_context(CHW::IMM_CTX_ID);

    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
    if (ctx->GetData(f.disjoint, &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK)
        return; // not ready - try again next frame rather than blocking

    f.issued = false;

    // Disjoint means the GPU clock changed while we were measuring (power state), so the numbers are
    // meaningless. Dropping the frame is the only honest option.
    if (dj.Disjoint || dj.Frequency == 0)
        return;

    // [DA_PORT] Раскладка по вызовам отрисовки удлинила строку -- буфер расширен под неё.
    string1024 line;
    xr_strcpy(line, "~ [DA_GPU]");

    double total = 0.0;
    u32 total_calls = 0;

    // [DA_PORT] Вложенные зоны в итог НЕ идут: их время и вызовы уже посчитаны в combine, и
    // сложение дало бы двойной счёт. Итог обязан сходиться с DIP -- по этому совпадению мы и
    // поймали, что теневые проходы считаются мимо.
    auto nested = [](u32 z) { return z == z_sky || z == z_forward || z == z_sorted || z == z_wait_fence || z == z_wait_cull; };
    for (u32 z = 0; z < z_count; ++z)
    {
        if (!f.zone_used[z])
            continue;

        // [DA_PORT] Пропуск зоны теперь ВИДЕН в отчёте.
        //
        // Было три молчаливых continue: не готов результат -- зоны в строке просто нет. Отличить
        // "фаза ничего не стоила" от "фазу не измерили" по такому отчёту невозможно, а решения по
        // нему принимаются. Один раз это уже стоило неверного вывода: отсутствие gbuffer прочли как
        // "он бесплатен", тогда как зона не размечалась вовсе.
        u64 t0 = 0, t1 = 0;
        const bool got_begin = ctx->GetData(f.begin[z], &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        const bool got_end = ctx->GetData(f.end[z], &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
        if (!nested(z))
            total_calls += f.calls[z];

        if (!got_begin || !got_end || t1 <= t0)
        {
            string128 skipped;
            xr_sprintf(skipped, " %s %.2f/??? %uc/%ukt |", zone_names[z], float(f.cpu_ms[z]), f.calls[z],
                f.polys[z] / 1000);
            xr_strcat(line, skipped);
            continue;
        }

        const double ms = 1000.0 * double(t1 - t0) / double(dj.Frequency);
        if (!nested(z))
            total += ms;

        if (z == z_sun_smap)
        {
            g_da_smap_gpu_ms += ms;
            ++g_da_smap_gpu_frames;
        }

        string128 part;
        // [DA_PORT] Пара чисел: сколько ПОПРОСИТЬ (процессор) и сколько СДЕЛАТЬ (видеокарта).
        // За ними -- чем фаза загрузила обе стороны: вызовы отрисовки и треугольники, тысячами.
        xr_sprintf(part, " %s %.2f/%.2f %uc/%ukt |", zone_names[z], float(f.cpu_ms[z]), ms, f.calls[z],
            f.polys[z] / 1000);
        xr_strcat(line, part);
    }

    string256 tail;
    // [DA_PORT] steal <выполнено>/<холостых витков> -- чем был занят поток в зоне wait_cull.
    // cull <порталы>/<статика>/<динамика> -- из чего складывается сам расчёт.
    // dyn <сбор>/<сортировка>/<сектор> xN -- динамика по частям; остаток цикла = динамика минус три.
    // Разбор -- у da_gpu_set_cull_wait и da_gpu_set_cull_parts ниже.
    xr_sprintf(tail, " GPU total %5.2f | calls %u | steal %u/%u | cull %.2f/%.2f/%.2f | dyn %.2f/%.2f/%.2f x%u sec%u | body %.2f/%.2f | skel %.2f/%.2f s%u l%u e%u | g2 %.2f/%.2f/%.2f",
        total, total_calls, s_cull_wait_executed, s_cull_wait_idle_spins,
        s_cull_portals_ms, s_cull_statics_ms, s_cull_dynamics_ms,
        s_dyn_collect_ms, s_dyn_sort_ms, s_dyn_sector_ms, s_dyn_count, s_dyn_sector_hits,
        s_body_hom_ms, s_body_render_ms,
        s_skel_bones_ms, s_skel_wallmarks_ms, s_skel_count, s_skel_leafs, s_skel_exact,
        s_g2_hud_ms, s_g2_lods_ms, s_g2_details_ms);
    xr_strcat(line, tail);
    Msg("%s", line);

    // [DA_PORT] Вторая строка: вызовы отрисовки по контекстам за кадр. Номера 0..2 -- каскады
    // солнца, 3 -- вспомогательный (дождь и точечные лампы), последний -- непосредственный, тот
    // самый, чей счётчик печатается как DIP.
    string512 ctx_line;
    xr_strcpy(ctx_line, "~ [DA_CTX]");
    u32 ctx_total = 0;
    for (u32 id = 0; id < R__NUM_CONTEXTS; ++id)
    {
        ctx_total += f.ctx_calls[id];
        string64 part;
        xr_sprintf(part, " %s %uc/%ukt |", id == R_dsgraph_structure::IMM_CTX_ID ? "imm" : "par",
            f.ctx_calls[id], f.ctx_polys[id] / 1000);
        xr_strcat(ctx_line, part);
    }
    string64 ctx_tail;
    xr_sprintf(ctx_tail, " всего %u", ctx_total);
    xr_strcat(ctx_line, ctx_tail);
    Msg("%s", ctx_line);

    if (--ps_da_gpu_log == 0)
        Msg("~ [DA_GPU] ---- done ----");
}
} // namespace xray::render::RENDER_NAMESPACE
