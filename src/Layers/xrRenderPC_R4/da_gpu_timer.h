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
        // [DA_PORT] Три вложенные зоны ВНУТРИ combine. Повод: у постобработки оказалось 674 вызова
        // отрисовки на 14 тысяч треугольников -- по два десятка треугольников на вызов. Столько
        // мелких отрисовок в постобработке взяться неоткуда, значит там рисуется что-то ещё, и
        // разложить combine надо было прежде, чем что-либо в нём трогать.
        z_sky,          // небо
        z_forward,      // прямой проход: то, что не поддерживает отложенное освещение
        z_sorted,       // из него -- строго сортированная часть, вложенная в z_forward
        // [DA_PORT] Четыре зоны, закрывающие ОСТАТОК кадра.
        //
        // Повод: на Кордоне процессорный рендер стоит 3.7 мс, а сумма размеченных фаз -- около
        // 1.55. Разрыв нельзя было объяснить: одни счётчики пиковые, другие покадровые, и «две
        // трети мимо приборов» звучало как вывод, а было догадкой. Эти зоны покрывают то, что
        // раньше не мерили вовсе: подготовку сцены с точкой синхронизации, проход перекрытий со
        // сбором пакетов света, следы с интерфейсом оружия и хвост кадра после постобработки.
        z_prepare,      // от начала кадра до G-буфера: ZFILL, ожидание точки синхронизации, подготовка
        z_occq,         // проход перекрытий и сбор пакетов источников света
        z_wmarks,       // следы на поверхностях и интерфейс активного предмета
        z_tail,         // хвост после постобработки
        // [DA_PORT] Две вложенные зоны ВНУТРИ prepare, обе про ожидание.
        //
        // Замер на Кордоне: prepare стоит 2.73 мс процессора при НУЛЕ на видеокарте и НУЛЕ вызовов
        // отрисовки -- больше, чем весь остальной кадр. Отрисовки там нет вовсе, значит это чистое
        // ожидание. Кандидатов два, и лечатся они по-разному: точка синхронизации (фенс видеокарты)
        // и r_main.sync() -- ожидание задачи, считающей видимость основной сцены.
        z_wait_fence,   // точка синхронизации: ждём видеокарту
        z_wait_cull,    // r_main.sync(): ждём расчёт видимости сцены
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

        // [DA_PORT] Вызовы отрисовки и треугольники ТОЙ ЖЕ фазы.
        //
        // Счётчик DIP в отчёте рендера один на весь кадр, и по нему нельзя сказать, куда уходят
        // вызовы: в основной проход, в каскады солнца или в теневой проход каждой лампы. Раскладка
        // по фазам отвечает на это прямо и тем же счётчиком, что даёт общее число -- сумма по
        // зонам сходится с DIP, так что расхождение сразу видно.
        std::array<u32, z_count> calls{};
        std::array<u32, z_count> polys{};

        // [DA_PORT] Те же вызовы, но в разрезе КОНТЕКСТОВ, за кадр целиком.
        //
        // Разметка по зонам считает только непосредственный контекст: теневые проходы строятся
        // рабочими потоками в своих списках команд, и по времени они попадают внутрь зоны (список
        // исполняется на общей очереди), а по вызовам -- никуда. Раскладку по зонам это чинить
        // нельзя: рабочий поток не обязан укладываться в границы зоны, и приписать его вызовы
        // какой-то одной фазе значило бы соврать. Поэтому здесь -- итог за кадр по каждому
        // контексту, снятый после того, как все списки команд уже исполнены.
        std::array<u32, R__NUM_CONTEXTS> ctx_calls{};
        std::array<u32, R__NUM_CONTEXTS> ctx_polys{};
    };

    std::array<frame_queries, RING> m_ring{};
    u32 m_write = 0;
    bool m_created = false;

    std::array<CTimer, z_count> m_cpu_timer{};

    // [DA_PORT] Снимок счётчиков на входе в зону; разница снимается на выходе.
    std::array<u32, z_count> m_call_mark{};
    std::array<u32, z_count> m_poly_mark{};

    void collect(frame_queries& f);
};

extern da_gpu_timer g_da_gpu_timer;

// [DA_PORT] Чем был занят главный поток в зоне wait_cull: сколько чужих задач он успел выполнить и
// сколько раз крутнулся впустую. Полный разбор -- у da_wait_stats в TaskManager.hpp.
//
// Коротко: одни и те же миллисекунды ожидания означают либо настоящий пузырь, либо честную работу
// кадра, просто записанную в эту зону, -- а лечение у них противоположное. Числа печатаются в конце
// строки DA_GPU как steal <выполнено>/<холостых>.
void da_gpu_set_cull_wait(u32 executed, u32 idle_spins);

// [DA_PORT] Из чего складывается сам расчёт видимости: порталы / статика / динамика, мс.
// Разбор -- в build_subspace (r__dsgraph_build.cpp). Печатается как cull <порталы>/<статика>/<динамика>.
void da_gpu_set_cull_parts(double portals_ms, double statics_ms, double dynamics_ms);

// [DA_PORT] Динамика по частям: сбор из дерева / сортировка по дальности / определение сектора, мс,
// и число объектов. Печатается как dyn <сбор>/<сортировка>/<сектор> xN. Остаток тела цикла --
// это динамика минус три эти числа.
void da_gpu_set_cull_dyn(double collect_ms, double sort_ms, double sector_ms, u32 count, u32 sector_hits);

// [DA_PORT] Тело цикла динамики: программный тест перекрытия и построение списков отрисовки, мс.
// Печатается как body <hom>/<render>. Остаток тела = динамика минус сбор, сортировка, сектор и эти два.
void da_gpu_set_cull_body(double hom_ms, double render_ms);

// [DA_PORT] Внутри renderable_Render: расчёт костей и следов на скелетных моделях, мс, плюс число
// скелетов и листовых визуалов за кадр. Печатается как skel <кости>/<следы> s<скелетов> l<листьев>.
void da_gpu_set_cull_skel(double bones_ms, double wallmarks_ms, u32 skeletons, u32 leafs, u32 exact);

// [DA_PORT] Вторая половина G-буфера по частям: интерфейс с оружием / импосторы / трава, мс.
// Печатается как g2 <hud>/<lods>/<трава>. Повод -- 0.90 мкс на вызов против 0.56 у первой половины.
void da_gpu_set_gbuf2(double hud_ms, double lods_ms, double details_ms);
} // namespace xray::render::RENDER_NAMESPACE
