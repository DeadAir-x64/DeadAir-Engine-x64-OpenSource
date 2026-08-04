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

// [DA_PORT] Frame-time watchdog: milliseconds above which a frame is worth a line in the log. Zero is
// off. Unlike the dump this is meant to be left running while playing - it says nothing until a frame
// actually misbehaves, then reports what it was doing, so the log names the interaction rather than
// the clock. See ProcessFrame.
ENGINE_API int ps_da_perf_watch = 0;

// [DA_PORT] Filled by the parallel task, read after the wait - see ProcessFrame.
static float g_da_perf_seq_ms = 0.f;
static float g_da_perf_mt_ms = 0.f;
static u32 g_da_perf_seq_count = 0;
static float g_da_perf_seq_inner_ms = 0.f;
static float g_da_perf_sleep_ms = 0.f;
static float g_da_perf_total_ms = 0.f;
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
        CTimer t;
        if (perf)
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
        CTimer entry_timer;
        for (u32 pit = 0; pit < count; pit++)
        {
            if (perf)
                entry_timer.Start();
            seqParallel[pit]();
            if (perf)
                inner += entry_timer.GetElapsed_sec() * 1000.0;
        }
        seqParallel.clear();
        if (perf)
            g_da_perf_seq_inner_ms = float(inner);

        if (perf)
        {
            g_da_perf_seq_ms = t.GetElapsed_sec() * 1000.f;
            g_da_perf_seq_count = count;
            t.Start();
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

    // [DA_PORT] The same figures the statistics overlay shows, written to the log for N frames.
    //
    // Reading them off a screenshot turned out to be worthless: the game saves the PNG inside the
    // frame the key was pressed on, so every timer still open at that moment swallows the encode. It
    // showed as the wait and the input costing thirteen milliseconds each - nearly the same number,
    // which is what gave it away. This costs one line per frame and nothing is open while it writes.
    if (perf)
    {
        const float ms_frame = fTimeDeltaReal * 1000.f;

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
        bool watch_fires = false;
        if (ps_da_perf_watch > 0 && ms_frame > float(ps_da_perf_watch))
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
            Msg("~ [DA_PERF]%s frame %5.2f | move %5.2f | render %5.2f | wait %5.2f | seq %5.2f (inner "
                "%5.2f) x%u | mt %5.2f | goap: actual %5.2f x%u, search %5.2f x%u, exec %5.2f x%u | "
                "oh: %5.2f in%u alive%u throw%u | path %5.2f x%u | obj %u | sleep %5.2f | total %5.2f | "
                "worst: %s",
                watch_fires ? " SPIKE" : "", ms_frame, ms_move, ms_render, ms_wait, g_da_perf_seq_ms,
                g_da_perf_seq_inner_ms, g_da_perf_seq_count, g_da_perf_mt_ms, float(g_da_goap_actual_ms),
                g_da_goap_calls, float(g_da_goap_search_ms), g_da_goap_searches, float(g_da_goap_exec_ms),
                g_da_goap_execs, float(g_da_oh_ms), g_da_oh_entries, g_da_oh_alive, g_da_oh_throws,
                float(g_da_lpb_ms), g_da_lpb_calls, objects, g_da_perf_sleep_ms, g_da_perf_total_ms,
                worst);
        }

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

    seqFrame.Process();

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
