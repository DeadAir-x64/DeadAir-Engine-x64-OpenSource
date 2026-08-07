#include "stdafx.h"

#include "xrEngine/IRenderable.h"
#include "xrEngine/CustomHUD.h"

#include <functional>

#include "FBasicVisual.h"
#include "FTreeVisual.h" // [DA_PORT] пакетная отрисовка деревьев
#include "SkeletonCustom.h"
#include "FLOD.h"

extern ENGINE_API float psHUD_FOV;
extern ENGINE_API float g_hud_fov_current; // [DA_PORT] nearwall: == psHUD_FOV unless modulated

namespace xray::render::RENDER_NAMESPACE
{
using namespace R_dsgraph;

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
    R_constant* instance_data{};
    R_constant* instance_control{};
    if (!da_tree_instance_constants(cmd_list, instance_data, instance_control))
    {
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
template <typename T>
bool cmp_pass(const T& left, const T& right)
{
    if (left->second.ssa != right->second.ssa)
        return left->second.ssa > right->second.ssa;
    return left->first < right->first;
}

void R_dsgraph_structure::render_graph(u32 _priority)
{
    PIX_EVENT_CTX(cmd_list, dsgraph_render_graph);
    RImplementation.BasicStats.Primitives.Begin(); // XXX: Refactor a bit later

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

            map.get_any_p(nrmPasses);
            std::sort(nrmPasses.begin(), nrmPasses.end(), cmp_pass<mapNormal_T::value_type*>);
            for (const auto& it : nrmPasses)
            {
                cmd_list.set_Pass(it->first);
                cmd_list.apply_lmaterial();


                mapNormalItems& items = it->second;
                items.ssa = 0;

                std::sort(items.begin(), items.end(), cmp_ssa<_NormalItem>);
#ifdef USE_DX11
                // [DA_PORT] Деревья, которые удалось сложить в пачки, уже нарисованы и из списка
                // убраны. Если после этого список пуст — рисовать больше нечего.
                if (da_render_tree_batches(cmd_list, items) && items.empty())
                    continue;
#endif
                for (const auto& item : items)
                {
                    const float LOD = calcLOD(item.ssa, item.pVisual->vis.sphere.R);
#ifdef USE_DX11
                    cmd_list.LOD.set_LOD(LOD);
#endif
                    // --#SM+#-- Обновляем шейдерные данные модели [update shader values for this model]
                    // RCache.hemi.c_update(item.pVisual);

                    item.pVisual->Render(cmd_list, LOD, o.phase == CRender::PHASE_SMAP);
                }
                items.clear();

            }
            nrmPasses.clear();
            map.clear();
            invalidate_pass_item_cache(_priority, iPass); // [DA_PORT] карта очищена — указатель мёртв
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

            map.get_any_p(matPasses);
            std::sort(matPasses.begin(), matPasses.end(), cmp_pass<mapMatrix_T::value_type*>);
            for (const auto& it : matPasses)
            {
                cmd_list.set_Pass(it->first);


                mapMatrixItems& items = it->second;
                items.ssa = 0;

                std::sort(items.begin(), items.end(), cmp_ssa<_MatrixItem>);
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

                    item.pVisual->Render(cmd_list, LOD, o.phase == CRender::PHASE_SMAP);
                }
                items.clear();
            }
            matPasses.clear();
            map.clear();
            invalidate_pass_item_cache(_priority, iPass); // [DA_PORT] карта очищена — указатель мёртв
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
                    V->Render(cmd_list, -1.f, o.phase == CRender::PHASE_SMAP);
                }
            }
        }
        break;
        }
    }
}
} // namespace xray::render::RENDER_NAMESPACE
