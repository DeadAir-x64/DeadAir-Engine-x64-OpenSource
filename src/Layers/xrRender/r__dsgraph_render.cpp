#include "stdafx.h"

#include "xrCommon/xr_set.h" // [DA_PORT] da_static_dedup_exact
#include "xrEngine/IRenderable.h"
#include "xrEngine/CustomHUD.h"

#include <functional>

#include "FBasicVisual.h"
#include "FTreeVisual.h" // [DA_PORT] пакетная отрисовка деревьев
#include "FVisual.h" // [DA_PORT] тег-based приведение к IRender_Mesh, см. da_as_mesh
#include "SkeletonCustom.h"
#include "FLOD.h"

extern ENGINE_API float psHUD_FOV;
extern ENGINE_API float g_hud_fov_current; // [DA_PORT] nearwall: == psHUD_FOV unless modulated

namespace xray::render::RENDER_NAMESPACE
{
using namespace R_dsgraph;

// [DA_PORT] Урезанный поток вершин годится теневому проходу только тогда, когда шейдер не читает
// развёртку. Альфа-тестовым материалам она нужна, и без неё они выпадают из теневой карты целиком:
// разбор и признак отказа — у da_vs_needs_uv в dx11R_Backend_Runtime.h.
ICF bool da_use_fast_geo(CBackend& cmd_list, u32 phase)
{
    if (phase != CRender::PHASE_SMAP)
        return false;
#if defined(USE_DX11)
    return !cmd_list.da_vs_needs_uv();
#else
    (void)cmd_list;
    return true;
#endif
}

extern float r_ssaHZBvsTEX;
extern float r_ssaGLOD_start, r_ssaGLOD_end;

ICF float calcLOD(float ssa /*fDistSq*/, float /*R*/)
{
    return _sqrt(clampr((ssa - r_ssaGLOD_end) / (r_ssaGLOD_start - r_ssaGLOD_end), 0.f, 1.f));
}

#ifdef USE_DX11
// [DA_PORT] --- Пакетная отрисовка деревьев ------------------------------------------------------
//
// Деревьев на уровне тысячи, и каждое до сих пор рисовалось своим вызовом. Здесь одинаковые
// собираются в пачки до 64 штук: их матрицы и освещение уезжают в константный буфер, и вся пачка
// рисуется одним DrawIndexedInstanced. То, что в пачку не попало, возвращается обычному пути.
//
// ⚠️ Отключается САМО, без ручки: если в текущем шейдере нет констант tree_instance_*, функция
// возвращает false и список остаётся нетронутым. Так что игрок со старыми шейдерами просто рисует
// по-старому, а не получает чёрный экран.
// [DA_PORT] Вне безымянного пространства имён: ручку регистрирует консоль (xrRender_console.cpp).
int ps_da_tree_batch = 1;

// [DA_PORT] Объявлен здесь (определение и регистрация — ниже, у instance-probe), чтобы
// da_render_tree_batches мог свериться с ним: не заводить второй флаг под то же самое.
extern int ps_da_instance_probe;

namespace
{
constexpr u32 da_tree_batch_capacity = 64; // столько же в tree_instance.h — расходиться нельзя

struct da_tree_batch_item
{
    FTreeVisual* visual{};
    FTreeVisualInstancedDraw draw{};
    float lod{};
    u32 lod_bucket{};
    size_t original_order{};
};

struct da_tree_batch_segment
{
    size_t begin{};
    u32 count{};
};

struct da_tree_batch_scratch
{
    xr_vector<da_tree_batch_item> items;
    xr_vector<da_tree_batch_segment> segments;
    xr_vector<u8> batched;
};

FTreeVisual* da_as_tree(dxRender_Visual* visual)
{
    switch (visual->Type)
    {
    case MT_TREE_ST:
    case MT_TREE_PM: return static_cast<FTreeVisual*>(visual);
    default: return nullptr;
    }
}

// [DA_PORT] Тот же приём, что у da_as_tree, только на весь IRender_Mesh — только Fvisual и
// FTreeVisual реализуют его (проверено грепом `public IRender_Mesh` по xrRender). Замена
// dynamic_cast<IRender_Mesh*>: A/B на r__static_sort_geom показал сортировку ХУЖЕ на ~0.2 мс
// именно из-за него — RTTI в компараторе, вызываемом O(n log n) раз за кадр, съедает саму
// экономию. Тег читается за одно сравнение, без похода в таблицы типов.
IRender_Mesh* da_as_mesh(dxRender_Visual* visual)
{
    switch (visual->Type)
    {
    case MT_NORMAL: return static_cast<Fvisual*>(visual);
    case MT_TREE_ST:
    case MT_TREE_PM: return static_cast<FTreeVisual*>(visual);
    default: return nullptr;
    }
}

// [DA_PORT] Уровень детализации огрубляется до семи ступеней. Иначе в пачку попадали бы только
// деревья с точь-в-точь совпавшим LOD, то есть почти никогда: величина непрерывная.
u32 da_tree_lod_bucket(float lod)
{
    return u32(clampr<float>(ceil(lod * lod * lod * lod * lod * 8.0f), 1, 7));
}

bool da_same_tree_geometry(const da_tree_batch_item& a, const da_tree_batch_item& b)
{
    const SGeometry& ga = *a.draw.geometry;
    const SGeometry& gb = *b.draw.geometry;
    return ga.vb == gb.vb && ga.ib == gb.ib && ga.dcl._get() == gb.dcl._get() &&
        ga.vb_stride == gb.vb_stride;
}

bool da_same_tree_draw(const da_tree_batch_item& a, const da_tree_batch_item& b)
{
    return da_same_tree_geometry(a, b) && a.draw.base_vertex == b.draw.base_vertex &&
        a.draw.vertex_count == b.draw.vertex_count && a.draw.start_index == b.draw.start_index &&
        a.draw.primitive_count == b.draw.primitive_count && a.lod_bucket == b.lod_bucket;
}

bool da_less_tree_draw(const da_tree_batch_item& a, const da_tree_batch_item& b)
{
    const SGeometry& ga = *a.draw.geometry;
    const SGeometry& gb = *b.draw.geometry;
    if (ga.vb != gb.vb)
        return std::less<VertexBufferHandle>{}(ga.vb, gb.vb);
    if (ga.ib != gb.ib)
        return std::less<IndexBufferHandle>{}(ga.ib, gb.ib);
    if (ga.dcl._get() != gb.dcl._get())
        return std::less<SDeclaration*>{}(ga.dcl._get(), gb.dcl._get());
    if (ga.vb_stride != gb.vb_stride)
        return ga.vb_stride < gb.vb_stride;
    if (a.draw.base_vertex != b.draw.base_vertex)
        return a.draw.base_vertex < b.draw.base_vertex;
    if (a.draw.vertex_count != b.draw.vertex_count)
        return a.draw.vertex_count < b.draw.vertex_count;
    if (a.draw.start_index != b.draw.start_index)
        return a.draw.start_index < b.draw.start_index;
    if (a.draw.primitive_count != b.draw.primitive_count)
        return a.draw.primitive_count < b.draw.primitive_count;
    if (a.lod_bucket != b.lod_bucket)
        return a.lod_bucket < b.lod_bucket;
    return a.original_order < b.original_order; // порядок отрисовки сохраняем при прочих равных
}

// [DA_PORT] Проверяем не только наличие констант, но и их вид. Шейдер мог собраться со СВОИМИ
// tree_instance_* другого размера или в другом регистре — тогда мы бы писали мимо и получили
// разъехавшийся лес без единого сообщения.
bool da_tree_instance_constants(CBackend& cmd_list, R_constant*& data, R_constant*& control)
{
    data = cmd_list.get_c("tree_instance_data")._get();
    control = cmd_list.get_c("tree_instance_control")._get();
    if (!data || !control)
        return false;
    if (!(data->destination & RC_dest_vertex) || !(control->destination & RC_dest_vertex))
        return false;

    const bool data_class_ok = data->vs.cls == RC_1x4 || data->vs.cls == RC_1x4a;
    if (data->type != RC_float || !data_class_ok || control->type != RC_float ||
        control->vs.cls != RC_1x4)
        return false;

    const u32 data_buffer = data->destination & RC_dest_vertex_cb_index_mask;
    const u32 control_buffer = control->destination & RC_dest_vertex_cb_index_mask;
    return data_buffer != control_buffer && !data->vs.index && !control->vs.index;
}

// [DA_PORT] Одна строка за запуск на каждую причину. Без неё «пакетирование не сработало»
// неотличимо от «деревьев в кадре не было», а это разные беды с разным лечением.
void da_tree_batch_note(pcstr reason)
{
    static std::atomic<u32> reported{ 0 };
    static pcstr seen[4]{};
    const u32 slot = reported.load(std::memory_order_relaxed);
    for (u32 i = 0; i < slot && i < 4; ++i)
        if (seen[i] == reason)
            return;
    if (slot >= 4)
        return;
    seen[slot] = reason;
    reported.store(slot + 1, std::memory_order_relaxed);
    Msg("* [DA_PORT] деревья пачками: %s", reason);
}

bool da_render_tree_batches(CBackend& cmd_list, mapNormalItems& items)
{
    // [DA_PORT] Ручка для чистого теста: r__tree_batch 0 возвращает одиночную отрисовку.
    //
    // Пакетирование не просто рисует иначе — оно СОРТИРУЕТ список и УДАЛЯЕТ из него всё, что
    // нарисовало пачками. Любая правка, меняющая состав списка отрисовки, обязана иметь выключатель:
    // иначе «пропал объект под определённым углом» приходится разбирать рассуждением, а не заменой
    // одной величины. Ровно так же сделано у программного отсечения (r__hom).
    if (!ps_da_tree_batch)
        return false;

    R_constant* instance_data{};
    R_constant* instance_control{};
    if (!da_tree_instance_constants(cmd_list, instance_data, instance_control))
    {
        // [DA_PORT] instance-probe (r__instance_probe 1): сколько ИМЕННО деревьев застряло в этом
        // проходе без tree_instance_* — иначе неясно, попутный это шейдер (без единого дерева) или
        // тот самый, что даёт TREE_ST/TREE_PM максимумом в 176 повторов геометрии.
        if (ps_da_instance_probe)
        {
            u32 tree_count = 0;
            for (const _NormalItem& item : items)
                if (da_as_tree(item.pVisual))
                    ++tree_count;
            if (tree_count)
            {
                static std::atomic<bool> reported{false};
                if (!reported.exchange(true))
                    Msg("* [DA_PORT] instance-probe: непакетированных деревьев в проходе без "
                        "tree_instance_*: %u из %u объектов",
                        tree_count, u32(items.size()));
            }
        }
        da_tree_batch_note("в шейдере нет констант tree_instance_* — рисуем по-старому");
        return false;
    }

    // Шейдер инстансный, но пачки может не быть — тогда он обязан рисовать по-старому.
    cmd_list.set_c(instance_control, 0.f, 0.f, 0.f, 0.f);
    if (items.size() < 2)
    {
        da_tree_batch_note("в проходе меньше двух объектов");
        return false;
    }

    static thread_local da_tree_batch_scratch scratch;
    xr_vector<da_tree_batch_item>& trees = scratch.items;
    trees.clear();
    if (trees.capacity() < items.size())
        trees.reserve(items.size());

    for (size_t index = 0; index < items.size(); ++index)
    {
        const _NormalItem& item = items[index];
        FTreeVisual* tree = da_as_tree(item.pVisual);
        if (!tree)
            continue;

        const float lod = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
        FTreeVisualInstancedDraw draw;
        if (!tree->GetInstancedDraw(lod, draw))
            continue;

        trees.push_back(da_tree_batch_item{ tree, draw, lod, da_tree_lod_bucket(lod), index });
    }

    if (trees.size() < 2)
    {
        da_tree_batch_note("деревьев в проходе меньше двух");
        return false;
    }

    std::sort(trees.begin(), trees.end(), da_less_tree_draw);

    // Режем на отрезки: подряд идущие одинаковые отрисовки, не длиннее вместимости буфера.
    xr_vector<da_tree_batch_segment>& segments = scratch.segments;
    segments.clear();
    for (size_t group_begin = 0; group_begin < trees.size();)
    {
        size_t group_end = group_begin + 1;
        while (group_end < trees.size() && da_same_tree_draw(trees[group_begin], trees[group_end]))
            ++group_end;

        size_t batch_begin = group_begin;
        while (group_end - batch_begin > 1)
        {
            u32 count = u32(std::min<size_t>(da_tree_batch_capacity, group_end - batch_begin));

            // Хвост в одно дерево не оставляем: пусть последняя пачка будет на одно короче,
            // зато обе нарисуются инстансами, а не «пачка и ещё один вызов».
            if (group_end - batch_begin - count == 1)
                --count;

            segments.push_back(da_tree_batch_segment{ batch_begin, count });
            batch_begin += count;
        }
        group_begin = group_end;
    }

    if (segments.empty())
    {
        da_tree_batch_note("одинаковых деревьев не нашлось — пачку собрать не из чего");
        return false;
    }

    static const shared_str instance_data_name = "tree_instance_data";
    void* probe{};
    cmd_list.get_ConstantDirect(
        instance_data_name, da_tree_batch_capacity * sizeof(FTreeVisualInstanceData), &probe, nullptr, nullptr);
    if (!probe)
    {
        da_tree_batch_note("константный буфер меньше пачки — уходим на обычный путь");
        return false;
    }

    xr_vector<u8>& batched = scratch.batched;
    batched.assign(items.size(), false);
    FTreeVisual::SetupInstancedGlobals(cmd_list);

    // Страница — столько отрезков, сколько влезает в один буфер: заполняем, потом рисуем.
    for (size_t page_begin = 0; page_begin < segments.size();)
    {
        size_t page_end = page_begin;
        u32 page_instances{};
        while (page_end < segments.size() &&
            page_instances + segments[page_end].count <= da_tree_batch_capacity)
        {
            page_instances += segments[page_end++].count;
        }

        void* vertex_data{};
        cmd_list.get_ConstantDirect(
            instance_data_name, page_instances * sizeof(FTreeVisualInstanceData), &vertex_data, nullptr, nullptr);
        if (!vertex_data)
            return false;

        auto* storage = static_cast<FTreeVisualInstanceData*>(vertex_data);
        u32 offset{};
        for (size_t s = page_begin; s < page_end; ++s)
        {
            const da_tree_batch_segment& segment = segments[s];
            for (u32 i = 0; i < segment.count; ++i)
            {
                da_tree_batch_item& item = trees[segment.begin + i];
                item.visual->FillInstanceData(cmd_list, storage[offset + i]);
                batched[item.original_order] = true;
            }
            offset += segment.count;
        }

        offset = 0;
        for (size_t s = page_begin; s < page_end; ++s)
        {
            const da_tree_batch_segment& segment = segments[s];
            const da_tree_batch_item& first = trees[segment.begin];
            cmd_list.set_c(instance_control, 1.f, float(offset), 0.f, 0.f);
            cmd_list.LOD.set_LOD(first.lod);
            cmd_list.set_Geometry(first.draw.geometry);
            cmd_list.RenderInstanced(D3DPT_TRIANGLELIST, first.draw.base_vertex, 0,
                first.draw.vertex_count, first.draw.start_index, first.draw.primitive_count, segment.count);
            cmd_list.stat.r.s_flora.add(first.draw.vertex_count * segment.count);
            offset += segment.count;
        }

        page_begin = page_end;
    }

    // Обратно в одиночный режим — следующий проход может рисовать не деревья.
    cmd_list.set_c(instance_control, 0.f, 0.f, 0.f, 0.f);

    // [DA_PORT] Одна строка за запуск — при первом же сработавшем пакетировании.
    //
    // Без неё работу этой функции нельзя отличить от её отсутствия: картинка обязана выглядеть
    // одинаково в обоих случаях, а молча не сработать она может по десятку причин — от старого
    // шейдера до вырезанной компилятором константы. Строки нет в логе — значит деревья рисуются
    // по-старому, и это надо знать сразу, а не гадать по кадрам в секунду.
    // Отдельно по проходам: основной и теневой ходят сюда независимо, и знать надо про оба.
    // Теневой прогоняется по разу на каждый каскад и на каждый теневой источник, поэтому экономия
    // там умножается - и молчание про него означало бы, что половина работы не проверена.
    const u32 phase = RImplementation.get_context(cmd_list.context_id).o.phase;
    static std::atomic<u32> reported_phases{ 0 };
    const u32 phase_bit = 1u << (phase & 31u);
    if (!(reported_phases.fetch_or(phase_bit) & phase_bit))
    {
        u32 instanced = 0;
        for (const da_tree_batch_segment& segment : segments)
            instanced += segment.count;
        Msg("* [DA_PORT] деревья пачками (%s): %u шт. за %u вызовов вместо %u, одиночно осталось %u",
            phase == CRender::PHASE_SMAP ? "теневой проход" : "основной проход",
            instanced, u32(segments.size()), instanced, u32(items.size()) - instanced);
    }

    size_t write = 0;
    for (size_t read = 0; read < items.size(); ++read)
    {
        if (!batched[read])
            items[write++] = items[read];
    }
    items.resize(write);
    return true;
}
} // namespace
#endif

// [DA_PORT] --- Зонд: сколько объектов в кадре делят ОДИН вершинный/индексный буфер (rm_geom) ------
//
// Вопрос не «работает ли инстансинг у деревьев» (работает, см. выше), а есть ли смысл делать его для
// остальной статики. У обычных объектов уровня (секция NORMAL ниже) вершины уже запечены в мировых
// координатах SDK-компилятором — `set_xform_world(Fidentity)` ставится ОДИН раз на весь проход, а не
// на объект, — так что даже повтор геометрии там инстансингом не собрать без переработки экспорта
// уровня.
//
// ⚠️ Первая версия зонда считала по указателю на `dxRender_Visual` и стабильно показывала 0 дублей
// на тысячах объектов — это была ложная цифра: каждый заспавненный объект получает СВОЮ обёртку
// (`Copy()`), даже когда её `rm_geom` смотрит в один и тот же общий буфер на видеокарте (ровно так
// уже сделано для деревьев — см. `da_same_tree_geometry` выше, она сравнивает не `FTreeVisual*`, а
// `vb`/`ib`/`dcl`). Здесь то же самое: ключ — сырой указатель на разделяемый `rm_geom`, а не на
// обёртку.
//
// Временный инструмент, не влияет на отрисовку. Включается `r__instance_probe 1`, печатает в лог не
// чаще раза в секунду.
int ps_da_instance_probe = 0;

// [DA_PORT] Регистрируется консолью (xrRender_console.cpp) как r__static_sort_geom. Определение
// функции — ниже, у instance-probe: ей нужен da_instance_probe_key.
int ps_da_static_sort_geom = 0;

// [DA_PORT] Регистрируется консолью как r__static_dedup. Определение da_static_dedup_exact — ниже.
// ⭐ Включён по умолчанию (25.08) — подтверждено дважды в игре: ~16 точных дублей на каждый проход
// отрисовки, стабильно, DIP 2131→1921 (-10%). Картинка не меняется — снимаются буквально повторные
// вставки того же диапазона геометрии в список, а не что-то видимое.
int ps_da_static_dedup = 1;

// [DA_PORT] Регистрируется консолью как r__matrix_dedup. Определение da_matrix_dedup_exact — ниже.
int ps_da_matrix_dedup = 0;

namespace
{
// [DA_PORT] Ключ идентичности геометрии: (rm_geom, vBase, iBase) у объектов с IRender_Mesh, иначе —
// указатель на саму обёртку. Указателя на rm_geom одного НЕ ХВАТАЕТ: SDK мог упаковать НЕСКОЛЬКО
// уже готовых (разных) кусков в один общий VB/IB и развести их смещением — тогда общий rm_geom это
// общий КОНТЕЙНЕР, а не общая отрисовка, ровно как обёртка была общим враньём в первой версии зонда.
// vBase/iBase — тот же individual-offset смысл, что различал деревья в пачке (draw.base_vertex).
struct da_instance_probe_key_t
{
    void* geom{};
    u32 vBase{};
    u32 iBase{};
    bool operator<(const da_instance_probe_key_t& other) const
    {
        if (geom != other.geom)
            return geom < other.geom;
        if (vBase != other.vBase)
            return vBase < other.vBase;
        return iBase < other.iBase;
    }
};

da_instance_probe_key_t da_instance_probe_key(dxRender_Visual* visual, u32& out_vertex_count)
{
    out_vertex_count = 0;
    if (auto* mesh = da_as_mesh(visual))
    {
        out_vertex_count = mesh->vCount;
        if (mesh->rm_geom)
            return {&*mesh->rm_geom, mesh->vBase, mesh->iBase};
    }
    return {visual, 0, 0};
}

struct da_instance_probe_sample
{
    u32 count{};
    u32 type{u32(-1)};
    u32 vertex_count{};
};

struct da_instance_probe_state
{
    xr_map<da_instance_probe_key_t, da_instance_probe_sample> normal_counts;
    xr_map<da_instance_probe_key_t, da_instance_probe_sample> matrix_counts;
    u32 normal_items{};
    u32 matrix_items{};
    u32 frame{u32(-1)};
    float last_report{-1000.f};
};

da_instance_probe_state& da_instance_probe_get()
{
    static da_instance_probe_state state;
    return state;
}

// [DA_PORT] Человеческое имя по MT_* (xrCore/FMesh.hpp) — иначе в логе голое число.
pcstr da_instance_probe_type_name(u32 type)
{
    switch (type)
    {
    case 0: return "NORMAL(Fvisual)";
    case 1: return "HIERRARHY";
    case 2: return "PROGRESSIVE";
    case 3: return "SKELETON_ANIM";
    case 4: return "SKELETON_GEOMDEF_PM";
    case 5: return "SKELETON_GEOMDEF_ST";
    case 6: return "LOD";
    case 7: return "TREE_ST";
    case 8: return "PARTICLE_EFFECT";
    case 9: return "PARTICLE_GROUP";
    case 10: return "SKELETON_RIGID";
    case 11: return "TREE_PM";
    default: return "?";
    }
}

void da_instance_probe_tally(const xr_map<da_instance_probe_key_t, da_instance_probe_sample>& counts, u32& groups,
    u32& removable, const da_instance_probe_sample*& biggest)
{
    groups = 0;
    removable = 0;
    biggest = nullptr;
    for (const auto& kv : counts)
    {
        if (kv.second.count > 1)
        {
            ++groups;
            removable += kv.second.count - 1;
            if (!biggest || kv.second.count > biggest->count)
                biggest = &kv.second;
        }
    }
}

void da_instance_probe_report(const da_instance_probe_state& state)
{
    u32 n_groups, n_removable;
    const da_instance_probe_sample* n_biggest;
    da_instance_probe_tally(state.normal_counts, n_groups, n_removable, n_biggest);
    u32 m_groups, m_removable;
    const da_instance_probe_sample* m_biggest;
    da_instance_probe_tally(state.matrix_counts, m_groups, m_removable, m_biggest);

    Msg("* [DA_PORT] instance-probe СТАТИКА (NORMAL, координаты уже мировые - справочно): "
        "%u объектов, %u геометрий, %u повторяются (макс %u раз, тип %s, вершин %u), слияние убрало бы %u вызовов",
        state.normal_items, u32(state.normal_counts.size()), n_groups,
        n_biggest ? n_biggest->count : 0,
        n_biggest ? da_instance_probe_type_name(n_biggest->type) : "-",
        n_biggest ? n_biggest->vertex_count : 0, n_removable);
    Msg("* [DA_PORT] instance-probe ДИНАМИКА (MATRIX, кандидат на инстансинг): "
        "%u объектов, %u геометрий, %u повторяются (макс %u раз, тип %s, вершин %u), слияние убрало бы %u вызовов",
        state.matrix_items, u32(state.matrix_counts.size()), m_groups,
        m_biggest ? m_biggest->count : 0,
        m_biggest ? da_instance_probe_type_name(m_biggest->type) : "-",
        m_biggest ? m_biggest->vertex_count : 0, m_removable);
}

// [DA_PORT] instance-probe: слитность vBase внутри страницы (r__instance_probe 1).
//
// Вопрос перед тем, как писать слияние статики в один DrawIndexed: у объектов одного материала на
// одной общей странице геометрии (getVB(ID) — пул на весь уровень, см. CRender::getVB) диапазоны
// vBase идут ПОДРЯД или вразброс? Если подряд — один общий вызов может накрыть всю страницу без
// перекраивания индексного буфера. Если вразброс — сшивать нечего без отдельной подсистемы
// (собственный объединённый IB), тема закрывается прямо здесь, без месяца работы вслепую.
//
// Деревья пропускаем: у них своя семантика (общая форма, разная матрица), не общий диапазон.
void da_instance_probe_contiguity(mapNormalItems& items)
{
    static std::atomic<bool> reported{false};
    if (reported.load(std::memory_order_relaxed))
        return;

    struct Entry
    {
        void* geom;
        u32 vBase;
        u32 vCount;
    };
    static thread_local xr_vector<Entry> entries;
    entries.clear();

    for (const _NormalItem& item : items)
    {
        if (da_as_tree(item.pVisual))
            continue;
        if (auto* mesh = da_as_mesh(item.pVisual))
            if (mesh->rm_geom)
                entries.push_back({&*mesh->rm_geom, mesh->vBase, mesh->vCount});
    }
    if (entries.size() < 10)
        return; // мал прок с горстки объектов — подождём прохода пожирнее

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.geom != b.geom)
            return a.geom < b.geom;
        return a.vBase < b.vBase;
    });

    size_t best_begin = 0, best_count = 0, group_begin = 0;
    for (size_t i = 1; i <= entries.size(); ++i)
    {
        if (i == entries.size() || entries[i].geom != entries[group_begin].geom)
        {
            if (i - group_begin > best_count)
            {
                best_count = i - group_begin;
                best_begin = group_begin;
            }
            group_begin = i;
        }
    }
    if (best_count < 10)
        return;

    u32 vsum = 0;
    u32 gaps = 0;
    u32 expected = entries[best_begin].vBase;
    for (size_t i = best_begin; i < best_begin + best_count; ++i)
    {
        vsum += entries[i].vCount;
        if (entries[i].vBase != expected)
            ++gaps;
        expected = entries[i].vBase + entries[i].vCount;
    }
    const Entry& last = entries[best_begin + best_count - 1];
    const u32 span = (last.vBase + last.vCount) - entries[best_begin].vBase;

    reported.store(true, std::memory_order_relaxed);
    Msg("* [DA_PORT] instance-probe: слитность страницы — %u объектов одного материала, %u из них с "
        "разрывом (не встык с предыдущим), сумма вершин %u, охват диапазона %u (заполнение %.0f%%)",
        u32(best_count), gaps, vsum, span, span ? 100.0 * vsum / span : 0.0);
}

// [DA_PORT] r__static_dedup 1 — не инстансинг, а устранение ТОЧНЫХ дублей в статике (NORMAL).
//
// У статики трансформ запечён в вершины (`set_xform_world(Fidentity)` ставится один раз на весь
// проход, см. render_graph) — то есть разных позиций для инстансинга здесь нет физически. Но если
// два элемента списка ссылаются на ОДИН И ТОТ ЖЕ диапазон геометрии (страница + vBase + iBase —
// da_instance_probe_key_t), это не «два похожих объекта», это буквально одни и те же треугольники в
// одном и том же месте, вставленные в список отрисовки дважды (например, объект у границы портала
// попал в видимость сразу с двух путей обхода секторов). Рисовать такое второй раз — чистая трата,
// без всякого риска для картинки: то, что осталось, покрывает ту же геометрию, что и удалённое.
//
// Дважды измерено сегодня (r__instance_probe), что настоящих повторов немного и в основном это
// остатки пакетирования деревьев — здесь фильтруются ТОЛЬКО точные совпадения диапазона, ничего не
// сливается «примерно». Пустая находка тоже честный результат, а не повод молчать — считаем и
// печатаем, сколько реально снято.
void da_static_dedup_exact(mapNormalItems& items)
{
    static thread_local xr_set<da_instance_probe_key_t> seen;
    seen.clear();

    size_t write = 0;
    u32 removed = 0;
    for (size_t read = 0; read < items.size(); ++read)
    {
        dxRender_Visual* V = items[read].pVisual;

        // ⛔ [DA_PORT] Снимать дубли можно ТОЛЬКО там, где положение запечено в вершины.
        //
        // Первая версия этого не проверяла и сравнивала один диапазон геометрии — а у деревьев и
        // кустов (MT_TREE_ST/MT_TREE_PM) экземпляры одного вида ДЕЛЯТ геометрию, различаясь лишь
        // матрицей xform, своей у каждого. Дедуп выбрасывал все копии кроме одной, а какая уцелеет
        // — решала сортировка по площади на экране, то есть камера. В игре это выглядело как
        // «кусты и деревья то появляются, то пропадают при повороте и приближении».
        //
        // Оставляем обычную статику, у которой мировая матрица единичная: там совпадение диапазона
        // действительно означает одни и те же треугольники в одном и том же месте.
        if (!V || (V->Type != MT_NORMAL && V->Type != MT_PROGRESSIVE))
        {
            items[write++] = items[read];
            continue;
        }

        u32 vcount{};
        da_instance_probe_key_t key = da_instance_probe_key(V, vcount);
        if (seen.insert(key).second)
            items[write++] = items[read];
        else
            ++removed;
    }
    items.resize(write);

    if (removed)
    {
        static std::atomic<u32> total_removed{0};
        u32 running = total_removed.fetch_add(removed, std::memory_order_relaxed) + removed;
        static std::atomic<float> last_report{-1000.f};
        float prev = last_report.load(std::memory_order_relaxed);
        if (Device.fTimeGlobal - prev > 1.f &&
            last_report.compare_exchange_strong(prev, Device.fTimeGlobal, std::memory_order_relaxed))
        {
            Msg("* [DA_PORT] static-dedup: снято %u точных дублей за проход (всего с включения: %u)",
                removed, running);
        }
    }
}

// [DA_PORT] r__matrix_dedup 1 — тот же дедуп, что у статики, только для MATRIX (динамика). Ключ —
// пара (объект, визуал), а не геометрия: у динамики матрица честно СВОЯ на каждый экземпляр (не
// запечена в вершины), поэтому совпадение геометрии между РАЗНЫМИ предметами — это норма, не дубль.
// А вот если один и тот же (IRenderable*, dxRender_Visual*) встретился в списке дважды — это
// однозначно повторная вставка ОДНОГО И ТОГО ЖЕ объекта (маркер `vis.marker[context_id]` защищает
// только внутри одного обхода секторов — see insert_static/insert_dynamic — а не через весь кадр).
void da_matrix_dedup_exact(mapMatrixItems& items)
{
    struct Key
    {
        IRenderable* object;
        dxRender_Visual* visual;
        bool operator<(const Key& o) const
        {
            if (object != o.object)
                return object < o.object;
            return visual < o.visual;
        }
    };
    static thread_local xr_set<Key> seen;
    seen.clear();

    size_t write = 0;
    u32 removed = 0;
    for (size_t read = 0; read < items.size(); ++read)
    {
        if (seen.insert({items[read].pObject, items[read].pVisual}).second)
            items[write++] = items[read];
        else
            ++removed;
    }
    items.resize(write);

    if (removed)
    {
        static std::atomic<u32> total_removed{0};
        u32 running = total_removed.fetch_add(removed, std::memory_order_relaxed) + removed;
        static std::atomic<float> last_report{-1000.f};
        float prev = last_report.load(std::memory_order_relaxed);
        if (Device.fTimeGlobal - prev > 1.f &&
            last_report.compare_exchange_strong(prev, Device.fTimeGlobal, std::memory_order_relaxed))
        {
            Msg("* [DA_PORT] matrix-dedup: снято %u точных дублей за проход (всего с включения: %u)",
                removed, running);
        }
    }
}

// [DA_PORT] Досортировка статики внутри шейдер-группы по странице геометрии (r__static_sort_geom).
//
// Материал/текстура внутри группы УЖЕ одинаковы (это и есть ключ mapNormalPasses), а вот из какой
// страницы общего пула вершин (getVB(ID)) взят объект — нет: список идёт по SSA, соседние вызовы
// дёргают то одну страницу, то другую. На DX11 смена привязки буфера — часть валидации состояния на
// каждый вызов (источник: gamedev.net/D3D11 state validation), а не сама отрисовка. Слияние в один
// буфер (как у деревьев) здесь не выйдет — сегодняшний замер показал 15% заполнения диапазона,
// чужая геометрия вперемешку. Но кластеризовать ВЫЗОВЫ по странице — можно и дёшево, без нового
// буфера: stable_sort сохраняет исходный порядок по SSA внутри каждой страницы, то есть съедает
// только сортировку МЕЖДУ разными страницами, а не сам front-to-back порядок.
void da_static_sort_by_geometry_page(mapNormalItems& items)
{
    std::stable_sort(items.begin(), items.end(), [](const _NormalItem& a, const _NormalItem& b) {
        u32 va{}, vb{};
        return da_instance_probe_key(a.pVisual, va).geom < da_instance_probe_key(b.pVisual, vb).geom;
    });
}
} // namespace

template <class T>
bool cmp_ssa(const T &lhs, const T &rhs)
{
    return lhs.ssa > rhs.ssa;
}

// Sorting by SSA and changes minimizations
// [DA_PORT] This used to be `if (equal) return false; return left.ssa >= right.ssa;`, which is not a
// strict weak ordering: for two different passes with the same ssa it reported BOTH cmp(a,b) and
// cmp(b,a) as true. std::sort is undefined with such a comparator — introsort's partition loop has no
// bounds check and relies on the comparator to stop it, so once enough passes share an ssa value it
// walks off the end of the vector and dereferences garbage as an SPass*. That crashed inside
// SPass::equal (stack: CRender::Render -> render_graph -> std::sort -> introsort -> cmp_pass ->
// SPass::equal), and only after tens of thousands of frames, because it needs the data to line up.
// Ordering by ssa with the pass pointer as tie-break is a proper strict weak ordering and keeps the
// original intent: front-to-back by ssa, identical passes (deduplicated, so same pointer) adjacent.
// [DA_PORT] r__pass_sort_ctable 1: группировать проходы по ТАБЛИЦЕ КОНСТАНТ.
//
// Замер: настройка прохода — 0.79 мс на кадр, и 1.15 из них съедает set_Constants, где почти
// половина уходит на перетасовку 84 умных указателей с АТОМАРНЫМ счётчиком ссылок. Но у
// set_Constants есть ранний выход `if (ctable == C) return`, который сейчас почти не срабатывает:
// проходы отсортированы по площади на экране, и таблицы чередуются. При этом разных шейдеров 20 на
// 313 проходов — то есть таблиц КРАТНО меньше, чем проходов. Сгруппировав по таблице, мы не
// удешевляем вызов, а убираем большую его часть целиком.
//
// ⚠️ Порядок «спереди назад» внутри таблицы СОХРАНЁН: он помогает видеокарте отбрасывать
// перекрытые пиксели заранее. Меняется только порядок МЕЖДУ таблицами, и это компромисс, который
// обязан подтвердить замер — отсюда выключатель.
extern int ps_da_pass_sort_ctable;

template <typename T>
bool cmp_pass(const T& left, const T& right)
{
    if (ps_da_pass_sort_ctable)
    {
        const void* lc = left->first ? left->first->constants._get() : nullptr;
        const void* rc = right->first ? right->first->constants._get() : nullptr;
        if (lc != rc)
            return std::less<const void*>{}(lc, rc);
    }
    if (left->second.ssa != right->second.ssa)
        return left->second.ssa > right->second.ssa;
    return left->first < right->first;
}

// [DA_PORT] Разбивка цикла отрисовки графа изнутри (r__graph_prof 1, печать раз в секунду).
//
// Зачем: счётчик prim (BasicStats.Primitives) меряет ВЕСЬ этот цикл одним числом — 1.73 мс, — и по
// нему нельзя понять, где они лежат. А из 4.25 мс кадра сумма всех именованных счётчиков даёт лишь
// 2.31: почти половина времени рендера не учтена НИЧЕМ (свет и тени в R2/R4 не заполняются вовсе,
// см. пометку в D3DXRenderBase.cpp). Прежде чем что-то чинить, надо знать, что чинить: сегодняшний
// день стоил отката ровно потому, что направление выбиралось по догадке, а не по замеру.
//
// Мерим ФАЗАМИ, а не объектами: таймер на каждый объект исказил бы то, что измеряет.
int ps_da_graph_prof = 0;
// [DA_PORT] Включено по умолчанию: замер без прибора дал render 3.79 -> 3.66 мс (-3.4%), GPU не
// изменился (3.39 -> 3.36), картинка проверена в игре. Выключатель оставлен для разбора.
int ps_da_pass_sort_ctable = 1; // [DA_PORT] см. cmp_pass
// [DA_PORT] Копилки для разбивки set_Pass — заполняются в R_Backend_Runtime.h, см. там пояснение.
float g_da_pass_state = 0.f, g_da_pass_shaders = 0.f, g_da_pass_const = 0.f, g_da_pass_tex = 0.f,
      g_da_pass_mat = 0.f;
// [DA_PORT] Разбивка самого set_Constants — заполняется в dx11R_Backend_Runtime.h.
float g_da_const_unmap = 0.f, g_da_const_shuffle = 0.f, g_da_const_bind = 0.f, g_da_const_loaders = 0.f;
// [DA_PORT] Ожидание отсечения (HOM) — см. r2_R_calculate.cpp.
float g_da_ms_hom_wait = 0.f;
namespace
{
struct da_graph_prof
{
    float sort_passes{}, sort_items{}, setup{}, setup_pass{}, setup_lmat{}, draw{}, matrix{};
    float maps{}, whole{}; // [DA_PORT] обслуживание карт проходов и весь цикл целиком
    u32 passes{}, items{}, mat_items{};
    float last_report{-1000.f};
};
da_graph_prof& da_graph_prof_get()
{
    static da_graph_prof p;
    return p;
}
} // namespace

void R_dsgraph_structure::render_graph(u32 _priority, da_graph_part da_part, bool da_keep)
{
    PIX_EVENT_CTX(cmd_list, dsgraph_render_graph);
    RImplementation.BasicStats.Primitives.Begin(); // XXX: Refactor a bit later

    CTimer da_whole;
    if (ps_da_graph_prof)
        da_whole.Start();

    // [DA_PORT] instance-probe: печатаем прошлый кадр и обнуляем счётчики на новый. Только для
    // основного цветового прохода — теневые каскады и лампы считать не даём, иначе цифры смешают
    // разные проходы одного кадра. Throttled: last_report переживает сброс состояния.
    if (ps_da_instance_probe && o.phase == CRender::PHASE_NORMAL)
    {
        da_instance_probe_state& probe = da_instance_probe_get();
        if (probe.frame != Device.dwFrame)
        {
            if (probe.frame != u32(-1) && Device.fTimeGlobal - probe.last_report > 1.f)
            {
                da_instance_probe_report(probe);
                probe.last_report = Device.fTimeGlobal;
            }
            const float keep_last_report = probe.last_report;
            probe = da_instance_probe_state{};
            probe.frame = Device.dwFrame;
            probe.last_report = keep_last_report;
        }
    }

    cmd_list.set_xform_world(Fidentity);

    // **************************************************** NORMAL
    // Perform sorting based on ScreenSpaceArea
    // Sorting by SSA and changes minimizations
    // Render several passes
    {
        ZoneScopedN("dsgraph_render_static");
        PIX_EVENT_CTX(cmd_list, dsgraph_render_static);

        for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
        {
            auto& map = mapNormalPasses[_priority][iPass];

            // [DA_PORT] Статика не нужна (её берут из кэша) — но список обязаны забрать, см. заголовок.
            if (da_part == da_graph_dynamic_only)
            {
                map.clear();
                invalidate_pass_item_cache(_priority, iPass);
                continue;
            }

            CTimer da_t;
            const bool da_prof = ps_da_graph_prof != 0;
            if (da_prof)
                da_t.Start();
            map.get_any_p(nrmPasses);
            if (da_prof)
            {
                da_graph_prof_get().maps += da_t.GetElapsed_sec() * 1000.f;
                da_t.Start();
            }
            std::sort(nrmPasses.begin(), nrmPasses.end(), cmp_pass<mapNormal_T::value_type*>);
            if (da_prof)
                da_graph_prof_get().sort_passes += da_t.GetElapsed_sec() * 1000.f;
            for (const auto& it : nrmPasses)
            {
                if (da_prof)
                {
                    // Разделяем: set_Pass это смена шейдеров и состояний, apply_lmaterial —
                    // поиск константы ПО ИМЕНИ плюс запись констант освещения. Что из двух дорого,
                    // по общему числу не видно, а лечится это по-разному.
                    da_graph_prof& P = da_graph_prof_get();
                    da_t.Start();
                    cmd_list.set_Pass(it->first);
                    const float t_pass = da_t.GetElapsed_sec() * 1000.f;
                    da_t.Start();
                    cmd_list.apply_lmaterial();
                    const float t_lmat = da_t.GetElapsed_sec() * 1000.f;
                    P.setup_pass += t_pass;
                    P.setup_lmat += t_lmat;
                    P.setup += t_pass + t_lmat;
                    ++P.passes;
                }
                else
                {
                    cmd_list.set_Pass(it->first);
                    cmd_list.apply_lmaterial();
                }


                mapNormalItems& items = it->second;
                items.ssa = 0;

                if (da_prof)
                    da_t.Start();
                std::sort(items.begin(), items.end(), cmp_ssa<_NormalItem>);
                if (da_prof)
                    da_graph_prof_get().sort_items += da_t.GetElapsed_sec() * 1000.f;
#ifdef USE_DX11
                // [DA_PORT] Деревья, которые удалось сложить в пачки, уже нарисованы и из списка
                // убраны. Если после этого список пуст — рисовать больше нечего.
                if (da_render_tree_batches(cmd_list, items) && items.empty())
                    continue;
#endif
                // [DA_PORT] instance-probe (r__instance_probe 1): замер ДО решения писать слияние
                // статики. Вопрос простой: у объектов одного материала на одной странице (общий
                // rm_geom из пула getVB) диапазоны vBase идут ПОДРЯД или вразброс? Если подряд —
                // один общий DrawIndexed возможен. Если вразброс — тема закрыта без месяца работы.
                if (ps_da_instance_probe && o.phase == CRender::PHASE_NORMAL)
                    da_instance_probe_contiguity(items);

                // [DA_PORT] r__static_sort_geom 1: досортировка по странице геометрии внутри
                // шейдер-группы (материал уже одинаков). Кластеризует смену привязки буфера вместо
                // хаотичного чередования — сам DIP не убирает, экономит проверку состояния на DX11.
                // stable_sort — порядок по SSA внутри страницы не трогаем, только порядок МЕЖДУ
                // страницами. Выключатель для чистого A/B в da_frame, без пересборки.
                if (ps_da_static_sort_geom)
                    da_static_sort_by_geometry_page(items);

                // [DA_PORT] r__static_dedup 1: снять точные дубли диапазона геометрии (см. пояснение
                // у da_static_dedup_exact). После сортировки по странице — так дубли уже соседние,
                // seen.insert находит их быстрее (не обязательно, xr_set сам ищет за log n, но раз
                // сортировка уже сделана, порядок вставки становится последовательным по странице).
                if (ps_da_static_dedup)
                    da_static_dedup_exact(items);

                if (da_prof)
                {
                    da_t.Start();
                    da_graph_prof_get().items += u32(items.size());
                }
                for (const auto& item : items)
                {
                    const float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
                    cmd_list.LOD.set_LOD(LOD);
#endif
                    // --#SM+#-- Обновляем шейдерные данные модели [update shader values for this model]
                    // RCache.hemi.c_update(item.pVisual);

                    if (ps_da_instance_probe && o.phase == CRender::PHASE_NORMAL)
                    {
                        da_instance_probe_state& probe = da_instance_probe_get();
                        u32 vcount{};
                        da_instance_probe_key_t key = da_instance_probe_key(item.pVisual, vcount);
                        da_instance_probe_sample& sample = probe.normal_counts[key];
                        ++sample.count;
                        sample.type = item.pVisual->Type;
                        sample.vertex_count = vcount;
                        ++probe.normal_items;
                    }

                    item.pVisual->Render(cmd_list, LOD, da_use_fast_geo(cmd_list, o.phase));
                }
                if (da_prof)
                    da_graph_prof_get().draw += da_t.GetElapsed_sec() * 1000.f;
                if (!da_keep)
                    items.clear();

            }
            if (da_prof)
                da_t.Start();
            nrmPasses.clear();
            if (!da_keep)
            {
                map.clear();
                invalidate_pass_item_cache(_priority, iPass); // [DA_PORT] карта очищена — указатель мёртв
            }
            if (da_prof)
                da_graph_prof_get().maps += da_t.GetElapsed_sec() * 1000.f;
        }
    }

    // **************************************************** MATRIX
    // Perform sorting based on ScreenSpaceArea
    // Sorting by SSA and changes minimizations
    // Render several passes
    {
        ZoneScopedN("dsgraph_render_dynamic");
        PIX_EVENT_CTX(cmd_list, dsgraph_render_dynamic);

        for (u32 iPass = 0; iPass < SHADER_PASSES_MAX; ++iPass)
        {
            auto& map = mapMatrixPasses[_priority][iPass];

            // [DA_PORT] Симметрично: рисуем только статику, но список динамики всё равно забираем.
            if (da_part == da_graph_static_only)
            {
                map.clear();
                invalidate_pass_item_cache(_priority, iPass);
                continue;
            }

            map.get_any_p(matPasses);
            CTimer da_tm;
            const bool da_prof_m = ps_da_graph_prof != 0;
            if (da_prof_m)
                da_tm.Start();
            std::sort(matPasses.begin(), matPasses.end(), cmp_pass<mapMatrix_T::value_type*>);
            for (const auto& it : matPasses)
            {
                cmd_list.set_Pass(it->first);


                mapMatrixItems& items = it->second;
                items.ssa = 0;

                std::sort(items.begin(), items.end(), cmp_ssa<_MatrixItem>);

                // [DA_PORT] r__matrix_dedup 1: снять точные дубли (объект, визуал) — см.
                // da_matrix_dedup_exact. Отдельный выключатель от статики: механизм тот же, но
                // затронутые объекты другие, и подтверждать нужно отдельным замером.
                if (ps_da_matrix_dedup)
                    da_matrix_dedup_exact(items);

                for (auto& item : items)
                {
                    cmd_list.set_xform_world(item.Matrix);
                    RImplementation.apply_object(cmd_list, item.pObject);
                    cmd_list.apply_lmaterial();

                    const float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
                    cmd_list.LOD.set_LOD(LOD);
#endif
                    // --#SM+#-- Обновляем шейдерные данные модели [update shader values for this model]
                    // RCache.hemi.c_update(item.pVisual);

                    if (ps_da_instance_probe && o.phase == CRender::PHASE_NORMAL)
                    {
                        da_instance_probe_state& probe = da_instance_probe_get();
                        u32 vcount{};
                        da_instance_probe_key_t key = da_instance_probe_key(item.pVisual, vcount);
                        da_instance_probe_sample& sample = probe.matrix_counts[key];
                        ++sample.count;
                        sample.type = item.pVisual->Type;
                        sample.vertex_count = vcount;
                        ++probe.matrix_items;
                    }

                    item.pVisual->Render(cmd_list, LOD, da_use_fast_geo(cmd_list, o.phase));
                }
                items.clear();
            }
            if (da_prof_m)
                da_graph_prof_get().matrix += da_tm.GetElapsed_sec() * 1000.f;
            matPasses.clear();
            map.clear();
            invalidate_pass_item_cache(_priority, iPass); // [DA_PORT] карта очищена — указатель мёртв
        }
    }

    // [DA_PORT] Отчёт раз в секунду. Числа НАКОПИТЕЛЬНЫЕ за секунду по всем проходам и приоритетам
    // (граф рисуется по многу раз за кадр: основной проход, каскады солнца, каждая теневая лампа) —
    // поэтому делим на число кадров, чтобы получить цену КАДРА, а не одного захода.
    if (ps_da_graph_prof)
    {
        da_graph_prof& P = da_graph_prof_get();
        P.whole += da_whole.GetElapsed_sec() * 1000.f;
        static u32 frames = 0;
        static u32 last_frame = u32(-1);
        if (last_frame != Device.dwFrame)
        {
            ++frames;
            last_frame = Device.dwFrame;
        }
        if (Device.fTimeGlobal - P.last_report > 1.f && frames)
        {
            const float k = 1.f / float(frames);
            {
                extern float g_da_ms_hom_wait;
                Msg("~ [DA_GRAPH] на кадр: ВЕСЬ ЦИКЛ %5.3f | карты %5.3f | ожидание отсечения %5.3f",
                    P.whole * k, P.maps * k, g_da_ms_hom_wait * k);
                g_da_ms_hom_wait = 0.f;
            }
            Msg("~ [DA_GRAPH] на кадр: сорт.проходов %5.3f | настройка %5.3f = проход %5.3f + "
                "материал %5.3f (проходов %u) | сорт.объектов %5.3f | отрисовка %5.3f (объектов %u) | "
                "динамика %5.3f",
                P.sort_passes * k, P.setup * k, P.setup_pass * k, P.setup_lmat * k,
                u32(float(P.passes) * k), P.sort_items * k, P.draw * k, u32(float(P.items) * k),
                P.matrix * k);
            Msg("~ [DA_GRAPH]   проход изнутри: состояния %5.3f | шейдеры %5.3f | константы %5.3f | "
                "текстуры %5.3f | матрицы %5.3f",
                g_da_pass_state * k, g_da_pass_shaders * k, g_da_pass_const * k, g_da_pass_tex * k,
                g_da_pass_mat * k);
            Msg("~ [DA_GRAPH]   константы изнутри: сброс %5.3f | перетасовка %5.3f | привязка %5.3f | "
                "обработчики %5.3f",
                g_da_const_unmap * k, g_da_const_shuffle * k, g_da_const_bind * k,
                g_da_const_loaders * k);
            g_da_pass_state = g_da_pass_shaders = g_da_pass_const = g_da_pass_tex = g_da_pass_mat = 0.f;
            g_da_const_unmap = g_da_const_shuffle = g_da_const_bind = g_da_const_loaders = 0.f;

            const float report_time = Device.fTimeGlobal;
            P = da_graph_prof{};
            P.last_report = report_time;
            frames = 0;
        }
    }

    RImplementation.BasicStats.Primitives.End(); // XXX: Refactor a bit later
}

//////////////////////////////////////////////////////////////////////////
// Helper classes and functions

/*
Предназначен для установки режима отрисовки HUD и возврата оригинального после отрисовки.
*/
class hud_transform_helper
{
    Fmatrix Pold;
    static u32 cullMode;
    static bool isActive;

    // [DA_PORT] HUD-камера для векторов движения: эта и прошлого кадра. Живут в самом объекте, потому
    // что на них ссылается бэкенд всё время отрисовки HUD. Подробности — у полей в R_Backend_xform.h.
    Fmatrix m_da_hud_VP;
    Fmatrix m_da_hud_VP_prev;

    CBackend& cmd_list;

public:
    explicit hud_transform_helper(CBackend& cmd_list_in)
        : cmd_list(cmd_list_in)
    {
        // Change projection
        Pold  = Device.mProject;

        // XXX: Xottab_DUTY: custom FOV. Implement it someday
        // It should be something like this:
        // float customFOV;
        // if (isCustomFOV)
        //     customFOV = V->getVisData().obj_data->m_hud_custom_fov;
        // else
        //     customFOV = psHUD_FOV * Device.fFOV;
        //
        // Device.mProject.build_projection(deg2rad(customFOV), Device.fASPECT,
        //    VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv.far_plane);
        //
        // Look at the function:
        // void __fastcall sorted_L1_HUD(mapSorted_Node* N)
        // In the commit:
        // https://github.com/ShokerStlk/xray-16-SWM/commit/869de0b6e74ac05990f541e006894b6fe78bd2a5#diff-4199ef700b18ce4da0e2b45dee1924d0R83

        Fmatrix prj_new;
        prj_new.build_projection(deg2rad(g_hud_fov_current * Device.fFOV /* *Device.fASPECT*/), Device.fASPECT,
            HUD_VIEWPORT_NEAR, g_pGamePersistent->Environment().CurrentEnv.far_plane);
        cmd_list.set_xform_project(prj_new);

        // [DA_PORT] Вектор движения для всего, что в руках, должен считаться в ТОЙ ЖЕ проекции, в
        // которой оно нарисовано. Проекция HUD своя (и меняется — отвод оружия у стены её двигает),
        // поэтому обе камеры собираем здесь: текущую и такую же из прошлого кадра.
        //
        // Прошлая берётся из статика, а не пересчитывается: обзор HUD за кадр мог измениться, и
        // «правильная» матрица прошлого кадра — только та, которой в прошлом кадре и рисовали.
        // Отрисовка HUD однопоточная (рядом на статиках живут cullMode и isActive), гонки тут нет.
        static Fmatrix s_hud_VP_prev = Fidentity;
        static Fmatrix s_hud_VP_curr = Fidentity;
        static u32 s_hud_VP_frame = u32(-1);

        m_da_hud_VP.mul(prj_new, Device.mView);
        if (s_hud_VP_frame != Device.dwFrame)
        {
            s_hud_VP_prev = s_hud_VP_curr;
            s_hud_VP_frame = Device.dwFrame;
        }
        s_hud_VP_curr = m_da_hud_VP;
        m_da_hud_VP_prev = s_hud_VP_prev;

        cmd_list.xforms.da_set_VP_overrides(&m_da_hud_VP_prev, &m_da_hud_VP);

        RImplementation.rmNear(cmd_list);

        // preserve culling mode
        cullMode = cmd_list.get_CullMode();
        isActive = true;
    }

    ~hud_transform_helper()
    {
        RImplementation.rmNormal(cmd_list);

        cmd_list.xforms.da_set_VP_overrides(nullptr, nullptr); // [DA_PORT] сцена снова считает по себе

        // Restore projection
        cmd_list.set_xform_project(Pold);
        // restore culling mode
        cmd_list.set_CullMode(cullMode);
        isActive = false;
    }

    static void apply_custom_state(CBackend& cmd_list)
    {
        if (!isActive || !psHUD_Flags.test(HUD_LEFT_HANDED))
            return;

        // Change culling mode if HUD meshes were flipped
        if (cullMode != CULL_NONE)
        {
            cmd_list.set_CullMode(cullMode == CULL_CW ? CULL_CCW : CULL_CW);
        }
    }
};

u32 hud_transform_helper::cullMode = CULL_NONE;
bool hud_transform_helper::isActive = false;

template<class T>
void __fastcall render_item(u32 context_id, const T& item)
{
    auto& dsgraph = RImplementation.get_context(context_id);

    dxRender_Visual* V = item.second.pVisual;
    VERIFY(V && V->shader._get());
    dsgraph.cmd_list.set_Element(item.second.se);
    dsgraph.cmd_list.set_xform_world(item.second.Matrix);
    RImplementation.apply_object(dsgraph.cmd_list, item.second.pObject);
    dsgraph.cmd_list.apply_lmaterial();
    hud_transform_helper::apply_custom_state(dsgraph.cmd_list);
    //--#SM+#-- Обновляем шейдерные данные модели [update shader values for this model]
    //RCache.hemi.c_update(V);
    V->Render(dsgraph.cmd_list, calcLOD(item.first, V->vis.sphere.R), dsgraph.o.phase == CRender::PHASE_SMAP);
}

template<class T>
ICF void sort_front_to_back_render_and_clean(u32 context_id, T& vec)
{
    vec.traverse_left_right(context_id, render_item);
    vec.clear();
}

template<class T>
ICF void sort_back_to_front_render_and_clean(u32 context_id, T& vec)
{
    vec.traverse_right_left(context_id, render_item);
    vec.clear();
}

//////////////////////////////////////////////////////////////////////////
// HUD render
void R_dsgraph_structure::render_hud()
{
    ZoneScoped;
    PIX_EVENT_CTX(cmd_list, dsgraph_render_hud);

    if (!mapHUD.empty())
    {
        hud_transform_helper helper{ cmd_list };
        sort_front_to_back_render_and_clean(context_id, mapHUD);
    }

#if RENDER == R_R1
    if (g_pGameLevel->pHUD && g_pGameLevel->pHUD->RenderActiveItemUIQuery())
        render_hud_ui(); // hud ui
#endif
}

void R_dsgraph_structure::render_hud_ui()
{
    ZoneScoped;
    CCustomHUD* levelHud = g_pGameLevel->pHUD;
    VERIFY(levelHud && levelHud->RenderActiveItemUIQuery());

    PIX_EVENT_CTX(cmd_list, dsgraph_render_hud_ui);

    hud_transform_helper helper{ cmd_list };

#if RENDER != R_R1
    // Targets, use accumulator for temporary storage
    const ref_rt rt_null;
    cmd_list.set_RT(0, 1);
    cmd_list.set_RT(0, 2);
    auto zb = RImplementation.Target->rt_Base_Depth;

#if (RENDER == R_R3) || (RENDER == R_R4) || (RENDER==R_GL)
    if (RImplementation.o.msaa)
        zb = RImplementation.Target->rt_MSAADepth;
#endif

    RImplementation.Target->u_setrt(cmd_list,
        RImplementation.o.albedo_wo ? RImplementation.Target->rt_Accumulator : RImplementation.Target->rt_Color,
        rt_null, rt_null, zb);
#endif // RENDER!=R_R1

    levelHud->RenderActiveItemUI();
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::render_sorted()
{
    ZoneScoped;
    PIX_EVENT_CTX(cmd_list, dsgraph_render_sorted);


    sort_back_to_front_render_and_clean(context_id, mapSorted);

    if (!mapHUDSorted.empty())
    {
        hud_transform_helper helper{ cmd_list };
        sort_back_to_front_render_and_clean(context_id, mapHUDSorted);
    }
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::render_emissive()
{
#if RENDER != R_R1
    ZoneScoped;
    PIX_EVENT_CTX(cmd_list, dsgraph_render_emissive);

    sort_front_to_back_render_and_clean(context_id, mapEmissive);

    if (!mapHUDEmissive.empty())
    {
        hud_transform_helper helper{ cmd_list };
        sort_front_to_back_render_and_clean(context_id, mapHUDEmissive);
    }
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::render_wmarks()
{
#if RENDER != R_R1
    ZoneScoped;
    PIX_EVENT(dsgraph_render_wmarks);

    sort_front_to_back_render_and_clean(context_id, mapWmark);
#endif
}

//////////////////////////////////////////////////////////////////////////
// strict-sorted render
void R_dsgraph_structure::render_distort()
{
    ZoneScoped;
    PIX_EVENT(dsgraph_render_distort);

    sort_back_to_front_render_and_clean(context_id, mapDistort);
}

// [DA_PORT] Проход по той же геометрии искажения, но нашим шейдером и без очистки списка.
//
// Зачем: вода не пишет векторов движения, а апскейлеру без них нечем перенести её между кадрами —
// он берёт вектор дна, и поверхность мигает целиком. Рисуем воду второй раз, прямо в буфер
// скоростей, шейдером da_water_velocity.
//
// Почему по списку искажения: вода уже собрана в нём (он наполняется по флагу bDistort в
// r__dsgraph_build.cpp), и другого готового перечня водной геометрии в кадре нет. Список НЕ
// очищаем — сразу после нас по нему пойдёт штатный render_distort, и он же его и уберёт.
//
// Элемент шейдера подменяется на наш: геометрия, мировая матрица и порядок остаются от объекта,
// меняется только то, чем он рисуется.
// Обход принимает УКАЗАТЕЛЬ НА ФУНКЦИЮ (FixedMap::callback), а не замыкание, поэтому подменяемый
// элемент передаём через файловую переменную. Проход зовётся из phase_combine, из главного потока и
// строго между наполнением списка и его очисткой — перекрыться самому с собой ему негде.
static ShaderElement* s_da_velocity_override = nullptr;

static void __fastcall da_render_item_override(u32 context_id, const R_dsgraph::mapSorted_T::value_type& item)
{
    auto& dsgraph = RImplementation.get_context(context_id);

    dxRender_Visual* V = item.second.pVisual;
    if (!V || !s_da_velocity_override)
        return;

    dsgraph.cmd_list.set_Element(s_da_velocity_override);
    dsgraph.cmd_list.set_xform_world(item.second.Matrix);
    RImplementation.apply_object(dsgraph.cmd_list, item.second.pObject);
    dsgraph.cmd_list.apply_lmaterial();

    V->Render(dsgraph.cmd_list, calcLOD(item.first, V->vis.sphere.R), FALSE);
}

void R_dsgraph_structure::da_render_distort_with(ShaderElement* se_override)
{
    ZoneScoped;
    PIX_EVENT(dsgraph_render_distort_velocity);

    if (!se_override || mapDistort.empty())
        return;

    s_da_velocity_override = se_override;
    mapDistort.traverse_right_left(context_id, da_render_item_override);
    s_da_velocity_override = nullptr;
}

void R_dsgraph_structure::render_R1_box(IRender_Sector::sector_id_t sector_id, Fbox& BB, int sh)
{
    VERIFY(sector_id != IRender_Sector::INVALID_SECTOR_ID);
    auto* S = Sectors[sector_id];

    PIX_EVENT(dsgraph_render_R1_box);

    lstVisuals.clear();
    lstVisuals.push_back(((CSector*)S)->root());

    for (size_t test = 0; test < lstVisuals.size(); ++test)
    {
        dxRender_Visual* V = lstVisuals[test];

        // Visual is 100% visible - simply add it
        switch (V->Type)
        {
        case MT_HIERRARHY:
        {
            // Add all children
            FHierrarhyVisual* pV = (FHierrarhyVisual*)V;
            for (auto& i : pV->children)
            {
                dxRender_Visual* T = i;
                if (BB.intersect(T->vis.box))
                    lstVisuals.push_back(T);
            }
        }
        break;
        case MT_SKELETON_ANIM:
        case MT_SKELETON_RIGID:
        {
            // Add all children	(s)
            CKinematics* pV = (CKinematics*)V;
            pV->CalculateBones(TRUE);
            for (auto& i : pV->children)
            {
                dxRender_Visual* T = i;
                if (BB.intersect(T->vis.box))
                    lstVisuals.push_back(T);
            }
        }
        break;
        case MT_LOD:
        {
            FLOD* pV = (FLOD*)V;
            for (auto& i : pV->children)
            {
                dxRender_Visual* T = i;
                if (BB.intersect(T->vis.box))
                    lstVisuals.push_back(T);
            }
        }
        break;
        default:
        {
            // Renderable visual
            ShaderElement* E2 = V->shader->E[sh]._get();
            if (E2 && !(E2->flags.bDistort))
            {
                for (u32 pass = 0; pass < E2->passes.size(); pass++)
                {
                    cmd_list.set_Element(E2, pass);
                    V->Render(cmd_list, -1.f, da_use_fast_geo(cmd_list, o.phase));
                }
            }
        }
        break;
        }
    }
}
} // namespace xray::render::RENDER_NAMESPACE
