#include "stdafx.h"

#include "Render.h"

#include "xrCore/FS_impl.h"
#include "xrCore/Threading/TaskManager.hpp"

#include "XR_IOConsole.h"
#include "xr_input.h"

#include "IGame_Level.h"
#include "IGame_Persistent.h"

#include "xrScriptEngine/script_space.hpp"

#include <SDL.h>

ENGINE_API CRenderDevice Device;
ENGINE_API CLoadScreenRenderer load_screen_renderer;

ENGINE_API bool g_bRendering = false;

ENGINE_API bool g_bBenchmark = false;
string512 g_sBenchmarkName;

u32 ps_fps_limit = 1000; // [DA_PORT] token cvar now; 1000 == unlimited

// [DA_PORT] Frames still to write into the log, see ProcessFrame. Counts itself down.
ENGINE_API int ps_da_perf_dump = 0;

// [DA_PORT] Три крупных блока игровой логики, каждый копится своим модулем за кадр.
//
// Понадобились потому, что da_perf_dump показал: логика съедает две трети кадра, обновление
// объектов объясняет из них только половину, а вторая половина не измерена ни разу. Здесь она и
// раскладывается: шаг физики (CPHWorld::OnFrame), обновление уровня целиком (CLevel::OnFrame) и
// обновление объектов внутри него (CObjectList::Update).
ENGINE_API float g_da_ms_phys = 0.f;
ENGINE_API float g_da_ms_objects = 0.f;
ENGINE_API float g_da_ms_level = 0.f;
ENGINE_API float g_da_ms_persist = 0.f; // [DA_PORT] CGamePersistent::OnFrame целиком
ENGINE_API float g_da_ms_sched = 0.f;   // [DA_PORT] Engine.Sheduler.Update() внутри него — ALife и «мозги» NPC
ENGINE_API float g_da_ms_weather = 0.f; // [DA_PORT] WeathersUpdate() там же

// [DA_PORT] Части обновления уровня. Пишет xrGame (Level.cpp), читает разбор кадра ниже.
// Живут здесь, а не в игре: ENGINE_API в xrGame означает импорт, определить там нельзя.
ENGINE_API float g_da_ms_bullets = 0.f;
ENGINE_API float g_da_ms_gameevents = 0.f;
ENGINE_API float g_da_ms_map = 0.f;
ENGINE_API float g_da_ms_tasks = 0.f;

// [DA_PORT] Ещё два неизмеренных места, вскрытых сторожем.
//
// В одном выбросе: move 13.66 при уровне 0.51, объектах 0.43, окружении 0.83 и физике 0 -- то есть
// двенадцать миллисекунд внутри FrameMove мимо всех счётчиков. В другом: окружение 8.24, а
// планировщик в нём 0.24 -- восемь миллисекунд мимо планировщика и погоды.
//
// seqframe -- вся рассылка подписчиков кадра (уровень, физика, окружение и все прочие вместе).
// Разница между move и seqframe покажет, есть ли что-то в FrameMove помимо рассылки.
// env -- IGame_Persistent::OnFrame, то есть система погоды и окружения внутри CGamePersistent.
ENGINE_API float g_da_ms_seqframe = 0.f;
ENGINE_API float g_da_ms_env = 0.f;

// [DA_PORT] Из чего состоит спавн. Прогрев визуалов срезал выброс с 9.4 до 6.6 мс -- значит модель
// была лишь четвертью цены, а остальное лежит внутри создания объекта. Граница проведена там, где
// она есть в коде: подготовка сущности и Objects.Create -- отдельно, net_Spawn -- отдельно.
ENGINE_API float g_da_ms_spawn_prep = 0.f;
ENGINE_API float g_da_ms_spawn_create = 0.f;
ENGINE_API float g_da_ms_spawn_net = 0.f;
ENGINE_API u32 g_da_spawn_count = 0;

// [DA_PORT] Frame-time watchdog: milliseconds above which a frame is worth a line in the log. Zero is
// off. Unlike the dump this is meant to be left running while playing - it says nothing until a frame
// actually misbehaves, then reports what it was doing, so the log names the interaction rather than
// the clock. See ProcessFrame.
ENGINE_API int ps_da_perf_watch = 0;

// [DA_PORT] Прогрев планировщика в конце загрузки уровня, миллисекунды. Определён в xr_ioc_cmd.cpp,
// применяется ниже — в RenderEnd, там же и разбор.
extern ENGINE_API int ps_da_sched_warmup_ms;

// [DA_PORT] Длительность последнего обмена с Discord — см. x_ray.cpp.
extern ENGINE_API float g_da_ms_discord;
extern ENGINE_API float g_da_ms_events;
extern ENGINE_API float g_da_ms_proc_full;

// [DA_PORT] Ловушка на выброс в отложенных задачах кадра, см. Device.h и xr_ioc_cmd.cpp.
ENGINE_API float ps_da_seq_trap = 0.f;
ENGINE_API int ps_da_seq_trap_max = 20; // сколько отчётов о выбросе печатать, 0 — без предела
ENGINE_API int ps_da_seq_stats = 0;     // 1 — напечатать накопленное, 2 — напечатать и обнулить

// Опознание задачи по её собственным байтам.
//
// Цикл видит безымянные делегаты, и назвать задачу он не может. Но делегат — это указатель на
// объект плюс указатель на метод, и этой пары достаточно, чтобы отличить одну задачу от другой и
// склеить её замеры за всю сессию. Имена, где они есть, добавляет da_seq_probe; здесь же ключ
// работает для ЛЮБОЙ задачи, в том числе той, куда пробу никто не вставлял.
//
// Номер в очереди для этого не годится: сталкеры появляются и исчезают, и «задача №17» в двух
// кадрах — разные задачи.
struct da_seq_key
{
    u64 a{}, b{};
    bool operator<(const da_seq_key& o) const { return (a != o.a) ? (a < o.a) : (b < o.b); }
};

struct da_seq_stat
{
    u64 calls{};
    double total_ms{};
    float max_ms{};
    u32 max_frame{};
};

struct da_seq_sample
{
    da_seq_key key;
    float ms;
    u32 idx;
};

static xr_map<da_seq_key, da_seq_stat> g_da_seq_stats;
static u32 g_da_seq_reports = 0;

static da_seq_key da_seq_key_of(const fastdelegate::FastDelegate0<>& d)
{
    da_seq_key k;
    const size_t n = _min(sizeof(d), sizeof(k));
    CopyMemory(&k, &d, n);
    return k;
}

static pcstr da_seq_key_str(const da_seq_key& k)
{
    static string128 buf;
    xr_sprintf(buf, "объект %016llx метод %016llx", (unsigned long long)k.a, (unsigned long long)k.b);
    return buf;
}

// [DA_PORT] Итог за сессию: кто из задач сколько съел суммарно, а не в один несчастный кадр.
//
// Разовый выброс и ровная дороговизна лечатся по-разному, а по одному отчёту о выбросе их не
// различить: задача, которая раз в минуту берёт 4 мс, и задача, которая берёт 0.2 мс каждый кадр,
// в отчёте выглядят одинаково — а стоят по-разному в сто раз.
ENGINE_API void da_seq_dump_stats(bool reset)
{
    if (g_da_seq_stats.empty())
    {
        Msg("~ [DA_SEQ] накоплено пусто: ловушка не была включена (da_seq_trap) или задач не было");
        return;
    }

    xr_vector<std::pair<da_seq_key, da_seq_stat>> rows;
    rows.reserve(g_da_seq_stats.size());
    for (const auto& it : g_da_seq_stats)
        rows.push_back(it);

    std::sort(rows.begin(), rows.end(),
        [](const auto& l, const auto& r) { return l.second.total_ms > r.second.total_ms; });

    double grand = 0.0;
    for (const auto& r : rows)
        grand += r.second.total_ms;

    Msg("~ [DA_SEQ] ---- итог за сессию: %u разных задач, суммарно %.1f мс ----",
        (u32)rows.size(), grand);
    Msg("~ [DA_SEQ] %-8s %10s %9s %9s %10s  %s", "вызовов", "всего мс", "среднее", "максимум",
        "макс.кадр", "задача");
    for (const auto& r : rows)
    {
        const da_seq_stat& s = r.second;
        Msg("~ [DA_SEQ] %-8llu %10.2f %9.3f %9.2f %10u  %s", (unsigned long long)s.calls,
            s.total_ms, s.total_ms / double(s.calls ? s.calls : 1), s.max_ms, s.max_frame,
            da_seq_key_str(r.first));
    }
    Msg("~ [DA_SEQ] ---- конец итога ----");

    if (reset)
    {
        g_da_seq_stats.clear();
        g_da_seq_reports = 0;
        Msg("~ [DA_SEQ] накопленное обнулено");
    }
}

da_seq_probe::da_seq_probe(const char* n) : name(n), armed(ps_da_seq_trap > 0.f)
{
    if (armed)
        timer.Start();
}

da_seq_probe::~da_seq_probe()
{
    if (!armed)
        return;

    const float ms = timer.GetElapsed_sec() * 1000.f;
    if (ms >= ps_da_seq_trap)
        Msg("~ [DA_SEQ] задача [%s] заняла %.2f мс", name, ms);
}

// [DA_PORT] Filled by the parallel task, read after the wait - see ProcessFrame.
static float g_da_perf_seq_ms = 0.f;
static float g_da_perf_seq_worst_ms = 0.f;
static u32 g_da_perf_seq_worst_idx = 0;
static float g_da_perf_mt_ms = 0.f;
static u32 g_da_perf_seq_count = 0;
static float g_da_perf_seq_inner_ms = 0.f;
static float g_da_perf_sleep_ms = 0.f;
static float g_da_perf_total_ms = 0.f;
static CTimer g_da_tail_timer;   // [DA_PORT] хвост кадра, см. ProcessFrame
static bool g_da_tail_report = false;
u32 ps_fps_limit_in_menu = 60;

bool g_bLoaded = false;
ref_light precache_light = 0;

using namespace xray;

bool CRenderDevice::RenderBegin()
{
    if (GEnv.isDedicatedServer)
        return true;

    ZoneScoped;

    switch (GEnv.Render->GetDeviceState())
    {
    case DeviceState::Normal: break;
    case DeviceState::Lost:
        // If the device was lost, do not render until we get it back
        Sleep(33);
        return false;

    case DeviceState::NeedReset:
        // Check if the device is ready to be reset
        Reset();
        return false;

    default: R_ASSERT(0);
    }
    GEnv.Render->Begin();
    g_bRendering = true;

    return true;
}

void CRenderDevice::Clear() { GEnv.Render->Clear(); }

void CRenderDevice::RenderEnd(void)
{
    if (GEnv.isDedicatedServer)
        return;

    ZoneScoped;
    if (dwPrecacheFrame)
    {
        GEnv.Sound->set_master_volume(0.f);
        dwPrecacheFrame--;
        if (!dwPrecacheFrame)
        {
            GEnv.Render->updateGamma();
            if (precache_light)
            {
                precache_light->set_active(false);
                precache_light.destroy();
            }
            GEnv.Sound->set_master_volume(1.f);
            GEnv.Render->ResourcesDestroyNecessaryTextures();
            // [DA_PORT] Прогрев планировщика: первые обновления всех объектов -- здесь, а не в игре.
            //
            // Замер da_perf_watch на Юпитере: сразу после загрузки один кадр стоил 2765 мс, и 367 из
            // них -- планировщик. При обычных 0.19 мс это в две тысячи раз больше. Причина простая:
            // объекты уровня зарегистрировались, срок первого обновления у всех наступил разом, и
            // весь ком достался первому же игровому кадру.
            //
            // Здесь предзагрузка только что кончилась: объекты созданы, а кадр игроку ещё не показан.
            // Работа не исчезает -- она переносится под экран загрузки, где её никто не чувствует.
            //
            // Бюджет планировщика на время прогрева снимаем: обычно он режет проход десятью
            // миллисекундами (psShedulerCurrent), и с ним ком за один заход не разошёлся бы.
            //
            // Проходов немного и они самоограничены: время игры внутри цикла не идёт, поэтому уже
            // после первого прохода объекты переносят свой срок в будущее, и следующий заход просто
            // не находит работы. Потолок в четыре прохода -- страховка от объекта, который сам себя
            // переназначает на "сейчас".
            if (ps_da_sched_warmup_ms > 0)
            {
                extern float psShedulerCurrent;
                const float saved_budget = psShedulerCurrent;
                psShedulerCurrent = float(ps_da_sched_warmup_ms);

                CTimer warmup;
                warmup.Start();
                u32 passes = 0;
                do
                {
                    Engine.Sheduler.Update();
                    ++passes;
                } while (passes < 4 && warmup.GetElapsed_sec() * 1000.f < float(ps_da_sched_warmup_ms));

                psShedulerCurrent = saved_budget;
                Msg("* [DA_PORT] прогрев планировщика: %u проходов за %.0f мс", passes,
                    warmup.GetElapsed_sec() * 1000.f);
            }

            // [DA_PORT] Полная сборка мусора Lua — тоже здесь, а не в первых игровых кадрах.
            //
            // Загрузка уровня оставляет за собой много мусора: разобранные конфиги, временные
            // таблицы схем логики, всё, что скрипты создали при спавне. В игре это разгребается
            // шагами по кадру, и после перевода сборки в главный поток (см. mtLUA_GC в
            // console_commands.cpp) каждый такой шаг стал видимым: ловушка da_seq_trap ловила
            // script_gc по 9-11 мс при бюджете кадра 16.6.
            //
            // Полная сборка под экраном загрузки убирает накопленное разом, и шаги в игре получают
            // на вход куда меньше живых объектов. Работа не исчезает — она переносится туда, где
            // её никто не чувствует, ровно как с прогревом планировщика выше.
            if (g_da_lua_full_gc)
            {
                CTimer gc_timer;
                gc_timer.Start();
                const int freed_kb = g_da_lua_full_gc();
                if (freed_kb >= 0)
                    Msg("* [DA_PORT] сборка мусора Lua на загрузке: освобождено %d КБ за %.0f мс",
                        freed_kb, gc_timer.GetElapsed_sec() * 1000.f);
            }

            Memory.mem_compact();
            Msg("* MEMORY USAGE: %d K", Memory.mem_usage() / 1024);
            Msg("* End of synchronization A[%d] R[%d]", b_is_Active, b_is_Ready);
            FIND_CHUNK_COUNTER_FLUSH();
            if (g_pGamePersistent->GameType() == 1 && !psDeviceFlags.test(rsAlwaysActive)) // haCk
            {
                const Uint32 flags = SDL_GetWindowFlags(m_sdlWnd);
                if ((flags & SDL_WINDOW_INPUT_FOCUS) == 0)
                    Pause(true, true, true, "application start");
            }
        }
    }
    // end scene
    g_bRendering = false;
    GEnv.Render->End();

    vCameraPositionSaved = vCameraPosition;
    vCameraDirectionSaved = vCameraDirection;
    vCameraTopSaved = vCameraTop;
    vCameraRightSaved = vCameraRight;

    mFullTransformSaved = mFullTransform;
    mViewSaved = mView;
    mProjectSaved = mProject;
}

void CRenderDevice::PreCache(u32 amount, bool wait_user_input)
{
    if (GEnv.isDedicatedServer)
        amount = 0;
    else if (GEnv.Render->GetForceGPU_REF())
        amount = 0;

    dwPrecacheFrame = dwPrecacheTotal = amount;
    if (amount && !precache_light && g_pGameLevel && g_loading_events.empty())
    {
        precache_light = GEnv.Render->light_create();
        precache_light->set_shadow(false);
        precache_light->set_position(vCameraPosition);
        precache_light->set_color(255, 255, 255);
        precache_light->set_range(5.0f);
        precache_light->set_active(true);
    }
    if (amount && !load_screen_renderer.IsActive())
    {
        load_screen_renderer.Start(wait_user_input);
    }
}

void CRenderDevice::CalcFrameStats()
{
    stats.RenderTotal.FrameEnd();
    do
    {
        // calc FPS & TPS
        if (fTimeDeltaReal <= EPS_S)
            break;
        const float fps = 1.f / fTimeDeltaReal;
        // if (Engine.External.tune_enabled) vtune.update (fps);
        constexpr float fOne = 0.3f;
        constexpr float fInv = 1.0f - fOne;
        stats.fFPS = fInv * stats.fFPS + fOne * fps;
        if (stats.RenderTotal.result > EPS_S)
        {
            const u32 renderedPolys = GEnv.Render->GetCacheStatPolys();
            stats.fTPS = fInv * stats.fTPS + fOne * float(renderedPolys) / (stats.RenderTotal.result * 1000.f);
            stats.fRFPS = fInv * stats.fRFPS + fOne * 1000.f / stats.RenderTotal.result;
        }
    } while (false);
    stats.RenderTotal.FrameStart();
}

int g_svDedicateServerUpdateReate = 100;

ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

bool CRenderDevice::BeforeFrame()
{
    ZoneScoped;

    if (!b_is_Ready)
    {
        Sleep(100);
        return false;
    }

    // [DA_PORT] The dump forces gathering on for its own duration. Without this the timers it prints
    // are whatever they held when the overlay was last up, decaying quietly frame by frame - which is
    // how a "wait" of 10.77ms came to be reported inside a 9.09ms frame. A measurement that cannot be
    // wrong in that direction is worth the one extra condition.
    // The watchdog belongs in this list too. Leaving it out cost a measurement: the GOAP counters live
    // behind this flag, so a watch run reported them all as zero while the frame plainly spent twelve
    // milliseconds in the object handlers, and that read as "the time is not in the planner" when it
    // only meant the planner was not being counted.
    if (psDeviceFlags.test(rsStatistic) || ps_da_perf_dump > 0 || ps_da_perf_watch > 0)
        g_bEnableStatGather = true; // XXX: why not use either rsStatistic or g_bEnableStatGather?
    else
        g_bEnableStatGather = false;

    if (!g_loading_events.empty())
    {
        if (g_loading_events.front()())
            g_loading_events.pop_front();
        g_pGamePersistent->LoadDraw();
        return false;
    }

    return true;
}

// [DA_PORT] TAA plumbing.
// g_da_taa_jitter is the sub-pixel offset CCameraManager::ApplyDevice put into mProject this frame, in
// NDC. g_da_taa_unjittered_VP is what mFullTransform would have been without it: temporal reprojection
// has to compare frames on a common, un-jittered grid, otherwise the jitter itself reads as camera
// motion and the history lands half a pixel off every frame.
extern int ps_r__fsr2;
// [DA_PORT] "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
extern ENGINE_API bool da_upscaler_active();
ENGINE_API Fvector2 g_da_taa_jitter = { 0.f, 0.f };

// [DA_PORT] The same jitter in PIXELS, which is the form FSR 2 takes it in. Kept separately
// rather than converted at the point of use: the conversion has a sign flip in it, and having it
// in one place is what finally stopped the picture shaking.
ENGINE_API Fvector2 g_da_fsr2_jitter_px = { 0.f, 0.f };
ENGINE_API Fmatrix g_da_taa_unjittered_VP = Fidentity;

void CRenderDevice::OnCameraUpdated()
{
    static u32 frame{ u32(-1) };
    if (frame == dwFrame)
        return;

    ZoneScoped;

    // Precache
    if (dwPrecacheFrame)
    {
        const float factor = float(dwPrecacheFrame) / float(dwPrecacheTotal);
        const float angle = PI_MUL_2 * factor;
        vCameraDirection.set(_sin(angle), 0, _cos(angle));
        vCameraDirection.normalize();
        vCameraTop.set(0, 1, 0);
        vCameraRight.crossproduct(vCameraTop, vCameraDirection);
        mView.build_camera_dir(vCameraPosition, vCameraDirection, vCameraTop);
    }

    // Matrices
    mInvView.invert(mView);
    mFullTransform.mul(mProject, mView);
    mInvFullTransform.invert_44(mFullTransform);

    // [DA_PORT] see g_da_taa_unjittered_VP above. The jitter is two entries of the projection matrix, so
    // undoing it costs one matrix copy and one multiply, and only when TAA is actually on.
    // Under an upscaler the jitter never reaches mProject at all — the scene shaders apply it
    // themselves — so mFullTransform is ALREADY un-jittered and subtracting the offset here would bake
    // a phantom negative jitter into every motion vector. That is a residual shake proportional to the
    // jitter, which is exactly what survived moving the jitter into the shaders.
    //
    // Named FSR 2 alone until FSR 3 and XeSS were added beside it; see da_upscaler_active().
    // [DA_PORT] Вычитать больше нечего: джиттер не попадает в mProject НИ В ОДНОМ режиме — его
    // накладывают шейдеры сцены. Значит mFullTransform уже несдвинутая, и прежнее вычитание для
    // пути без апскейлера теперь запекло бы в векторы движения фантомный отрицательный джиттер —
    // ровно та остаточная тряска, о которой предупреждал комментарий выше.
    g_da_taa_unjittered_VP.set(mFullTransform);
    GEnv.Render->OnCameraUpdated();
    GEnv.Render->SetCacheXform(mView, mProject);

    frame = dwFrame;
}

static void UpdateViewports()
{
    // Update and Render additional Platform Windows
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void CRenderDevice::DoRender()
{
    if (GEnv.isDedicatedServer)
        return;

    ZoneScoped;

    CStatTimer renderTotalReal;
    renderTotalReal.FrameStart();
    renderTotalReal.Begin();
    if (b_is_Active && RenderBegin())
    {
        {
            ZoneScopedN("Render process");
            seqRender.Process(); // all rendering is done here
        }

        CalcFrameStats();
        Statistic->Show();

        ImGui::Render();
        m_imgui_render->Render(ImGui::GetDrawData());
        UpdateViewports();

        RenderEnd(); // Present goes here
    }
    else
    {
        UpdateViewports();
    }
    renderTotalReal.End();
    renderTotalReal.FrameEnd();
    stats.RenderTotal.accum = renderTotalReal.accum;
}

void CRenderDevice::ProcessFrame()
{
    ZoneScoped;

    if (!BeforeFrame())
        return;

    const u64 frameStartTime = TimerGlobal.GetElapsed_ns(); // [DA_PORT] ns: whole ms cannot express 165 fps
    // [DA_PORT] Pace the frame on a clock that CANNOT be paused. TimerGlobal can: while paused it
    // returns a constant, so waiting on it froze the game solid the moment the menu opened.
    const auto frameStartSteady = std::chrono::steady_clock::now();

    // [DA_PORT] Own timers for the dump, because the engine's own are peak-hold, not per-frame:
    // CStatTimer::FrameEnd jumps straight to any new maximum and then decays by one percent a frame.
    // Every figure the overlay shows is therefore the worst frame in recent memory, which is useful for
    // spotting hitches and useless for adding up - it reported a 9.28ms wait inside a 8.99ms frame, and
    // that impossibility is what gave it away. These three are plain elapsed time, and they sum.
    const bool perf = ps_da_perf_dump > 0 || ps_da_perf_watch > 0;
    CTimer perf_timer;
    float ms_move = 0.f, ms_render = 0.f, ms_wait = 0.f;

    // [DA_PORT] Отдельно -- ВЕСЬ ProcessFrame, чтобы отделить его от остального кадра.
    //
    // Длина кадра берётся из времени МЕЖДУ кадрами (fTimeDeltaReal), то есть включает и то, что
    // происходит вне измеряемой части: разбор сообщений окна, ожидания драйвера, подгрузку ресурсов
    // по первому обращению. Замер после загрузки Юпитера: кадр 462 мс, из них логика 8 и рендер 11 --
    // остальные 443 не видел ни один счётчик, и было непонятно даже, внутри они или снаружи.
    //
    // Теперь в отчёте есть "проц" (весь ProcessFrame). Разница между кадром и им -- это ровно то,
    // что происходит ВНЕ его, и дальше искать надо уже там, а не гадать.
    CTimer proc_timer;
    if (perf)
        proc_timer.Start();

    if (perf)
        perf_timer.Start();

    FrameMove();

    if (perf)
        ms_move = perf_timer.GetElapsed_sec() * 1000.f;

    OnCameraUpdated();

    const auto& processSeqParallel = TaskScheduler->AddTask([this, perf]
    {
        ZoneScopedN("ProcessParallelSequence");

        // [DA_PORT] Split for the dump. This sequence turned out to be the frame - some eleven
        // milliseconds of it against two for everything the main thread does itself - and it holds two
        // quite different things: one entry per stalker for their object handlers, and then the sound
        // renderer and network. Timing them apart is the difference between knowing where the frame
        // goes and guessing at it.
        // [DA_PORT] Ловушка da_seq_trap меряет и без da_perf_dump: выброс редкий, и ждать его,
        // держа включённым дамп кадра, значит писать в лог гигабайты ради одной строки.
        //
        // ⚠️ Таймер цикла обязан стартовать при ЛЮБОМ из двух режимов. Первая версия оставила здесь
        // `if (perf)`, а читала таймер ловушка — и на незапущенном CTimer получала мусор: в лог
        // ушло 11928 строк с «циклом» в 1785846169600 мс. Инструмент, врущий молча, хуже
        // отсутствующего, поэтому условие тут и ниже теперь одно и то же.
        const bool trap = ps_da_seq_trap > 0.f;
        const bool timing = perf || trap;

        CTimer t;
        if (timing)
            t.Start();

        const u32 count = (u32)seqParallel.size();

        // [DA_PORT] Sum the entries individually as well as timing the loop around them, because those
        // two numbers answer different questions and only together do they say anything.
        //
        // The loop is wall time on whichever thread picked the task up. If that thread is descheduled -
        // and there are sixteen workers plus a main thread that used to spin flat out - the wall time
        // includes the time it was not running. The per-entry sum cannot include that: it only counts
        // while an entry is actually executing. So a large loop total against a small sum means the
        // work is not slow, the thread is starved; the two being equal means the work really is slow.
        double inner = 0.0;
        float worst_ms = 0.f;
        u32 worst_idx = 0;
        CTimer entry_timer;

        // Данные кадра держим в статике: при выбросе печатается ВЕСЬ список задач, а не только
        // худшая, — одна дорогая задача среди двадцати пяти и двадцать пять по чуть-чуть требуют
        // разного лечения, и по одному числу их не различить. Буфер переиспользуется, чтобы замер
        // не выделял память в кадре.
        static xr_vector<da_seq_sample> s_samples;
        if (timing)
        {
            s_samples.clear();
            s_samples.reserve(count);
        }

        // [DA_PORT] Границу проверяем на КАЖДОМ шаге, а не по снятому заранее count.
        //
        // Задача из этого списка может уничтожить игровой объект, а CCustomMonster::net_Destroy
        // вычёркивает свои делегаты отсюда же (remove_from_seq_parallel). Вектор становится короче,
        // а count остаётся прежним — последние итерации читают ЗА границей и вызывают делегат по
        // мусору. Падение выглядело как чтение по адресу 0x40 внутри CSoundPlayer::update: делегат
        // указывал на давно снесённого монстра.
        //
        // Копировать список перед исполнением нельзя — тогда снятые делегаты остались бы в копии и
        // вызвались бы гарантированно. Пропуск одного элемента при сдвиге приемлем: это звук за
        // один кадр, и в следующем кадре задача встанет заново.
        //
        // Мина взвелась вместе с уборкой тел: объекты стали удаляться пачками и посреди кадра.
        for (u32 pit = 0; pit < count && pit < seqParallel.size(); pit++)
        {
            if (!timing)
            {
                seqParallel[pit]();
                continue;
            }

            const da_seq_key key = da_seq_key_of(seqParallel[pit]);
            entry_timer.Start();
            seqParallel[pit]();
            const float ms = entry_timer.GetElapsed_sec() * 1000.f;

            inner += ms;
            if (ms > worst_ms)
            {
                worst_ms = ms;
                worst_idx = pit;
            }
            s_samples.push_back({ key, ms, pit });

            // Накопление за сессию — по ключу задачи, а не по номеру: номера едут, когда сталкеры
            // появляются и исчезают, и «задача №17» в двух кадрах это разные задачи.
            da_seq_stat& st = g_da_seq_stats[key];
            ++st.calls;
            st.total_ms += ms;
            if (ms > st.max_ms)
            {
                st.max_ms = ms;
                st.max_frame = Device.dwFrame;
            }
        }
        seqParallel.clear();

        if (timing)
        {
            g_da_perf_seq_inner_ms = float(inner);
            g_da_perf_seq_worst_ms = worst_ms;
            g_da_perf_seq_worst_idx = worst_idx;
            g_da_perf_seq_ms = t.GetElapsed_sec() * 1000.f;
            g_da_perf_seq_count = count;
            t.Start();
        }

        // Пустой кадр не выброс: задач не было, мерить нечего. Первая версия этого не проверяла и
        // рапортовала о «выбросе» на пустом списке.
        if (trap && count > 0 && g_da_perf_seq_ms >= ps_da_seq_trap)
        {
            if (ps_da_seq_trap_max <= 0 || g_da_seq_reports < u32(ps_da_seq_trap_max))
            {
                ++g_da_seq_reports;

                // Разница между суммой и временем цикла говорит, кто виноват: если сумма заметно
                // меньше, работа не медленная, а поток не получал процессор.
                Msg("~ [DA_SEQ] ВЫБРОС кадр %u: цикл %.2f мс, сумма задач %.2f мс, задач %u, "
                    "худшая №%u на %.2f мс%s",
                    Device.dwFrame, g_da_perf_seq_ms, float(inner), count, worst_idx, worst_ms,
                    (inner < g_da_perf_seq_ms * 0.5) ? "  <- сумма вдвое меньше цикла: поток голодал"
                                                     : "");

                for (const da_seq_sample& s : s_samples)
                {
                    if (s.ms < ps_da_seq_trap * 0.05f)
                        continue; // мелочь не печатаем, иначе список тонет в нулях
                    Msg("~ [DA_SEQ]    №%-3u %6.2f мс  задача %s", s.idx, s.ms,
                        da_seq_key_str(s.key));
                }

                if (ps_da_seq_trap_max > 0 && g_da_seq_reports == u32(ps_da_seq_trap_max))
                    Msg("~ [DA_SEQ] это был отчёт номер %d, дальше молчу. Порог: da_seq_trap_max",
                        ps_da_seq_trap_max);
            }
        }

        seqFrameMT.Process();

        if (perf)
            g_da_perf_mt_ms = t.GetElapsed_sec() * 1000.f;
    });

    if (perf)
        perf_timer.Start();

    DoRender();

    if (perf)
    {
        ms_render = perf_timer.GetElapsed_sec() * 1000.f;
        perf_timer.Start();
    }

    // [DA_PORT] The frame has three parts and the statistics only count two of them. ENGINE times
    // seqFrame.Process(), RENDER times the drawing including Present - and this wait, for the parallel
    // sequence started above, belongs to neither. Whenever that parallel work outlasts the rendering,
    // the frame stands still here and nothing on screen says so.
    gTestTimer0.Begin();
    TaskScheduler->Wait(processSeqParallel);
    gTestTimer0.End();

    if (perf)
        ms_wait = perf_timer.GetElapsed_sec() * 1000.f;

    // [DA_PORT] Печать накопленного по задачам — по команде da_seq_stats, разово.
    //
    // Опрашивается ЗДЕСЬ, а не в самой команде: таблицу наполняет параллельная задача, и печатать
    // её из консольного потока значило бы читать чужое состояние на ходу. Тут же задача уже
    // дождалась, и никто в таблицу не пишет.
    if (ps_da_seq_stats != 0)
    {
        da_seq_dump_stats(ps_da_seq_stats >= 2);
        ps_da_seq_stats = 0;
    }

    // [DA_PORT] The same figures the statistics overlay shows, written to the log for N frames.
    //
    // Reading them off a screenshot turned out to be worthless: the game saves the PNG inside the
    // frame the key was pressed on, so every timer still open at that moment swallows the encode. It
    // showed as the wait and the input costing thirteen milliseconds each - nearly the same number,
    // which is what gave it away. This costs one line per frame and nothing is open while it writes.
    if (perf)
    {
        // [DA_PORT] РАБОТА текущего кадра, а не длительность предыдущего.
        //
        // Раньше здесь стояло fTimeDeltaReal -- время МЕЖДУ кадрами, то есть длительность
        // ПРЕДЫДУЩЕГО. В одной строке отчёта соседствовали два разных кадра: "frame" и "полный" от
        // прошлого, "проц", "move", "render" от текущего. На ровном ходу разницы не видно, а на
        // выбросе строка становится бессмысленной -- ровно на этом я потерял два захода, читая
        // "кадр 520 мс при проц 18" как загадку, тогда как это просто два разных кадра.
        //
        // Считаем от начала ЭТОГО кадра. Сон ограничителя сюда не входит по построению: он идёт
        // ниже по функции, и это правильно -- намеренное ожидание работой не является. Заодно
        // отпала нужда отсеивать менюшные кадры вручную.
        const float ms_work = float(TimerGlobal.GetElapsed_ns() - frameStartTime) / 1000000.f;
        const float ms_frame = fTimeDeltaReal * 1000.f; // предыдущий кадр целиком, для сравнения
        const float ms_proc = perf ? proc_timer.GetElapsed_sec() * 1000.f : 0.f;

        // [DA_PORT] Watchdog: silent until a frame actually misbehaves.
        //
        // A dump of three hundred consecutive frames answers "what does a normal frame cost". It does
        // not answer "what made it stutter just then", because the interesting frame is one in a
        // thousand and nobody can press a key at the right moment. This runs while playing instead and
        // writes only when a frame crosses the threshold, so the log ends up naming the interaction -
        // opening the inventory, a fight starting, walking into a new area - rather than the clock.
        //
        // Rate-limited, because a real stall lasts many frames and one line per frame would bury the
        // first one, which is the only interesting one.
        // [DA_PORT] Кадр, который ПРОСПАЛИ, провалом не считается.
        //
        // Ограничитель частоты (в меню он 60) досыпает кадр до нужной длины, и такой кадр честно
        // длинный: 16.67 мс при пороге 8. Сторож на них срабатывал подряд, в лог шли строки с
        // obj 0 и нулями во всех счётчиках, а выглядело это как найденная просадка. Один разбор
        // так и ушёл впустую: сорок пять «выбросов», все до единого — меню.
        //
        // Признак прямой: sleep занимает большую часть кадра, значит время потрачено на ожидание
        // по нашей же воле, а не на работу.
        //
        // ⚠️ Счётчик сна выставляется НИЖЕ по кадру, поэтому здесь читается значение предыдущего.
        // Для ограничителя частоты это верно — он спит кадр за кадром, — но на первом кадре после
        // включения ограничителя сторож ещё сработает один раз. Печатаемое рядом значение sleep
        // берётся оттуда же, так что отчёт и признак согласованы между собой.
        // [DA_PORT] Второй повод сработать: в кадре был дорогой спавн. По одному лишь времени кадра
        // такие кадры теряются — спавн приходит редко, и попасть в него коротким da_frame почти
        // невозможно. Порог низкий (1 мс), потому что интересна как раз редкая дорогая штука.
        const float da_spawn_ms = g_da_ms_spawn_prep + g_da_ms_spawn_create + g_da_ms_spawn_net;

        bool watch_fires = false;
        if (ps_da_perf_watch > 0 && (ms_work > float(ps_da_perf_watch) || da_spawn_ms > 1.f))
        {
            static u32 last_report = 0;
            if (dwTimeGlobal - last_report > 200)
            {
                last_report = dwTimeGlobal;
                watch_fires = true;
            }
        }

        if (ps_da_perf_dump > 0 || watch_fires)
        {
            // Name the part that took the most, so a line reads at a glance without arithmetic.
            pcstr worst = "move";
            float worst_ms = ms_move;
            if (ms_render > worst_ms) { worst = "render"; worst_ms = ms_render; }
            if (ms_wait > worst_ms) { worst = "wait"; worst_ms = ms_wait; }

            // Object counts alongside the times: if the active count moves with the frame time, ALife
            // bringing squads online is the answer, written in the same line.
            const u32 objects = g_pGameLevel ? g_pGameLevel->Objects.o_count() : 0;
            Msg("~ [DA_PERF]%s работа %5.2f | проц %5.2f | пред.кадр %5.2f (полный %5.2f, события %5.2f, discord %5.2f) | move %5.2f (физика %5.2f, уровень %5.2f [пули %5.2f, события %5.2f (спавн: подгот %5.2f, объект %5.2f, net %6.2f, x%u), карта %5.2f, задания %5.2f], объекты "
                "%5.2f, рассылка %5.2f, окружение %5.2f [планировщик %5.2f, погода %5.2f, среда %5.2f]) | render %5.2f | wait %5.2f | seq %5.2f (inner "
                "%5.2f) x%u | mt %5.2f | goap: actual %5.2f x%u, search %5.2f x%u, exec %5.2f x%u | "
                "oh: %5.2f in%u alive%u throw%u | path %5.2f x%u | obj %u | sleep %5.2f | total %5.2f | "
                "worst: %s",
                watch_fires ? " SPIKE" : "", ms_work, ms_proc, ms_frame, g_da_ms_proc_full,
                g_da_ms_events, g_da_ms_discord, ms_move, g_da_ms_phys, g_da_ms_level, g_da_ms_bullets, g_da_ms_gameevents,
                g_da_ms_spawn_prep, g_da_ms_spawn_create, g_da_ms_spawn_net, g_da_spawn_count, g_da_ms_map, g_da_ms_tasks,
                g_da_ms_objects, g_da_ms_seqframe, g_da_ms_persist, g_da_ms_sched, g_da_ms_weather,
                g_da_ms_env, ms_render, ms_wait, g_da_perf_seq_ms,
                g_da_perf_seq_inner_ms, g_da_perf_seq_count, g_da_perf_mt_ms, float(g_da_goap_actual_ms),
                g_da_goap_calls, float(g_da_goap_search_ms), g_da_goap_searches, float(g_da_goap_exec_ms),
                g_da_goap_execs, float(g_da_oh_ms), g_da_oh_entries, g_da_oh_alive, g_da_oh_throws,
                float(g_da_lpb_ms), g_da_lpb_calls, objects, g_da_perf_sleep_ms, g_da_perf_total_ms,
                worst);
        }

        // [DA_PORT] Отсюда начинается ХВОСТ кадра -- всё, что идёт после отчёта.
        //
        // Замер показал: при кадре 520 мс до отчёта проходит 18, а весь ProcessFrame занимает 520.79.
        // Значит полтысячи миллисекунд лежат здесь, между печатью и концом функции. Что именно --
        // печатается отдельной строкой ниже, уже после ограничителя частоты: сон и всё остальное
        // порознь, потому что сон в основном отчёте показывает ПРЕДЫДУЩИЙ кадр и на выбросе врёт.
        g_da_tail_report = watch_fires || ps_da_perf_dump > 0;
        if (g_da_tail_report)
            g_da_tail_timer.Start();

        if (ps_da_perf_dump > 0)
        {
            --ps_da_perf_dump;
            if (ps_da_perf_dump == 0)
                Msg("~ [DA_PERF] ---- done ----");
        }

        g_da_goap_actual_ms = g_da_goap_search_ms = g_da_goap_exec_ms = g_da_oh_ms = g_da_lpb_ms = 0.0;
        g_da_goap_calls = g_da_goap_searches = g_da_goap_execs = 0;
        g_da_oh_entries = g_da_oh_alive = g_da_oh_throws = g_da_lpb_calls = 0;
    }

    const u64 frameEndTime = TimerGlobal.GetElapsed_ns();
    const u64 frameTime = frameEndTime - frameStartTime;

    // [DA_PORT] The budget is computed in NANOSECONDS.
    //
    // It used to be `1000 / ps_fps_limit` in whole milliseconds, which cannot express most caps at all:
    // 165 became 6ms (166 fps by luck), 144 became 6ms as well, 120 became 8ms (125 fps), and anything
    // above 500 collapsed to 1ms. That is why the limiter looked broken - it either did nothing or
    // capped at a number nobody asked for.
    u64 budget = ps_fps_limit ? 1000000000ull / ps_fps_limit : 0;

    // [DA_PORT] При включённой синхронизации своего потолка у нас нет: шаг кадру задаёт развёртка.
    //
    // Раскладывать кадры должен КТО-ТО ОДИН. Наш потолок держит свой бюджет по таймеру,
    // синхронизация держит кадр до развёртки, и вместе ровного шага они не дают: на 260 Гц потолок
    // в 75 кадров - это 3.47 развёртки, поэтому кадр то ждёт три, то четыре, и период скачет между
    // 11.5 и 15.4 мс. Счётчик показывает честные 75, а глаз видит рывки. С закрытого теста пришло
    // ровно это: «при 75 микро-фризы, а без ограничения всё ровно».
    if (psDeviceFlags.test(rsVSync))
        budget = 0;

    if (GEnv.isDedicatedServer)
        budget = 1000000000ull / g_svDedicateServerUpdateReate;
    // [DA_PORT] Потолок кадров в меню НЕ применяется при вертикальной синхронизации.
    //
    // Два ограничителя на один кадр дерутся: синхронизация ждёт развёртки, наш сон ждёт своего
    // бюджета, и они почти никогда не совпадают — кадры в меню идут рывками. У конкурента этот
    // потолок сняли целиком (Dead Air Refined, 5d02c6c8); мы оставляем его для тех, у кого
    // синхронизация выключена, — там он честно экономит питание и нагрев на статичной картинке.
    else if ((Paused() || g_pGameLevel == nullptr) && !psDeviceFlags.test(rsVSync))
        budget = ps_fps_limit_in_menu ? 1000000000ull / ps_fps_limit_in_menu : 0;

    // [DA_PORT] Time the sleep, and the whole of this function, because the parts measured above stopped
    // adding up: a 22ms frame with 1.4 of processing, 3.0 of drawing and nothing waiting leaves
    // eighteen milliseconds that belong to none of them. There are only two places left for them - this
    // sleep, and whatever happens between one frame and the next outside this function.
    //
    // Worth knowing what this limiter actually does. updateDelta is 1000/ps_fps_limit in WHOLE
    // milliseconds, and frameTime is integer milliseconds too, so at the maximum setting of 501 the
    // budget rounds to 1ms and any frame whose work rounds to 0 sleeps. Sleep(1) on Windows does not
    // return in a millisecond either - it returns on the next scheduler tick, which is 15.6ms unless
    // some process on the machine has asked for a finer timer. Which process, and whether one is
    // running at all, differs from launch to launch. That would fit the symptom exactly: a frame rate
    // that comes up either fast or slow at startup and stays there.
    CTimer sleep_timer;
    if (perf)
        sleep_timer.Start();

    // Sleep for the whole milliseconds only and spin out the remainder: Sleep() rounds up to the
    // scheduler tick, so sleeping the fractional part would overshoot the cap it is meant to hold.
    if (budget)
    {
        const auto elapsed_ns = [&frameStartSteady]() -> u64
        {
            return u64(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - frameStartSteady).count());
        };

        const u64 done = elapsed_ns();
        if (done < budget)
        {
            const u64 remaining = budget - done;
            if (remaining > 2000000ull) // sleep the whole milliseconds, spin the remainder
                Sleep(static_cast<u32>((remaining - 1000000ull) / 1000000ull));

            while (elapsed_ns() < budget)
                std::this_thread::yield();
        }
    }

    if (!b_is_Active)
        Sleep(1);

    if (perf)
    {
        g_da_perf_sleep_ms = sleep_timer.GetElapsed_sec() * 1000.f;
        g_da_perf_total_ms = float(TimerGlobal.GetElapsed_ns() - frameStartTime) / 1000000.f;
    }

    // [DA_PORT] Хвост кадра — своей строкой, здесь и только здесь его видно целиком.
    if (g_da_tail_report)
    {
        const float tail = g_da_tail_timer.GetElapsed_sec() * 1000.f;
        Msg("~ [DA_PERF] хвост кадра %u: всего %5.2f мс, из них сон %5.2f, прочее %5.2f",
            dwFrame, tail, g_da_perf_sleep_ms, tail - g_da_perf_sleep_ms);
        g_da_tail_report = false;
    }
}

void CRenderDevice::ProcessEvent(const SDL_Event& event)
{
    ZoneScoped;

    switch (event.type)
    {
    case SDL_DISPLAYEVENT:
    {
        switch (event.display.type)
        {
        case SDL_DISPLAYEVENT_ORIENTATION:
        case SDL_DISPLAYEVENT_CONNECTED:
        case SDL_DISPLAYEVENT_DISCONNECTED:
            CleanupVideoModes();
            FillVideoModes();
            if (event.display.display == psDeviceMode.Monitor && event.display.type != SDL_DISPLAYEVENT_CONNECTED)
                Reset();
            else
                UpdateWindowProps();
            break;
        } // switch (event.display.type)
        break;
    }
    case SDL_WINDOWEVENT:
    {
        const auto window = SDL_GetWindowFromID(event.window.windowID);
        if (!window)
            break;
        ImGuiViewport* viewport = ImGui::FindViewportByPlatformHandle(window);
        if (!viewport)
            break;

        switch (event.window.event)
        {
        case SDL_WINDOWEVENT_MOVED:
        {
            if (window == m_sdlWnd)
            {
                UpdateWindowRects();
            }
            if (viewport)
                viewport->PlatformRequestMove = true;
            break;
        }

        case SDL_WINDOWEVENT_DISPLAY_CHANGED:
            psDeviceMode.Monitor = event.window.data1;
            break;

        case SDL_WINDOWEVENT_RESIZED:
            if (window == m_sdlWnd)
                UpdateWindowRects();
            break;

        case SDL_WINDOWEVENT_SIZE_CHANGED:
        {
            if (window == m_sdlWnd)
            {
                UpdateWindowRects();

                if (static_cast<int>(psDeviceMode.Width) == event.window.data1 &&
                    static_cast<int>(psDeviceMode.Height) == event.window.data2)
                    break; // we don't need to reset device if resolution wasn't really changed

                psDeviceMode.Width = event.window.data1;
                psDeviceMode.Height = event.window.data2;

                Reset();
            }
            if (viewport)
                viewport->PlatformRequestResize = true;

            break;
        }

        case SDL_WINDOWEVENT_CLOSE:
        {
            if (viewport)
                viewport->PlatformRequestClose = true;

            if (window == m_sdlWnd)
            {
                Engine.Event.Defer("KERNEL:disconnect");
                Engine.Event.Defer("KERNEL:quit");
            }
            break;
        }
        } // switch (event.window.event)
    }
    } // switch (event.type)

    editor().ProcessEvent(event);
}

void CRenderDevice::Run()
{
    ZoneScoped;

    g_bLoaded = false;
    Log("Starting engine...");

    // Startup timers and calculate timer delta
    dwTimeGlobal = 0;
    Timer_MM_Delta = 0;
    {
        const u32 time_mm = CPU::GetTicks();
        while (CPU::GetTicks() == time_mm)
            ; // wait for next tick
        const u32 time_system = CPU::GetTicks();
        const u32 time_local = TimerAsync();
        Timer_MM_Delta = time_system - time_local;
    }

    SDL_HideWindow(m_sdlWnd); // workaround for SDL bug
    UpdateWindowProps();
    SDL_ShowWindow(m_sdlWnd);
    SDL_RaiseWindow(m_sdlWnd);
}

void CRenderDevice::Shutdown()
{
    ZoneScoped;
    seqAppEnd.Process();
}

u32 app_inactive_time = 0;
u32 app_inactive_time_start = 0;

void CRenderDevice::FrameMove()
{
    ZoneScoped;

    dwFrame++;
    Core.dwFrame = dwFrame;
    dwTimeContinual = TimerMM.GetElapsed_ms() - app_inactive_time;

    fTimeDeltaReal = Timer.GetElapsed_sec();
    if (!_valid(fTimeDeltaReal))
        fTimeDeltaReal = EPS_S + EPS_S;
    Timer.Start(); // previous frame

    if (psDeviceFlags.test(rsConstantFPS))
    {
        // 20ms = 50fps
        // fTimeDelta = 0.020f;
        // fTimeGlobal += 0.020f;
        // dwTimeDelta = 20;
        // dwTimeGlobal += 20;
        // 33ms = 30fps
        fTimeDelta = 0.033f;
        fTimeGlobal += 0.033f;
        dwTimeDelta = 33;
        dwTimeGlobal += 33;
    }
    else
    {
        if (Paused())
            fTimeDelta = 0.0f;
        else
        {
            fTimeDelta = 0.1f * fTimeDelta + 0.9f * fTimeDeltaReal; // smooth random system activity - worst case ~7% error
            clamp(fTimeDelta, EPS_S + EPS_S, .1f); // limit to 10fps minimum
        }
        fTimeGlobal = TimerGlobal.GetElapsed_sec();
        const u32 _old_global = dwTimeGlobal;
        dwTimeGlobal = TimerGlobal.GetElapsed_ms();
        dwTimeDelta = dwTimeGlobal - _old_global;
    }
    ImGui::GetIO().DeltaTime = fTimeDeltaReal;

    m_imgui_render->Frame();
    ImGui::NewFrame();

    // Frame move
    stats.EngineTotal.FrameStart();
    stats.EngineTotal.Begin();
    // TODO: HACK to test loading screen.
    // if(!g_bLoaded)

    g_da_ms_phys = g_da_ms_objects = g_da_ms_level = g_da_ms_persist = g_da_ms_sched = g_da_ms_weather = g_da_ms_seqframe = g_da_ms_env = 0.f;

    // [DA_PORT] Разбор проверки свойств мира GOAP: da_goap_dump <кадров>. Счёт идёт за весь прогон
    // (свойства проверяются пачками и редко), поэтому здесь только счёт кадров и вывод итога.
    if (ps_da_goap_dump > 0)
    {
        ++g_da_goap_prop_frames;
        if (--ps_da_goap_dump == 0)
        {
            const u32 frames = g_da_goap_prop_frames ? g_da_goap_prop_frames : 1;
            double all = 0.0;
            for (int i = 0; i < DA_GOAP_PROPS; ++i)
                all += g_da_goap_prop_ms[i];

            Msg("~ [DA_GOAP] ---- итог за %u кадров ----", g_da_goap_prop_frames);
            Msg("~ [DA_GOAP] проверка свойств мира всего %.2f мс на кадр", float(all / frames));
            if (g_da_goap_prop_over_calls)
                Msg("~ [DA_GOAP] ⚠ свойств с номером ≥ %d: %.3f мс на кадр, вызовов %u — их разбор "
                    "здесь не виден, поднимите DA_GOAP_PROPS",
                    int(DA_GOAP_PROPS), float(g_da_goap_prop_over_ms / frames), g_da_goap_prop_over_calls);

            for (int shown = 0; shown < 20; ++shown)
            {
                int best = -1;
                for (int i = 0; i < DA_GOAP_PROPS; ++i)
                    if (g_da_goap_prop_calls[i] && (best < 0 || g_da_goap_prop_ms[i] > g_da_goap_prop_ms[best]))
                        best = i;
                if (best < 0 || g_da_goap_prop_ms[best] <= 0.0)
                    break;
                Msg("~ [DA_GOAP]   свойство %3d: %6.3f мс на кадр (%4.1f%%), вызовов %7u, худший %6.2f мс",
                    best, float(g_da_goap_prop_ms[best] / frames),
                    float(all > 0.0 ? 100.0 * g_da_goap_prop_ms[best] / all : 0.0),
                    g_da_goap_prop_calls[best], float(g_da_goap_prop_max[best]));
                g_da_goap_prop_ms[best] = 0.0; // вывели — убираем из поиска следующего
                g_da_goap_prop_calls[best] = 0;
            }

            for (int i = 0; i < DA_GOAP_PROPS; ++i)
            {
                g_da_goap_prop_ms[i] = 0.0;
                g_da_goap_prop_max[i] = 0.0;
                g_da_goap_prop_calls[i] = 0;
            }
            // Та же выборка, но по классам вычислителей — читаемая половина отчёта.
            Msg("~ [DA_GOAP] ---- по классам вычислителей ----");
            for (u32 shown = 0; shown < 15; ++shown)
            {
                int best = -1;
                for (u32 i = 0; i < g_da_goap_kinds_used; ++i)
                    if (g_da_goap_kinds[i].calls &&
                        (best < 0 || g_da_goap_kinds[i].total_ms > g_da_goap_kinds[best].total_ms))
                        best = int(i);
                if (best < 0 || g_da_goap_kinds[best].total_ms <= 0.0)
                    break;
                Msg("~ [DA_GOAP]   %-52s %6.3f мс на кадр, вызовов %7u, худший %6.2f мс",
                    g_da_goap_kinds[best].name, float(g_da_goap_kinds[best].total_ms / frames),
                    g_da_goap_kinds[best].calls, float(g_da_goap_kinds[best].max_ms));
                g_da_goap_kinds[best].total_ms = 0.0;
                g_da_goap_kinds[best].calls = 0;
            }
            for (u32 i = 0; i < u32(DA_GOAP_KINDS); ++i)
                g_da_goap_kinds[i] = da_goap_kind();
            g_da_goap_kinds_used = 0;

            g_da_goap_prop_frames = 0;
            g_da_goap_prop_over_ms = 0.0;
            g_da_goap_prop_over_calls = 0;
        }
    } // [DA_PORT] счётчики блоков — за кадр
    {
        // [DA_PORT] Вся рассылка подписчиков кадра -- см. пояснение у g_da_ms_seqframe.
        CTimer da_sf;
        da_sf.Start();
        seqFrame.Process();
        g_da_ms_seqframe = da_sf.GetElapsed_sec() * 1000.f;
    }

    g_bLoaded = true;
    // else
    // seqFrame.Process(rp_Frame);
    stats.EngineTotal.End();
    stats.EngineTotal.FrameEnd();

    ImGui::EndFrame();
}

ENGINE_API bool bShowPauseString = true;

void CRenderDevice::Pause(bool bOn, bool bTimer, bool bSound, [[maybe_unused]] pcstr reason)
{
    static int snd_emitters_ = -1;
    if (g_bBenchmark || GEnv.isDedicatedServer)
        return;

    if (bOn)
    {
        if (!Paused())
        {
            if (editor_mode())
                bShowPauseString = false;
#ifdef DEBUG
            else if (xr_strcmp(reason, "li_pause_key_no_clip") == 0)
                bShowPauseString = false;
#endif
            else
                bShowPauseString = true;
        }
        if (bTimer && (!g_pGamePersistent || g_pGamePersistent->CanBePaused()))
        {
            g_pauseMngr().Pause(true);
#ifdef DEBUG
            if (xr_strcmp(reason, "li_pause_key_no_clip") == 0)
                TimerGlobal.Pause(false);
#endif
        }
        if (bSound && GEnv.Sound)
            snd_emitters_ = GEnv.Sound->pause_emitters(true);
    }
    else
    {
        if (bTimer && g_pauseMngr().Paused())
        {
            fTimeDelta = EPS_S + EPS_S;
            g_pauseMngr().Pause(false);
        }
        if (bSound)
        {
            if (snd_emitters_ > 0) // avoid crash
                snd_emitters_ = GEnv.Sound->pause_emitters(false);
            else
            {
#ifdef DEBUG
                Log("GEnv.Sound->pause_emitters underflow");
#endif
            }
        }
    }
}

bool CRenderDevice::Paused() { return g_pauseMngr().Paused(); }

void CRenderDevice::OnWindowActivate(SDL_Window* window, bool activated)
{
    ZoneScoped;

    if (editor().GetState() == editor::ide::visible_state::full)
    {
        if (window != m_sdlWnd)
        {
            if (activated)
                editor().OnAppActivate();
            else
                editor().OnAppDeactivate();
        }
        return;
    }

    if (!GEnv.isDedicatedServer && activated)
        pInput->GrabInput(true);
    else
        pInput->GrabInput(false);

    b_is_Active = activated || psDeviceFlags.test(rsAlwaysActive);

    // [DA_PORT] Диагностика зависания при альт-табе.
    //
    // Симптом (29.07): игра теряет фокус и НЕ возвращается — ввод мёртв, симуляция стоит, в лог не
    // попадает ни строки, при этом окно отвечает и штатный выход отрабатывает. Это ровно поведение
    // ветки ниже: `TaskScheduler->Pause(true)` останавливает работу, а снять паузу может только
    // переход состояния. Если событие возврата фокуса до нас не доехало или состояние разошлось,
    // игра остаётся замороженной навсегда.
    //
    // Гипотез было несколько, проверить чтением ни одну не удалось, поэтому здесь замер, а не
    // правка: строка печатается ТОЛЬКО на смену состояния (события фокуса редки, спама нет) и в
    // следующий раз скажет, пришёл ли `activated=1` вообще. Молчание лога при живом окне будет
    // означать, что событие не дошло до движка — тогда чинить надо приём в x_ray.cpp; строка с
    // `activated=1` при замёрзшей игре укажет на другое место.
    //
    if (activated != b_is_InFocus)
    {
        Msg("* [DA_PORT] окно: %s (b_is_Active=%d, always_active=%d)", activated ? "фокус получен" : "фокус потерян",
            b_is_Active ? 1 : 0, psDeviceFlags.test(rsAlwaysActive) ? 1 : 0);
        FlushLog();

        b_is_InFocus = activated;
        if (b_is_InFocus)
        {
            TaskScheduler->Pause(false);
            seqAppActivate.Process();
            app_inactive_time += TimerMM.GetElapsed_ms() - app_inactive_time_start;
        }
        else
        {
            app_inactive_time_start = TimerMM.GetElapsed_ms();
            seqAppDeactivate.Process();

            // [DA_PORT] При rs_always_active планировщик НЕ останавливаем.
            //
            // Флаг обещает «работать и без фокуса», но до сих пор влиял только на b_is_Active, то
            // есть на рендер: картинка рисовалась, а симуляция всё равно вставала здесь. Игроку,
            // который альт-табнулся, это ровно то же зависание.
            //
            // Заодно это единственный доступный обход зависания при возврате фокуса: если пауза не
            // ставилась, то и снимать её не нужно, и потерянное событие активации игру не заморозит.
            // Цена — фоновая работа при свёрнутой игре, но её игрок включает сознательно.
            if (!psDeviceFlags.test(rsAlwaysActive))
                TaskScheduler->Pause(true);
        }
    }
}

void CRenderDevice::time_factor(const float time_factor)
{
    Timer.time_factor(time_factor);
    TimerGlobal.time_factor(time_factor);
    if (!strstr(Core.Params, "-sound_constant_speed"))
        psSoundTimeFactor = time_factor; //--#SM+#--
}

void CRenderDevice::AddSeqFrame(pureFrame* f, bool mt)
{
    if (mt)
        seqFrameMT.Add(f, REG_PRIORITY_HIGH);
    else
        seqFrame.Add(f, REG_PRIORITY_LOW);
}

void CRenderDevice::RemoveSeqFrame(pureFrame* f)
{
    seqFrameMT.Remove(f);
    seqFrame.Remove(f);
}

void CRenderDevice::script_register(lua_State* luaState)
{
    using namespace luabind;
    module(luaState)
    [
        class_<CRenderDevice>("render_device")
            .def_readonly("width", &CRenderDevice::dwWidth)
            .def_readonly("height", &CRenderDevice::dwHeight)
            .def_readonly("time_delta", &CRenderDevice::dwTimeDelta)
            .def_readonly("f_time_delta", &CRenderDevice::fTimeDelta)
            .def_readonly("cam_pos", &CRenderDevice::vCameraPosition)
            .def_readonly("cam_dir", &CRenderDevice::vCameraDirection)
            .def_readonly("cam_top", &CRenderDevice::vCameraTop)
            .def_readonly("cam_right", &CRenderDevice::vCameraRight)
            //			.def_readonly("view",					&CRenderDevice::mView)
            //			.def_readonly("projection",				&CRenderDevice::mProject)
            //			.def_readonly("full_transform",			&CRenderDevice::mFullTransform)
            .def_readonly("fov", &CRenderDevice::fFOV)
            .def_readonly("aspect_ratio", &CRenderDevice::fASPECT)
            .def_readonly("precache_frame", &CRenderDevice::dwPrecacheFrame)
            .def_readonly("frame", &CRenderDevice::dwFrame)
            .def("time_global", +[](const CRenderDevice* self)
            {
                return (self->dwTimeGlobal);
            })
            .def("is_paused", +[](CRenderDevice* device)
            {
                return device->Paused();
            })
            .def("pause", +[](CRenderDevice* device, bool b)
            {
                device->Pause(b, TRUE, FALSE, "set_device_paused_script");
            }),

        def("app_ready", +[]()
        {
            return g_pGamePersistent->IsLoaded();
        }),
        def("device", +[]()
        {
            return &Device;
        }),
        def("time_global", +[]()
        {
            return Device.dwTimeGlobal;
        }),
        def("time_global_async", +[]()
        {
            return Device.TimerAsync_MMT();
        })
    ];
};

void CLoadScreenRenderer::Start(bool b_user_input)
{
    Device.seqFrame.Add(this, 0);
    Device.seqRender.Add(this, 0);
    m_registered = true;
    m_need_user_input = b_user_input;

    g_pGamePersistent->ShowLoadingScreen(true);
    g_pGamePersistent->LoadBegin();
}

void CLoadScreenRenderer::Stop()
{
    if (!m_registered)
        return;
    Device.seqFrame.Remove(this);
    Device.seqRender.Remove(this);

    m_registered = false;
    m_need_user_input = false;

    g_pGamePersistent->ShowLoadingScreen(false);
    g_pGamePersistent->LoadEnd();
}

void CLoadScreenRenderer::OnFrame()
{
    g_pGamePersistent->LoadStage(false);
}

void CLoadScreenRenderer::OnRender()
{
    g_pGamePersistent->load_draw_internal();
}
