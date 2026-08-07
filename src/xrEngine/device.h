#pragma once
#ifndef xr_device
#define xr_device

// Note:
// ZNear - always 0.0f
// ZFar  - always 1.0f

#include "pure.h"

#include "xrCore/FTimer.h"
#include "Stats.h"
#include "xrCommon/xr_list.h"
#include "xrCore/Threading/Event.hpp"
#include "xrCore/fastdelegate.h"
#include "xrCore/ModuleLookup.hpp"

#define DEVICE_RESET_PRECACHE_FRAME_COUNT 10

#include "editor_base.h"
#include "Include/xrRender/FactoryPtr.h"
#include "Render.h"

#include "xrScriptEngine/ScriptExporter.hpp"

#include <SDL.h>

// refs
class Task;

constexpr float VIEWPORT_NEAR = 0.2f;
constexpr float HUD_VIEWPORT_NEAR = 0.05f;

class ENGINE_API CRenderDevice : public IWindowHandler
{
public:
    // Main objects used for creating and rendering the 3D scene
    // Real application window resolution
    SDL_Rect m_rcWindowBounds{};

    // Real game window resolution
    SDL_Rect m_rcWindowClient{};

private:
    u32 Timer_MM_Delta{};
    CTimer_paused Timer;
    CTimer_paused TimerGlobal;
    CTimer TimerMM;

    void SetupStates();

public:
    // Main window
    SDL_Window* m_sdlWnd{};

    // Engine flow-control
    u32 dwFrame{};
    u32 dwPrecacheFrame{};
    u32 dwPrecacheTotal{};

    // Output resolution — the swap-chain / window size. The UI, HUD and the final present all use this.
    u32 dwWidth{};
    u32 dwHeight{};

    // [DA_PORT] Internal resolution the 3D scene is actually rendered at, driven by "r__render_scale".
    // It is the size of every scene render target, so the deferred pipeline works at this resolution and
    // the final post-process pass stretches the result up to dwWidth x dwHeight. Equal to the output
    // resolution when the scale is 100% — the groundwork FSR needs, and useful on its own for weak GPUs.
    u32 dwRenderWidth{};
    u32 dwRenderHeight{};

    void UpdateRenderResolution();

    float fWidth_2{};
    float fHeight_2{};

    bool b_is_Ready{};
    bool b_is_Active{};
    bool b_is_InFocus{};

    bool m_bNearer{};

public:
    void SetNearer(bool enabled)
    {
        if (enabled && !m_bNearer)
        {
            m_bNearer = true;
            mProject._43 -= EPS_L;
        }
        else if (!enabled && m_bNearer)
        {
            m_bNearer = false;
            mProject._43 += EPS_L;
        }
        GEnv.Render->SetCacheXform(mView, mProject);
        // R_ASSERT(0);
        // TODO: re-implement set projection
        // RCache.set_xform_project (mProject);
    }

public:
    // Registrators
    MessageRegistry<pureRender> seqRender;
    MessageRegistry<pureAppActivate> seqAppActivate;
    MessageRegistry<pureAppDeactivate> seqAppDeactivate;
    MessageRegistry<pureAppEnd> seqAppEnd;
    MessageRegistry<pureFrame> seqFrame;
    MessageRegistry<pureFrame> seqFrameMT;
    MessageRegistry<pureDeviceReset> seqDeviceReset;
    MessageRegistry<pureUIReset> seqUIReset;
    xr_vector<fastdelegate::FastDelegate0<>> seqParallel;

private:
    struct RenderDeviceStatistics
    {
        CStatTimer RenderTotal; // pureRender
        CStatTimer EngineTotal; // pureFrame
        float fFPS, fRFPS, fTPS; // FPS, RenderFPS, TPS

        RenderDeviceStatistics()
        {
            fFPS = 30.f;
            fRFPS = 30.f;
            fTPS = 0;
        }
    };

    RenderDeviceStatistics stats;
    CStats* Statistic{};

public:
    // Engine flow-control
    float fTimeDelta{};
    float fTimeDeltaReal{};
    float fTimeGlobal{};
    u32 dwTimeDelta{};
    u32 dwTimeGlobal{};
    u32 dwTimeContinual{};

    // Cameras & projection
    Fvector vCameraPosition{};
    Fvector vCameraDirection{};
    Fvector vCameraTop{};
    Fvector vCameraRight{};

    Fmatrix mView{};
    Fmatrix mInvView{};
    Fmatrix mProject{};
    Fmatrix mFullTransform{};
    Fmatrix mInvFullTransform{};

    // Copies of corresponding members. Used for synchronization.
    Fvector vCameraPositionSaved{};
    Fvector vCameraDirectionSaved{};
    Fvector vCameraTopSaved{};
    Fvector vCameraRightSaved{};

    Fmatrix mViewSaved{};
    Fmatrix mProjectSaved{};
    Fmatrix mFullTransformSaved{};

    float fFOV{};
    float fASPECT{};

    bool m_allowWindowDrag{}; // For windowed mode
    bool IsAnselActive{};

    CRenderDevice()
    {
        Timer.Start();
    }

    void Pause(bool bOn, bool bTimer, bool bSound, pcstr reason);
    bool Paused();

public:
    // Scene control
    void ProcessFrame();

    void PreCache(u32 amount, bool wait_user_input);

    bool BeforeFrame();
    void FrameMove();

    void OnCameraUpdated();
    void DoRender();
    bool RenderBegin();
    void Clear();
    void RenderEnd();

    void overdrawBegin();
    void overdrawEnd();

    // Mode control
    IC CTimer_paused* GetTimerGlobal() { return &TimerGlobal; }
    u32 TimerAsync() { return TimerGlobal.GetElapsed_ms(); }
    u32 TimerAsync_MMT() { return TimerMM.GetElapsed_ms() + Timer_MM_Delta; }

public:
    // Creation & Destroying
    void Create();
    void Destroy();

    void Reset(bool precache = true);

    void Run();
    void Shutdown();

    void ProcessEvent(const SDL_Event& event);
    void OnWindowActivate(SDL_Window* window, bool activated);

    void UpdateWindowProps();
    void UpdateWindowRects();
    void SelectResolution(bool windowed);

    void Initialize();

    void InitializeImGui();
    void DestroyImGui();

    void FillVideoModes();
    void CleanupVideoModes();

    const RenderDeviceStatistics& GetStats() const { return stats; }
    void DumpStatistics(class IGameFont& font, class IPerformanceAlert* alert);

    void SetWindowDraggable(bool draggable);
    bool IsWindowDraggable() const { return m_allowWindowDrag; }

    void* GetApplicationWindowHandle() const override;
    SDL_Window* GetApplicationWindow() override;
    void OnErrorDialog(bool beforeDialog) override;
    void OnFatalError() override;

    void time_factor(const float time_factor);

    IC float time_factor() const
    {
        VERIFY(Timer.time_factor() == TimerGlobal.time_factor());
        return (Timer.time_factor());
    }

public:
    // Multi-threading
    Event PresentationFinished = nullptr;

    static constexpr u32 MaximalWaitTime = 16; // ms

    // Usable only when called from thread, that initialized SDL
    // Calls SDL_PumpEvents() at least twice.
    static void WaitEvent(Event& event)
    {
        // Once at the beginning:
        SDL_PumpEvents();

        while (!event.Wait(MaximalWaitTime))
            SDL_PumpEvents();

        // And once in the end:
        SDL_PumpEvents();
    }

    void AddSeqFrame(pureFrame* f, bool mt);
    void RemoveSeqFrame(pureFrame* f);

    // [DA_PORT] Снимаем ВСЕ вхождения делегата, а не первое найденное.
    //
    // Было `std::find` + `erase` одного элемента, и это молчаливая мина. Обычно объект попадает в
    // список один раз за кадр, а в конце кадра список очищается целиком — дублей не возникает.
    // Но при выгрузке уровня кадров нет: IGame_Level::net_Stop гоняет Objects.Update ШЕСТЬ раз
    // подряд, и каждый проход добавляет делегат заново. Получается до шести одинаковых записей,
    // из которых net_Destroy снимал ровно одну.
    //
    // Остальные пять переживали уничтожение объекта и вызывались уже по освобождённой памяти:
    // падение выглядело как чтение по адресу 0x40 внутри CSoundPlayer::update, в рабочем потоке
    // TaskManager, без всякой связи с местом настоящей ошибки.
    ICF void remove_from_seq_parallel(const fastdelegate::FastDelegate0<>& delegate)
    {
        seqParallel.erase(std::remove(seqParallel.begin(), seqParallel.end(), delegate), seqParallel.end());
    }

private:
    void CalcFrameStats();

public:
    [[nodiscard]]
    auto& editor() { return m_editor; }

    [[nodiscard]]
    auto editor_mode() const { return m_editor.is_shown(); }

    [[nodiscard]]
    auto GetImGuiContext() const { return m_imgui_context; }

public:
    struct ImGuiViewportData
    {
        SDL_Window* Window;
        bool        WindowOwned;

        ImGuiViewportData(SDL_Window* window) : Window(window), WindowOwned(false) {}

        ImGuiViewportData(ImVec2 pos, ImVec2 size, Uint32 flags)
        {
            Window = SDL_CreateWindow("ImGui Viewport (no title yet)",
                (int)pos.x, (int)pos.y, (int)size.x, (int)size.y, flags);
            WindowOwned = true;
        }

        ~ImGuiViewportData()
        {
            if (Window && WindowOwned)
            {
                SDL_DestroyWindow(Window);
            }
        }
    };

private:
    xray::editor::ide m_editor;

    ImGuiContext* m_imgui_context{};
    IImGuiRender* m_imgui_render{};

private:
    DECLARE_SCRIPT_REGISTER_FUNCTION();
};

extern ENGINE_API CRenderDevice Device;

extern ENGINE_API bool g_bBenchmark;

// [DA_PORT] Ловушка на выброс в отложенных задачах кадра. Порог в миллисекундах, 0 — выключено.
// Команда da_seq_trap, разбор в xr_ioc_cmd.cpp.
extern ENGINE_API float ps_da_seq_trap;

// [DA_PORT] Именная проба для задачи из seqParallel.
//
// Сам цикл видит только безымянные делегаты: сказать «худшая задача номер семнадцать» он может, а
// назвать её — нет. Поэтому подозреваемые представляются сами: одна строка в начале функции, и при
// превышении порога она пишет в лог своё имя и время.
//
// Стоит ровно ноль, пока ловушка выключена: замер идёт только при ps_da_seq_trap > 0.
struct ENGINE_API da_seq_probe
{
    const char* name;
    CTimer timer;
    bool armed;

    explicit da_seq_probe(const char* n);
    ~da_seq_probe();
};

typedef fastdelegate::FastDelegate0<bool> LOADING_EVENT;
extern ENGINE_API xr_list<LOADING_EVENT> g_loading_events;

class ENGINE_API CLoadScreenRenderer : public pureFrame, public pureRender
{
public:
    void OnFrame() override;
    void OnRender() override;

    void Start(bool b_user_input);
    void Stop();

    bool IsActive() const { return m_registered; }
    bool NeedsUserInput() const { return m_need_user_input; }

private:
    bool m_registered{};
    bool m_need_user_input{};
};
extern ENGINE_API CLoadScreenRenderer load_screen_renderer;

class CDeviceResetNotifier : public pureDeviceReset
{
public:
    CDeviceResetNotifier(const int prio = REG_PRIORITY_NORMAL) { Device.seqDeviceReset.Add(this, prio); }
    virtual ~CDeviceResetNotifier() { Device.seqDeviceReset.Remove(this); }
};

class CUIResetNotifier : public pureUIReset
{
public:
    CUIResetNotifier(const int uiResetPrio = REG_PRIORITY_NORMAL)
    {
        Device.seqUIReset.Add(this, uiResetPrio);
    }

    virtual ~CUIResetNotifier()
    {
        Device.seqUIReset.Remove(this);
    }
};

#endif
