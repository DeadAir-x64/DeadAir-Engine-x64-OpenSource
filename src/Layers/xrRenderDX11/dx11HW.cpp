#include "stdafx.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp).
extern ENGINE_API int ps_r__d3d_debug;

#include "dx11HW.h"

#include "StateManager/dx11SamplerStateCache.h"
#include "dx11TextureUtils.h"

#include <SDL_syswm.h>

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Метка последней фазы кадра; определена в da_gpu_timer.cpp. Заголовок оттуда сюда не
// тянем -- он про замер, а нужны три переменные, и объявление в том же пространстве имён их найдёт.
extern pcstr g_da_stage;
extern u32 g_da_stage_frame;
extern u32 g_da_stage_seq;

CHW HW;

CHW::CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Add(this);
    Device.seqAppDeactivate.Add(this);
}

CHW::~CHW()
{
    if (!ThisInstanceIsGlobal())
        return;

    Device.seqAppActivate.Remove(this);
    Device.seqAppDeactivate.Remove(this);
}

// [DA_PORT] Полноэкранным режимом распоряжается ТОЛЬКО SDL — отсюда и пустота в этих двух функциях.
//
// Раньше хозяев было два: окно переводил в полноэкранный SDL (Device_mode.cpp), а цепочка буферов
// делала это же сама через SetFullscreenState. Пока никто не переключался, оба говорили одно и то же,
// но стоило свернуть игру — и они расходились: здесь DXGI выходил из полноэкранного, SDL про это не
// знал и режим монитора не возвращал, а при возврате DXGI входил обратно, хотя окно у SDL уже не
// числилось полноэкранным. Получалось окно 1280x1024 в углу рабочего стола 1920x1080 — та самая
// «половина экрана», которая лечилась переоткрытием окна: только полный цикл закрыть-открыть заново
// сводил два состояния в одно.
//
// Одного хозяина достаточно: SDL меняет режим монитора и флаги окна, DXGI получает цепочку под
// готовый размер. На Windows 10 и новее это ничего не стоит — при цепочке flip-модели, накрывающей
// экран целиком, система и так отдаёт независимый переворот, то есть тот же путь, ради которого
// монопольный режим и нужен.
//
// Сворачивание тоже отдано SDL: у полноэкранного окна он сворачивает сам при потере фокуса, а
// «полный экран в окне» специально НЕ сворачивает — в этом весь смысл режима, и прежний
// принудительный SW_MINIMIZE его ломал.
void CHW::OnAppActivate()
{
}

void CHW::OnAppDeactivate()
{
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
void CHW::CreateD3D()
{
    ZoneScoped;

    hDXGI = XRay::LoadModule("dxgi");
    hD3D = XRay::LoadModule("d3d11");
    if (!hD3D->IsLoaded() || !hDXGI->IsLoaded())
    {
        Valid = false;
        return;
    }

    // Минимально поддерживаемая версия Windows => Windows Vista SP2 или Windows 7.
    const auto createDXGIFactory = reinterpret_cast<decltype(&CreateDXGIFactory1)>(hDXGI->GetProcAddress("CreateDXGIFactory1"));
    if (createDXGIFactory)
        createDXGIFactory(__uuidof(IDXGIFactory1), (void**)(&m_pFactory));

    if (m_pFactory)
        m_pFactory->EnumAdapters1(0, &m_pAdapter);

#ifdef HAS_DXGI1_4
    // [DA_PORT] Тот же адаптер через интерфейс с бюджетом памяти. Отсутствие -- не ошибка: на
    // системе без DXGI 1.4 просто не будет учёта видеопамяти, всё остальное работает как прежде.
    if (m_pAdapter)
        m_pAdapter->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&m_pAdapter3);
#endif

    Valid = m_pAdapter;
}

void CHW::DestroyD3D()
{
#ifdef HAS_DXGI1_4
    _RELEASE(m_pAdapter3);
#endif

    _SHOW_REF("refCount:m_pAdapter", m_pAdapter);
    _RELEASE(m_pAdapter);

    _SHOW_REF("refCount:m_pFactory", m_pFactory);
    _RELEASE(m_pFactory);

    // Manually close and unload additional DLLs
    // To make it work with DXVK, etc.
    hD3D->Close();
    hDXGI->Close();
    // [DA_PORT] #70: принудительная выгрузка d3d11.dll/dxgi.dll допустима ТОЛЬКО при чистом сносе
    // устройства. Наш hD3D->Close()/hDXGI->Close() выше уже отпустил нашу ссылку на модуль; лишняя
    // FreeLibrary(GetModuleHandle) добивала счётчик загрузки до нуля и выгружала код API-DLL, пока
    // ЖИВЫ COM-объекты (утёкшие RT/SRV/текстуры + NGX/драйвер держали устройство). Их vtable уезжали
    // в снесённую память, и следующий Release/драйверный колбэк прыгал в никуда — детерминированный
    // C0000005 «исполнение по фиксированному адресу вне модулей» на выходе после смены видео-настройки.
    // При остаточных ссылках НЕ трогаем — эти DLL корректно закроет системный process-exit.
    if (m_device_teardown_refs == 0)
    {
        if (auto hModule = GetModuleHandleA("d3d11.dll"))
            FreeLibrary(hModule);
        if (auto hModule = GetModuleHandleA("dxgi.dll"))
            FreeLibrary(hModule);
    }
    else
    {
        Msg("! [DA_PORT] #70: устройство D3D ушло с %u внешними ссылками — d3d11/dxgi вручную НЕ "
            "выгружаем (иначе teardown падает в снесённом коде); закроет process-exit",
            m_device_teardown_refs);
    }
}

void CHW::CreateDevice(SDL_Window* sdlWnd)
{
    ZoneScoped;

    CreateD3D();
    if (!Valid)
        return;

    m_DriverType = Caps.bForceGPU_REF ? D3D_DRIVER_TYPE_REFERENCE : D3D_DRIVER_TYPE_HARDWARE;

    // Display the name of video board
    DXGI_ADAPTER_DESC1 Desc{};
    if (FAILED(m_pAdapter->GetDesc1(&Desc)))
        Msg("! [%s] failed to retrieve adapter description", __FUNCTION__);
    //  Warning: Desc.Description is wide string
    Msg("* GPU [vendor:%X]-[device:%X]: %S", Desc.VendorId, Desc.DeviceId, Desc.Description);

    Caps.id_vendor = Desc.VendorId;
    Caps.id_device = Desc.DeviceId;

    u32 createDeviceFlags = 0;

#ifdef DEBUG
    if (xrDebug::DebuggerIsPresent())
        createDeviceFlags |= D3D_CREATE_DEVICE_DEBUG;
#endif

    // [DA_PORT] The validation layer, on a console variable and available in Release.
    //
    // It answers a whole class of question nothing else can: a resource bound for writing while a
    // shader reads it, a slot that does not match what the shader declares, a view created against the
    // wrong format. D3D11 does none of that by default - it returns success and hands the shader zeros,
    // which is why these faults show up as a wrong picture rather than an error, and why guessing at
    // them is hopeless. Requires the "Graphics Tools" optional Windows feature; without it device
    // creation with this flag fails, so the failure is caught and the flag dropped rather than fatal.
    // ⛔ [DA_PORT] Ключ командной строки обязателен, консольной ручки НЕ ХВАТАЕТ.
    //
    // Слой читается ЗДЕСЬ, при создании устройства, то есть до того как консоль вообще существует.
    // А `r__d3d_debug` заведён как отладочная ручка (CCC_DaDebugInteger) и по нашей же правке в
    // user.ltx не сохраняется. Получалось замкнутое кольцо: задать в консоли можно, но включается
    // он только на старте, а до старта задать негде — перезапуск значение терял. Инструмент был
    // недостижим при живой на вид ручке, и это выяснилось только когда он понадобился по делу.
    const bool da_debug_param = strstr(Core.Params, "-d3d_debug") != nullptr;
    if (da_debug_param)
    {
        // ⚠️ Поднять ИМЕННО переменную, а не только флаг создания устройства. Слив сообщений
        // (da_d3d_debug_drain) сторожится этой же переменной, и первая версия ключа её не трогала:
        // слой включался, исправно копил сообщения, а печатать их было некому. Один источник
        // истины — одна строка ниже; иначе половина проводки живёт, а половина молчит.
        ::ps_r__d3d_debug = 1;
    }
    if (::ps_r__d3d_debug)
    {
        createDeviceFlags |= D3D_CREATE_DEVICE_DEBUG;
        Msg("* [DA_PORT] D3D11 validation layer requested (%s)",
            da_debug_param ? "-d3d_debug" : "r__d3d_debug");
    }

    HRESULT R;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
#ifdef HAS_DX11_3
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
#endif
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevels2[] =
    {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    D3D_FEATURE_LEVEL featureLevels3[] =
    {
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    auto& pContext = d3d_contexts_pool[CHW::IMM_CTX_ID];

    const auto createDevice = [&](const D3D_FEATURE_LEVEL* level, const u32 levels)
    {
        ZoneScopedN("CreateDevice");

        static const auto d3d11CreateDevice = reinterpret_cast<PFN_D3D11_CREATE_DEVICE>(hD3D->GetProcAddress("D3D11CreateDevice"));
        return d3d11CreateDevice(m_pAdapter, D3D_DRIVER_TYPE_UNKNOWN,
            nullptr, createDeviceFlags, level, levels,
            D3D11_SDK_VERSION, &pDevice, &FeatureLevel, &pContext);
    };

    const auto createDeviceAll = [&]()
    {
        if (DX10Only)
            return createDevice(featureLevels3, std::size(featureLevels3));
        HRESULT r = createDevice(featureLevels, std::size(featureLevels));
        if (FAILED(r))
            r = createDevice(featureLevels2, std::size(featureLevels2));
        return r;
    };

    R = createDeviceAll();

    // [DA_PORT] The validation layer is refused outright when the "Graphics Tools" Windows feature is
    // not installed, and the whole device creation fails with it. Drop the flag and try again rather
    // than leave the game unable to start because of a diagnostic setting.
    if (FAILED(R) && (createDeviceFlags & D3D_CREATE_DEVICE_DEBUG))
    {
        Msg("! [DA_PORT] device creation failed WITH the validation layer - is the 'Graphics Tools' "
            "Windows feature installed? Retrying without it.");
        createDeviceFlags &= ~u32(D3D_CREATE_DEVICE_DEBUG);
        R = createDeviceAll();
    }

    // [DA_PORT] Drain the validation layer into our own log.
    //
    // Its messages otherwise go to the debugger output, which does not exist when the game is started
    // normally - so the layer would be enabled and still tell us nothing. Pulled once per frame from
    // da_d3d_debug_drain(); this only sets up the filter.
    if (SUCCEEDED(R) && (createDeviceFlags & D3D_CREATE_DEVICE_DEBUG))
    {
        ID3D11InfoQueue* iq = nullptr;
        if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&iq))) && iq)
        {
            // The steady per-frame chatter is not what we are after; corruption shows up as warnings
            // and errors, and letting INFO through buries them thousands deep.
            D3D11_INFO_QUEUE_FILTER filter{};
            D3D11_MESSAGE_SEVERITY allow[] = { D3D11_MESSAGE_SEVERITY_CORRUPTION,
                D3D11_MESSAGE_SEVERITY_ERROR, D3D11_MESSAGE_SEVERITY_WARNING };
            filter.AllowList.NumSeverities = 3;
            filter.AllowList.pSeverityList = allow;
            iq->PushStorageFilter(&filter);
            iq->Release();
            Msg("* [DA_PORT] validation layer active, messages will appear in this log");
        }
    }

    if (SUCCEEDED(R))
    {
        pContext->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&pContext1));
#ifdef HAS_DX11_3
        pDevice->QueryInterface(__uuidof(ID3D11Device3), reinterpret_cast<void**>(&pDevice3));
#endif
        if (FeatureLevel >= D3D_FEATURE_LEVEL_11_0)
        {
            D3DCompile = &::D3DCompile;
            ComputeShadersSupported = true;
        }
        else
        {
            if (ClearSkyMode)
            {
                hD3DCompiler = XRay::LoadModule("d3dcompiler_37");
                D3DCompile = reinterpret_cast<D3DCompileFunc>(hD3DCompiler->GetProcAddress("D3DCompileFromMemory"));
            }
            else
            {
                D3DCompile = &::D3DCompile;
            }

            D3D11_FEATURE_DATA_D3D10_X_HARDWARE_OPTIONS data;
            pDevice->CheckFeatureSupport(D3D11_FEATURE_D3D10_X_HARDWARE_OPTIONS,
                &data, sizeof(data));
            ComputeShadersSupported = data.ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x;
        }
        D3D11_FEATURE_DATA_D3D11_OPTIONS options;
        pDevice->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));

        D3D11_FEATURE_DATA_DOUBLES doubles;
        pDevice->CheckFeatureSupport(D3D11_FEATURE_DOUBLES, &doubles, sizeof(doubles));

        DoublePrecisionFloatShaderOps = doubles.DoublePrecisionFloatShaderOps;
        SAD4ShaderInstructions = options.SAD4ShaderInstructions;
        ExtendedDoublesShaderInstructions = options.ExtendedDoublesShaderInstructions;
    }

    if (FAILED(R))
    {
        Valid = false;
        if (!ThisInstanceIsGlobal())
            return;
        // Fatal error! Cannot create rendering device AT STARTUP !!!
        Msg("Failed to initialize graphics hardware.\n"
            "Please try to restart the game.\n"
            "CreateDevice returned 0x%08x", R);
        xrDebug::DoExit("Failed to initialize graphics hardware.\nPlease try to restart the game.");
    }

    _SHOW_REF("* CREATE: DeviceREF:", pDevice);

    // Register immediate context in profiler
    if (ThisInstanceIsGlobal())
    {
        TaskScheduler->AddTask([this]
        {
            ZoneScopedN("TracyD3D11Context");
            profiler_ctx = TracyD3D11Context(pDevice, get_context(CHW::IMM_CTX_ID));
        });
    }

    // Create deferred contexts
    if (ThisInstanceIsGlobal())
    {
        ZoneScopedN("Create deferred contexts");
        for (int id = 0; id < R__NUM_PARALLEL_CONTEXTS; ++id)
        {
            R = pDevice->CreateDeferredContext(0, &d3d_contexts_pool[id]);
            VERIFY(SUCCEEDED(R));
        }
    }

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);

    if (!SDL_GetWindowWMInfo(sdlWnd, &info))
    {
        Msg("! Failed to retrieve SDL window handle: %s", SDL_GetError());
        Valid = false;
        return;
    }

    const HWND hwnd = info.info.win.window;

    if (!CreateSwapChain2(hwnd))
    {
        if (!CreateSwapChain(hwnd))
            Valid = false;
    }

    // Select depth-stencil format
    constexpr DXGI_FORMAT formats[] =
    {
        //DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
        DXGI_FORMAT_D24_UNORM_S8_UINT,
    };
    const DXGI_FORMAT selectedFormat = SelectFormat(D3D_FORMAT_SUPPORT_DEPTH_STENCIL, formats);
    if (selectedFormat == DXGI_FORMAT_UNKNOWN)
    {
        Valid = false;
        if (!ThisInstanceIsGlobal())
            return;
        Log("Failed to initialize graphics hardware: "
            "failed to select depth-stencil format.\n"
            "Please try to restart the game.");
        xrDebug::DoExit("Failed to initialize graphics hardware.\nPlease try to restart the game.");
    }
    Caps.fDepth = dx11TextureUtils::ConvertTextureFormat(selectedFormat);

    const auto memory = Desc.DedicatedVideoMemory;
    Msg("*   Texture memory: %d M", memory / (1024 * 1024));
}

bool CHW::CreateSwapChain(HWND hwnd)
{
    ZoneScoped;

    // Set up the presentation parameters
    DXGI_SWAP_CHAIN_DESC& sd = m_ChainDesc;
    ZeroMemory(&sd, sizeof(sd));

    // Back buffer
    sd.BufferDesc.Width = Device.dwWidth;
    sd.BufferDesc.Height = Device.dwHeight;

    //  TODO: DX11: implement dynamic format selection
    constexpr DXGI_FORMAT formats[] =
    {
        //DXGI_FORMAT_R16G16B16A16_FLOAT, // Do we even need this?
        //DXGI_FORMAT_R10G10B10A2_UNORM, // D3DX11SaveTextureToMemory fails on this format
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };

    // Select back-buffer format
    sd.BufferDesc.Format = SelectFormat(D3D_FORMAT_SUPPORT_DISPLAY, formats);
    Caps.fTarget = dx11TextureUtils::ConvertTextureFormat(sd.BufferDesc.Format);

    // Buffering
    BackBufferCount = 1;
    sd.BufferCount = BackBufferCount;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    // Multisample
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;

    // Windoze
    /* XXX:
       Probably the reason of weird tearing
       glitches reported by Shoker in windowed
       mode with VSync enabled.
       XXX: Fix this windoze stuff!!!
    */
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    sd.OutputWindow = hwnd;

    // [DA_PORT] Цепочка всегда оконная: полноэкранный режим держит SDL, см. CHW::OnAppActivate.
    sd.Windowed = TRUE;

    //  Additional set up
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    const auto hr = m_pFactory->CreateSwapChain(pDevice, &sd, &m_pSwapChain);
    return SUCCEEDED(hr);
}

bool CHW::CreateSwapChain2(HWND hwnd)
{
    if (strstr(Core.Params, "-no_dx11_2"))
        return false;

    ZoneScoped;

#ifdef HAS_DX11_2
    IDXGIFactory2* pFactory2{};
    m_pAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&pFactory2);
    if (!pFactory2)
        return false;

    // Set up the presentation parameters
    DXGI_SWAP_CHAIN_DESC1 desc{};

    // Back buffer
    desc.Width = Device.dwWidth;
    desc.Height = Device.dwHeight;

    constexpr DXGI_FORMAT formats[] =
    {
        //DXGI_FORMAT_R16G16B16A16_FLOAT,
        //DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
    };

    // Select back-buffer format
    desc.Format = SelectFormat(D3D11_FORMAT_SUPPORT_DISPLAY, formats);
    Caps.fTarget = dx11TextureUtils::ConvertTextureFormat(desc.Format);

    // Buffering
    BackBufferCount = 1; // For DXGI_SWAP_EFFECT_FLIP_DISCARD we need at least two
    desc.BufferCount = BackBufferCount;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

    // Multisample
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    // Windoze
    //desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // XXX: tearing glitches with flip presentation model
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;

    // Additional setup
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    // [DA_PORT] Цепочка всегда оконная (пустое описание полноэкранного = nullptr): полноэкранный
    // режим держит SDL, см. CHW::OnAppActivate.
    IDXGISwapChain1* swapchain{};
    const HRESULT result = pFactory2->CreateSwapChainForHwnd(pDevice, hwnd, &desc,
        nullptr, nullptr, &swapchain);
    _RELEASE(pFactory2);

    if (FAILED(result))
        return false;

    if (FAILED(swapchain->GetDesc(&m_ChainDesc)))
    {
        _RELEASE(swapchain);
        return false;
    }
    m_pSwapChain = swapchain;

    m_pSwapChain->QueryInterface(__uuidof(IDXGISwapChain2), reinterpret_cast<void**>(&m_pSwapChain2));

    if (m_pSwapChain2 && ThisInstanceIsGlobal())
        Device.PresentationFinished = m_pSwapChain2->GetFrameLatencyWaitableObject();

    return true;
#else // #ifdef HAS_DX11_2
    UNUSED(hwnd);
#endif

    return false;
}

bool CHW::ThisInstanceIsGlobal() const
{
    return this == &HW;
}

void CHW::DestroyDevice()
{
    if (ThisInstanceIsGlobal()) // only if we are global HW
    {
        RSManager.ClearStateArray();
        DSSManager.ClearStateArray();
        BSManager.ClearStateArray();
        SSManager.ClearStateArray();
    }
    //  Must switch to windowed mode to release swap chain
    // [DA_PORT] Условие теперь не срабатывает никогда — цепочка всегда оконная, полноэкранным
    // распоряжается SDL. Оставлено намеренно: если кто-то вернёт полноэкранную цепочку, освобождать
    // её всё равно придётся из оконного состояния, и правило должно попасться ему на глаза здесь.
    if (!m_ChainDesc.Windowed && m_pSwapChain)
        m_pSwapChain->SetFullscreenState(FALSE, NULL);
#ifdef HAS_DX11_2
    _RELEASE(m_pSwapChain2);
#endif
    _SHOW_REF("refCount:m_pSwapChain", m_pSwapChain);
    _RELEASE(m_pSwapChain);

    if (profiler_ctx)
        TracyD3D11Destroy(profiler_ctx);

    // [DA_PORT] Отвязать всё от конвейера и слить очередь уничтожения ПЕРЕД тем, как считать живые
    // объекты. DirectX освобождает лениво: пока привязки висят на контексте, а отложенные удаления
    // не выполнены, перепись показывает трупы — объекты с нулём внешних ссылок, которых уже никто не
    // держит. Именно они составляли 849 строк из 1024 в переписи 30.07. Документация к
    // ReportLiveDeviceObjects требует этой пары вызовов, иначе список читать бессмысленно.
    if (ps_r__d3d_debug >= 2)
    {
        if (auto* imm = d3d_contexts_pool[IMM_CTX_ID])
        {
            imm->ClearState();
            imm->Flush();
        }
    }

    _RELEASE(pContext1);
    for (int id = 0; id < R__NUM_CONTEXTS; ++id)
    {
        _SHOW_REF("refCount:pContext", d3d_contexts_pool[id]);
        _RELEASE(d3d_contexts_pool[id]);
    }

#ifdef HAS_DX11_3
    _RELEASE(pDevice3);
#endif
    _SHOW_REF("refCount:pDevice:", pDevice);

    // [DA_PORT] #70 Поимённый список живых объектов D3D при выходе.
    //
    // Строка выше печатает ЧИСЛО неосвобождённых ссылок, и оно оказалось говорящим: 198 после одной
    // загрузки уровня и 5806 после семи, то есть около девятисот объектов теряется на каждый переход.
    // Держат они память драйвера, а не наши кучи, поэтому ни обход куч, ни HeapCompact их не видят —
    // мы честно мерили «живые аллокации» и не находили ничего, пока закоммиченная память росла на
    // 800 МБ за переход.
    //
    // Само число не говорит, ЧТО именно течёт, а перебирать подозреваемых по коду дорого. DirectX
    // умеет перечислить живые объекты сам, с типами и счётчиками ссылок, — это и есть кратчайший путь
    // к имени.
    //
    // ⚠️ Перепись живёт под ОТДЕЛЬНЫМ уровнем r__d3d_debug 2, а не вместе со слоем проверки. Причина
    // прямая: она дважды роняла выход из игры (31.07), и хотя причина каждый раз была в ней самой,
    // цена ошибки здесь — испорченный сеанс отладки. При r__d3d_debug 1 работает только слой
    // проверки, который за месяц не подвёл ни разу.
    if (ps_r__d3d_debug >= 2)
    {
        ID3D11Debug* debug = nullptr;
        if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&debug)) && debug)
        {
            Msg("* [DA_PORT] живые объекты D3D на выходе:");
            FlushLog();

            // [DA_PORT] Перепись уходит в очередь сообщений слоя отладки, а не в наш лог, и очередь
            // эта с потолком. Слить её надо БЕЗ обычного ограничения в 200 строк: то поставлено
            // против ежекадрового спама, а здесь единственная за сеанс перепись, и интересен как раз
            // хвост.
            //
            // ⚠️ Ноль потолок НЕ снимает, хотя выглядит как «без ограничения»: перепись 30.07 с ним
            // пришла ровно на 1024 строки (заводское значение), оборвалась на полуслове — итоговой
            // строки устройства в ней нет, — и число живых объектов, взятое из такого списка, было
            // потолком очереди, а не измерением.
            //
            // ⚠️⚠️ И «без ограничения» через -1 тоже нельзя: 31.07 выход с ним умер молча сразу после
            // первой строки сводки. Здесь конечный потолок с большим запасом.
            //
            // ⚠️⚠️⚠️ И держать ID3D11InfoQueue ПОВЕРХ вызова ReportLiveDeviceObjects тоже нельзя: с
            // указателем, взятым ДО переписи, GetMessage падает внутри DXGIDebug (чтение по адресу
            // 0x14). Работает только порядок «перепись → взять очередь → слить → отпустить», каждый
            // раз заново. Он и восстановлен ниже; ради него дублируется получение интерфейса.
            constexpr UINT64 QUEUE_LIMIT = 1u << 20;

            // Потолок ставится отдельным коротким обращением — очередь тут не переживает перепись.
            {
                ID3D11InfoQueue* iq_limit = nullptr;
                if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&iq_limit)) && iq_limit)
                {
                    iq_limit->ClearStoredMessages();
                    iq_limit->SetMessageCountLimit(QUEUE_LIMIT);
                    Msg("* [DA_PORT] перепись: потолок очереди %llu",
                        (unsigned long long)iq_limit->GetMessageCountLimit());
                    iq_limit->Release();
                }
                FlushLog();
            }

            // Перепись + слив её в лог. Интерфейс очереди берётся ПОСЛЕ отчёта и отпускается сразу.
            const auto report_and_drain = [&](D3D11_RLDO_FLAGS flags, LPCSTR stage) {
                debug->ReportLiveDeviceObjects(flags);

                ID3D11InfoQueue* iq = nullptr;
                if (FAILED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&iq)) || !iq)
                {
                    Msg("! [DA_PORT] перепись(%s): очередь сообщений недоступна, список ушёл в отладочный вывод",
                        stage);
                    FlushLog();
                    return;
                }

                UINT64 count = iq->GetNumStoredMessages();
                if (count > QUEUE_LIMIT) // если счётчик соврёт, цикл не должен уйти в бесконечность
                {
                    Msg("! [DA_PORT] перепись(%s): очередь вернула %llu сообщений, обрезаю", stage,
                        (unsigned long long)count);
                    count = QUEUE_LIMIT;
                }
                const UINT64 dropped = iq->GetNumMessagesDiscardedByMessageCountLimit();
                Msg("* [DA_PORT] перепись(%s): строк %u, выброшено очередью %u", stage, (u32)count, (u32)dropped);
                FlushLog();

                for (UINT64 i = 0; i < count; ++i)
                {
                    SIZE_T len = 0;
                    if (FAILED(iq->GetMessage(i, nullptr, &len)) || !len)
                        continue;
                    auto* msg = static_cast<D3D11_MESSAGE*>(xr_malloc(len));
                    if (SUCCEEDED(iq->GetMessage(i, msg, &len)) && msg->pDescription)
                        Msg("~ [D3D11-LIVE] %s", msg->pDescription);
                    xr_free(msg);
                }
                iq->ClearStoredMessages();
                iq->Release();
                FlushLog();
            };

            // Сводка идёт первой: это несколько строк с итогами по типам, и она переживёт даже полный
            // потолок очереди. Поимённый список после неё — уже подробности.
            report_and_drain(D3D11_RLDO_SUMMARY, "сводка");

            // Только D3D11_RLDO_DETAIL: IGNORE_INTERNAL объявлен не во всех заголовках MinGW.
            report_and_drain(D3D11_RLDO_DETAIL, "поимённо");

            Msg("* [DA_PORT] перепись окончена");
            FlushLog();
            debug->Release();
        }
        else
            Msg("! [DA_PORT] ID3D11Debug недоступен: слой отладки DirectX не установлен");
    }

    // [DA_PORT] #70: захватываем остаточный refcount устройства. Release() возвращает число внешних
    // ссылок, ОСТАВШИХСЯ после нашей (утёкшие RT/SRV/текстуры + NGX/драйверный интероп). Если оно не 0,
    // устройство ещё живо в чужих руках, и DestroyD3D НЕ должен форсировать FreeLibrary(d3d11/dxgi) —
    // иначе код API-DLL выгружается из-под живых COM-объектов и teardown прыгает в снесённую память
    // (C0000005 по фикс. адресу вне модулей — 8/11 вылетов тестера при смене видео → выходе).
    if (pDevice)
    {
        m_device_teardown_refs = pDevice->Release();
        pDevice = nullptr;
    }
    DestroyD3D();
}

//////////////////////////////////////////////////////////////////////
// Resetting device
//////////////////////////////////////////////////////////////////////
void CHW::Reset()
{
    ZoneScoped;
    DXGI_SWAP_CHAIN_DESC& cd = m_ChainDesc;

    // [DA_PORT] Ни SetFullscreenState, ни ResizeTarget здесь больше нет.
    //
    // Первое отдано SDL целиком (см. CHW::OnAppActivate) — двоевластие над полноэкранным режимом и
    // было причиной окна на пол-экрана. Второе изменяет размер САМОГО ОКНА, а окном тоже
    // распоряжается SDL (SDL_SetWindowSize и смена режима монитора в UpdateWindowProps); два
    // источника размера спорили бы ровно так же, как два источника полноэкранности.
    //
    // Порядок вызовов при этом был ещё и обратным рекомендованному: сначала переход, потом
    // объявление режима. Ровно та же ошибка, что нашлась на стороне SDL, — и она тоже давала
    // полный экран в разрешении «от прошлого раза».
    //
    // Остаётся то, ради чего Reset и зовут: пересоздать буферы под текущий размер кадра.
    cd.Windowed = TRUE;
    DXGI_MODE_DESC& desc = cd.BufferDesc;
    desc.Width = Device.dwWidth;
    desc.Height = Device.dwHeight;

    // [DA_PORT] Снимок памяти по обе стороны пересоздания буферов.
    //
    // Перезапуск рендера идёт при каждой загрузке уровня, и именно на нём накапливается всё, что
    // забыли освободить. Одна пара чисел за сброс -- и утечка на перезапуске видна прямо в логе
    // игрока: занято до и занято после расходятся с каждым разом.
    da_vram_report("до пересоздания буферов");

    CHK_DX(m_pSwapChain->ResizeBuffers(
        cd.BufferCount, desc.Width, desc.Height, desc.Format, cd.Flags));

    da_vram_report("после пересоздания буферов");
}

void CHW::SetPrimaryAttributes(u32& /*windowFlags*/)
{

}

bool CHW::CheckFormatSupport(const DXGI_FORMAT format, const u32 feature) const
{
    u32 supports;

    if (SUCCEEDED(pDevice->CheckFormatSupport(format, &supports)))
    {
        if (supports & feature)
            return true;
    }

    return false;
}

DXGI_FORMAT CHW::SelectFormat(D3D_FORMAT_SUPPORT feature, const DXGI_FORMAT formats[], size_t count) const
{
    for (size_t i = 0; i < count; ++i)
        if (CheckFormatSupport(formats[i], feature))
            return formats[i];

    return DXGI_FORMAT_UNKNOWN;
}

bool CHW::UsingFlipPresentationModel() const
{
    return m_ChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL
#ifdef HAS_DXGI1_4
        || m_ChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD
#endif
    ;
}

std::pair<u32, u32> CHW::GetSurfaceSize() const
{
    return
    {
        m_ChainDesc.BufferDesc.Width,
        m_ChainDesc.BufferDesc.Height
    };
}

// [DA_PORT] Учёт видеопамяти. Разбор, зачем и почём, -- у объявления в dx11HW.h.
void CHW::da_vram_report(pcstr when)
{
#ifdef HAS_DXGI1_4
    if (!m_pAdapter3)
        return;

    // Две кучи, и обе нужны. Local -- собственная память карты, туда всё и кладётся. NonLocal --
    // системная память, отданная видеокарте: когда своя кончается, драйвер начинает вытеснять туда,
    // и кадр обваливается ЗАДОЛГО до какой-либо ошибки. Рост NonLocal при упёршемся Local -- это и
    // есть картина исчерпания, по одному Local её не отличить от нормы.
    DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
    const bool got_local =
        SUCCEEDED(m_pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local));
    const bool got_nonlocal =
        SUCCEEDED(m_pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal));
    if (!got_local)
        return;

    da_vram_budget = local.Budget;
    da_vram_usage = local.CurrentUsage;
    if (local.CurrentUsage > da_vram_peak)
        da_vram_peak = local.CurrentUsage;

    const auto mb = [](u64 bytes) { return u32(bytes / (1024 * 1024)); };
    const u32 percent = local.Budget ? u32((local.CurrentUsage * 100) / local.Budget) : 0;

    Msg("* [DA_VRAM] %s: занято %u МБ из %u МБ (%u%%), пик %u МБ, вытеснено в ОЗУ %u МБ", when,
        mb(local.CurrentUsage), mb(local.Budget), percent, mb(da_vram_peak),
        got_nonlocal ? mb(nonlocal.CurrentUsage) : 0u);
#else
    (void)when;
#endif
}

void CHW::da_vram_poll()
{
#ifdef HAS_DXGI1_4
    if (!m_pAdapter3)
        return;

    // Раз в секунду. Опрашивать каждый кадр незачем: столько памяти за кадр не появляется, а вызов
    // всё-таки идёт в драйвер.
    static u32 s_next = 0;
    if (Device.dwTimeGlobal < s_next)
        return;
    s_next = Device.dwTimeGlobal + 1000;

    DXGI_QUERY_VIDEO_MEMORY_INFO local{};
    if (FAILED(m_pAdapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)))
        return;

    da_vram_budget = local.Budget;
    da_vram_usage = local.CurrentUsage;
    if (local.CurrentUsage > da_vram_peak)
        da_vram_peak = local.CurrentUsage;

    if (!local.Budget)
        return;

    // В лог идёт не опрос, а ПЕРЕСЕЧЕНИЕ порога, и каждая ступень -- по одному разу. Иначе при
    // нехватке памяти лог заполнялся бы одинаковыми строками ровно тогда, когда игре и без того
    // тяжело, а именно эти секунды потом и разбирать.
    static u32 s_step_reported = 0;
    const u32 percent = u32((local.CurrentUsage * 100) / local.Budget);
    const u32 step = percent >= 100 ? 3 : percent >= 95 ? 2 : percent >= 85 ? 1 : 0;
    if (step > s_step_reported)
    {
        s_step_reported = step;
        da_vram_report(step >= 3 ? "ВИДЕОПАМЯТЬ ИСЧЕРПАНА" : "видеопамять на пределе");
    }
    else if (step == 0 && s_step_reported != 0)
    {
        s_step_reported = 0; // отпустило -- следующий подъём снова будет виден
    }
#endif
}

void CHW::BeginScene() { }
void CHW::EndScene() { }

void CHW::Present()
{
    const bool bUseVSync = psDeviceMode.WindowStyle == rsFullscreen &&
        psDeviceFlags.test(rsVSync); // xxx: weird tearing glitches when VSync turned on for windowed mode in DX11

    switch (m_pSwapChain->Present(bUseVSync ? 1 : 0, 0))
    {
    case DXGI_STATUS_OCCLUDED:
    case DXGI_ERROR_DEVICE_REMOVED:
        doPresentTest = true;
        break;
    }

    CurrentBackBuffer = (CurrentBackBuffer + 1) % BackBufferCount;

    da_vram_poll(); // [DA_PORT] раз в секунду, разбор -- у da_vram_poll

    TracyD3D11Collect(profiler_ctx);
}

DeviceState CHW::GetDeviceState()
{
    if (doPresentTest)
    {
        switch (m_pSwapChain->Present(0, DXGI_PRESENT_TEST))
        {
        case S_OK:
            doPresentTest = false;
            break;

        case DXGI_STATUS_OCCLUDED:
            // Do not render until we become visible again
            return DeviceState::Lost;

        case DXGI_ERROR_DEVICE_RESET:
            return DeviceState::NeedReset;

        case DXGI_ERROR_DEVICE_REMOVED:
            {
                // [DA_PORT] Спрашиваем ПРИЧИНУ, а не гадаем.
                //
                // Прежний текст называл две: обновился драйвер или вынули карту. На деле
                // DXGI_ERROR_DEVICE_REMOVED приходит ещё и от зависания видеокарты (сторожевой
                // таймер Windows перезапустил драйвер), от внутренней ошибки драйвера и от
                // неверного вызова с нашей стороны. Это разные беды: одна к игре отношения не
                // имеет, другая означает, что подвесили её мы. Отличает их только
                // GetDeviceRemovedReason, и он не спрашивался ни разу.
                //
                // Причина уходит и в лог, и в окно: у игрока часто есть только снимок экрана.
                // [DA_PORT] Докладываем ОДИН РАЗ за запуск.
                //
                // Замер по логу игрока: отчёт повторился 16 раз подряд, по разу на кадр. Причина в
                // том, что после фатала движок продолжает крутить кадры, `doPresentTest` остаётся
                // взведённым, и следующая проба возвращает ту же потерю. Толку от повторов нет —
                // числа в них одинаковые, — а окно всплывает снова и снова, и разобрать лог мешает
                // именно этот повтор.
                //
                // Дальше отвечаем «устройство потеряно»: движок перестаёт рисовать, окно остаётся
                // живым, и игру можно закрыть по-человечески.
                static bool da_reported = false;
                if (da_reported)
                    return DeviceState::Lost;
                da_reported = true;

                const HRESULT da_reason = pDevice ? pDevice->GetDeviceRemovedReason() : S_OK;
                pcstr da_name = "драйвер причины не назвал";
                switch (da_reason)
                {
                case DXGI_ERROR_DEVICE_HUNG:
                    da_name = "DEVICE_HUNG: видеокарта зависла на наших командах";
                    break;
                case DXGI_ERROR_DEVICE_REMOVED:
                    da_name = "DEVICE_REMOVED: драйвер обновлён или карта отключена";
                    break;
                case DXGI_ERROR_DEVICE_RESET:
                    da_name = "DEVICE_RESET: сброс из-за неверной команды";
                    break;
                case DXGI_ERROR_DRIVER_INTERNAL_ERROR:
                    da_name = "DRIVER_INTERNAL_ERROR: внутренняя ошибка драйвера";
                    break;
                case DXGI_ERROR_INVALID_CALL:
                    da_name = "INVALID_CALL: неверный вызов с нашей стороны";
                    break;
                default:
                    break;
                }

                Msg("! [DA_PORT] устройство отрисовки потеряно, причина драйвера 0x%08x - %s",
                    (u32)da_reason, da_name);

                // [DA_PORT] Вторая строка -- «хлебная крошка»: до какой фазы кадра мы дошли.
                //
                // Одной причины мало: DEVICE_HUNG говорит, что видеокарта встала на наших командах,
                // но не на каких. Метку пишет da_gpu_timer::zone_begin бесплатно и всегда (разбор
                // цены -- там же). Отставание номера кадра от текущего показывает, сколько мы уже
                // ждали: если оно велико, процессор давно упёрся в видеокарту.
                const u32 da_lag = Device.dwFrame - g_da_stage_frame;
                Msg("! [DA_PORT] последняя фаза кадра: %s | кадр %u (текущий %u, отставание %u) | фаз "
                    "отправлено %u",
                    g_da_stage, g_da_stage_frame, Device.dwFrame, da_lag, g_da_stage_seq);

                // [DA_PORT] Видеопамять. Спрашивать адаптер сейчас уже поздно -- устройства нет,
                // -- поэтому печатаются последние снятые числа: их обновлял опрос раз в секунду.
                // Ими исчерпание памяти либо подтверждается, либо снимается с подозрения сразу.
                {
                    const auto da_mb = [](u64 bytes) { return u32(bytes / (1024 * 1024)); };
                    const u32 da_pc =
                        da_vram_budget ? u32((da_vram_usage * 100) / da_vram_budget) : 0;
                    Msg("! [DA_PORT] видеопамять за секунду до потери: занято %u МБ из %u МБ (%u%%), "
                        "пик за сеанс %u МБ",
                        da_mb(da_vram_usage), da_mb(da_vram_budget), da_pc, da_mb(da_vram_peak));
                }
                FlushLog();

                FATAL_F("Устройство отрисовки потеряно.\n\nПричина: %s\n(код 0x%08x)\nПоследняя фаза: %s\n\n"
                        "Перезапустите игру. Если повторяется - пришлите лог.",
                    da_name, (u32)da_reason, g_da_stage);
            }
            break;
        }
    }

    return DeviceState::Normal;
}

// [DA_PORT] Pull whatever the validation layer has to say into the engine log, once per frame.
//
// Without this the layer is enabled and still tells us nothing: its messages go to the debugger, and
// there is no debugger attached when the game is started normally.
//
// Capped per run, not per frame: a genuine state fault repeats every frame and would otherwise fill
// the log with thousands of identical lines and slow the game to a crawl. The first occurrences are
// the informative ones.
void da_d3d_debug_drain()
{
    if (!::ps_r__d3d_debug || !HW.pDevice)
        return;

    static u32 s_reported = 0;
    static const u32 s_limit = 200;

    ID3D11InfoQueue* iq = nullptr;
    if (FAILED(HW.pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&iq))) || !iq)
        return;

    // [DA_PORT] Достигнув потолка печати, функция раньше уходила по return ДО очистки очереди —
    // и слой проверки продолжал копить сообщения, которые уже никто не заберёт. Потолок обязан
    // глушить ПЕЧАТЬ, а не слив: очередь сливаем всегда, пока слой включён.
    if (s_reported > s_limit)
    {
        iq->ClearStoredMessages();
        _RELEASE(iq);
        return;
    }

    const UINT64 count = iq->GetNumStoredMessages();
    for (UINT64 i = 0; i < count && s_reported < s_limit; ++i)
    {
        SIZE_T len = 0;
        if (FAILED(iq->GetMessage(i, nullptr, &len)) || !len)
            continue;
        auto* msg = static_cast<D3D11_MESSAGE*>(xr_malloc(len));
        if (SUCCEEDED(iq->GetMessage(i, msg, &len)) && msg->pDescription)
        {
            Msg("~ [D3D11] %s", msg->pDescription);
            ++s_reported;
        }
        xr_free(msg);
    }
    iq->ClearStoredMessages();
    iq->Release();

    if (s_reported >= s_limit)
    {
        Msg("~ [D3D11] message limit reached, further validation output suppressed");
        ++s_reported;
    }
}

} // namespace xray::render::RENDER_NAMESPACE
