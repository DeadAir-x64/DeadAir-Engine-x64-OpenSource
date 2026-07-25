#include "stdafx.h"

namespace xray::render::RENDER_NAMESPACE
{
void CRenderTarget::u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, ID3DDepthStencilView* zb)
{
    VERIFY(_1 || zb);
    if (_1)
    {
        dwWidth[cmd_list.context_id] = _1->dwWidth;
        dwHeight[cmd_list.context_id] = _1->dwHeight;
    }
    else
    {
        D3D_DEPTH_STENCIL_VIEW_DESC desc;
        zb->GetDesc(&desc);

        if (!RImplementation.o.msaa)
            VERIFY(desc.ViewDimension == D3D_DSV_DIMENSION_TEXTURE2D || desc.ViewDimension == D3D_DSV_DIMENSION_TEXTURE2DARRAY);

        ID3DResource* pRes;

        zb->GetResource(&pRes);

        ID3DTexture2D* pTex = (ID3DTexture2D*)pRes;

        D3D_TEXTURE2D_DESC TexDesc;

        pTex->GetDesc(&TexDesc);

        dwWidth[cmd_list.context_id] = TexDesc.Width;
        dwHeight[cmd_list.context_id] = TexDesc.Height;
        _RELEASE(pRes);
    }

    if (_1)
        cmd_list.set_RT(_1->pRT, 0);
    else
        cmd_list.set_RT(NULL, 0);
    if (_2)
        cmd_list.set_RT(_2->pRT, 1);
    else
        cmd_list.set_RT(NULL, 1);
    if (_3)
        cmd_list.set_RT(_3->pRT, 2);
    else
        cmd_list.set_RT(NULL, 2);
    cmd_list.set_ZB(zb);

    // [DA_PORT] NOTE: deliberately does NOT set the viewport, unlike CBackend::set_pass_targets.
    // Several passes draw quads whose coordinates belong to a different buffer than the one they
    // render into (the luminance chain draws a BLOOM_size quad into a 64x64 target), because the
    // vertex shader maps pixels to clip space through screen_res, i.e. the WINDOW size. Those passes
    // need a window-sized viewport, not a target-sized one; phase_bloom now sets that explicitly for
    // itself and the luminance chain it calls. Anything added here that needs a specific viewport
    // should do the same rather than changing this shared path.

}

void CRenderTarget::u_setrt(CBackend& cmd_list, const ref_rt& _1, const ref_rt& _2, ID3DDepthStencilView* zb)
{
    VERIFY(_1 || zb);
    if (_1)
    {
        dwWidth[cmd_list.context_id] = _1->dwWidth;
        dwHeight[cmd_list.context_id] = _1->dwHeight;
    }
    else
    {
        D3D_DEPTH_STENCIL_VIEW_DESC desc;
        zb->GetDesc(&desc);
        if (!RImplementation.o.msaa)
            VERIFY(desc.ViewDimension == D3D_DSV_DIMENSION_TEXTURE2D || desc.ViewDimension == D3D_DSV_DIMENSION_TEXTURE2DARRAY);

        ID3DResource* pRes;

        zb->GetResource(&pRes);

        ID3DTexture2D* pTex = (ID3DTexture2D*)pRes;

        D3D_TEXTURE2D_DESC TexDesc;

        pTex->GetDesc(&TexDesc);

        dwWidth[cmd_list.context_id] = TexDesc.Width;
        dwHeight[cmd_list.context_id] = TexDesc.Height;
        _RELEASE(pRes);
    }

    if (_1)
        cmd_list.set_RT(_1->pRT, 0);
    else
        cmd_list.set_RT(NULL, 0);
    if (_2)
        cmd_list.set_RT(_2->pRT, 1);
    else
        cmd_list.set_RT(NULL, 1);
    cmd_list.set_ZB(zb);

    // [DA_PORT] NOTE: deliberately does NOT set the viewport, unlike CBackend::set_pass_targets.
    // Several passes draw quads whose coordinates belong to a different buffer than the one they
    // render into (the luminance chain draws a BLOOM_size quad into a 64x64 target), because the
    // vertex shader maps pixels to clip space through screen_res, i.e. the WINDOW size. Those passes
    // need a window-sized viewport, not a target-sized one; phase_bloom now sets that explicitly for
    // itself and the luminance chain it calls. Anything added here that needs a specific viewport
    // should do the same rather than changing this shared path.

}

void CRenderTarget::u_setrt(CBackend& cmd_list, u32 W, u32 H, ID3DRenderTargetView* _1, ID3DRenderTargetView* _2, ID3DRenderTargetView* _3,
    ID3DDepthStencilView* zb)
{
    // VERIFY									(_1);
    dwWidth[cmd_list.context_id] = W;
    dwHeight[cmd_list.context_id] = H;
    // VERIFY									(_1);
    cmd_list.set_RT(_1, 0);
    cmd_list.set_RT(_2, 1);
    cmd_list.set_RT(_3, 2);
    cmd_list.set_ZB(zb);

    // [DA_PORT] Unlike the ref_rt overloads (see CBackend::set_pass_targets), this raw-view path never
    // touched the viewport — it just inherited whatever the previous pass left. That was harmless while
    // every target matched the screen, but this overload is exactly how the finished frame is blitted to
    // the back buffer, so with r__render_scale < 100 the scene-sized viewport survived and the upscaled
    // image was drawn 1:1 into the top-left corner of the screen instead of filling it.
#if defined(USE_DX11)
    // Keep curr_rt_* in step as well — the screen_res shader constant is derived from it, and the UI is
    // drawn through this path.
    cmd_list.curr_rt_width = W;
    cmd_list.curr_rt_height = H;

    const D3D_VIEWPORT viewport = { 0.f, 0.f, float(W), float(H), 0.f, 1.f };
    cmd_list.SetViewport(viewport);
#endif
    //	RImplementation.rmNormal				();
}
} // namespace xray::render::RENDER_NAMESPACE
