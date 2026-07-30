// DetailManager.cpp: implementation of the CDetailManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#pragma hdrstop

#include "DetailManager.h"
#include "xrCDB/Intersect.hpp"

#ifdef _EDITOR
#include "ESceneClassList.h"
#include "Scene.h"
#include "SceneObject.h"
#include "IGame_Persistent.h"
#include "Environment.h"
#else
#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/Environment.h"

#include "xrCore/Threading/TaskManager.hpp"

// [DA_PORT] Множители качания травы, см. MT_Render ниже. Определены в xrEngine/xr_ioc_cmd.cpp.
extern ENGINE_API float ps_r__grass_sway;
extern ENGINE_API float ps_r__grass_sway_speed;

#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K) || defined(XR_ARCHITECTURE_PPC64)
#include <xmmintrin.h>
#elif defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64)
#include "sse2neon/sse2neon.h"
#elif defined(XR_ARCHITECTURE_RISCV)
#include "sse2rvv/sse2rvv.h"
#else
#error Add your platform here
#endif
#endif

namespace xray::render::RENDER_NAMESPACE
{
const float dbgOffset = 0.f;
const int dbgItems = 128;

//--------------------------------------------------- Decompression
static int magic4x4[4][4] = {{0, 14, 3, 13}, {11, 5, 8, 6}, {12, 2, 15, 1}, {7, 9, 4, 10}};

void bwdithermap(int levels, int magic[16][16])
{
    /* Get size of each step */
    float N = 255.0f / (levels - 1);

    /*
     * Expand 4x4 dither pattern to 16x16.  4x4 leaves obvious patterning,
     * and doesn't give us full intensity range (only 17 sublevels).
     *
     * magicfact is (N - 1)/16 so that we get numbers in the matrix from 0 to
     * N - 1: mod N gives numbers in 0 to N - 1, don't ever want all
     * pixels incremented to the next level (this is reserved for the
     * pixel value with mod N == 0 at the next level).
     */

    float magicfact = (N - 1) / 16;
    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            for (int k = 0; k < 4; k++)
            {
                for (int l = 0; l < 4; l++)
                {
                    magic[4 * k + i][4 * l + j] =
                        (int)(0.5 + magic4x4[i][j] * magicfact + (magic4x4[k][l] / 16.) * magicfact);
                }
            }
        }
    }
}
//--------------------------------------------------- Decompression

void CDetailManager::SSwingValue::lerp(const SSwingValue& A, const SSwingValue& B, float f)
{
    float fi = 1.f - f;
    amp1 = fi * A.amp1 + f * B.amp1;
    amp2 = fi * A.amp2 + f * B.amp2;
    rot1 = fi * A.rot1 + f * B.rot1;
    rot2 = fi * A.rot2 + f * B.rot2;
    speed = fi * A.speed + f * B.speed;
}
//---------------------------------------------------

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
// XXX stats: add to statistics
CDetailManager::CDetailManager() : xrc("detail manager")
{
    ZoneScoped;

    dtFS = nullptr;
    dtSlots = nullptr;
    soft_Geom = nullptr;
    hw_Geom = nullptr;
    hw_BatchSize = 0;
    m_time_rot_1 = 0;
    m_time_rot_2 = 0;
    m_time_pos = 0;
    m_global_time_old = 0;
    // [DA_PORT] motion vectors: no previous sway yet, and an uninitialised flag here would let the very
    // first frame treat uninitialised phases as the previous ones.
    m_time_rot_1_old = 0;
    m_time_rot_2_old = 0;
    m_time_pos_old = 0;
    m_swing_seeded = false;

    // KD: variable detail radius
    dm_size = dm_current_size;
    dm_cache_line = dm_current_cache_line;
    dm_cache1_line = dm_current_cache1_line;
    dm_cache_size = dm_current_cache_size;
    dm_fade = dm_current_fade;
    ps_r__Detail_density = ps_current_detail_density;
    ps_current_detail_height = ps_r__Detail_height;
    cache_level1 = (CacheSlot1**)xr_malloc(dm_cache1_line * sizeof(CacheSlot1*));
    for (u32 i = 0; i < dm_cache1_line; ++i)
    {
        cache_level1[i] = (CacheSlot1*)xr_malloc(dm_cache1_line * sizeof(CacheSlot1));
        for (u32 j = 0; j < dm_cache1_line; ++j)
            new(&cache_level1[i][j]) CacheSlot1();
    }
    cache = (Slot***)xr_malloc(dm_cache_line * sizeof(Slot**));
    for (u32 i = 0; i < dm_cache_line; ++i)
        cache[i] = (Slot**)xr_malloc(dm_cache_line * sizeof(Slot*));

    cache_pool = (Slot *)xr_malloc(dm_cache_size * sizeof(Slot));

    for (u32 i = 0; i < dm_cache_size; ++i)
        new(&cache_pool[i]) Slot();
    /*
    CacheSlot1 cache_level1[dm_cache1_line][dm_cache1_line];
    Slot* cache [dm_cache_line][dm_cache_line]; // grid-cache itself
    Slot cache_pool [dm_cache_size]; // just memory for slots
    */
}

CDetailManager::~CDetailManager()
{
    ZoneScoped;

    // [DA_PORT] И здесь тоже: удаление приходит не только через Unload. См. WaitCalcTask.
    WaitCalcTask();

    for (u32 i = 0; i < dm_cache_size; ++i)
        cache_pool[i].~Slot();
    xr_free(cache_pool);

    for (u32 i = 0; i < dm_cache_line; ++i)
        xr_free(cache[i]);
    xr_free(cache);

    for (u32 i = 0; i < dm_cache1_line; ++i)
    {
        for (u32 j = 0; j < dm_cache1_line; ++j)
            cache_level1[i][j].~CacheSlot1();
        xr_free(cache_level1[i]);
    }
    xr_free(cache_level1);
}

#ifndef _EDITOR

/*
void dump(CDetailManager::vis_list& lst)
{
    for (int i = 0; i<lst.size(); i++)
    {
        Msg("%8x / %8x / %8x",  lst[i]._M_start, lst[i]._M_finish, lst[i]._M_end_of_storage._M_data);
    }
}
*/
void CDetailManager::Load()
{
    ZoneScoped;

    // Open file stream
    if (!FS.exist("$level$", "level.details"))
    {
        dtFS = nullptr;
        return;
    }

    string_path fn;
    FS.update_path(fn, "$level$", "level.details");
    dtFS = FS.r_open(fn);

    // Header
    dtFS->r_chunk_safe(0, &dtH, sizeof(dtH));
    R_ASSERT(dtH.version() == DETAIL_VERSION);
    u32 m_count = dtH.object_count();
    objects.reserve(m_count);

    // Models
    IReader* m_fs = dtFS->open_chunk(1);
    for (u32 m_id = 0; m_id < m_count; m_id++)
    {
        CDetail* dt = xr_new<CDetail>();
        IReader* S = m_fs->open_chunk(m_id);
        dt->Load(S);
        objects.push_back(dt);
        S->close();
    }
    m_fs->close();

    // Get pointer to database (slots)
    IReader* m_slots = dtFS->open_chunk(2);
    dtSlots = (DetailSlot*)m_slots->pointer();
    m_slots->close();

    // Initialize 'vis' and 'cache'
    for (u32 i = 0; i < 3; ++i)
        m_visibles[i].resize(objects.size());
    cache_Initialize();

    // Make dither matrix
    bwdithermap(2, dither);

    // Hardware specific optimizations
    if (UseVS())
        hw_Load();
    else
        soft_Load();

    // swing desc
    // normal
    swing_desc[0].amp1 = pSettings->r_float("details", "swing_normal_amp1");
    swing_desc[0].amp2 = pSettings->r_float("details", "swing_normal_amp2");
    swing_desc[0].rot1 = pSettings->r_float("details", "swing_normal_rot1");
    swing_desc[0].rot2 = pSettings->r_float("details", "swing_normal_rot2");
    swing_desc[0].speed = pSettings->r_float("details", "swing_normal_speed");
    // fast
    swing_desc[1].amp1 = pSettings->r_float("details", "swing_fast_amp1");
    swing_desc[1].amp2 = pSettings->r_float("details", "swing_fast_amp2");
    swing_desc[1].rot1 = pSettings->r_float("details", "swing_fast_rot1");
    swing_desc[1].rot2 = pSettings->r_float("details", "swing_fast_rot2");
    swing_desc[1].speed = pSettings->r_float("details", "swing_fast_speed");
}
#endif
// [DA_PORT] Дождаться фоновой задачи расчёта травы. Без этого её нельзя ни выгружать, ни удалять.
//
// Задача читает кэш и список объектов этого самого менеджера, а сброс устройства менеджер УДАЛЯЕТ:
// смена высоты или плотности травы в настройках приводит к `Details->Unload(); xr_delete(Details);`
// в CRender::reset_begin, и делается это, пока задача считает в рабочем потоке. Дальше обращение по
// освобождённой памяти - падение в чужом потоке, где по стеку видно только cache_Update и
// декомпрессию, а причина осталась в главном.
//
// Проверка `if (nullptr == RImplementation.Details) return;` в самой задаче от этого НЕ защищает:
// она стоит на входе, а гонка происходит после него. Такую защиту легко принять за достаточную -
// поэтому здесь и написано, почему она не достаточна.
void CDetailManager::WaitCalcTask()
{
    if (m_calc_task)
    {
        TaskScheduler->Wait(*m_calc_task);
        m_calc_task = nullptr;
    }

    // [DA_PORT] Ожидания по указателю на задачу ОКАЗАЛОСЬ МАЛО - падение повторилось с тем же
    // стеком уже после него. Поэтому ждём по признаку, который ставит и снимает сама задача:
    // он не зависит ни от того, жив ли объект задачи, ни от того, сколько их успели поставить в
    // очередь. Указатель хранит только ПОСЛЕДНЮЮ задачу, а за кадр без отрисовки травы их может
    // накопиться несколько - ожидание последней тогда ничего не говорит о предыдущих.
    //
    // Задача короткая (обход кэша), поэтому уступаем время, а не крутимся вхолостую. Потолок
    // ожидания есть, и его достижение - это сообщение, а не молчание: если признак не снялся за
    // секунду, значит дело не в гонке, и запись об этом важнее, чем зависший наглухо кадр.
    if (m_calc_active.load(std::memory_order_acquire))
    {
        const u64 started = CPU::QPC();
        u32 spins = 0;
        while (m_calc_active.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
            ++spins;
            if (float(double(CPU::QPC() - started) / double(CPU::qpc_freq)) > 1.f)
            {
                Msg("! [DA_PORT] расчёт травы не завершился за секунду, продолжаю без него");
                break;
            }
        }
        Msg("~ [DA_PORT] ожидание расчёта травы: %u уступок времени", spins);
    }
}

void CDetailManager::Unload()
{
    ZoneScoped;
    WaitCalcTask(); // [DA_PORT] см. WaitCalcTask: ниже освобождается то, что задача читает

    if (UseVS())
        hw_Unload();
    else
        soft_Unload();

    for (CDetail* detailObject : objects)
    {
        detailObject->Unload();
        xr_delete(detailObject);
    }

    objects.clear();
    m_visibles[0].clear();
    m_visibles[1].clear();
    m_visibles[2].clear();
    FS.r_close(dtFS);
}

extern ECORE_API float r_ssaDISCARD;

void CDetailManager::UpdateVisibleM()
{
    ZoneScoped;

    for (int i = 0; i != 3; ++i)
        for (auto& vis : m_visibles[i])
            vis.clear();

    CFrustum View;
    View.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB + FRUSTUM_P_FAR);

    float fade_limit = dm_fade;
    fade_limit = fade_limit * fade_limit;
    float fade_start = 1.f;
    fade_start = fade_start * fade_start;
    float fade_range = fade_limit - fade_start;
    float r_ssaCHEAP = 16 * r_ssaDISCARD;

    // Initialize 'vis' and 'cache'
    // Collect objects for rendering
    RImplementation.BasicStats.DetailVisibility.Begin();
    for (u32 _mz = 0; _mz < dm_cache1_line; _mz++)
    {
        for (u32 _mx = 0; _mx < dm_cache1_line; _mx++)
        {
            CacheSlot1& MS = cache_level1[_mz][_mx];
            if (MS.empty)
            {
                continue;
            }
            u32 mask = 0xff;
            u32 res = View.testSAABB(MS.vis.sphere.P, MS.vis.sphere.R, MS.vis.box.data(), mask);
            if (fcvNone == res)
            {
                continue; // invisible-view frustum
            }
            // test slots

            u32 dwCC = dm_cache1_count * dm_cache1_count;

            for (u32 _i = 0; _i < dwCC; _i++)
            {
                Slot* PS = *MS.slots[_i];
                Slot& S = *PS;

                //if (_i+1<dwCC);
                //    _mm_prefetch((char*)*MS.slots[_i+1], _MM_HINT_T1);

                // if slot empty - continue
                if (S.empty)
                {
                    continue;
                }

                // if upper test = fcvPartial - test inner slots
                if (fcvPartial == res)
                {
                    u32 _mask = mask;
                    u32 _res = View.testSAABB(S.vis.sphere.P, S.vis.sphere.R, S.vis.box.data(), _mask);
                    if (fcvNone == _res)
                    {
                        continue; // invisible-view frustum
                    }
                }
#ifndef _EDITOR
                if (!RImplementation.HOM.visible(S.vis))
                {
                    continue; // invisible-occlusion
                }
#endif
                // Add to visibility structures
                if (Device.dwFrame > S.frame)
                {
                    // Calc fade factor (per slot)
                    float dist_sq = EYE.distance_to_sqr(S.vis.sphere.P);
                    if (dist_sq > fade_limit)
                        continue;
                    float alpha = (dist_sq < fade_start) ? 0.f : (dist_sq - fade_start) / fade_range;
                    float alpha_i = 1.f - alpha;
                    float dist_sq_rcp = 1.f / dist_sq;

                    S.frame = Device.dwFrame + Random.randI(15, 30);
                    for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                    {
                        SlotPart& sp = S.G[sp_id];
                        if (sp.id == DetailSlot::ID_Empty)
                            continue;

                        sp.r_items[0].clear();
                        sp.r_items[1].clear();
                        sp.r_items[2].clear();

                        float R = objects[sp.id]->bv_sphere.R;
                        float Rq_drcp = R * R * dist_sq_rcp; // reordered expression for 'ssa' calc

                        for(auto& siIT : sp.items)
                        {
                            SlotItem& Item = *siIT;
                            float scale = Item.scale_calculated = Item.scale * alpha_i;
                            float ssa = scale * scale * Rq_drcp;
                            if (ssa < r_ssaDISCARD)
                            {
                                continue;
                            }
                            u32 vis_id = 0;
                            if (ssa > r_ssaCHEAP)
                                vis_id = Item.vis_ID;

                            sp.r_items[vis_id].push_back(siIT);

                            // 2 visible[vis_id][sp.id].push_back(&Item);
                        }
                    }
                }
                for (int sp_id = 0; sp_id < dm_obj_in_slot; sp_id++)
                {
                    SlotPart& sp = S.G[sp_id];
                    if (sp.id == DetailSlot::ID_Empty)
                        continue;
                    if (!sp.r_items[0].empty())
                    {
                        m_visibles[0][sp.id].push_back(&sp.r_items[0]);
                    }
                    if (!sp.r_items[1].empty())
                    {
                        m_visibles[1][sp.id].push_back(&sp.r_items[1]);
                    }
                    if (!sp.r_items[2].empty())
                    {
                        m_visibles[2][sp.id].push_back(&sp.r_items[2]);
                    }
                }
            }
        }
    }
    RImplementation.BasicStats.DetailVisibility.End();
}

bool CDetailManager::UseVS() const
{
    return HW.Caps.geometry_major >= 1 && !RImplementation.o.ffp;
}

void CDetailManager::Render(CBackend& cmd_list)
{
#ifndef _EDITOR
    if (nullptr == dtFS)
        return;
    if (!psDeviceFlags.is(rsDrawDetails))
        return;
#endif

    ZoneScoped;

    // [DA_PORT] Проверка на пустоту обязательна: задачу теперь снимают и в WaitCalcTask, поэтому
    // между сбросом устройства и первым DispatchMTCalc указателя может не быть вовсе.
    if (m_calc_task)
        TaskScheduler->Wait(*m_calc_task);

    RImplementation.BasicStats.DetailRender.Begin();
    g_pGamePersistent->m_pGShaderConstants->m_blender_mode.w = 1.0f; //--#SM+#-- Флаг начала рендера травы [begin of grass render]

#ifndef _EDITOR
    float factor = g_pGamePersistent->Environment().wind_strength_factor;
#else
    float factor = 0.3f;
#endif
    // [DA_PORT] Сильный ветер: смешиваем исходное качание с модовским, доля задаётся r__grass_sway.
    //
    // Значения ниже - те самые, что стояли в движке до Dead Air. Автор мода их не удалил, а оставил
    // рядом в комментариях к [details]: `swing_fast_amp1 = 0.65 ;0.25`, `amp2 = .50 ;0.15`,
    // `rot2 = .5 ;0.75`, `speed = 20.0 ;1`. То есть разгон сделан сознательно и обратим - вот
    // обратная дорога. Читать их из конфига нельзя: они там комментарии, а не ключи.
    //
    // ⚠️ Смешивается ТОЛЬКО сильный набор. Тихий (swing_normal_*) автор не трогал вовсе, он
    // одинаков и там, и там, - и трогать его значило бы менять то, чего никто не менял: в штиль
    // трава стала бы неподвижной.
    static const SSwingValue swing_fast_base = { /*rot1*/ 1.f, /*rot2*/ .75f, /*amp1*/ .25f,
        /*amp2*/ .15f, /*speed*/ 1.f };

    SSwingValue swing_fast;
    swing_fast.lerp(swing_fast_base, swing_desc[1], ps_r__grass_sway);

    swing_current.lerp(swing_desc[0], swing_fast, factor);

    // Частота - отдельной ручкой поверх: именно она даёт ощущение «трясётся», и она же дороже всего
    // обходится временной реконструкции (чем быстрее мечется стебель, тем меньше смысла в
    // накопленной истории). Единица = не вмешиваться.
    swing_current.speed *= ps_r__grass_sway_speed;

    cmd_list.set_CullMode(CULL_NONE);
    cmd_list.set_xform_world(Fidentity);
    if (UseVS())
        hw_Render(cmd_list);
    else
        soft_Render();
    cmd_list.set_CullMode(CULL_CCW);

    g_pGamePersistent->m_pGShaderConstants->m_blender_mode.w = 0.0f; //--#SM+#-- Флаг конца рендера травы [end of grass render]
    RImplementation.BasicStats.DetailRender.End();
}

void CDetailManager::DispatchMTCalc()
{
    // [DA_PORT] ⭐ Одновременно может считаться только ОДНА задача травы. Здесь причина падений.
    //
    // Задача ставится каждый кадр, а дожидаются её в MT_Render - и только там. Но MT_Render
    // выходит раньше ожидания, если трава выключена, а кадр, на котором идёт сброс устройства или
    // загрузка, не доходит до отрисовки вовсе. Достаточно одного такого кадра, чтобы следующая
    // задача встала рядом с ещё работающей.
    //
    // А работают они с общим состоянием менеджера: кэш слотов, списки видимого и пул, из которого
    // берутся травинки (poolSI). Пул не потокобезопасен ни на грамм. Две задачи в нём - и create()
    // отдаёт мусорный указатель, по которому тут же пишут матрицу поворота. Стек падения при этом
    // указывает в тригонометрию внутри Decompress, то есть на запись по этому указателю, а причина
    // - парой кадров раньше и в другом потоке. Отсюда же и то, что падало именно после сброса.
    //
    // Пропуск кадра здесь безвреден: кэш травы досчитается на следующем. Ждать нельзя - это главный
    // поток, и ожидание превратило бы гонку в подтормаживание.
    if (m_calc_active.load(std::memory_order_acquire))
        return;

    m_calc_active.store(true, std::memory_order_release);
    m_calc_task = &TaskScheduler->AddTask([this]
    {
        // [DA_PORT] Признак снимается на ЛЮБОМ выходе, включая ранние: иначе ожидание в
        // WaitCalcTask ждало бы задачу, которая ничего не делает и уже завершилась.
        struct done
        {
            std::atomic<bool>& flag;
            ~done() { flag.store(false, std::memory_order_release); }
        } mark{ m_calc_active };

#ifndef _EDITOR
        if (nullptr == RImplementation.Details)
            return; // possibly deleted
        if (nullptr == dtFS)
            return;
        if (!psDeviceFlags.is(rsDrawDetails))
            return;
#endif

        ZoneScoped;

        EYE = Device.vCameraPosition;

        const int s_x = iFloor(EYE.x / dm_slot_size + .5f);
        const int s_z = iFloor(EYE.z / dm_slot_size + .5f);

        RImplementation.BasicStats.DetailCache.Begin();
        cache_Update(s_x, s_z, EYE);
        RImplementation.BasicStats.DetailCache.End();

        UpdateVisibleM();
    });
}
} // namespace xray::render::RENDER_NAMESPACE
