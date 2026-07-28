#include "stdafx.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API float ps_r__reactive_object;
extern ENGINE_API int ps_r__reactive_dilate;
extern ENGINE_API float ps_r__reactive_deadzone;
extern ENGINE_API int ps_r__reactive_debug;
extern ENGINE_API int ps_r__reactive_ref_fps;
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API int ps_r__xess;
extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]

extern ENGINE_API int ps_r__reactive_selftest;

namespace xray::render::RENDER_NAMESPACE
{
namespace
{
// [DA_PORT] ---- Self-test: read the buffers back and put numbers in the log --------------------
//
// A render pass that produces nothing looks exactly like a render pass whose inputs are all empty,
// and from outside the two are the same black screen. Every attempt to tell them apart by eye costs
// a round trip through someone launching the game, walking to the right spot and judging a picture -
// and judgement is the part that cannot be checked afterwards. Numbers can.
//
// One-shot, triggered by r__reactive_selftest 1: it stalls the pipeline to map the targets, which is
// fine for a single frame and would not be for any other purpose.

float da_half(u16 h)
{
    const u32 sign = (h >> 15) & 1u;
    const u32 exp = (h >> 10) & 0x1fu;
    const u32 man = h & 0x3ffu;

    float v;
    if (exp == 0)
        v = float(man) * (1.f / 1024.f) * 6.103515625e-05f; // subnormal: 2^-14 * man/1024
    else if (exp == 31)
        v = man ? 0.f : flt_max; // NaN reported as zero, infinity as a huge number
    else
        v = (1.f + float(man) * (1.f / 1024.f)) * powf(2.f, float(int(exp) - 15));

    return sign ? -v : v;
}

// comp_offset/comp_count select which channels form the magnitude - the xy of a motion vector, the z
// of an eye-space position, the single channel of the mask.
void da_probe(pcstr name, const ref_rt& rt, u32 comps_in_pixel, u32 comp_offset, u32 comp_count)
{
    if (!rt || !rt->pTexture)
    {
        Msg("~ [DA_PROBE] %-18s : target does not exist", name);
        return;
    }

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    if (!res)
    {
        Msg("~ [DA_PROBE] %-18s : no surface", name);
        return;
    }

    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
    {
        Msg("~ [DA_PROBE] %-18s : not a 2D texture", name);
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = HW.pDevice->CreateTexture2D(&sd, nullptr, &staging);
    if (FAILED(hr) || !staging)
    {
        Msg("~ [DA_PROBE] %-18s : staging copy refused (0x%08x)", name, hr);
        _RELEASE(tex);
        return;
    }

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
    ctx->CopyResource(staging, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr))
    {
        Msg("~ [DA_PROBE] %-18s : map refused (0x%08x)", name, hr);
        _RELEASE(staging);
        _RELEASE(tex);
        return;
    }

    // Every 4th pixel in both directions - sixteen times less work, and no statistic here needs more.
    const u32 step = 4;
    u32 samples = 0, nonzero = 0;
    float maxv = 0.f, sum = 0.f;

    for (u32 y = 0; y < desc.Height; y += step)
    {
        const u16* row = (const u16*)((const u8*)map.pData + size_t(y) * map.RowPitch);
        for (u32 x = 0; x < desc.Width; x += step)
        {
            float sq = 0.f;
            for (u32 c = 0; c < comp_count; ++c)
            {
                const float v = da_half(row[size_t(x) * comps_in_pixel + comp_offset + c]);
                sq += v * v;
            }
            const float m = _sqrt(sq);
            ++samples;
            if (m > 1e-5f)
                ++nonzero;
            if (m > maxv)
                maxv = m;
            sum += m;
        }
    }

    ctx->Unmap(staging, 0);
    _RELEASE(staging);
    _RELEASE(tex);

    Msg("~ [DA_PROBE] %-18s : %ux%u  nonzero %5.1f%%  max %.5f  mean %.6f", name, desc.Width,
        desc.Height, samples ? 100.f * float(nonzero) / float(samples) : 0.f, maxv,
        samples ? sum / float(samples) : 0.f);
}
} // namespace

// Widens the reactive mask around things that are actually moving through the world, so that an
// upscaler stops trusting its history in the band a moving figure has just uncovered. That band is
// where ghosting lives: the figure itself reprojects correctly, but the ground behind it is being
// blended with a history that still holds the figure. The whole derivation is in da_reactive.ps.
//
// Reads the mask the G-buffer left, widens it, writes the result to the scratch target and copies it
// back - a draw cannot read and write the same target. Skipped entirely at zero scale.
void CRenderTarget::phase_reactive()
{
    if (ps_r__reactive_object <= 0.f || !s_reactive || !s_reactive_dilate_h || !s_reactive_dilate_v ||
        !rt_Reactive || !rt_Reactive_scratch || !rt_Reactive_scratch2)
        return;

    // Nothing but an upscaler ever reads this mask, so with all of them off the pass is pure cost.
    // [DA_PORT] Через общий список, а не перечислением: этот if уже дважды забывали обновить при
    // добавлении бэкенда, и оба раза маска молча переставала строиться.
    if (!da_upscaler_active())
        return;

    PIX_EVENT(DA_phase_reactive);

    // [DA_PORT] Everything this pass works in is travel PER FRAME, so every setting it takes is frame
    // rate dependent - a threshold that separates a walking figure from swaying grass at 130fps admits
    // the grass at 60, and a band wide enough for the trail at 130 covers half of it at 60. Values
    // tuned on one machine would be wrong on every other, which is no use in something meant to ship.
    //
    // Converted here rather than in the shader, because it costs nothing on the CPU and keeps the
    // arithmetic in one readable place: slower frames mean a proportionally larger threshold, a
    // proportionally smaller scale, and a proportionally wider band.
    const float dt = _max(Device.fTimeDelta, 0.0005f);
    const float dt_ref = 1.f / _max(float(ps_r__reactive_ref_fps), 1.f);
    const float k = dt_ref / dt; // below 1 when frames are slower than the reference

    const float deadzone = ps_r__reactive_deadzone / k;
    const float scale = ps_r__reactive_object * k;

    // Widening runs one axis at a time now, so the radius costs 2r+1 reads rather than its square and
    // a wide band is affordable - which it has to be, because at sixty frames a second the trail is
    // over twenty pixels across. The ceiling is only there so a low frame rate cannot turn one pass
    // into a thousand reads per pixel and make the frame rate worse still.
    int radius = iFloor(float(ps_r__reactive_dilate) / k + 0.5f);
    if (radius < 1)
        radius = 1;
    if (radius > 40)
        radius = 40;

    // [DA_PORT] Inputs measured before the draw touches anything, output after - see da_probe.
    const bool selftest = !!ps_r__reactive_selftest;
    if (selftest)
    {
        Msg("~ [DA_PROBE] ---- reactive pass, inputs ----");
        Msg("~ [DA_PROBE] set: scale %.1f  dilate %d  deadzone %.5f  ref %d fps",
            ps_r__reactive_object, ps_r__reactive_dilate, ps_r__reactive_deadzone,
            ps_r__reactive_ref_fps);
        Msg("~ [DA_PROBE] now: %.1f fps (dt %.5f)  ->  scale %.1f  dilate %d  deadzone %.5f",
            1.f / dt, dt, scale, radius, deadzone);
        da_probe("rt_Reactive in", rt_Reactive, 1, 0, 1);   // the mask the G-buffer left
        da_probe("rt_Velocity", rt_Velocity, 2, 0, 2);      // motion vectors, xy
        da_probe("rt_Position z", rt_Position, 4, 2, 1);    // eye-space depth
    }

    // One full-screen quad, drawn with whichever shader and into whichever target the caller names.
    const auto quad = [&](const ref_rt& target, const ref_shader& shader)
    {
        u_setrt(RCache, target, nullptr, nullptr, nullptr);

        RCache.set_Stencil(FALSE);
        RCache.set_Z(FALSE);
        RCache.set_CullMode(CULL_NONE);

        u32 Offset = 0;
        FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
        RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(shader->E[0]);
        RCache.set_Geometry(g_combine);
        return Offset;
    };

    // Motion through the world, one evaluation per pixel, into scratch2.
    const auto draw = [&](float debug_mode)
    {
        const u32 offset = quad(rt_Reactive_scratch2, s_reactive);
        RCache.set_c("da_reactive", scale, float(radius), deadzone, debug_mode);
        RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
    };

    // The widening, one axis per call. Across into scratch, then down into scratch2, where the mask
    // the G-buffer left joins undilated - the order matters only in that neither target is ever read
    // and written by the same draw.
    const auto dilate = [&](bool vertical)
    {
        const u32 offset = quad(vertical ? rt_Reactive_scratch2 : rt_Reactive_scratch,
            vertical ? s_reactive_dilate_v : s_reactive_dilate_h);
        RCache.set_c("da_dilate", vertical ? 0.f : 1.f, vertical ? 1.f : 0.f, float(radius),
            vertical ? 1.f : 0.f);
        RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
    };

    // [DA_PORT] Fill the target with a value by hand first, then draw over it. The two readings that
    // follow separate the last two possibilities outright: if the marker survives the draw, the draw
    // reaches nothing; if it is gone, the draw lands and the shader is at fault. Every earlier probe
    // measured the two together and could not tell them apart.
    if (selftest && rt_Reactive_scratch2->pRT)
    {
        const float mark[4] = { 0.25f, 0.f, 0.f, 0.f };
        HW.get_context(CHW::IMM_CTX_ID)->ClearRenderTargetView(rt_Reactive_scratch2->pRT, mark);
        da_probe("marker 0.25", rt_Reactive_scratch2, 1, 0, 1);
    }

    draw(float(ps_r__reactive_debug));

    if (selftest)
    {
        Msg("~ [DA_PROBE] ---- reactive pass, output ----");
        da_probe("motion, undilated", rt_Reactive_scratch2, 1, 0, 1);

        // [DA_PORT] The same draw again per debug mode, each writing one ingredient AS THE SHADER SEES
        // IT rather than as the buffer holds it. That distinction is the whole reason for doing this:
        // the probes above prove the targets have content, not that this pass can read them, and a
        // constant or a texture that fails to arrive looks identical to arithmetic that returns zero.
        static pcstr what[] = { "base (mask in)", "velocity x200", "eye depth x0.02", "world motion x250" };
        for (int m = 1; m <= 4; ++m)
        {
            draw(float(m));
            da_probe(what[m - 1], rt_Reactive_scratch2, 1, 0, 1);
        }

        // Leave the buffer holding the real thing, not the last diagnostic.
        draw(float(ps_r__reactive_debug));
    }

    dilate(false); // across
    dilate(true);  // and down, folding in the G-buffer's own mask

    if (selftest)
    {
        da_probe("final mask", rt_Reactive_scratch2, 1, 0, 1);
        Msg("~ [DA_PROBE] ---- done ----");
        ps_r__reactive_selftest = 0; // one shot: the readback stalls the pipeline
    }

    // Back over the original, so the upscalers keep reading rt_Reactive and need no knowledge of
    // this pass at all - exactly as with the velocity guard.
    ID3DBaseTexture* src = rt_Reactive_scratch2->pTexture->surface_get();
    ID3DBaseTexture* dst = rt_Reactive->pTexture->surface_get();
    if (src && dst)
        HW.get_context(CHW::IMM_CTX_ID)->CopyResource(dst, src);
    _RELEASE(src);
    _RELEASE(dst);
}
} // namespace xray::render::RENDER_NAMESPACE
