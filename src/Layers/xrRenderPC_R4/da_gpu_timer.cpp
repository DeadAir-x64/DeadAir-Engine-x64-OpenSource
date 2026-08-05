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

static const char* zone_names[da_gpu_timer::z_count] = { "sun_smap", "sun_apply", "selfillum", "gbuffer", "gbuffer2", "lights", "combine" };

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
    HW.get_context(CHW::IMM_CTX_ID)->Begin(f.disjoint);
    f.issued = true;
}

void da_gpu_timer::frame_end()
{
    if (!m_created || ps_da_gpu_log <= 0)
        return;

    frame_queries& f = m_ring[m_write];
    if (!f.issued)
        return;

    HW.get_context(CHW::IMM_CTX_ID)->End(f.disjoint);
    m_write = (m_write + 1) % RING;
}

void da_gpu_timer::zone_begin(zone z)
{
    if (!m_created || ps_da_gpu_log <= 0)
        return;
    frame_queries& f = m_ring[m_write];
    if (!f.issued)
        return;
    HW.get_context(CHW::IMM_CTX_ID)->End(f.begin[z]); // timestamps use End() to record, not Begin()
    f.zone_used[z] = true;
    m_cpu_timer[z].Start(); // [DA_PORT] то же самое процессором
}

void da_gpu_timer::zone_end(zone z)
{
    if (!m_created || ps_da_gpu_log <= 0)
        return;
    frame_queries& f = m_ring[m_write];
    if (!f.issued || !f.zone_used[z])
        return;
    f.cpu_ms[z] += m_cpu_timer[z].GetElapsed_sec() * 1000.0; // [DA_PORT]
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

    string512 line;
    xr_strcpy(line, "~ [DA_GPU]");

    double total = 0.0;
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
        if (!got_begin || !got_end || t1 <= t0)
        {
            string64 skipped;
            xr_sprintf(skipped, " %s %.2f/??? |", zone_names[z], float(f.cpu_ms[z]));
            xr_strcat(line, skipped);
            continue;
        }

        const double ms = 1000.0 * double(t1 - t0) / double(dj.Frequency);
        total += ms;

        if (z == z_sun_smap)
        {
            g_da_smap_gpu_ms += ms;
            ++g_da_smap_gpu_frames;
        }

        string64 part;
        // [DA_PORT] Пара чисел: сколько ПОПРОСИТЬ (процессор) и сколько СДЕЛАТЬ (видеокарта).
        xr_sprintf(part, " %s %.2f/%.2f |", zone_names[z], float(f.cpu_ms[z]), ms);
        xr_strcat(line, part);
    }

    string64 tail;
    xr_sprintf(tail, " GPU total %5.2f", total);
    xr_strcat(line, tail);
    Msg("%s", line);

    if (--ps_da_gpu_log == 0)
        Msg("~ [DA_GPU] ---- done ----");
}
} // namespace xray::render::RENDER_NAMESPACE
