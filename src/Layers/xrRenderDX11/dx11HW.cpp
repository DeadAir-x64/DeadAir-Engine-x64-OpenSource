#include "stdafx.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp).
extern ENGINE_API int ps_r__d3d_debug;

#include "dx11HW.h"

#include "StateManager/dx11SamplerStateCache.h"
#include "dx11TextureUtils.h"

#include <SDL_syswm.h>

namespace xray::render::RENDER_NAMESPACE
{
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

    Valid = m_pAdapter;
}

void CHW::DestroyD3D()
{
    _SHOW_REF("refCount:m_pAdapter", m_pAdapter);
    _RELEASE(m_pAdapter);

    _SHOW_REF("refCount:m_pFactory", m_pFactory);
    _RELEASE(m_pFactory);

    // Manually close and unload additional DLLs
    // To make it work with DXVK, etc.
    hD3D->Close();
    hDXGI->Close();
    if (auto hModule = GetModuleHandleA("d3d11.dll"))
        FreeLibrary(hModule);
    if (auto hModule = GetModuleHandleA("dxgi.dll"))
        FreeLibrary(hModule);
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
    if (::ps_r__d3d_debug)
    {
        createDeviceFlags |= D3D_CREATE_DEVICE_DEBUG;
        Msg("* [DA_PORT] D3D11 validation layer requested (r__d3d_debug)");
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

    // [DA_PORT] Поимённый список живых объектов D3D при выходе.
    //
    // Строка выше печатает ЧИСЛО неосвобождённых ссылок, и оно оказалось говорящим: 198 после одной
    // загрузки уровня и 5806 после семи, то есть около девятисот объектов теряется на каждый переход.
    // Держат они память драйвера, а не наши кучи, поэтому ни обход куч, ни HeapCompact их не видят —
    // мы честно мерили «живые аллокации» и не находили ничего, пока закоммиченная память росла на
    // 800 МБ за переход.
    //
    // Само число не говорит, ЧТО именно течёт, а перебирать подозреваемых по коду дорого. DirectX
    // умеет перечислить живые объекты сам, с типами и счётчиками ссылок, — это и есть кратчайший путь
    // к имени. Требует слоя отладки (r__d3d_debug 1) и потому ничего не стоит в обычной игре.
    if (ps_r__d3d_debug)
    {
        ID3D11Debug* debug = nullptr;
        if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11Debug), (void**)&debug)) && debug)
        {
            Msg("* [DA_PORT] живые объекты D3D на выходе:");

            // [DA_PORT] Снять потолок очереди ДО переписи. По умолчанию слой отладки хранит 1024
            // сообщения, и первая перепись пришла ровно на 1024 строки — то есть была обрезана, а
            // счётчик ссылок при этом показывал пятнадцать тысяч. Обрезанная перепись хуже, чем
            // никакой: по ней легко посчитать доли типов и принять верхушку за целое.
            {
                ID3D11InfoQueue* iq_limit = nullptr;
                if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&iq_limit)) && iq_limit)
                {
                    iq_limit->ClearStoredMessages();
                    iq_limit->SetMessageCountLimit(0); // 0 = без ограничения
                    iq_limit->Release();
                }
            }

            // Только D3D11_RLDO_DETAIL: IGNORE_INTERNAL объявлен не во всех заголовках MinGW.
            debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
            debug->Release();

            // [DA_PORT] Перепись уходит в очередь сообщений слоя отладки, а не в наш лог. Сливаем её
            // здесь же, БЕЗ обычного потолка в 200 строк: тот поставлен против ежекадрового спама, а
            // это единственная за сеанс перепись, и обрезать её нельзя — интересен как раз хвост.
            {
                ID3D11InfoQueue* iq = nullptr;
                if (SUCCEEDED(pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), (void**)&iq)) && iq)
                {
                    const UINT64 count = iq->GetNumStoredMessages();
                    Msg("* [DA_PORT] строк переписи: %u", (u32)count);
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
                }
                else
                    Msg("! [DA_PORT] очередь сообщений недоступна, перепись ушла в отладочный вывод");
            }
        }
        else
            Msg("! [DA_PORT] ID3D11Debug недоступен: слой отладки DirectX не установлен");
    }

    _RELEASE(pDevice);
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

    CHK_DX(m_pSwapChain->ResizeBuffers(
        cd.BufferCount, desc.Width, desc.Height, desc.Format, cd.Flags));
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
            FATAL("Graphics driver was updated or GPU was physically removed from computer.\n"
                  "Please, restart the game.");
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
    if (s_reported > s_limit)
        return;

    ID3D11InfoQueue* iq = nullptr;
    if (FAILED(HW.pDevice->QueryInterface(__uuidof(ID3D11InfoQueue), reinterpret_cast<void**>(&iq))) || !iq)
        return;

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
