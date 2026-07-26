#pragma once

// [DA_PORT] The few FSR 3 entry points the rest of the renderer needs, declared WITHOUT dragging in
// any FidelityFX header.
//
// FSR 2 and FSR 3 ship separate copies of ffx_types.h that declare the same type names - FfxResource,
// FfxSurfaceFormat and a dozen others - under different include guards. Nothing stops both being
// included, and then every one of those names is defined twice and the translation unit does not
// compile. Keeping the two APIs in separate translation units is the whole point of this file.

struct ID3D11Device;

namespace xray::render::RENDER_NAMESPACE
{
bool da_fsr3_create(u32 render_w, u32 render_h, u32 display_w, u32 display_h, ID3D11Device* device);
void da_fsr3_destroy();
bool da_fsr3_ready();
} // namespace xray::render::RENDER_NAMESPACE
