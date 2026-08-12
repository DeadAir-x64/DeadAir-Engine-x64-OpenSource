#include "stdafx.h"
#pragma hdrstop

#include "blender_light_occq.h"

namespace xray::render::RENDER_NAMESPACE
{
CBlender_light_occq::CBlender_light_occq() { description.CLS = 0; }
CBlender_light_occq::~CBlender_light_occq() {}
void CBlender_light_occq::Compile(CBlender_Compile& C)
{
    IBlender::Compile(C);

#if RENDER == R_R2
    switch (C.iElement)
    {
    case 0: // occlusion testing
        C.r_Pass("dumb", "dumb", false, TRUE, FALSE, FALSE);
        C.r_End();
        break;
    case 1: // NV40 optimization :)
        C.r_Pass("null", "dumb", false, FALSE, FALSE, FALSE);
        C.r_End();
        break;
    }
#else
    switch (C.iElement)
    {
    case 0: // occlusion testing
        C.r_Pass("dumb", "dumb", false, TRUE, FALSE, FALSE);
        C.r_End();
        // Color write as well as culling and stencil are set up manually in code.
        break;
    case 1: // NV40 optimization :)
        C.r_Pass("stub_notransform_t", "dumb", false, FALSE, FALSE, FALSE);
        C.r_ColorWriteEnable(false, false, false, false);
        C.r_CullMode(D3DCULL_NONE);
        C.r_Stencil(TRUE, D3DCMP_LESSEQUAL, 0xff, 0x00); // keep/keep/keep
        C.r_End();
        break;
    // [DA_PORT] Заливка глубины единицей в пределах области просмотра.
    //
    // Нужна, чтобы чистить теневой атлас ПО ЯЧЕЙКАМ, а не целиком. Целиком его чистит
    // phase_smap_spot_clear, и пока он это делает, содержимое прошлого кадра переиспользовать
    // нельзя — а без этого невозможен кэш теней ламп: сцену незачем рисовать заново, если она
    // не изменилась, но её стирают.
    //
    // ⭐ Нового шейдера не нужно: берём уже собранные stub_notransform_t и dumb.
    //
    // ⚠️ Тонкость нашего слоя состояний: bZtest=FALSE здесь НЕ выключает глубину, как было бы в
    // чистом DirectX 11, а ставит функцию сравнения «всегда» (Blender_Recorder.cpp, PassSET_ZB),
    // при этом DepthEnable в переводе на DX11 жёстко TRUE и не сбрасывается. Значит пара
    // «сравнение всегда + запись включена» выражается именно так, и отдельная ручка для функции
    // сравнения не понадобилась.
    case 3:
        C.r_Pass("stub_notransform_t", "dumb", false, FALSE, TRUE, FALSE);
        C.r_ColorWriteEnable(false, false, false, false);
        C.r_CullMode(D3DCULL_NONE);
        C.r_Stencil(FALSE);
        C.r_End();
        break;
    case 2: // Stencil clear in case we've ran out of markers.
        C.r_Pass("stub_notransform_t", "dumb", false, FALSE, FALSE, FALSE);
        C.r_ColorWriteEnable(false, false, false, false);
        C.r_CullMode(D3DCULL_NONE);
        if (RImplementation.o.msaa)
            C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0x00, 0x7E, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
        else
        {
            // Clear all bits except the last one
            C.r_Stencil(TRUE, D3DCMP_ALWAYS, 0x00, 0xFE, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
        }
        // C.r_Stencil(TRUE,D3DCMP_ALWAYS,0x00,0xFF, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO, D3DSTENCILOP_ZERO);
        // keep/keep/keep
        C.r_End();
        break;
    }
#endif
}
} // namespace xray::render::RENDER_NAMESPACE
