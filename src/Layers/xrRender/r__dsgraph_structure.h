#pragma once

#include "r__sector.h"

namespace xray::render::RENDER_NAMESPACE
{
// feedback	for receiving visuals
class R_feedback
{
public:
    virtual void rfeedback_static(dxRender_Visual* V) = 0;
};

struct R_dsgraph_structure
{
    static constexpr auto INVALID_CONTEXT_ID = static_cast<u32>(-1);
#if RENDER == R_R1
    static constexpr auto IMM_CTX_ID = 0; // TODO: to remove this ugly #ifdef we need to introduce per-render configuration
#else
    static constexpr auto IMM_CTX_ID = R__NUM_PARALLEL_CONTEXTS; // the next after pooled
#endif

    R_feedback* val_feedback{}; // feedback for geometry being rendered
    u32 val_feedback_breakp{}; // breakpoint
    xr_vector<Fbox3>* val_recorder; // coarse structure recorder
    u32 marker{};
    u32 context_id{ INVALID_CONTEXT_ID };

    struct options_t
    {
        u32 phase{};
        u32 portal_traverse_flags{};
        u32 spatial_traverse_flags{};
        u32 spatial_types{ STYPE_RENDERABLE };
        float query_box_side{ EPS_L * 20.0f };
        Fvector view_pos{};
        Fmatrix xform{};
        CFrustum view_frustum{};
        IRender_Sector::sector_id_t sector_id;
        bool pmask[2];
        bool pmask_wmark;
        bool use_hom{ false };
        bool precise_portals{ false };
        bool is_main_pass{ false };
        bool mt_calculate{ false };

        // [DA_PORT] Не собирать статику при обходе. Разбор -- у da_skip_static в r2_R_lights.cpp.
        //
        // Коротко: кэш теневых карт ламп не рисует статику кэшированной лампы (render_graph
        // получает da_graph_dynamic_only и статические списки ОЧИЩАЕТ, не рисуя). Но собираются
        // они всё равно -- полным обходом секторов, каждый кадр, для каждой лампы. То есть список
        // строится ради того, чтобы его выбросить. Здесь обход статики отключается целиком.
        bool skip_static{ false };

        // [DA_PORT] Готовый список динамики вместо запроса к дереву объектов.
        //
        // Замер назвал виновника фазы света: 96 вызовов SpatialSpace.q_frustum за кадр, по одному
        // на лампу, дают 1.1 мс из 1.25. Проверено отключением: без запроса ожидание падает с 1.25
        // до 0.14 мс, а отключение сбора статики не меняет НИЧЕГО.
        //
        // Здесь список подсовывается снаружи: одна выборка по объединяющему объёму на всю фазу, а
        // каждая лампа лишь фильтрует её своей пирамидой. Тест повторяет приёмку из q_frustum
        // дословно -- маска типа и testSphere, -- иначе состав теней разошёлся бы.
        const xr_vector<ISpatial*>* dyn_source{ nullptr };
    } o;

    // Dynamic scene graph
    // R_dsgraph::mapNormal_T										mapNormal	[2]		;	// 2==(priority/2)
    R_dsgraph::mapNormalPasses_T mapNormalPasses[2]; // 2==(priority/2)
    // R_dsgraph::mapMatrix_T										mapMatrix	[2]		;
    R_dsgraph::mapMatrixPasses_T mapMatrixPasses[2];

    // [DA_PORT] Кэш последней корзины прохода. Подряд идущие визуалы одного материала искали её в
    // xr_fixed_map заново на КАЖДЫЙ визуал; запоминаем последний SPass и найденное значение.
    //
    // Указатель внутрь карты законен только потому, что писателей у неё ровно два - insert_static и
    // insert_dynamic, - и оба ходят через аксессор, который на промахе берёт указатель заново. Любая
    // вставка мимо аксессора может переселить значения и оставить здесь мусор.
    //
    // Экземпляр R_dsgraph_structure свой у каждого контекста рендера, поэтому кэш не общий и гонки
    // тут нет; см. пул контекстов в D3DXRenderBase.h.
    //
    // Перенесено из Dead Air Refined (0d60934a).
    SPass* cachedNormalPasses[2][SHADER_PASSES_MAX]{};
    R_dsgraph::mapNormalItems* cachedNormalItems[2][SHADER_PASSES_MAX]{};
    SPass* cachedMatrixPasses[2][SHADER_PASSES_MAX]{};
    R_dsgraph::mapMatrixItems* cachedMatrixItems[2][SHADER_PASSES_MAX]{};

    R_dsgraph::mapSorted_T mapSorted;
    R_dsgraph::mapHUD_T mapHUD;
    R_dsgraph::mapLOD_T mapLOD;
    R_dsgraph::mapSorted_T mapDistort;
    R_dsgraph::mapHUD_T    mapHUDSorted;

#if RENDER != R_R1
    R_dsgraph::mapSorted_T mapWmark; // sorted
    R_dsgraph::mapSorted_T mapEmissive;
    R_dsgraph::mapSorted_T mapHUDEmissive;
#endif

    xr_vector<CSector*> Sectors;
    xr_vector<CPortal*> Portals;
    CPortalTraverser PortalTraverser;
    xrXRC Sectors_xrc;

    // Runtime structures
    xr_vector<R_dsgraph::mapNormal_T::value_type*> nrmPasses;
    xr_vector<R_dsgraph::mapMatrix_T::value_type*> matPasses;
    xr_vector<R_dsgraph::_LodItem> lstLODs;
    xr_vector<int> lstLODgroups;
    xr_vector<ISpatial*> lstRenderables;
    xr_vector<ISpatial*> lstSpatial;
    xr_vector<dxRender_Visual*> lstVisuals;

    CBackend cmd_list{};

    u32 counter_S{};
    u32 counter_D{};

    void set_Feedback(R_feedback* V, u32 id)
    {
        val_feedback_breakp = id;
        val_feedback = V;
    }
    void set_Recorder(xr_vector<Fbox3>* dest)
    {
        val_recorder = dest;
        if (dest)
            dest->clear();
    }
    void get_Counters(u32& s, u32& d)
    {
        s = counter_S;
        d = counter_D;
    }
    void clear_Counters() { counter_S = counter_D = 0; }

    // [DA_PORT] см. поля cached* выше
    R_dsgraph::mapNormalItems& get_normal_pass_items(u32 priority, u32 passIndex, SPass* pass)
    {
        if (cachedNormalPasses[priority][passIndex] != pass)
        {
            cachedNormalPasses[priority][passIndex] = pass;
            cachedNormalItems[priority][passIndex] = &mapNormalPasses[priority][passIndex][pass];
        }
        return *cachedNormalItems[priority][passIndex];
    }

    R_dsgraph::mapMatrixItems& get_matrix_pass_items(u32 priority, u32 passIndex, SPass* pass)
    {
        if (cachedMatrixPasses[priority][passIndex] != pass)
        {
            cachedMatrixPasses[priority][passIndex] = pass;
            cachedMatrixItems[priority][passIndex] = &mapMatrixPasses[priority][passIndex][pass];
        }
        return *cachedMatrixItems[priority][passIndex];
    }

    void invalidate_pass_item_cache(u32 priority, u32 passIndex)
    {
        cachedNormalPasses[priority][passIndex] = nullptr;
        cachedNormalItems[priority][passIndex] = nullptr;
        cachedMatrixPasses[priority][passIndex] = nullptr;
        cachedMatrixItems[priority][passIndex] = nullptr;
    }

    R_dsgraph_structure() : Sectors_xrc("dsgraph")
    {
        r_pmask(true, true);
    };

    void reset()
    {
        //marker = 0;
        context_id = INVALID_CONTEXT_ID;

        o.query_box_side = EPS_L * 20;
        o.use_hom = false;
        o.precise_portals = false;
        o.is_main_pass = false;
        o.spatial_traverse_flags = 0;
        // [DA_PORT] Наши поля тоже: контекст берётся из пула и достаётся следующему потребителю.
        // Ставит их ламповый проход и сам же снимает, но полагаться на это нельзя -- чужой проход
        // с оставшимся skip_static потерял бы статику молча, а с оставшимся dyn_source взял бы
        // список, собранный под другой объём.
        o.skip_static = false;
        o.dyn_source = nullptr;
        o.portal_traverse_flags = 0;
        o.spatial_types = STYPE_RENDERABLE;

        val_recorder = nullptr;
        val_feedback = nullptr;

        nrmPasses.clear();
        matPasses.clear();

        lstLODs.clear();
        lstLODgroups.clear();
        lstRenderables.clear();
        lstSpatial.clear();
        lstVisuals.clear();

        for (int i = 0; i < SHADER_PASSES_MAX; ++i)
        {
            // [DA_PORT] Сначала кэш, потом сами карты: указатель ведёт внутрь них.
            invalidate_pass_item_cache(0, i);
            invalidate_pass_item_cache(1, i);

            mapNormalPasses[0][i].destroy();
            mapNormalPasses[1][i].destroy();
            mapMatrixPasses[0][i].destroy();
            mapMatrixPasses[1][i].destroy();
        }
        mapSorted.destroy();
        mapHUD.destroy();
        mapLOD.destroy();
        mapDistort.destroy();
        mapHUDSorted.destroy();

#if RENDER != R_R1
        mapWmark.destroy();
        mapEmissive.destroy();
        mapHUDEmissive.destroy();
#endif
        cmd_list.Invalidate();
    }

    void r_pmask(bool _1, bool _2, bool _wm = false)
    {
        o.pmask[0] = _1;
        o.pmask[1] = _2;
        o.pmask_wmark = _wm;
    }

    void load(const xr_vector<CSector::level_sector_data_t> &sectors, const xr_vector<CPortal::level_portal_data_t> &portals);
    void unload();

    ICF IRender_Portal* get_portal(size_t id) const
    {
        VERIFY(id < Portals.size());
        return Portals[id];
    }
    ICF IRender_Sector* get_sector(size_t id) const
    {
        VERIFY(id < Sectors.size());
        return Sectors[id];
    }
    IRender_Sector::sector_id_t detect_sector(const Fvector& P);
    IRender_Sector::sector_id_t detect_sector(const Fvector& P, Fvector& D);

    void add_static(dxRender_Visual* pVisual, const CFrustum& view, u32 planes);
    void add_leafs_dynamic(IRenderable* root, dxRender_Visual* pVisual, Fmatrix& xform); // if detected node's full visibility
    void add_leafs_static(dxRender_Visual* pVisual); // if detected node's full visibility

    void insert_dynamic(IRenderable* root, dxRender_Visual* pVisual, Fmatrix& xform, Fvector& Center);
    void insert_static(dxRender_Visual* pVisual);

    // render primitives
    // [DA_PORT] Какую половину графа рисовать. Нужно для кэша теневых карт ламп: статику можно
    // нарисовать один раз и переиспользовать, динамику приходится рисовать каждый кадр.
    // Деление не наше — оно уже есть внутри render_graph: mapNormalPasses это статика
    // (зона профилировщика dsgraph_render_static), mapMatrixPasses — динамика.
    //
    // ⚠️ Пропущенная половина всё равно ОЧИЩАЕТ свои списки. Иначе набранные за кадр элементы
    // остались бы в карте и всплыли в следующем проходе как чужая геометрия.
    enum da_graph_part
    {
        da_graph_all = 0,
        da_graph_static_only,
        da_graph_dynamic_only,
    };
    // [DA_PORT] da_keep — НЕ очищать списки после отрисовки, чтобы нарисовать их ещё раз.
    //
    // Нужно ровно одной лампе — той, у которой статику пришлось обновить. Её статика уходит в атлас
    // статики (для следующих кадров), но в РАБОЧИЙ атлас копия этого кадра её уже не принесёт:
    // копия делается один раз и до всех пачек. Чтобы кадра с неверной тенью не было, та же статика
    // рисуется второй раз, обычным проходом.
    void render_graph(u32 _priority, da_graph_part da_part = da_graph_all, bool da_keep = false);
    void render_hud();
    void render_hud_ui();
    void render_lods(bool _setup_zb, bool _clear);
    void render_sorted();
    void render_emissive();
    void render_wmarks();
    void render_distort();

    // [DA_PORT] Та же геометрия, что и у искажения, но нарисованная ЧУЖИМ элементом шейдера и БЕЗ
    // очистки списка: нужен второй проход по воде — в буфер скоростей. Разбор в
    // r4_rendertarget_phase_water_velocity.cpp.
    void da_render_distort_with(ShaderElement* se_override);
    void render_R1_box(IRender_Sector::sector_id_t sector_id, Fbox& _bb, int _element);

    void build_subspace();
};
} // namespace xray::render::RENDER_NAMESPACE
