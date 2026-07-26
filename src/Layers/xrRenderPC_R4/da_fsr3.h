#pragma once

// [DA_PORT] AMD FidelityFX Super Resolution 3 — upscaler only, no frame generation.
//
// Why a second temporal upscaler when FSR 2 already works: FSR 3 runs two passes FSR 2 does not have,
// `shading_change` and `luma_instability`. They exist for the case where a surface's shading changes
// while its motion vectors say it has not moved — which is exactly the artefact that ruins metal in
// this mod, where swaying vegetation drags its own motion onto the static surfaces behind it through
// FSR 2's velocity dilation. Whether those passes actually help here can only be judged in game; the
// two upscalers live side by side so the comparison is one console command.
//
// AMD ships no DirectX 11 backend for FSR 3 — this is built from a community port, see
// Externals/ffx-fsr3/README.md for how the library is produced and what has to be patched.
//
// One structural difference from FSR 2 worth knowing: FSR 3 hands three intermediate buffers back and
// forth with whatever comes after it (frame interpolation, which we do not use), and the application
// has to own them. They are created here rather than in the render-target system, because nothing else
// in the engine has any use for them and they need UAV binding the RT system does not offer.

#include <FidelityFX/host/ffx_fsr3upscaler.h>
#include <FidelityFX/host/backends/dx11/ffx_dx11.h>

namespace xray::render::RENDER_NAMESPACE
{
class da_fsr3
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
        ID3D11Resource* reactive{}; // 1 where the history must not be trusted
        ID3D11Resource* tandc{};    // transparency and composition, null to disable

        u32 render_width{};
        u32 render_height{};
        u32 display_width{};
        u32 display_height{};

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

    ~da_fsr3() { destroy(); }

    bool create(const init_params& p);
    void destroy();
    bool draw(const draw_params& p);

    bool ready() const { return m_created; }

    // Render resolution FSR 3 expects for a given quality mode, so the scene can be sized to match.
    // Deliberately the same five steps the menu offers for every upscaler.
    static void render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h);

private:
    bool create_shared(u32 width, u32 height);
    void destroy_shared();

    FfxFsr3UpscalerContext m_context{};
    FfxInterface m_backend{};
    void* m_scratch{};
    bool m_created{};

    // The three buffers FSR 3 expects the application to own, see the note at the top.
    ID3D11Texture2D* m_dilated_depth{};
    ID3D11Texture2D* m_dilated_motion{};
    ID3D11Texture2D* m_prev_depth{};
};

extern da_fsr3 g_da_fsr3;
} // namespace xray::render::RENDER_NAMESPACE
