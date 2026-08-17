#include "stdafx.h"

#include "FHierrarhyVisual.h"
#include "SkeletonCustom.h"
#include "xrCore/Threading/ParallelFor.hpp"
#include "xrEngine/CustomHUD.h"
#include "xrEngine/IRenderable.h"
#include "xrEngine/xr_object.h"

#include "FLOD.h"
#include "LightTrack.h"
#include "ParticleGroup.h"
#include "FTreeVisual.h"

// [DA_PORT] Разбор расчёта видимости по частям -- см. build_subspace ниже. Копилка живёт в
// da_gpu_timer (только R4), этот файл идёт и в ветку GL, поэтому через заглушку.
#if RENDER == R_R4
#   include "Layers/xrRenderPC_R4/da_gpu_timer.h"
#   define DA_CULL_PARTS(p, s, d) da_gpu_set_cull_parts(p, s, d)
#   define DA_CULL_DYN(c, so, se, n, h) da_gpu_set_cull_dyn(c, so, se, n, h)
#   define DA_CULL_BODY(hom, rnd) da_gpu_set_cull_body(hom, rnd)
#   define DA_CULL_SKEL(b, w, ns, nl, ne) da_gpu_set_cull_skel(b, w, ns, nl, ne)
#else
#   define DA_CULL_PARTS(p, s, d)
#   define DA_CULL_DYN(c, so, se, n, h)
#   define DA_CULL_BODY(hom, rnd)
#   define DA_CULL_SKEL(b, w, ns, nl, ne)
#endif

namespace xray::render::RENDER_NAMESPACE
{
using namespace R_dsgraph;

// [DA_PORT] Ручка прибора ожидания, объявление -- в xrRender_console.cpp.
extern int ps_da_cull_prof;

// [DA_PORT] Из чего складывается renderable_Render -- он забрал 80% динамики (0.64 мс из 0.79).
//
// Внутри add_leafs_dynamic каждый СКЕЛЕТНЫЙ визуал считает всю иерархию костей и следы на модели,
// и делает это прямо в обходе видимости -- то есть в той задаче, которую главный поток ждёт
// вхолостую. Считаем кости и следы врозь: у них разная природа и чинятся они по-разному.
//
// ⚠️ Копилки файловые и без синхронизации, поэтому пишутся ТОЛЬКО из основного прохода
// (o.is_main_pass): теневые каскады идут своими контекстами на рабочих потоках, и без этого условия
// сюда попадала бы сумма пяти разных обходов вместо одного нужного.
//
// Листья только СЧИТАЮТСЯ, а не хронометрируются: их на порядок больше объектов, и два обращения к
// часам на каждый заметно исказили бы измеряемую величину. Их доля берётся вычитанием.
static double s_da_bones_ms = 0.0;
static double s_da_wallmarks_ms = 0.0;
static u32 s_da_skeletons = 0;
static u32 s_da_leafs = 0;

// Скольким скелетам поза считалась ТОЧНО. Без этого числа «кости подешевели» неотличимо от
// «скелетов стало меньше», и порог da_anim_lod нельзя было бы подобрать осмысленно.
static u32 s_da_skel_exact = 0;

// [DA_PORT] Порог точного расчёта позы, метры; 0 -- выключено. Объявление -- в xrRender_console.cpp,
// полный разбор -- у места применения в add_leafs_dynamic.
extern int ps_da_anim_lod;

////////////////////////////////////////////////////////////////////////////////////////////////////
// Scene graph actual insertion and sorting ////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
float r_ssaDISCARD;

// [DA_PORT] Сколько объектов сняли по экранной площади -- чтобы порог настраивался по числу, а не
// по ощущению. Считается только когда взведён разбор очереди (da_geom_dump): в обычном кадре это
// атомарный инкремент на каждый снятый объект, а снимаются они сотнями.
//
// Отчёт печатает РАЗНИЦУ с прошлым разом, поэтому обнулять счётчик не нужно: построение списков
// идёт из рабочих потоков и до отчёта, и любое обнуление из главного пришлось бы к чужому моменту.
std::atomic<u32> g_da_ssa_discarded{ 0 };
extern int ps_da_geom_dump;
float r_ssaDONTSORT;
float r_ssaLOD_A, r_ssaLOD_B;
float r_ssaGLOD_start, r_ssaGLOD_end;
float r_ssaHZBvsTEX;

ICF float CalcSSA(float& distSQ, Fvector& C, dxRender_Visual* V)
{
    float R = V->vis.sphere.R + 0;
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}
ICF float CalcSSA(float& distSQ, Fvector& C, float R)
{
    distSQ = Device.vCameraPosition.distance_to_sqr(C) + EPS;
    return R / distSQ;
}

// [DA_PORT] ---- Geometry cut-off by size and distance --------------------------------------------
// Ported from the Dead Air sources (_engine_diff/DA_render_R2.patch); the OpenXRay base has no
// equivalent, so the port was rendering every visual the frustum let through. A visual is skipped
// once it is both small enough and far enough that it cannot read as anything on screen.
//
// Distance is divided by the field-of-view ratio, so aiming down a scope does not start culling the
// things the player is deliberately looking at — zooming in makes distant objects larger on screen,
// and the threshold has to follow.
//
// The shadow-map pass uses its own, much harsher set of thresholds: the sun's cascades traverse the
// world several times per frame, and a pebble's shadow at 100 m is not visible in the result.
IC float da_cull_adjusted_distance(const Fvector& world_pos)
{
    const float distance_to = Device.vCameraPosition.distance_to(world_pos) + EPS;
    const float fov_K = 67.f / Device.fFOV;
    return distance_to / fov_K;
}

IC bool da_is_valuable_static(dxRender_Visual* pVisual, u32 phase)
{
    const bool smap = (phase == CRender::PHASE_SMAP) && ps_r_high_optimize_sun_shad;
    if (ps_r_optimize_static < 1 && !smap)
        return true;

    const float sphere_volume = pVisual->vis.sphere.volume();
    const float d = da_cull_adjusted_distance(pVisual->vis.sphere.P);

    if (smap)
    {
        // Nothing beyond the last cascade can cast a shadow that ends up in the frame at all.
        if (sphere_volume < 50000.f && d > ps_r2_sun_shadows_far_casc)
            return false;

        if (sphere_volume < O_S_L1_S_ULT && d > O_S_L1_D_ULT) return false;
        if (sphere_volume < O_S_L2_S_ULT && d > O_S_L2_D_ULT) return false;
        if (sphere_volume < O_S_L3_S_ULT && d > O_S_L3_D_ULT) return false;
        if (sphere_volume < O_S_L4_S_ULT && d > O_S_L4_D_ULT) return false;
        if (sphere_volume < O_S_L5_S_ULT && d > O_S_L5_D_ULT) return false;
        return true;
    }

    if (sphere_volume < o_optimize_static_l1_size && d > o_optimize_static_l1_dist) return false;
    if (sphere_volume < o_optimize_static_l2_size && d > o_optimize_static_l2_dist) return false;
    if (sphere_volume < o_optimize_static_l3_size && d > o_optimize_static_l3_dist) return false;
    if (sphere_volume < o_optimize_static_l4_size && d > o_optimize_static_l4_dist) return false;
    if (sphere_volume < o_optimize_static_l5_size && d > o_optimize_static_l5_dist) return false;
    return true;
}

IC bool da_is_valuable_dynamic(dxRender_Visual* pVisual, const Fmatrix& xform, u32 phase)
{
    const bool smap = (phase == CRender::PHASE_SMAP) && ps_r_high_optimize_sun_shad;
    if (ps_r_optimize_dynamic < 1 && !smap)
        return true;

    const float sphere_volume = pVisual->vis.sphere.volume();

    // Dynamic visuals carry their position in object space, so it has to go through the object's
    // transform before the distance means anything.
    Fvector world_pos;
    xform.transform_tiny(world_pos, pVisual->vis.sphere.P);
    const float d = da_cull_adjusted_distance(world_pos);

    if (smap)
    {
        // Same reasoning as the static path, except the author applies it to dynamics regardless of
        // their size: nothing past the last cascade can cast a shadow that lands in the frame, and a
        // stalker walking 300 m away is no exception. This line was missed in the first pass of the
        // port, so distant NPCs and physics props were still being pushed through every cascade.
        if (d > ps_r2_sun_shadows_far_casc)
            return false;

        if (sphere_volume < O_D_L1_S_ULT && d > O_D_L1_D_ULT) return false;
        if (sphere_volume < O_D_L2_S_ULT && d > O_D_L2_D_ULT) return false;
        if (sphere_volume < O_D_L3_S_ULT && d > O_D_L3_D_ULT) return false;
        return true;
    }

    if (sphere_volume < o_optimize_dynamic_l1_size && d > o_optimize_dynamic_l1_dist) return false;
    if (sphere_volume < o_optimize_dynamic_l2_size && d > o_optimize_dynamic_l2_dist) return false;
    if (sphere_volume < o_optimize_dynamic_l3_size && d > o_optimize_dynamic_l3_dist) return false;
    return true;
}

void R_dsgraph_structure::insert_dynamic(IRenderable* root, dxRender_Visual* pVisual, Fmatrix& xform, Fvector& Center)
{
    ZoneScoped;

    CRender& RI = RImplementation;

    if (pVisual->vis.marker[context_id] == marker)
        return;
    pVisual->vis.marker[context_id] = marker;

#if RENDER == R_R1
    if (RI.o.vis_intersect && (pVisual->vis.accept_frame != Device.dwFrame))
        return;
    pVisual->vis.accept_frame = Device.dwFrame;
#endif

    float distSQ;
    float SSA = CalcSSA(distSQ, Center, pVisual);
    if (SSA <= r_ssaDISCARD)
    {
        if (ps_da_geom_dump > 0)
            g_da_ssa_discarded.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Distortive geometry should be marked and R2 special-cases it
    // a) Allow to optimize RT order
    // b) Should be rendered to special distort buffer in another pass
    VERIFY(pVisual->shader._get());
    const Shader* vis_sh = pVisual->shader._get();
    ShaderElement* sh_d = vis_sh ? vis_sh->E[4]._get() : nullptr; // 4=L_special
    if (sh_d)
    {
        if (RImplementation.o.distortion && sh_d && sh_d->flags.bDistort && o.pmask[sh_d->flags.iPriority / 2])
        {
            mapDistort.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh_d })); // sh_d -> L_special
        }
    }

    // Select shader
    ShaderElement* sh = RImplementation.rimp_select_sh_dynamic(pVisual, distSQ, o.phase);
    if (nullptr == sh)
        return;
    if (!o.pmask[sh->flags.iPriority / 2])
        return;

    // HUD rendering
    if (root && root->renderable_HUD())
    {
        if (sh->flags.bStrictB2F)
        {
            mapHUDSorted.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh }));
            return;
        }
        mapHUD.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh }));

#if RENDER != R_R1
        if (sh->flags.bEmissive && sh_d)
            mapHUDEmissive.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh_d })); // sh_d -> L_special
#endif
        return;
    }

// Shadows registering
#if RENDER == R_R1
    RI.L_Shadows->add_element(_MatrixItem{ SSA, root, pVisual, xform });
#endif
    if (root && root->renderable_Invisible())
        return;

    // strict-sorting selection
    if (sh->flags.bStrictB2F)
    {
        mapSorted.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh }));
        return;
    }

#if RENDER != R_R1
    // Emissive geometry should be marked and R2 special-cases it
    // a) Allow to skeep already lit pixels
    // b) Allow to make them 100% lit and really bright
    // c) Should not cast shadows
    // d) Should be rendered to accumulation buffer in the second pass
    if (sh->flags.bEmissive)
    {
        mapEmissive.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh_d })); // sh_d -> L_special
    }
    if (sh->flags.bWmark && o.pmask_wmark)
    {
        mapWmark.insert_anyway(distSQ, _MatrixItemS({ SSA, root, pVisual, xform, sh }));
        return;
    }
#endif

    for (u32 iPass = 0; iPass < sh->passes.size(); ++iPass)
    {
        SPass* pass = sh->passes[iPass]._get();
        mapMatrixItems& matrixItems = get_matrix_pass_items(sh->flags.iPriority / 2, iPass, pass);

        // Create common node
        // NOTE: Invisible elements exist only in R1
        matrixItems.emplace_back(_MatrixItem{ SSA, root, pVisual, xform });

        // Need to sort for HZB efficient use
        if (SSA > matrixItems.ssa)
        {
            matrixItems.ssa = SSA;
        }
    }

#if RENDER != R_R1
    if (val_recorder)
    {
        Fbox3 temp;
        temp.xform(pVisual->vis.box, xform);
        val_recorder->push_back(temp);
    }
#endif
}

// [DA_PORT] Зонд разбора статики: почему объект не доехал до списка отрисовки.
//
// Десять механизмов отсечения проверены рассуждением, все отпали, три раза виновник был назван
// неверно. Дальше гадать дороже, чем измерить: зонд пишет по каждому визуалу рядом с камерой, какая
// именно строка его сняла — или что он добавлен.
//
// ⚠️ Печатает и СВОДКУ за кадр. Это не украшение: стенд сцену не рисует (доказано меткой в отрисовке
// травы), и без сводки «зонд молчит» неотличимо от «объект не отсекается». Нет сводки — значит этот
// путь на стенде не проходят вовсе, и мерить надо в живой игре.
int ps_da_portal_frustum = 1; // 0 = обходить статику камерной пирамидой
int ps_da_vis_dump = 0; // сколько кадров писать; сбрасывается сам при взведении
float ps_da_vis_radius = 60.f; // метров от камеры

namespace
{
// 🪤 Первая версия считала кадры потоковой переменной и уменьшала счётчик прямо в зонде. Разбор
// списка идёт на НЕСКОЛЬКИХ рабочих потоках, поэтому первый же поток съедал единицу и гасил зонд
// до единой записи: `da_vis_dump 1` давал пустоту, неотличимую от «объект не отсекается».
// Теперь взведение атомарное и по НОМЕРАМ кадров, а не по количеству срабатываний.
std::atomic<u32> da_vis_until{ 0 };
std::atomic<u32> da_vis_seen{ 0 };
std::atomic<u32> da_vis_dropped{ 0 };
std::atomic<u32> da_vis_reported{ 0 };

bool da_vis_armed()
{
    const int want = ps_da_vis_dump;
    if (want > 0)
    {
        ps_da_vis_dump = 0;
        const u32 until = Device.dwFrame + u32(want);
        da_vis_until.store(until, std::memory_order_relaxed);
        da_vis_seen.store(0, std::memory_order_relaxed);
        da_vis_dropped.store(0, std::memory_order_relaxed);
        da_vis_reported.store(0, std::memory_order_relaxed);
        Msg("~ [DA_VIS] зонд взведён: кадры %u..%u, радиус %.0f м", Device.dwFrame, until, ps_da_vis_radius);
    }

    const u32 until = da_vis_until.load(std::memory_order_relaxed);
    if (until == 0)
        return false;
    if (Device.dwFrame <= until)
        return true;

    // Кадры вышли — один раз подвести итог. Он и отвечает на вопрос «зонд молчал или не работал».
    if (da_vis_reported.exchange(1, std::memory_order_relaxed) == 0)
        Msg("~ [DA_VIS] зонд снят: визуалов в радиусе %u, из них снято %u",
            da_vis_seen.load(std::memory_order_relaxed), da_vis_dropped.load(std::memory_order_relaxed));
    da_vis_until.store(0, std::memory_order_relaxed);
    return false;
}

// ⚠️ Первая версия не писала ФАЗУ, и это обесценило целый прогон: `insert_static` вызывается и для
// основного прохода, и для теневого, а в теневом у части материалов элемента шейдера нет ШТАТНО.
// Без номера фазы 156 снятий нельзя ни обвинить, ни оправдать. Пишем фазу и МИРОВЫЕ координаты —
// по ним объект опознаётся однозначно, в отличие от радиуса.
void da_vis_say(dxRender_Visual* v, float dist, u32 phase, pcstr what)
{
    da_vis_dropped.fetch_add(1, std::memory_order_relaxed);
    Msg("~ [DA_VIS] фаза %u  тип %2u  R %7.2f  d %6.2f  xyz %.1f %.1f %.1f  %s", phase, (u32)v->Type,
        v->vis.sphere.R, dist, v->vis.sphere.P.x, v->vis.sphere.P.y, v->vis.sphere.P.z, what);
}
} // namespace

void R_dsgraph_structure::insert_static(dxRender_Visual* pVisual)
{
    ZoneScoped;

    CRender& RI = RImplementation;

    float da_dist = 0.f;
    bool da_watch = da_vis_armed();
    if (da_watch)
    {
        da_dist = Device.vCameraPosition.distance_to(pVisual->vis.sphere.P);
        da_watch = da_dist <= ps_da_vis_radius;
        if (da_watch)
            da_vis_seen.fetch_add(1, std::memory_order_relaxed);
    }

    if (pVisual->vis.marker[context_id] == marker)
    {
        if (da_watch)
            da_vis_say(pVisual, da_dist, o.phase, "СНЯТ: уже помечен в этом проходе");
        return;
    }
    pVisual->vis.marker[context_id] = marker;

#if RENDER == R_R1
    if (RI.o.vis_intersect && (pVisual->vis.accept_frame != Device.dwFrame))
        return;
    pVisual->vis.accept_frame = Device.dwFrame;
#endif

    float distSQ;
    float SSA = CalcSSA(distSQ, pVisual->vis.sphere.P, pVisual);
    if (SSA <= r_ssaDISCARD)
    {
        if (da_watch)
            da_vis_say(pVisual, da_dist, o.phase, "СНЯТ: площадь на экране ниже порога (r_ssaDISCARD)");
        if (ps_da_geom_dump > 0)
            g_da_ssa_discarded.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Distortive geometry should be marked and R2 special-cases it
    // a) Allow to optimize RT order
    // b) Should be rendered to special distort buffer in another pass
    VERIFY(pVisual->shader._get());
    const Shader* vis_sh = pVisual->shader._get();
    ShaderElement* sh_d = vis_sh ? vis_sh->E[4]._get() : nullptr; // 4=L_special
    if (sh_d)
    {
        if (RImplementation.o.distortion && sh_d && sh_d->flags.bDistort && o.pmask[sh_d->flags.iPriority / 2])
        {
            mapDistort.insert_anyway(distSQ, _MatrixItemS({ SSA, nullptr, pVisual, Fidentity, sh_d })); // sh_d -> L_special
        }
    }

    // Select shader
    ShaderElement* sh = RImplementation.rimp_select_sh_static(pVisual, distSQ, o.phase);
    if (nullptr == sh)
    {
        if (da_watch)
            da_vis_say(pVisual, da_dist, o.phase, "СНЯТ: у шейдера нет элемента для этой фазы/дальности");
        return;
    }
    if (!o.pmask[sh->flags.iPriority / 2])
    {
        if (da_watch)
            da_vis_say(pVisual, da_dist, o.phase, "СНЯТ: маска приоритетов прохода");
        return;
    }
    if (da_watch)
        Msg("~ [DA_VIS] ДОБАВЛЕН фаза %u  тип %2u  R %7.2f  d %6.2f  xyz %.1f %.1f %.1f", o.phase,
            (u32)pVisual->Type, pVisual->vis.sphere.R, da_dist, pVisual->vis.sphere.P.x,
            pVisual->vis.sphere.P.y, pVisual->vis.sphere.P.z);

    // strict-sorting selection
    if (sh->flags.bStrictB2F)
    {
        // TODO: Выяснить, почему в единственном месте параметр ssa не используется
        // Визуально различий не замечено
        mapSorted.insert_anyway(distSQ, _MatrixItemS({ /*0*/SSA, nullptr, pVisual, Fidentity, sh }));
        return;
    }

#if RENDER != R_R1
    // Emissive geometry should be marked and R2 special-cases it
    // a) Allow to skeep already lit pixels
    // b) Allow to make them 100% lit and really bright
    // c) Should not cast shadows
    // d) Should be rendered to accumulation buffer in the second pass
    if (sh->flags.bEmissive && sh_d)
    {
        mapEmissive.insert_anyway(distSQ, _MatrixItemS({ SSA, nullptr, pVisual, Fidentity, sh_d })); // sh_d -> L_special
    }
    if (sh->flags.bWmark && o.pmask_wmark)
    {
        mapWmark.insert_anyway(distSQ, _MatrixItemS({ SSA, nullptr, pVisual, Fidentity, sh }));
        return;
    }
#endif

    if (val_feedback && counter_S == val_feedback_breakp)
        val_feedback->rfeedback_static(pVisual);

    counter_S++;

    for (u32 iPass = 0; iPass < sh->passes.size(); ++iPass)
    {
        SPass* pass = sh->passes[iPass]._get();
        mapNormalItems& normalItems = get_normal_pass_items(sh->flags.iPriority / 2, iPass, pass);

        normalItems.emplace_back(_NormalItem{ SSA, pVisual });

        // Need to sort for HZB efficient use
        if (SSA > normalItems.ssa)
        {
            normalItems.ssa = SSA;
        }
    }

#if RENDER != R_R1
    if (val_recorder)
    {
        val_recorder->push_back(pVisual->vis.box);
    }
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
void R_dsgraph_structure::add_leafs_dynamic(IRenderable* root, dxRender_Visual* pVisual, Fmatrix& xform)
{
    ZoneScoped;

    if (nullptr == pVisual)
        return;

    if (!da_is_valuable_dynamic(pVisual, xform, o.phase)) // [DA_PORT] geometry cut-off
        return;

    // Visual is 100% visible - simply add it
    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        // Add all children, doesn't perform any tests
        PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual;
        for (auto& it : pG->items)
        {
            PS::CParticleGroup::SItem& I = it;
            if (I._effect)
                add_leafs_dynamic(root, I._effect, xform);
            for (auto& pit : I._children_related)
                add_leafs_dynamic(root, pit, xform);
            for (auto& pit : I._children_free)
                add_leafs_dynamic(root, pit, xform);
        }
    }
        return;
    case MT_HIERRARHY:
    {
        // Add all children, doesn't perform any tests
        FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
        for (auto& i : pV->children)
        {
            //i->vis.obj_data = pV->getVisData().obj_data; // Наследники используют шейдерные данные от родительского визуала
                                                         // [use shader data from parent model, rather than it childrens]

            add_leafs_dynamic(root, i, xform);
        }
    }
        return;
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Add all children, doesn't perform any tests
        CKinematics* pV = (CKinematics*)pVisual;
        BOOL _use_lod = FALSE;
        if (pV->m_lod)
        {
            Fvector Tpos;
            float D;
            xform.transform_tiny(Tpos, pV->vis.sphere.P);
            float ssa = CalcSSA(D, Tpos, pV->vis.sphere.R / 2.f); // assume dynamics never consume full sphere
            if (ssa < r_ssaLOD_A)
                _use_lod = TRUE;
        }
        if (_use_lod)
        {
            add_leafs_dynamic(root, pV->m_lod, xform);
        }
        else
        {
            // [DA_PORT] Кости и следы -- врозь. Разбор у s_da_bones_ms.
            const bool da_skel_prof = ps_da_cull_prof && o.is_main_pass;
            CTimer da_skel_timer;
            if (da_skel_prof)
            {
                ++s_da_skeletons;
                da_skel_timer.Start();
            }

            // [DA_PORT] Точный расчёт позы -- только вблизи. Дальше работает штатный интервал.
            //
            // ЗАЧЕМ. Замер: CalculateBones стоит 0.49 мс на 116 скелетов в кадре -- это 82% всего
            // renderable_Render, 65% динамики расчёта видимости и почти половина того ожидания, в
            // котором главный поток стоит вхолостую. То есть анимация пересчитывается внутри
            // определения видимости, на задаче, которую все ждут.
            //
            // ПОЧЕМУ АРГУМЕНТ. Механизм «обновлять не каждый кадр» в движке УЖЕ есть -- ранний выход
            // в CalculateBones по UCalc_Interval, который в Kinematics.h объявлен как 100 мс (10 Гц)
            // с авторским же комментарием. Отменяет его ровно bForceExact, и рендер передавал сюда
            // безусловное TRUE -- для всех скелетов кадра, включая те, что в двух сотнях метров
            // занимают несколько пикселей.
            //
            // ОТРАСЛЕВОЙ АНАЛОГ. Это ровно Update Rate Optimization из Unreal: дальние и закадровые
            // модели пропускают полный расчёт позы, рекомендация Epic -- 15 Гц и ниже на подходящих
            // дистанциях. Наши 10 Гц в неё укладываются. Приём считается самым базовым в LOD
            // анимации и сам по себе срезает 50-70% процессорной цены анимации.
            //
            // Дистанция делится на отношение поля зрения тем же помощником, что и отсечка геометрии:
            // в прицеле дальний сталкер занимает на экране больше, и порог обязан ехать вместе с
            // увеличением, иначе анимация замедлится ровно у того, на кого игрок смотрит в упор.
            //
            // ⛔ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО (0). Цена -- дальние модели анимируются 10 раз в секунду
            // вместо частоты кадров; на дистанции это незаметно, но подбирается глазами. Отдельно
            // проверить стоит стрельбу по дальним целям: попадания считаются по костям, и хотя
            // игровые пути дёргают CalculateBones сами, эту связь надёжнее увидеть в игре, чем
            // вывести рассуждением.
            BOOL da_exact = TRUE;
            if (ps_da_anim_lod > 0)
            {
                Fvector da_world_pos;
                xform.transform_tiny(da_world_pos, pV->vis.sphere.P);
                da_exact = da_cull_adjusted_distance(da_world_pos) <= float(ps_da_anim_lod);
                if (da_skel_prof && da_exact)
                    ++s_da_skel_exact;
            }
            else if (da_skel_prof)
                ++s_da_skel_exact;

            pV->CalculateBones(da_exact);

            if (da_skel_prof)
            {
                s_da_bones_ms += da_skel_timer.GetElapsed_sec() * 1000.0;
                da_skel_timer.Start();
            }

            if (o.phase == CRender::PHASE_NORMAL)
            {
                pV->CalculateWallmarks(root ? root->renderable_HUD() : false); //. bug?
            }

            if (da_skel_prof)
                s_da_wallmarks_ms += da_skel_timer.GetElapsed_sec() * 1000.0;
            for (auto& i : pV->children)
            {
                //i->vis.obj_data = pV->getVisData().obj_data; // Наследники используют шейдерные данные от родительского визуала
                                                             // [use shader data from parent model, rather than it childrens]
                add_leafs_dynamic(root, i, xform);
            }
        }
    }
        return;
    default:
    {
        // General type of visual
        // Calculate distance to it's center
        Fvector Tpos;
        xform.transform_tiny(Tpos, pVisual->vis.sphere.P);
        if (ps_da_cull_prof && o.is_main_pass)
            ++s_da_leafs; // [DA_PORT] листья только считаем, см. разбор у s_da_bones_ms
        insert_dynamic(root, pVisual, xform, Tpos);
    }
        return;
    }
}

void R_dsgraph_structure::add_leafs_static(dxRender_Visual* pVisual)
{
    ZoneScoped;

    if (o.use_hom && !RImplementation.HOM.visible(pVisual->vis))
        return;

    if (!da_is_valuable_static(pVisual, o.phase)) // [DA_PORT] geometry cut-off
        return;

    // Visual is 100% visible - simply add it
    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        // Xottab_DUTY: for dynamic objects we need matrixб
        // which is nullptr, when we use add_leafs_Static
        Log("Dynamic particles added via static procedure. Please, contact Xottab_DUTY and tell him about the issue.");
        NODEFAULT;

        // Add all children, doesn't perform any tests
        /*PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual;
        for (auto& it : pG->items)
        {
            PS::CParticleGroup::SItem& I = it;
            if (I._effect)
                add_leafs_Dynamic(I._effect);
            for (auto& pit : I._children_related)
                add_leafs_Dynamic(pit);
            for (auto& pit : I._children_free)
                add_leafs_Dynamic(pit);
        }*/
    }
    return;
    case MT_HIERRARHY:
    {
        // Add all children, doesn't perform any tests
        FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
        for (auto& i : pV->children)
        {
            //i->vis.obj_data = pV->getVisData().obj_data; // Наследники используют шейдерные данные от родительского визуала
                                                         // [use shader data from parent model, rather than it childrens]
            add_leafs_static(i);
        }
    }
    return;
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Add all children, doesn't perform any tests
        CKinematics* pV = (CKinematics*)pVisual;
        pV->CalculateBones(TRUE);
        for (auto& i : pV->children)
        {
            //i->vis.obj_data = pV->getVisData().obj_data; // Наследники используют шейдерные данные от родительского визуала
                                                         // [use shader data from parent model, rather than it childrens]
            add_leafs_static(i);
        }
    }
    return;
    case MT_LOD:
    {
        FLOD* pV = (FLOD*)pVisual;
        float D;
        float ssa = CalcSSA(D, pV->vis.sphere.P, pV);
        ssa *= pV->lod_factor;
        if (ssa < r_ssaLOD_A)
        {
            if (ssa < r_ssaDISCARD)
                return;
            mapLOD.insert_anyway(D, _LodItem({ ssa, pVisual }));
        }
#if RENDER != R_R1
        if (ssa > r_ssaLOD_B || o.phase == CRender::PHASE_SMAP)
#else
        if (ssa > r_ssaLOD_B)
#endif
        {
            // Add all children, doesn't perform any tests
            for (auto& i : pV->children)
            {
                //i->vis.obj_data = pV->getVisData().obj_data; // Наследники используют шейдерные данные от родительского визуала
                                                             // [use shader data from parent model, rather than it childrens]
                add_leafs_static(i);
            }
        }
    }
    return;
    case MT_TREE_PM:
    case MT_TREE_ST:
    {
        // General type of visual
        insert_static(pVisual);
    }
    return;
    default:
    {
        // General type of visual
        insert_static(pVisual);
    }
    return;
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

/* Xottab_DUTY: this function is only called from add_Static,
 * but we need a matrix, which is nullptr at this point
BOOL R_dsgraph_structure::add_Dynamic(dxRender_Visual* pVisual, u32 planes) // normal processing
{
    // Check frustum visibility and calculate distance to visual's center
    Fvector Tpos; // transformed position
    EFC_Visible VIS;

    val_pTransform->transform_tiny(Tpos, pVisual->vis.sphere.P);
    VIS = View->testSphere(Tpos, pVisual->vis.sphere.R, planes);
    if (fcvNone == VIS)
        return FALSE;

    // If we get here visual is visible or partially visible
    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        // Add all children, doesn't perform any tests
        PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual;
        for (auto& it : pG->items)
        {
            PS::CParticleGroup::SItem& I = it;
            if (fcvPartial == VIS)
            {
                if (I._effect)
                    add_Dynamic(I._effect, planes);
                for (auto& pit : I._children_related)
                    add_Dynamic(pit, planes);
                for (auto& pit : I._children_free)
                    add_Dynamic(pit, planes);
            }
            else
            {
                if (I._effect)
                    add_leafs_Dynamic(I._effect);
                for (auto& pit : I._children_related)
                    add_leafs_Dynamic(pit);
                for (auto& pit : I._children_free)
                    add_leafs_Dynamic(pit);
            }
        }
    }
    break;
    case MT_HIERRARHY:
    {
        // Add all children
        FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
        if (fcvPartial == VIS)
        {
            for (auto& i : pV->children)
                add_Dynamic(i, planes);
        }
        else
        {
            for (auto& i : pV->children)
                add_leafs_Dynamic(i);
        }
    }
    break;
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Add all children, doesn't perform any tests
        CKinematics* pV = (CKinematics*)pVisual;
        BOOL _use_lod = FALSE;
        if (pV->m_lod)
        {
            Fvector Tpos2;
            float D;
            val_pTransform->transform_tiny(Tpos2, pV->vis.sphere.P);
            float ssa = CalcSSA(D, Tpos2, pV->vis.sphere.R / 2.f); // assume dynamics never consume full sphere
            if (ssa < r_ssaLOD_A)
                _use_lod = TRUE;
        }
        if (_use_lod)
        {
            add_leafs_Dynamic(pV->m_lod);
        }
        else
        {
            pV->CalculateBones(TRUE);
            pV->CalculateWallmarks(val_pObject ? val_pObject->renderable_HUD() : false); //. bug?
            for (auto& i : pV->children)
                add_leafs_Dynamic(i);
        }
    }
    break;
    default:
    {
        // General type of visual
        r_dsgraph_insert_dynamic(pVisual, Tpos);
    }
    break;
    }
    return TRUE;
}*/

void R_dsgraph_structure::add_static(dxRender_Visual* pVisual, const CFrustum& view, u32 planes)
{
    ZoneScoped;

    vis_data& vis = pVisual->vis;

    // Check frustum visibility and calculate distance to visual's center
    const u32 da_planes_in = planes; // [DA_PORT] testSAABB портит маску — сохраняем вход для зонда
    const EFC_Visible VIS = view.testSAABB(vis.sphere.P, vis.sphere.R, vis.box.data(), planes);
    if (fcvNone == VIS)
    {
        // [DA_PORT] Зонд отказа по пирамиде видимости.
        //
        // Замер показал: объект, видимый на экране, снимается ИМЕННО здесь — в основном проходе его
        // нет, а в теневом есть (у теневого своя пирамида, от солнца). Отсюда и целая тень при
        // пропавшем объекте. Осталось назвать, ЧТО именно не сходится: сфера, ящик или плоскость.
        //
        // Маску порчей объяснить нельзя: сюда она приходит по значению, а нулевая маска даёт
        // «видим», а не «не видим» — то есть ложного отказа из неё не получается.
        if (da_vis_armed())
        {
            const float d = Device.vCameraPosition.distance_to(vis.sphere.P);
            if (d <= ps_da_vis_radius)
            {
                da_vis_dropped.fetch_add(1, std::memory_order_relaxed);
                const float* mM = vis.box.data();
                Msg("~ [DA_VIS] ОТКАЗ ПИРАМИДЫ фаза %u  xyz %.1f %.1f %.1f  R %.2f  d %.2f  маска %u  "
                    "ящик [%.1f %.1f %.1f]..[%.1f %.1f %.1f]  камера %.1f %.1f %.1f",
                    o.phase, vis.sphere.P.x, vis.sphere.P.y, vis.sphere.P.z, vis.sphere.R, d, da_planes_in,
                    mM[0], mM[1], mM[2], mM[3], mM[4], mM[5], Device.vCameraPosition.x,
                    Device.vCameraPosition.y, Device.vCameraPosition.z);

                u32 bit = 1;
                for (u32 i = 0; i < view.p_count; ++i, bit <<= 1)
                {
                    if (!(da_planes_in & bit))
                        continue;
                    const float cls = view.planes[i].classify(vis.sphere.P);
                    if (cls > vis.sphere.R)
                    {
                        Msg("~ [DA_VIS]   плоскость %u отвергла ПО СФЕРЕ: удаление %.2f > радиус %.2f "
                            "(нормаль %.2f %.2f %.2f)",
                            i, cls, vis.sphere.R, view.planes[i].n.x, view.planes[i].n.y, view.planes[i].n.z);
                        break;
                    }
                    // ⚠️ AABB_OverlapPlane звать нельзя: она встроенная и тянет frustum_aabb_remap,
                    // который из xrCDB не экспортирован — линковка падает. Считаем сами: ящик
                    // целиком за плоскостью, если даже самая «дальняя по нормали» его вершина
                    // остаётся снаружи.
                    if (_abs(cls) < vis.sphere.R)
                    {
                        const Fvector& n = view.planes[i].n;
                        Fvector far_corner;
                        far_corner.x = (n.x >= 0.f) ? mM[3] : mM[0];
                        far_corner.y = (n.y >= 0.f) ? mM[4] : mM[1];
                        far_corner.z = (n.z >= 0.f) ? mM[5] : mM[2];
                        const float cls_box = n.dotproduct(far_corner) + view.planes[i].d;
                        if (cls_box > 0.f)
                        {
                            Msg("~ [DA_VIS]   плоскость %u отвергла ПО ЯЩИКУ: дальняя вершина %.2f > 0 "
                                "(центр %.2f, радиус %.2f, нормаль %.2f %.2f %.2f)",
                                i, cls_box, cls, vis.sphere.R, n.x, n.y, n.z);
                            break;
                        }
                    }
                }
            }
        }
        return;
    }

    if (o.use_hom && !RImplementation.HOM.visible(vis))
        return;

    if (!da_is_valuable_static(pVisual, o.phase)) // [DA_PORT] geometry cut-off
        return;

    // If we get here visual is visible or partially visible
    switch (pVisual->Type)
    {
    case MT_PARTICLE_GROUP:
    {
        // Xottab_DUTY: for dynamic objects we need matrix,
        // which is nullptr, when we use add_Static
        Log("Dynamic particles added via static procedure. Please, contact Xottab_DUTY and tell him about the issue.");
        NODEFAULT;

        // Add all children, doesn't perform any tests
        /*PS::CParticleGroup* pG = (PS::CParticleGroup*)pVisual;
        for (auto& it : pG->items)
        {
            PS::CParticleGroup::SItem& I = it;
            if (fcvPartial == VIS)
            {
                if (I._effect)
                    add_Dynamic(I._effect, planes);
                for (auto& pit : I._children_related)
                    add_Dynamic(pit, planes);
                for (auto& pit : I._children_free)
                    add_Dynamic(pit, planes);
            }
            else
            {
                if (I._effect)
                    add_leafs_Dynamic(I._effect);
                for (auto& pit : I._children_related)
                    add_leafs_Dynamic(pit);
                for (auto& pit : I._children_free)
                    add_leafs_Dynamic(pit);
            }
        }*/
    }
    break;
    case MT_HIERRARHY:
    {
        // Add all children
        FHierrarhyVisual* pV = (FHierrarhyVisual*)pVisual;
        if (fcvPartial == VIS)
        {
            for (auto& i : pV->children)
                add_static(i, view, planes);
        }
        else
        {
            for (auto& i : pV->children)
                add_leafs_static(i);
        }
    }
    break;
    case MT_SKELETON_ANIM:
    case MT_SKELETON_RIGID:
    {
        // Add all children, doesn't perform any tests
        CKinematics* pV = (CKinematics*)pVisual;
        pV->CalculateBones(TRUE);
        if (fcvPartial == VIS)
        {
            for (auto& i : pV->children)
                add_static(i, view, planes);
        }
        else
        {
            for (auto& i : pV->children)
                add_leafs_static(i);
        }
    }
    break;
    case MT_LOD:
    {
        FLOD* pV = (FLOD*)pVisual;
        float D;
        float ssa = CalcSSA(D, pV->vis.sphere.P, pV);
        ssa *= pV->lod_factor;
        if (ssa < r_ssaLOD_A)
        {
            if (ssa < r_ssaDISCARD)
                return;
            mapLOD.insert_anyway(D, _LodItem({ ssa, pVisual }));
        }
#if RENDER != R_R1
        if (ssa > r_ssaLOD_B || o.phase == CRender::PHASE_SMAP)
#else
        if (ssa > r_ssaLOD_B)
#endif
        {
            // Add all children, perform tests
            for (auto& i : pV->children)
                add_leafs_static(i);
        }
    }
    break;
    case MT_TREE_ST:
    case MT_TREE_PM:
    {
        // General type of visual
        insert_static(pVisual);
    }
        return;
    default:
    {
        // General type of visual
        insert_static(pVisual);
    }
    break;
    }
}

void R_dsgraph_structure::load(const xr_vector<CSector::level_sector_data_t>& sectors_data,
    const xr_vector<CPortal::level_portal_data_t>& portals_data)
{
    ZoneScoped;

    const auto portals_count = portals_data.size();
    const auto sectors_count = sectors_data.size();

    Sectors.resize(sectors_count);
    Portals.resize(portals_count);

    for (u32 idx = 0; idx < portals_count; ++idx)
    {
        auto* portal = xr_new<CPortal>();
        Portals[idx] = portal;
    }

    for (u32 idx = 0; idx < sectors_count; ++idx)
    {
        auto* sector = xr_new<CSector>();

        sector->unique_id = static_cast<IRender_Sector::sector_id_t>(idx);
        sector->setup(sectors_data[idx], Portals);
        Sectors[idx] = sector;
    }

    for (u32 idx = 0; idx < portals_count; ++idx)
    {
        auto* portal = static_cast<CPortal*>(Portals[idx]);

        portal->setup(portals_data[idx], Sectors);
    }
}

void R_dsgraph_structure::unload()
{
    for (auto* sector : Sectors)
        xr_delete(sector);
    Sectors.clear();

    for (auto* portal : Portals)
        xr_delete(portal);
    Portals.clear();
}


// sub-space rendering - main procedure
void R_dsgraph_structure::build_subspace()
{
    ZoneScoped;

    marker++; // !!! critical here

    if (o.precise_portals && RImplementation.rmPortals)
    {
        // Check if camera is too near to some portal - if so force DualRender
        Fvector box_radius;
        box_radius.set(o.query_box_side, o.query_box_side, o.query_box_side);
        Sectors_xrc.box_query(CDB::OPT_FULL_TEST, RImplementation.rmPortals, o.view_pos, box_radius);
        for (size_t K = 0; K < Sectors_xrc.r_count(); K++)
        {
            CPortal* pPortal = Portals[RImplementation.rmPortals->get_tris()[Sectors_xrc.r_begin()[K].id].dummy];
            pPortal->bDualRender = TRUE;
        }
    }

    if (o.is_main_pass && (o.sector_id == IRender_Sector::INVALID_SECTOR_ID))
    {
        if (g_pGameLevel)
            g_pGameLevel->pHUD->Render_Last(context_id);
        return;
    }

    // [DA_PORT] Из чего складывается расчёт видимости: порталы / статика / динамика.
    //
    // ЗАЧЕМ. Замер доказал, что ожидание этого расчёта -- ЧИСТЫЙ простой главного потока (1.5
    // украденных задачи против 22000 холостых витков), и стоит он 1.32 мс при кадре 5.50. Убрать его
    // -- это +30% кадров. Но убрать можно только распараллеливанием, а распараллеливать надо ДОРОГОЕ:
    // обход порталов последовательный по своей природе, и если время лежит в нём, вся затея с
    // разделением карт визуалов по номеру потока не даст ничего.
    //
    // Границы выбраны по смыслу: обход секторов и порталов, обход статической геометрии (именно его
    // и предлагали распараллелить в закрытой `#if 0` ветке ниже) и сбор динамики из дерева объектов.
    //
    // Только основной проход: у теневых каскадов свои вызовы, и смешивать их в одну копилку значит
    // получить сумму пяти разных задач вместо той одной, которую ждём.
    const bool da_prof = ps_da_cull_prof && o.is_main_pass;
    CTimer da_timer;
    double da_ms_portals = 0.0, da_ms_statics = 0.0;

    // [DA_PORT] Динамика разбирается отдельно: первый замер отдал ей 74% (1.15 мс из 1.56), а
    // порталам 1% -- значит дробить надо здесь. Четыре куска: сбор из дерева объектов, сортировка
    // по дальности, определение сектора и всё остальное тело цикла.
    //
    // ⚠️ detect_sector меряется ПОКАДРОВЫМ накоплением, то есть двумя обращениями к часам на объект.
    // При тысяче объектов это порядка 4% от измеряемой величины -- её собственный результат слегка
    // завышен, и вывод «дорого» из него делать можно, а «дёшево» надёжнее.
    CTimer da_sector_timer;
    double da_ms_dyn_collect = 0.0, da_ms_dyn_sort = 0.0, da_ms_dyn_sector = 0.0;
    u32 da_dyn_count = 0;

    // Скольким объектам сектор реально пересчитали. Без этого числа «сектор стал 0.00» неотличимо
    // от «сцена случайно оказалась неподвижной», и правку нельзя ни подтвердить, ни опровергнуть.
    u32 da_dyn_sector_hits = 0;

    // [DA_PORT] Тело цикла делится на два куска с разной природой: программный тест перекрытия и
    // построение списков отрисовки. Второй уходит в игру (renderable_Render -> визуалы, кости, LOD),
    // первый целиком наш. Лечатся они по-разному, поэтому и меряются врозь.
    CTimer da_body_timer;
    double da_ms_dyn_hom = 0.0, da_ms_dyn_render = 0.0;

    if (da_prof)
    {
        // Копилки скелетов обнуляются здесь, а не в конце: их наполняет add_leafs_dynamic, которую
        // зовут из середины этой же функции, и обнулять их после отчёта значило бы терять кадр.
        s_da_bones_ms = 0.0;
        s_da_wallmarks_ms = 0.0;
        s_da_skeletons = 0;
        s_da_leafs = 0;
        s_da_skel_exact = 0;
        da_timer.Start();
    }

    // Traverse sector/portal structure
    PortalTraverser.traverse(Sectors[o.sector_id], o.view_frustum, o.view_pos, o.xform, o.portal_traverse_flags);

    if (da_prof)
    {
        da_ms_portals = da_timer.GetElapsed_sec() * 1000.0;
        da_timer.Start();
    }

    // Determine visibility for static geometry hierarchy
#if 0
    static xr_vector<Task*> static_geo_tasks;
    static_geo_tasks.resize(PortalTraverser.r_sectors.size());
#endif

    if (psDeviceFlags.test(rsDrawStatic) && !o.skip_static)
    {
        for (u32 s_it = 0; s_it < PortalTraverser.r_sectors.size(); s_it++)
        {
            CSector* sector = PortalTraverser.r_sectors[s_it];
            dxRender_Visual* root = sector->root();
            //VERIFY(root->getType() == MT_HIERRARHY);

            const auto &children = static_cast<FHierrarhyVisual*>(root)->children;

            // [DA_PORT] Ручка r__portal_frustum 0 — обходить статику ПИРАМИДОЙ ПРОХОДА, а не
            // портальными.
            //
            // Замер довёл пропажу объекта до этой точки: три сегмента мачты снимаются проверкой по
            // пирамиде, плоскостью с осевой нормалью (-0.99 -0.01 0.11), их ящик снаружи на ПЯТЬ
            // САНТИМЕТРОВ при расстоянии 54 м. Осевая нормаль — почерк портала, а не камеры:
            // боковая плоскость камеры при 67° выглядела бы как (±0.83 · 0.55).
            //
            // Обход порталов у нас дословно совпадает с оригиналом (вплоть до аргументов
            // CreateFromPortal), формулы порогов тоже, а в оригинальном 32-битном движке объект на
            // месте. Значит расходится не код обхода, а то, какие пирамиды в итоге получает сектор.
            // Ручка отвечает на это одним переключением: если с камерной пирамидой объект
            // возвращается — сужение портальное, и чинить надо там.
            //
            // ⚠️ Отсечение по порталам — это то, ради чего в помещениях не рисуется весь уровень.
            // Ручка отладочная, для постоянного использования не предназначена.
            if (!ps_da_portal_frustum)
            {
                add_static(root, o.view_frustum, o.view_frustum.getMask());
                continue;
            }

            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
            {
#if 0
                const auto traverse_children = [&, this](const TaskRange<size_t>& range)
                {
                    for (size_t id = range.cbegin(); id != range.cend(); ++id)
                    {
                        const auto& view = sector->r_frustums[v_it];
                        add_static(children[id], view, view.getMask());
                    }
                };

                if (o.mt_calculate) // NOTE: this code doesn't work until visuals maps are separated by worker ID.
                {
                    static_geo_tasks[s_it] = &xr_parallel_for(TaskRange<size_t>(0, children.size()), false, traverse_children);
                }
                else
                {
                    traverse_children(TaskRange<size_t>(0, children.size()));
                }
#else
                const auto& view = sector->r_frustums[v_it];
                add_static(root, view, view.getMask());
#endif
            }
        }
    }

    if (da_prof)
    {
        da_ms_statics = da_timer.GetElapsed_sec() * 1000.0;
        da_timer.Start();
    }

    const bool collect_dynamic_any = (o.spatial_types != 0) && psDeviceFlags.test(rsDrawDynamic);

    if (collect_dynamic_any)
    {
        // Traverse object database
        // [DA_PORT] Либо дерево, либо готовый список -- разбор у o.dyn_source.
        if (o.dyn_source)
        {
            lstRenderables.clear();
            lstRenderables.reserve(o.dyn_source->size());
            const u32 base_mask = o.view_frustum.getMask();
            for (ISpatial* S : *o.dyn_source)
            {
                SpatialData& sd = S->GetSpatialData();
                if (0 == (sd.type & o.spatial_types))
                    continue;
                Fvector sC = sd.sphere.P;
                u32 tmask = base_mask;
                if (fcvNone == o.view_frustum.testSphere(sC, sd.sphere.R, tmask))
                    continue;
                lstRenderables.push_back(S);
            }
        }
        else
        {
            g_pGamePersistent->SpatialSpace.q_frustum(lstRenderables, o.spatial_traverse_flags, o.spatial_types, o.view_frustum);
        }

        if (da_prof)
        {
            da_ms_dyn_collect = da_timer.GetElapsed_sec() * 1000.0;
            da_dyn_count = (u32)lstRenderables.size();
            da_timer.Start();
        }

        if (o.spatial_traverse_flags & ISpatial_DB::O_ORDERED) // this should be inside of query functions
        {
            // Exact sorting order (front-to-back)
            std::sort(lstRenderables.begin(), lstRenderables.end(), [&](ISpatial* s1, ISpatial* s2)
                {
                    const float d1 = s1->GetSpatialData().sphere.P.distance_to_sqr(o.view_pos);
                    const float d2 = s2->GetSpatialData().sphere.P.distance_to_sqr(o.view_pos);
                    return d1 < d2;
                });
        }

        if (da_prof)
        {
            da_ms_dyn_sort = da_timer.GetElapsed_sec() * 1000.0;
            da_timer.Start();
        }

        u32 uID_LTRACK = 0xffffffff;
        if (o.is_main_pass) // temporary
        {
            if (o.phase == CRender::PHASE_NORMAL)
            {
                RImplementation.uLastLTRACK++;
                if (!lstRenderables.empty())
                    uID_LTRACK = RImplementation.uLastLTRACK % lstRenderables.size();

                // update light-vis for current entity / actor
                IGameObject* O = g_pGameLevel->CurrentViewEntity();
                if (O)
                {
                    CROS_impl* R = (CROS_impl*)O->ROS();
                    if (R)
                        R->update(O);
                }
            }
        }

        const bool collect_lights = o.spatial_types & STYPE_LIGHTSOURCE;

        // Determine visibility for dynamic part of scene
        for (u32 o_it = 0; o_it < lstRenderables.size(); o_it++)
        {
            ISpatial* spatial = lstRenderables[o_it];
            // [DA_PORT] Сектор определяется только тем, у кого он помечен устаревшим.
            //
            // ⛔ Это НЕ эвристика «раз в сколько-то кадров» и не кэш: проверка ровно та же, что
            // стояла шагом ниже, просто теперь она стоит ДО работы, а не после.
            //
            // Было так: detect_sector считался для КАЖДОГО объекта в кадре, а результат уходил в
            // spatial_updatesector, который начинается с
            //
            //     if (0 == (spatial.type & STYPEFLAG_INVALIDSECTOR)) return;
            //
            // -- то есть для неподвижного объекта посчитанное просто выбрасывалось. А стоило это
            // недёшево: detect_sector пускает луч вниз по модели порталов И по статической геометрии
            // (а если вниз не нашлось -- ещё два вверх), то есть до четырёх запросов к базе
            // столкновений на объект на кадр.
            //
            // Флаг ведёт сам движок и ведёт полно: spatial_register взводит его новому объекту,
            // spatial_move -- сдвинувшемуся, spatial_updatesector_internal снимает после записи
            // сектора. Сектор зависит только от положения и статической геометрии уровня, поэтому у
            // не сдвинувшегося объекта пересчитывать нечего.
            //
            // Замер: 0.47 мс из 1.20 мс всей динамики уходило именно сюда, при 355 объектах в кадре.
            //
            // ⚠️ Случай «detect_sector вернул недействительный сектор» ведёт себя как прежде:
            // spatial_updatesector_internal и раньше снимал флаг, не записав номер, -- повторной
            // попытки не было и тогда.
            if (o.is_main_pass && (spatial->GetSpatialData().type & STYPEFLAG_INVALIDSECTOR))
            {
                if (da_prof)
                    da_sector_timer.Start();

                const auto& entity_pos = spatial->spatial_sector_point();
                const auto sector_id = detect_sector(entity_pos);
                spatial->spatial_updatesector(sector_id);

                if (da_prof)
                {
                    da_ms_dyn_sector += da_sector_timer.GetElapsed_sec() * 1000.0;
                    ++da_dyn_sector_hits;
                }
            }
            const auto& data = spatial->GetSpatialData();
            const auto& [type, sphere, sector_id] = std::tuple(data.type, data.sphere, data.sector_id);
            // [DA_PORT] Номер сектора ограничивается по размеру массива, а не только сверяется с
            // «недействительным». Он приходит из detect_sector, а тот получает его лучом по МОДЕЛИ
            // ПОРТАЛОВ, то есть из данных уровня: рассинхрон портальной модели и списка секторов
            // даёт номер вне диапазона, и `Sectors[sector_id]` читает за границей.
            if (sector_id == IRender_Sector::INVALID_SECTOR_ID || sector_id >= Sectors.size())
                continue; // disassociated from S/P structure
            auto* sector = Sectors[sector_id];

            if (collect_lights && (type & STYPE_LIGHTSOURCE))
            {
                // lightsource
                light* L = (light*)spatial->dcast_Light();
                VERIFY(L);
                float lod = L->get_LOD();
                if (lod > EPS_L)
                {
                    // TODO: check for HOM flag
                    vis_data& vis = L->get_homdata();
                    if (RImplementation.HOM.visible(vis))
                        RImplementation.Lights.add_light(L);
                }
                continue;
            }

            // [DA_PORT] Зонд ВТОРОГО пути. Первая версия стояла только на статике уровня, и это
            // оказалось половиной конвейера: объекты игры идут сюда, через список отрисовываемых и
            // ПРОСТРАНСТВЕННУЮ сферу — она хранится отдельно от сферы модели и может с ней
            // разойтись. Без этой ветки «объекта нет в дампе» читалось как «он не отсекается».
            float da_d_dyn = 0.f;
            bool da_watch_dyn = da_vis_armed();
            if (da_watch_dyn)
            {
                da_d_dyn = Device.vCameraPosition.distance_to(spatial->GetSpatialData().sphere.P);
                da_watch_dyn = da_d_dyn <= ps_da_vis_radius;
                if (da_watch_dyn)
                    da_vis_seen.fetch_add(1, std::memory_order_relaxed);
            }

            if (PortalTraverser.i_marker != sector->r_marker)
            {
                if (da_watch_dyn)
                {
                    da_vis_dropped.fetch_add(1, std::memory_order_relaxed);
                    Msg("~ [DA_VIS] ОБЪЕКТ фаза %u  xyz %.1f %.1f %.1f  R %.2f  d %.2f  СНЯТ: сектор %u не "
                        "помечен обходом",
                        o.phase, spatial->GetSpatialData().sphere.P.x, spatial->GetSpatialData().sphere.P.y,
                        spatial->GetSpatialData().sphere.P.z, spatial->GetSpatialData().sphere.R, da_d_dyn,
                        sector_id);
                }
                continue; // inactive (untouched) sector
            }

            bool da_any_frustum = false;
            for (u32 v_it = 0; v_it < sector->r_frustums.size(); v_it++)
            {
                const CFrustum& view = sector->r_frustums[v_it];
                if (!view.testSphere_dirty(spatial->GetSpatialData().sphere.P, spatial->GetSpatialData().sphere.R))
                    continue;
                if (da_watch_dyn && !da_any_frustum)
                {
                    da_any_frustum = true;
                    Msg("~ [DA_VIS] ОБЪЕКТ фаза %u  xyz %.1f %.1f %.1f  R %.2f  d %.2f  ПРОШЁЛ пирамиду %u из %u",
                        o.phase, spatial->GetSpatialData().sphere.P.x, spatial->GetSpatialData().sphere.P.y,
                        spatial->GetSpatialData().sphere.P.z, spatial->GetSpatialData().sphere.R, da_d_dyn, v_it,
                        (u32)sector->r_frustums.size());
                }

                if (o.is_main_pass)
                {
                    if (type & STYPE_RENDERABLE)
                    {
                        // renderable
                        IRenderable* renderable = spatial->dcast_Renderable();
                        VERIFY(renderable);

                        // Occlusion
                        //	casting is faster then using getVis method
                        if (da_prof)
                            da_body_timer.Start();

                        vis_data& v_orig = ((dxRender_Visual*)renderable->GetRenderData().visual)->vis;
                        vis_data v_copy = v_orig;
                        v_copy.box.xform(renderable->GetRenderData().xform);
                        BOOL bVisible = RImplementation.HOM.visible(v_copy);
                        memcpy(v_orig.marker, v_copy.marker, sizeof(v_copy.marker));
                        v_orig.accept_frame = v_copy.accept_frame;
                        v_orig.hom_frame = v_copy.hom_frame;
                        v_orig.hom_tested = v_copy.hom_tested;

                        if (da_prof)
                            da_ms_dyn_hom += da_body_timer.GetElapsed_sec() * 1000.0;

                        if (!bVisible)
                            break; // exit loop on frustums

                        // update light-vis for selected entity
                        if (o_it == uID_LTRACK && renderable->renderable_ROS())
                        {
                            // track lighting environment
                            CROS_impl* T = (CROS_impl*)renderable->renderable_ROS();
                            T->update(renderable);
                        }

                        // Rendering
                        if (da_prof)
                            da_body_timer.Start();

                        renderable->renderable_Render(context_id, renderable);

                        if (da_prof)
                            da_ms_dyn_render += da_body_timer.GetElapsed_sec() * 1000.0;
                    }
                    break; // exit loop on frustums
                }
                else
                {
                    // renderable
                    IRenderable* renderable = spatial->dcast_Renderable();
                    if (nullptr == renderable)
                        continue; // unknown, but renderable object (r1_glow???)

                    renderable->renderable_Render(context_id, nullptr);
                }
            }

            // [DA_PORT] Ни одна пирамида сектора объект не приняла — это и есть «пропал».
            if (da_watch_dyn && !da_any_frustum)
            {
                da_vis_dropped.fetch_add(1, std::memory_order_relaxed);
                Msg("~ [DA_VIS] ОБЪЕКТ фаза %u  xyz %.1f %.1f %.1f  R %.2f  d %.2f  СНЯТ: ни одна из %u "
                    "пирамид сектора %u не приняла",
                    o.phase, spatial->GetSpatialData().sphere.P.x, spatial->GetSpatialData().sphere.P.y,
                    spatial->GetSpatialData().sphere.P.z, spatial->GetSpatialData().sphere.R, da_d_dyn,
                    (u32)sector->r_frustums.size(), sector_id);
            }
        }

        if (g_pGameLevel)
        {
#if RENDER != R_R1
            // Actor Shadow (Sun + Light)
            if (o.phase == CRender::PHASE_SMAP && ps_r__common_flags.test(RFLAG_ACTOR_SHADOW))
            {
                do
                {
                    IGameObject* viewEntity = g_pGameLevel->CurrentViewEntity();
                    if (viewEntity == nullptr)
                        break;
                    const auto& entity_pos = viewEntity->spatial_sector_point();
                    viewEntity->spatial_updatesector(detect_sector(entity_pos));
                    const auto sector_id = viewEntity->GetSpatialData().sector_id;
                    // [DA_PORT] См. разбор выше: границу массива тоже проверяем.
                    if (sector_id == IRender_Sector::INVALID_SECTOR_ID || sector_id >= Sectors.size())
                        break; // disassociated from S/P structure
                    CSector* sector = Sectors[sector_id];
                    if (PortalTraverser.i_marker != sector->r_marker)
                        break; // inactive (untouched) sector
                    for (const CFrustum& view : sector->r_frustums)
                    {
                        if (!view.testSphere_dirty(
                            viewEntity->GetSpatialData().sphere.P, viewEntity->GetSpatialData().sphere.R))
                            continue;

                        // renderable
                        // [DA_PORT] Здесь строится ТЕНЕВОЕ подпространство, а не картинка для
                        // игрока, поэтому и модель нужна своя — целая, а не перволичные ноги.
                        g_pGameLevel->pHUD->Render_ActorShadow(context_id);
                    }
                } while (0);
            }
#endif

            // [DA_PORT] Перволичное тело: модель актёра в ГЛАВНОМ проходе.
            //
            // В первом лице актёр помечен невидимым (CActor::UpdateCL: setVisible(!HUDview())), то
            // есть в обычный обход он не попадает вовсе — ровно поэтому, посмотрев вниз, игрок не
            // видел ничего. Единственный впрыск модели, что здесь был, работал только в фазе
            // теневой карты выше.
            //
            // У самого Dead Air этого нет: в его движке тело CHUDManager::Render_First
            // закомментировано целиком, а модели actors\legs\*.ogf лежат в конфигах без потребителя.
            // Это наша добавка, и она именно на них рассчитана — они без головы и рук, то есть
            // сделаны под взгляд изнутри.
            if (o.is_main_pass)
            {
                g_pGameLevel->pHUD->Render_First(context_id);
                g_pGameLevel->pHUD->Render_Last(context_id);
            }
        }
    }

#if 0
    // wait for static geo collecting to be done.
    for (auto* task : static_geo_tasks)
    {
        if (task)
            TaskScheduler->Wait(*task);
    }
#endif

    // [DA_PORT] Разбор ожидания по частям -- разбор у da_prof выше. Ранних выходов между замером и
    // этой строкой нет, единственный `return` стоит ДО старта таймера, так что счётчик не врёт
    // недосчитанным кадром.
    if (da_prof)
    {
        DA_CULL_PARTS(da_ms_portals, da_ms_statics,
            da_ms_dyn_collect + da_ms_dyn_sort + da_timer.GetElapsed_sec() * 1000.0);
        DA_CULL_DYN(da_ms_dyn_collect, da_ms_dyn_sort, da_ms_dyn_sector, da_dyn_count, da_dyn_sector_hits);
        DA_CULL_BODY(da_ms_dyn_hom, da_ms_dyn_render);
        DA_CULL_SKEL(s_da_bones_ms, s_da_wallmarks_ms, s_da_skeletons, s_da_leafs, s_da_skel_exact);
    }
}
} // namespace xray::render::RENDER_NAMESPACE
