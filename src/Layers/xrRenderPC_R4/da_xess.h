#pragma once

// [DA_PORT] Intel XeSS — the second temporal upscaler, alongside FSR 2.
//
// Worth having even though FSR 2 already works: XeSS runs on any vendor (through DP4a on non-Intel
// hardware), and on the old cards most of this mod's players own the two reconstruct differently
// enough that one of them usually looks better. Both stay available, the player picks.
//
// It takes exactly the inputs the port already produces for FSR 2 — that was the point of doing the
// motion vectors properly. Two things are simpler here than they were with AMD's library:
//
//   - Intel ships a DX11 path and a prebuilt libxess_dx11 binary, so there is no MinGW build to patch.
//   - XESS_INIT_FLAG_USE_NDC_VELOCITY takes our vectors in the space they are already stored in, so
//     there is no motionVectorScale to get the sign of wrong. That single parameter cost hours on FSR 2.
//
// Distribution note: Intel's licence requires their copyright and licence text to travel with the
// build. Externals/xess/LICENSE.txt and third-party-programs.txt must reach the release.

#include <xess/xess.h>
#include <xess/xess_d3d11.h>

namespace xray::render::RENDER_NAMESPACE
{
class da_xess
{
public:
    struct init_params
    {
        u32 display_width{};
        u32 display_height{};
        u32 quality{};        // r__xess, see quality_for()
        ID3D11Device* device{};
    };

    struct draw_params
    {
        ID3D11DeviceContext* context{};

        ID3D11Resource* colour{};   // scene at render resolution, HDR
        ID3D11Resource* depth{};    // the real depth buffer, not rt_Position
        ID3D11Resource* velocity{}; // rt_Velocity, motion vectors in NDC
        ID3D11Resource* output{};   // display resolution, needs unordered access
        ID3D11Resource* reactive{}; // [DA_PORT] Intel calls it the responsive pixel mask

        u32 render_width{};
        u32 render_height{};

        // In PIXELS, the same value handed to FSR 2 — XeSS wants the range [-0.5, 0.5], which is
        // exactly how CCameraManager generates it.
        float jitter_x{};
        float jitter_y{};

        bool reset{}; // discard history: level load, teleport, camera cut
    };

    ~da_xess() { destroy(); }

    bool create(const init_params& p);
    void destroy();
    bool draw(const draw_params& p);

    bool ready() const { return m_created; }

    // Render resolution XeSS expects for a given quality mode, so the scene can be sized to match.
    static void render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h);

private:
    static xess_quality_settings_t quality_for(u32 quality);

    xess_context_handle_t m_context{};
    bool m_created{};
};

extern da_xess g_da_xess;
} // namespace xray::render::RENDER_NAMESPACE
