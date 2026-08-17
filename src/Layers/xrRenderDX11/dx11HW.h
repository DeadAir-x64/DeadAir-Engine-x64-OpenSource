#pragma once

#include "xrCore/ModuleLookup.hpp"

#include "Layers/xrRender/HWCaps.h"
#include "Layers/xrRender/stats_manager.h"

#include <SDL.h>

namespace xray::render::RENDER_NAMESPACE
{
class CHW
    : public pureAppActivate,
      public pureAppDeactivate
{
public:
    CHW();
    ~CHW();

    void CreateD3D();
    void DestroyD3D();

    void CreateDevice(SDL_Window* sdlWnd);
    void DestroyDevice();

    void Reset();

    void SetPrimaryAttributes(u32& windowFlags);

    std::pair<u32, u32> GetSurfaceSize() const;

    bool CheckFormatSupport(DXGI_FORMAT format, u32 feature) const;
    DXGI_FORMAT SelectFormat(D3D_FORMAT_SUPPORT feature, const DXGI_FORMAT formats[], size_t count) const;
    template <size_t count>
    inline DXGI_FORMAT SelectFormat(D3D_FORMAT_SUPPORT feature, const DXGI_FORMAT (&formats)[count]) const
    {
        return SelectFormat(feature, formats, count);
    }
    bool UsingFlipPresentationModel() const;
    DeviceState GetDeviceState();

    // [DA_PORT] Учёт видеопамяти через бюджет DXGI.
    //
    // Зачем. Потеря устройства у игрока может быть исчерпанием видеопамяти, а может быть чем угодно
    // ещё, и по логу эти случаи неотличимы: движок не знает о видеопамяти НИЧЕГО, кроме объёма
    // адаптера, объявленного при запуске. Между тем именно этот случай самый вероятный на карте с
    // 8 ГБ при сверхширокой матрице и апскейлере, и именно он подтверждается или опровергается одним
    // числом.
    //
    // Почему бюджет, а не «сколько мы выделили». Windows раздаёт видеопамять между всеми
    // приложениями, поэтому предел у игры не равен объёму карты: он зависит от того, что ещё
    // запущено. DXGI сообщает и текущий предел (Budget), и наш расход (CurrentUsage) -- сравнивать
    // надо именно их. У NVIDIA и AMD это и есть штатный способ следить за памятью.
    //
    // Цена. Один вызов раз в секунду. Замерять чаще нечего: память так быстро не меняется, а в лог
    // идёт не каждый опрос, а только пересечение порога -- иначе лог зарастёт.
    void da_vram_poll();
    void da_vram_report(pcstr when);

public:
    void BeginScene();
    void EndScene();
    void Present();

public:
    void OnAppActivate() override;
    void OnAppDeactivate() override;

private:
    bool CreateSwapChain(HWND hwnd);
    bool CreateSwapChain2(HWND hwnd);

    bool ThisInstanceIsGlobal() const;

public:
    ICF ID3DDeviceContext* get_context(u32 context_id)
    {
        VERIFY(context_id < R__NUM_CONTEXTS);
        return d3d_contexts_pool[context_id];
    }

public:
    static constexpr auto IMM_CTX_ID = R__NUM_PARALLEL_CONTEXTS;

    CHWCaps Caps;

    u32 BackBufferCount{};
    u32 CurrentBackBuffer{};

    ID3DDevice* pDevice = nullptr; // render device
    u32 m_device_teardown_refs = 0; // [DA_PORT] #70: остаточный refcount устройства при сносе (см. DestroyD3D)

    D3D_DRIVER_TYPE m_DriverType;

    IDXGIFactory1* m_pFactory = nullptr;
    IDXGIAdapter1* m_pAdapter = nullptr; // pD3D equivalent
#ifdef HAS_DXGI1_4
    // [DA_PORT] Тот же адаптер, только через интерфейс с бюджетом памяти. Разбор -- у da_vram_poll.
    IDXGIAdapter3* m_pAdapter3 = nullptr;
#endif
    // [DA_PORT] Последние снятые числа: их печатает обработчик потери устройства, когда спрашивать
    // адаптер уже поздно.
    u64 da_vram_budget{};
    u64 da_vram_usage{};
    u64 da_vram_peak{};
    IDXGISwapChain* m_pSwapChain = nullptr;
    D3D_FEATURE_LEVEL FeatureLevel;
    bool Valid = true;
    bool ComputeShadersSupported;
    bool DoublePrecisionFloatShaderOps;
    bool SAD4ShaderInstructions;
    bool ExtendedDoublesShaderInstructions;

    ID3DDeviceContext* d3d_contexts_pool[R__NUM_CONTEXTS]{};

    bool DX10Only = false;
#ifdef HAS_DX11_2
    IDXGISwapChain2* m_pSwapChain2 = nullptr;
#endif
#ifdef HAS_DX11_3
    ID3D11Device3* pDevice3 = nullptr;
#endif
    ID3D11DeviceContext1* pContext1 = nullptr;

    using D3DCompileFunc = decltype(&D3DCompile);
    D3DCompileFunc D3DCompile = nullptr;

#if !defined(_MAYA_EXPORT)
    stats_manager stats_manager;
#endif
    TracyD3D11Ctx profiler_ctx{}; // TODO: this should be one per d3d11 context
private:
    DXGI_SWAP_CHAIN_DESC m_ChainDesc; // DevPP equivalent
    bool doPresentTest{};
    XRay::Module hD3DCompiler;
    XRay::Module hDXGI;
    XRay::Module hD3D;
};

extern ECORE_API CHW HW;
} // namespace xray::render::RENDER_NAMESPACE
