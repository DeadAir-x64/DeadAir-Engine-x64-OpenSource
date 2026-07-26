#pragma once

// [DA_PORT] Common interface for temporal upscalers: FSR 2, XeSS, DLSS.
//
// All three take the SAME inputs, which is the whole point of this file: colour, depth, motion vectors,
// the sub-pixel jitter that was applied this frame, the frame time, and the camera's near/far/FOV. The
// port already produces every one of them and, more importantly, they are debugged - that was the work
// of the FSR 2 integration. Adding a second backend is therefore plumbing, not research.
//
// Read docs/09_UPSCALERS.md and the memory note on silent render failures before touching this: the
// hard part of FSR 2 was never the library, it was three engine bugs that produced no diagnostics at
// all. A new backend can hit exactly the same ones.
//
// WHAT IS NOT DONE HERE, and cannot be done without you:
//
//   XeSS  - needs Intel's SDK (github.com/intel/xess). Open licence, runs on any vendor through DP4a,
//           has a DX11 path. This is the one worth doing first: our players mostly have old cards.
//   DLSS  - needs NVIDIA's SDK, which requires accepting their licence and shipping nvngx_dlss.dll.
//           RTX only, so it is a bonus for some players rather than a replacement for FSR 2.
//
// Both are downloads that carry licence terms. Fetch them yourself and drop them in Externals/, the
// same way ffx-fsr2-api sits there now; then define the matching DA_HAS_* below.
//
// IX-Ray has working references for both (ixray-1.6-stcop, r4_rendertarget_phase_xess.cpp and
// phase_dlss.cpp). Their jitter and motion-vector conventions match ours exactly - verified against
// their FSR 2 wrapper while debugging ours - so those files are worth reading before writing anything.

//#define DA_HAS_XESS   // define once Externals/xess is present
//#define DA_HAS_DLSS   // define once Externals/nvngx is present, and the licence is accepted

namespace xray::render::RENDER_NAMESPACE
{
// Everything a temporal upscaler needs for one frame. Deliberately API-neutral: the FSR 2 wrapper
// already fills a struct shaped like this (da_fsr2::draw_params), and XeSS/DLSS want the same values
// under different names.
struct da_upscale_frame
{
    ID3D11DeviceContext* context{};

    ID3D11Resource* colour{};   // scene at render resolution, HDR, not yet tonemapped
    ID3D11Resource* depth{};    // the real depth buffer - NOT rt_Position, which holds eye-space
                                // positions and cost us hours when it was passed as depth
    ID3D11Resource* velocity{}; // rt_Velocity, motion vectors in NDC
    ID3D11Resource* output{};   // display resolution, needs unordered access

    u32 render_width{};
    u32 render_height{};

    // In PIXELS of the render resolution, exactly as generated in CCameraManager. The shaders apply it
    // themselves through m_taa_jitter (see cl_taa_jitter in r2.cpp) - it must NOT go into
    // Device.mProject, because that matrix also drives shadows, particles and the HUD, which the
    // upscaler does not compensate. Y is negated on application, following AMD's convention.
    float jitter_x{};
    float jitter_y{};

    float frame_time_ms{}; // never zero
    float near_plane{};
    float far_plane{};
    float fov_vertical{};  // radians. Device.fFOV is HORIZONTAL - convert, do not pass it straight

    bool reset{};          // discard history: level load, teleport, camera cut
    bool sharpening{};
    float sharpness{};     // r__upscale_sharpness / 100
};

// Where a new backend hooks in, and the two traps that are not visible from the call site:
//
//   1. Dispatch from INSIDE phase_combine, immediately before phase_pp - see phase_fsr2(). phase_pp is
//      called from within phase_combine, so a dispatch placed after that call runs a pass too late and
//      its output is silently never displayed.
//
//   2. Release the render targets first. Combine leaves rt_Color bound as the render target and
//      rt_Base_Depth as the depth-stencil, and D3D11 hands a shader zeros for anything bound for
//      writing - no warning, no error, and the upscaler reports success on a black frame.
//
// Both cost a night to find. Neither produces a single line in any log.
} // namespace xray::render::RENDER_NAMESPACE
