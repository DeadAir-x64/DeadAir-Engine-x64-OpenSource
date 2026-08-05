#pragma once

// [DA_PORT] GPU timing per render phase.
//
// Why this exists: every CPU-side counter we have says the frame is cheap - culling 1.27ms, primitive
// submission 1.83ms - and then the frame blocks for another 2.7-4.7ms inside R_sync_point::Wait, which
// is a D3D_QUERY_EVENT fence, i.e. the CPU waiting for the GPU to finish. So the time is on the GPU and
// none of the existing counters can see it: they all measure how long it took to ASK, not to DO.
//
// Cordon runs at 130 fps and the swamps at 400+ with the same render resolution, so it is not fill
// rate. Cordon has 1.9x the vertices, and 1.07M vertices in 6.87ms is about 156M vertices/second - tens
// of times below what the card should manage. That gap is what this measures: which pass on the GPU
// actually consumes the frame.
//
// D3D11 timestamps come with two rules that make a naive implementation lie:
//  - they must be bracketed by a DISJOINT query, which reports the tick frequency and whether the clock
//    changed mid-frame (power management). A disjoint frame is thrown away rather than reported.
//  - reading them back stalls the pipeline, which is the very thing being measured. So results are read
//    N frames LATE, from a ring of query sets, and never in the frame that issued them.

#include <array>

namespace xray::render::RENDER_NAMESPACE
{
class da_gpu_timer
{
public:
    // Keep in sync with zone_names in the .cpp.
    enum zone : u32
    {
        z_sun_smap = 0, // sun cascades: the scene re-rendered into each shadow map
        z_sun_apply,    // full-screen application of sun light (accum_direct_blend)
        z_selfillum,    // accumulator + emissive geometry
        z_gbuffer,      // main scene pass into the G-buffer
        // [DA_PORT] Вторая половина G-буфера. В режиме с разделением сцены (он же и работает)
        // геометрия рисуется двумя заходами: между ними идёт проверка видимости источников света.
        // Раньше зона была одна и обёрнута вокруг НЕразделённой ветки -- поэтому "gbuffer" не
        // появлялся в отчёте ни разу за триста кадров, хотя проход исполнялся каждый.
        z_gbuffer2,
        z_lights,       // deferred light accumulation
        z_combine,      // combine + sky + post
        z_count
    };

    void create();
    void destroy();

    void frame_begin();
    void frame_end();

    void zone_begin(zone z);
    void zone_end(zone z);

private:
    static constexpr u32 RING = 4; // frames of latency before a result is safe to read

    struct frame_queries
    {
        ID3D11Query* disjoint = nullptr;
        std::array<ID3D11Query*, z_count> begin{};
        std::array<ID3D11Query*, z_count> end{};
        bool issued = false;
        std::array<bool, z_count> zone_used{};

        // [DA_PORT] Процессорное время тех же фаз -- сколько заняло ПОПРОСИТЬ видеокарту.
        //
        // Раньше это мерили отдельной командой и по другим границам, и сравнить два числа между
        // собой было нельзя: разные проходы, разные счётчики. Здесь фаза одна и та же, и в отчёте
        // они стоят рядом -- сразу видно, ждём мы видеокарту или сами не успеваем её загрузить.
        std::array<double, z_count> cpu_ms{};
    };

    std::array<frame_queries, RING> m_ring{};
    u32 m_write = 0;
    bool m_created = false;

    std::array<CTimer, z_count> m_cpu_timer{};

    void collect(frame_queries& f);
};

extern da_gpu_timer g_da_gpu_timer;
} // namespace xray::render::RENDER_NAMESPACE
