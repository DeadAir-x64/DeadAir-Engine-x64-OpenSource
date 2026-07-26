#pragma once

// [DA_PORT] AMD FidelityFX Super Resolution 2.
//
// A temporal upscaler: the scene is rendered at a lower resolution and reconstructed to the output
// size using the history of previous frames. Unlike the spatial upscale we already have (FSR 1.0,
// r__render_scale plus a bicubic fetch), this one recovers genuine detail rather than interpolating
// what is there — because it knows where every pixel came from, via the motion vector buffer.
//
// Everything it needs the port now produces: colour, depth, motion vectors (rt_Velocity), and the
// sub-pixel jitter, which we already apply for temporal AA.

#include <ffx-fsr2-api/ffx_fsr2.h>
#include <ffx-fsr2-api/dx11/ffx_fsr2_dx11.h>

namespace xray::render::RENDER_NAMESPACE
{
class da_fsr2
{
public:
    struct init_params
    {
        u32 render_width{};   // size the scene is rendered at
        u32 render_height{};
        u32 display_width{};  // size it must end up at
        u32 display_height{};
        ID3D11Device* device{};
    };

    struct draw_params
    {
        ID3D11DeviceContext* context{};

        ID3D11Resource* colour{};   // scene at render resolution
        ID3D11Resource* depth{};
        ID3D11Resource* velocity{};
        ID3D11Resource* output{};   // upscaled result, at display resolution
        ID3D11Resource* reactive{}; // [DA_PORT] 1 where the history must not be trusted

        u32 render_width{};
        u32 render_height{};

        float jitter_x{};           // the same offset the projection was jittered by
        float jitter_y{};
        float frame_time_ms{};
        float near_plane{};
        float far_plane{};
        float fov_vertical{};       // radians

        bool reset{};               // discard history: teleport, level change, camera cut
        bool sharpening{};
        float sharpness{};
    };

    ~da_fsr2() { destroy(); }

    bool create(const init_params& p);
    void destroy();
    bool draw(const draw_params& p);

    bool ready() const { return m_created; }

    // Render resolution FSR 2 expects for a given quality mode, so the scene can be sized to match.
    static void render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h);

private:
    FfxFsr2Context m_context{};
    void* m_scratch{};
    bool m_created{};
};

extern da_fsr2 g_da_fsr2;
} // namespace xray::render::RENDER_NAMESPACE
