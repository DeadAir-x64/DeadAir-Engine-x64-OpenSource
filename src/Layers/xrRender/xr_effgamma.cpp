#include "stdafx.h"
#include "xr_effgamma.h"

namespace xray::render::RENDER_NAMESPACE
{
bool g_da_gamma_ramp_active = false;

IC u16 clr2gamma(float c)
{
    int C = iFloor(c);
    clamp(C, 0, 65535);
    return u16(C);
}

void CGammaControl::GenLUT(u16* r, u16* g, u16* b, u16 count) const
{
    const float og = 1.f / (fGamma + EPS);
    const float B = fBrightness / 2.f;
    const float C = fContrast / 2.f;
    for (u16 i = 0; i < count; i++)
    {
        //	const	float	c	= 65535.f*(powf(float(i)/255, og) + fBrightness);
        const float c = (C + .5f) * powf(float(i) / 255.f, og) * 65535.f + (B - 0.5f) * 32768.f - C * 32768.f + 16384.f;
        r[i] = clr2gamma(c * cBalance.r);
        g[i] = clr2gamma(c * cBalance.g);
        b[i] = clr2gamma(c * cBalance.b);
    }
}

#if defined(USE_DX11)
void CGammaControl::GenLUT(const DXGI_GAMMA_CONTROL_CAPABILITIES& GC, DXGI_GAMMA_CONTROL& G) const
{
    constexpr DXGI_RGB Offset = { 0, 0, 0 };
    constexpr DXGI_RGB Scale  = { 1, 1, 1 };
    G.Offset = Offset;
    G.Scale = Scale;

    const float DeltaCV = (GC.MaxConvertedValue - GC.MinConvertedValue);

    const float og = 1.f / (fGamma + EPS);
    const float B = fBrightness / 2.f;
    const float C = fContrast / 2.f;

    for (u32 i = 0; i < GC.NumGammaControlPoints; i++)
    {
        float c = (C + .5f) * powf(GC.ControlPointPositions[i], og) + (B - 0.5f) * 0.5f - C * 0.5f + 0.25f;

        c = GC.MinConvertedValue + c * DeltaCV;

        G.GammaCurve[i].Red = c * cBalance.r;
        G.GammaCurve[i].Green = c * cBalance.g;
        G.GammaCurve[i].Blue = c * cBalance.b;

        clamp(G.GammaCurve[i].Red, GC.MinConvertedValue, GC.MaxConvertedValue);
        clamp(G.GammaCurve[i].Green, GC.MinConvertedValue, GC.MaxConvertedValue);
        clamp(G.GammaCurve[i].Blue, GC.MinConvertedValue, GC.MaxConvertedValue);
    }
}
#endif

void CGammaControl::Update() const
{
#if defined(USE_DX11)
    if (HW.pDevice)
    {
        DXGI_GAMMA_CONTROL_CAPABILITIES GC;
        DXGI_GAMMA_CONTROL G;
        IDXGIOutput* pOutput{};

        HRESULT hr = HW.m_pSwapChain->GetContainingOutput(&pOutput);
        // Метод выполнится успешно только в полноэкранном режиме.
        if (SUCCEEDED(hr))
        {
            hr = pOutput->GetGammaControlCapabilities(&GC);
            if (SUCCEEDED(hr))
            {
                GenLUT(GC, G);
                hr = pOutput->SetGammaControl(&G);
            }
        }

        _RELEASE(pOutput);
        if (SUCCEEDED(hr))
        {
            g_da_gamma_ramp_active = true;
            return;
        }
    }
#endif
    u16 red[256], green[256], blue[256];
    GenLUT(red, green, blue, 256);
    SDL_SetWindowGammaRamp(Device.m_sdlWnd, red, green, blue);

    // [DA_PORT] On Windows this call is a no-op that still reports success: the OS has ignored
    // per-window gamma ramps since Windows 10 (the SDL2 function is deprecated and gone in SDL3).
    // Dead Air originally ran on DX9 in exclusive fullscreen, where the ramp did work, so the mod's
    // own default of rs_c_brightness 1.2 was always being applied; here it silently did nothing and
    // the picture came out ~20% darker than the author intended. Report the ramp as inactive so the
    // post-process pass applies gamma/brightness/contrast in the shader instead.
#if defined(XR_PLATFORM_WINDOWS)
    g_da_gamma_ramp_active = false;
#else
    g_da_gamma_ramp_active = true;
#endif
}
} // namespace xray::render::RENDER_NAMESPACE
