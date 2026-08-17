#include "stdafx.h"
#pragma hdrstop

#include "LightTrack.h"
#include "xrEngine/IRenderable.h"

#if defined(USE_DX11)
#include <DirectXMath.h>
#endif

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here: an extern written inside
// namespace xray::render::render_r4 would be looking for a symbol in that namespace.
extern ENGINE_API int ps_r__taa;
extern ENGINE_API int ps_r__taa_mipbias;

namespace xray::render::RENDER_NAMESPACE
{
void CBackend::OnFrameEnd()
{
    if (!GEnv.isDedicatedServer)
    {
#if defined(USE_DX11)
        HW.get_context(CHW::IMM_CTX_ID)->ClearState();
#endif
        Invalidate();
    }
}

void CBackend::OnFrameBegin()
{
    if (!GEnv.isDedicatedServer)
    {
        PGO(Msg("PGO:*****frame[%d]*****", Device.dwFrame));

#ifndef USE_DX9
        Invalidate();
        // DX9 sets base rt and base zb by default
#ifndef USE_OGL
        // XXX: Getting broken HUD hands for OpenGL after calling rmNormal()
        RImplementation.rmNormal(*this);
#else
        set_FB(HW.pFB);
#endif
        set_RT(RImplementation.Target->get_base_rt());

        // [DA_PORT] Глубину привязываем ТОЛЬКО когда её размер совпадает с целью.
        //
        // Стоковый код вешал сюда базовую цель и базовую глубину вместе, и это было верно ровно до
        // тех пор, пока обе имели размер окна. Наш масштаб рендера это развёл: базовая цель осталась
        // размером с окно (rt_Base создаётся по Device.dwWidth/Height), а базовая глубина стала
        // размером сцены (rt_Base_Depth — по dwRenderWidth/Height). При 77% это 1920x1080 против
        // 1478x830 в одной привязке.
        //
        // DirectX такое сочетание запрещает: у цели и глубины должны совпадать размеры. Слой
        // проверки говорит об этом прямо, а без него отказ молчаливый — рисование в несовместимую
        // пару просто отбрасывается. Тот же класс, что уже ловил нас на несовпадении формата вершины
        // с шейдером: ни сообщения, ни стека, просто чего-то нет на экране.
        //
        // При масштабе 100% размеры совпадают, и поведение остаётся стоковым до последнего вызова.
        // При уменьшенном — глубина не привязывается вовсе, и это безопасно: каждый проход, которому
        // она нужна, ставит свою сам (u_setrt), а до первого такого прохода читать её всё равно
        // некому.
        // [DA_PORT] ⚠️ Разбор выше — про DirectX, и правка обязана быть его же. В OpenGL буфер
        // глубины это GLuint, а не указатель: `nullptr` туда не ложится, и общий код переставал
        // собираться для GL целиком. Обнаружено сборкой под санитайзеры — см. da_port/tools/asan.
#ifndef USE_OGL
        const bool base_depth_matches = Device.dwRenderWidth == Device.dwWidth &&
            Device.dwRenderHeight == Device.dwHeight;
        set_ZB(base_depth_matches ? RImplementation.Target->get_base_zb() : nullptr);
#else
        set_ZB(RImplementation.Target->get_base_zb());
#endif
#endif

        ZeroMemory(&stat, sizeof(stat));
        set_Stencil(FALSE);
    }
}

void CBackend::Invalidate()
{
    pRT[0] = 0;
    pRT[1] = 0;
    pRT[2] = 0;
    pRT[3] = 0;
    pZB = 0;
#if defined(USE_OGL)
    pFB = 0;
    pp = 0;
#endif

    decl = nullptr;
    vb = 0;
    ib = 0;
    vb_stride = 0;

    state = nullptr;
    ps = 0;
    vs = 0;
    DX11_ONLY(gs = NULL);
#ifdef USE_DX11
    hs = 0;
    ds = 0;
    cs = 0;
#endif
    ctable = nullptr;

    T = nullptr;
    M = nullptr;
    C = nullptr;

    stencil_enable = u32(-1);
    stencil_func = u32(-1);
    stencil_ref = u32(-1);
    stencil_mask = u32(-1);
    stencil_writemask = u32(-1);
    stencil_fail = u32(-1);
    stencil_pass = u32(-1);
    stencil_zfail = u32(-1);
    cull_mode = u32(-1);
    fill_mode = u32(-1);
    z_enable = u32(-1);
    z_func = u32(-1);
    alpha_ref = u32(-1);
    colorwrite_mask = u32(-1);

    // Since constant buffers are unmapped (for DirecX 10)
    // transform setting handlers should be unmapped too.
    xforms.unmap();

#if defined(USE_DX11)
    m_pInputLayout = NULL;
    m_PrimitiveTopology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    m_bChangedRTorZB = false;
    m_pInputSignature = NULL;
    for (int i = 0; i < MaxCBuffers; ++i)
    {
        m_aPixelConstants[i] = 0;
        m_aVertexConstants[i] = 0;
        m_aGeometryConstants[i] = 0;
        m_aHullConstants[i] = 0;
        m_aDomainConstants[i] = 0;
        m_aComputeConstants[i] = 0;
    }
    StateManager.Reset();
    // Redundant call. Just no note that we need to unmap const
    // if we create dedicated class.
    StateManager.UnmapConstants();
    SRVSManager.ResetDeviceState();

    for (u32 gs_it = 0; gs_it < CTexture::mtMaxGeometryShaderTextures;)
        textures_gs[gs_it++] = 0;
    for (u32 hs_it = 0; hs_it < CTexture::mtMaxHullShaderTextures;)
        textures_hs[hs_it++] = 0;
    for (u32 ds_it = 0; ds_it < CTexture::mtMaxDomainShaderTextures;)
        textures_ds[ds_it++] = 0;
    for (u32 cs_it = 0; cs_it < CTexture::mtMaxComputeShaderTextures;)
        textures_cs[cs_it++] = 0;

    context_id = CHW::IMM_CTX_ID;
#endif // USE_DX11

    for (u32 ps_it = 0; ps_it < CTexture::mtMaxPixelShaderTextures;)
        textures_ps[ps_it++] = nullptr;
    for (u32 vs_it = 0; vs_it < CTexture::mtMaxVertexShaderTextures;)
        textures_vs[vs_it++] = nullptr;
    for (auto& matrix : matrices)
        matrix = nullptr;
}

// [DA_PORT] Сброс кэша непосредственного контекста после отправки списка команд.
// Разбор, зачем и почему статическая, — у объявления в R_Backend.h рядом с submit().
void CBackend::da_invalidate_immediate()
{
#ifdef USE_DX11
    RCache.Invalidate();
#endif
}

void CBackend::set_ClipPlanes(u32 _enable, Fplane* _planes /*=NULL */, u32 count /* =0*/)
{
#if defined(USE_DX11) || defined(USE_OGL)
    // TODO: DX11: Implement in the corresponding vertex shaders
    // Use this to set up location, were shader setup code will get data
    // VERIFY(!"CBackend::set_ClipPlanes not implemented!");
    UNUSED(_enable);
    UNUSED(_planes);
    UNUSED(count);
    return;
#else
#   error No graphics API selected or enabled!
#endif
}

#ifndef DEDICATED_SREVER
void CBackend::set_ClipPlanes(u32 _enable, Fmatrix* _xform /*=NULL */, u32 fmask /* =0xff */)
{
    if (0 == HW.Caps.geometry.dwClipPlanes)
        return;
    if (!_enable)
    {
#if defined(USE_DX11) || defined(USE_OGL)
    // TODO: DX11: Implement in the corresponding vertex shaders
    // Use this to set up location, were shader setup code will get data
    // VERIFY(!"CBackend::set_ClipPlanes not implemented!");
#else
#   error No graphics API selected or enabled!
#endif
        return;
    }
    VERIFY(_xform && fmask);
    CFrustum F;
    F.CreateFromMatrix(*_xform, fmask);
    set_ClipPlanes(_enable, F.planes, F.p_count);
}

void CBackend::set_Textures(STextureList* textures_list)
{
    // TODO: expose T invalidation method
    //if (T == textures_list) // disabled due to cases when the set of resources the same, but different srv is need to be bind
    //    return;
    T = textures_list;
    // If resources weren't set at all we should clear from resource #0.
    int _last_ps = -1;
    int _last_vs = -1;
#if defined(USE_DX11)
    int _last_gs = -1;
    int _last_hs = -1;
    int _last_ds = -1;
    int _last_cs = -1;
#endif
    auto it = textures_list->begin();
    const auto end = textures_list->end();

    for (; it != end; ++it)
    {
        std::pair<u32, ref_texture>& loader = *it;
        u32 load_id = loader.first;
        CTexture* load_surf = loader.second._get();
        //if (load_id < 256) {
        if (load_id < CTexture::rstVertex)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::mtMaxPixelShaderTextures);
            // ordinary pixel surface
            if ((int)load_id > _last_ps)
                _last_ps = load_id;
            if (textures_ps[load_id] != load_surf || (load_surf && (load_surf->last_slice != load_surf->curr_slice)))
            {
                textures_ps[load_id] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Apply(load_id);
                    load_surf->last_slice = load_surf->curr_slice;
                }
            }
        }
        else
#if defined(USE_DX11)
        if (load_id < CTexture::rstGeometry)
#endif
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstVertex + CTexture::mtMaxVertexShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstVertex;
            if ((int)load_id_remapped > _last_vs)
                _last_vs = load_id_remapped;
            if (textures_vs[load_id_remapped] != load_surf)
            {
                textures_vs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
#if defined(USE_DX11)
        else if (load_id < CTexture::rstHull)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstGeometry + CTexture::mtMaxGeometryShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstGeometry;
            if ((int)load_id_remapped > _last_gs)
                _last_gs = load_id_remapped;
            if (textures_gs[load_id_remapped] != load_surf)
            {
                textures_gs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstDomain)
        {
            //  Set up pixel shader resources
            VERIFY(load_id < CTexture::rstHull + CTexture::mtMaxHullShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstHull;
            if ((int)load_id_remapped > _last_hs)
                _last_hs = load_id_remapped;
            if (textures_hs[load_id_remapped] != load_surf)
            {
                textures_hs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstCompute)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstDomain + CTexture::mtMaxDomainShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstDomain;
            if ((int)load_id_remapped > _last_ds)
                _last_ds = load_id_remapped;
            if (textures_ds[load_id_remapped] != load_surf)
            {
                textures_ds[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Apply(load_id);
                }
            }
        }
        else if (load_id < CTexture::rstInvalid)
        {
            // Set up pixel shader resources
            VERIFY(load_id < CTexture::rstCompute + CTexture::mtMaxComputeShaderTextures);

            // vertex only //d-map or vertex
            u32 load_id_remapped = load_id - CTexture::rstCompute;
            if ((int)load_id_remapped > _last_cs)
                _last_cs = load_id_remapped;
            if (textures_cs[load_id_remapped] != load_surf)
            {
                textures_cs[load_id_remapped] = load_surf;
                stat.textures++;

                if (load_surf)
                {
                    PGO(Msg("PGO:tex%d:%s", load_id, load_surf->cName.c_str()));
                    load_surf->bind(*this, load_id);
                    //load_surf->Applyload_id);
                }
            }
        }
        else
        {
            VERIFY("Invalid enum");
        }
#endif // USE_DX11
    }

    // clear remaining stages (PS)
    for (++_last_ps; _last_ps < CTexture::mtMaxPixelShaderTextures; _last_ps++)
    {
        if (!textures_ps[_last_ps])
            continue;

        textures_ps[_last_ps] = nullptr;
#if defined(USE_DX11)
        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->PSSetShaderResources(_last_ps, 1, &pRes);
        SRVSManager.SetPSResource(_last_ps, pRes);
#elif defined(USE_OGL)
        CHK_GL(glActiveTexture(GL_TEXTURE0 + _last_ps));
        CHK_GL(glBindTexture(GL_TEXTURE_2D, 0));
        if (RImplementation.o.msaa)
            CHK_GL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_3D, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
#else
#   error No graphics API selected or enabled!
#endif
    }
    // clear remaining stages (VS)
    for (++_last_vs; _last_vs < CTexture::mtMaxVertexShaderTextures; _last_vs++)
    {
        if (!textures_vs[_last_vs])
            continue;

        textures_vs[_last_vs] = nullptr;
#if defined(USE_DX11)
        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->VSSetShaderResources(_last_vs, 1, &pRes);
        SRVSManager.SetVSResource(_last_vs, pRes);
#elif defined(USE_OGL)
        CHK_GL(glActiveTexture(GL_TEXTURE0 + CTexture::rstVertex + _last_vs));
        CHK_GL(glBindTexture(GL_TEXTURE_2D, 0));
        if (RImplementation.o.msaa)
            CHK_GL(glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_3D, 0));
        CHK_GL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
#else
#   error No graphics API selected or enabled!
#endif
    }

#if defined(USE_DX11)
    // clear remaining stages (VS)
    for (++_last_gs; _last_gs < CTexture::mtMaxGeometryShaderTextures; _last_gs++)
    {
        if (!textures_gs[_last_gs])
            continue;

        textures_gs[_last_gs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        // HW.pDevice->GSSetShaderResources(_last_gs, 1, &pRes);
        SRVSManager.SetGSResource(_last_gs, pRes);
    }
    for (++_last_hs; _last_hs < CTexture::mtMaxHullShaderTextures; _last_hs++)
    {
        if (!textures_hs[_last_hs])
            continue;

        textures_hs[_last_hs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetHSResource(_last_hs, pRes);
    }
    for (++_last_ds; _last_ds < CTexture::mtMaxDomainShaderTextures; _last_ds++)
    {
        if (!textures_ds[_last_ds])
            continue;

        textures_ds[_last_ds] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetDSResource(_last_ds, pRes);
    }
    for (++_last_cs; _last_cs < CTexture::mtMaxComputeShaderTextures; _last_cs++)
    {
        if (!textures_cs[_last_cs])
            continue;

        textures_cs[_last_cs] = 0;

        // TODO: DX11: Optimise: set all resources at once
        ID3DShaderResourceView* pRes = 0;
        SRVSManager.SetCSResource(_last_cs, pRes);
    }
#endif // USE_DX11
}
#else

void CBackend::set_ClipPlanes(u32 _enable, Fmatrix* _xform /*=NULL */, u32 fmask /* =0xff */) {}
void CBackend::set_Textures(STextureList* textures_list) {}

#endif // DEDICATED SERVER

void CBackend::SetupStates()
{
    set_CullMode(CULL_CCW);
#if defined(USE_DX11)
    SSManager.SetMaxAnisotropy(ps_r__tf_Anisotropic);

    // [DA_PORT] Compensate the mip selection for r__render_scale. Rendering the scene smaller makes the
    // hardware pick coarser mip levels, so on top of simply having fewer pixels the textures themselves
    // arrive pre-blurred — which is what reads as "mushy" after upscaling. Shifting the bias by
    // log2(render / output) asks for the mips the full-resolution image would have used; this is the
    // standard companion to any upscaler. At 100% the term is 0 and the user's own bias is untouched.
    float mip_bias = ps_r__tf_Mipbias;
    if (Device.dwRenderWidth > 0 && Device.dwRenderWidth < Device.dwWidth)
        mip_bias += log2f(float(Device.dwRenderWidth) / float(Device.dwWidth));

    // [DA_PORT] With TAA on, optionally ask for sharper mips than the hardware would pick: mip selection
    // assumes one sample per pixel, and temporal accumulation gives several. It does buy back distant
    // detail — but only where the history actually holds, and on dense vegetation it holds poorly, so
    // the aliasing the standard bias was suppressing comes straight back as shimmer. Off by default;
    // r__taa_mipbias is in hundredths of a mip level.
    if (::ps_r__taa && ::ps_r__taa_mipbias)
        mip_bias -= float(::ps_r__taa_mipbias) / 100.f;

    SSManager.SetMipLODBias(mip_bias);
#elif defined(USE_OGL)
    // TODO: OGL: Implement SetupStates().
#else
#   error No graphics API selected or enabled!
#endif
}


// Device dependance
void CBackend::OnDeviceCreate()
{
    ZoneScoped;

#if defined(USE_DX11)
    // [DA_PORT] Результат обязательно проверять и говорить о нём в лог. Интерфейс появился в
    // рантайме D3D11.1: на Windows 7 без Platform Update (KB2670838) запрос не удаётся, указатель
    // остаётся пустым — а раньше это выяснялось только падением на первом PIX_EVENT, без единого
    // сообщения. Метки нужны лишь профилировщику, поэтому отсутствие интерфейса не ошибка.
    const HRESULT annotation_hr = HW.get_context(context_id)->QueryInterface(
        __uuidof(ID3DUserDefinedAnnotation), reinterpret_cast<void**>(&pAnnotation));
    if (FAILED(annotation_hr) || !pAnnotation)
    {
        pAnnotation = nullptr;
        if (context_id == CHW::IMM_CTX_ID)
            Msg("* [DA_PORT] отладочные метки GPU недоступны (нет ID3DUserDefinedAnnotation, "
                "рантайм ниже D3D11.1) - рендер не затронут");
    }
#endif

    // Debug Draw
    InitializeDebugDraw();

    // invalidate caching
    Invalidate();
}

void CBackend::OnDeviceDestroy()
{
    // Debug Draw
    DestroyDebugDraw();

#if defined(USE_DX11)
    //  Destroy state managers
    StateManager.Reset();
#endif

#if defined(USE_DX11)
    _RELEASE(pAnnotation);
#endif
}

void CBackend::apply_lmaterial()
{
    R_constant* C = get_c(c_sbase)._get(); // get sampler
    if (!C)
        return;

    VERIFY(RC_dest_sampler == C->destination);
#if defined(USE_DX11)
    VERIFY(RC_dx11texture == C->type);
#elif defined(USE_OGL)
    VERIFY(RC_sampler == C->type);
#else
#   error No graphics API selected or enabled!
#endif

    CTexture* T = get_ActiveTexture(u32(C->samp.index));
    VERIFY(T);
    float mtl = T->m_material;
#ifdef DEBUG
    if (ps_r2_ls_flags.test(R2FLAG_GLOBALMATERIAL))
        mtl = ps_r2_gmaterial;
#endif
    hemi.set_material(o_hemi, o_sun, 0, (mtl + .5f) / 4.f);
    hemi.set_pos_faces(o_hemi_cube[CROS_impl::CUBE_FACE_POS_X],
                                o_hemi_cube[CROS_impl::CUBE_FACE_POS_Y],
                                o_hemi_cube[CROS_impl::CUBE_FACE_POS_Z]);
    hemi.set_neg_faces(o_hemi_cube[CROS_impl::CUBE_FACE_NEG_X],
                                o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Y],
                                o_hemi_cube[CROS_impl::CUBE_FACE_NEG_Z]);
}
} // namespace xray::render::RENDER_NAMESPACE
