#pragma once

#include "StateManager/dx11ShaderResourceStateCache.h"

namespace xray::render::RENDER_NAMESPACE
{
IC void CBackend::set_xform(u32 ID, const Fmatrix& M)
{
    stat.xforms++;
    //  TODO: DX11: Implement CBackend::set_xform
    // VERIFY(!"Implement CBackend::set_xform");
}

IC void CBackend::set_RT(ID3DRenderTargetView* RT, u32 ID)
{
    if (RT != pRT[ID])
    {
        PGO(Msg("PGO:setRT"));
        stat.target_rt++;
        pRT[ID] = RT;
        //  Mark RT array dirty
        // HW.pDevice->OMSetRenderTargets(sizeof(pRT)/sizeof(pRT[0]), pRT, 0);
        // HW.pDevice->OMSetRenderTargets(sizeof(pRT)/sizeof(pRT[0]), pRT, pZB);
        //  Reset all RT's here to allow RT to be bounded as input
        if (!m_bChangedRTorZB)
            HW.get_context(context_id)->OMSetRenderTargets(0, 0, 0);

        m_bChangedRTorZB = true;
    }
}

IC void CBackend::set_ZB(ID3DDepthStencilView* ZB)
{
    if (ZB != pZB)
    {
        PGO(Msg("PGO:setZB"));
        stat.target_zb++;
        pZB = ZB;
        // HW.pDevice->OMSetRenderTargets(0, 0, pZB);
        // HW.pDevice->OMSetRenderTargets(sizeof(pRT)/sizeof(pRT[0]), pRT, pZB);
        //  Reset all RT's here to allow RT to be bounded as input
        if (!m_bChangedRTorZB)
            HW.get_context(context_id)->OMSetRenderTargets(0, 0, 0);
        m_bChangedRTorZB = true;
    }
}

IC void CBackend::ClearRT(ID3DRenderTargetView* rt, const Fcolor& color)
{
    HW.get_context(context_id)->ClearRenderTargetView(rt, reinterpret_cast<const FLOAT*>(&color));
}

IC void CBackend::ClearZB(ID3DDepthStencilView* zb, float depth)
{
    HW.get_context(context_id)->ClearDepthStencilView(zb, D3D_CLEAR_DEPTH, depth, 0);
}

IC void CBackend::ClearZB(ID3DDepthStencilView* zb, float depth, u8 stencil)
{
    HW.get_context(context_id)->ClearDepthStencilView(zb, D3D_CLEAR_DEPTH | D3D_CLEAR_STENCIL, depth, stencil);
}

IC bool CBackend::ClearRTRect(ID3DRenderTargetView* rt, const Fcolor& color, size_t numRects, const Irect* rects)
{
#ifdef USE_DX11
    if (HW.pContext1)
    {
        HW.pContext1->ClearView(rt, reinterpret_cast<const FLOAT*>(&color),
            reinterpret_cast<const D3D_RECT*>(rects), numRects);
        return true;
    }
#else
    UNUSED(numRects);
    UNUSED(rects);
#endif

    return false;
}

IC bool CBackend::ClearZBRect(ID3DDepthStencilView* zb, float depth, size_t numRects, const Irect* rects)
{
#ifdef USE_DX11
    if (HW.pContext1)
    {
        Fcolor color = { depth, depth, depth, depth };
        HW.pContext1->ClearView(zb, reinterpret_cast<FLOAT*>(&color),
            reinterpret_cast<const D3D_RECT*>(rects), numRects);
        return true;
    }
#else
    UNUSED(numRects);
    UNUSED(rects);
#endif

    return false;
}

ICF void CBackend::set_Format(SDeclaration* _decl)
{
    if (decl != _decl)
    {
        PGO(Msg("PGO:v_format:%x", _decl));
        stat.decl++;
        decl = _decl;
    }
}

ICF void CBackend::set_PS(ID3DPixelShader* _ps, LPCSTR _n)
{
    if (ps != _ps)
    {
        PGO(Msg("PGO:Pshader:%x", _ps));
        stat.ps++;
        ps = _ps;
#ifdef USE_DX11
        HW.get_context(context_id)->PSSetShader(ps, 0, 0);
#else
        HW.pContext->PSSetShader(ps);
#endif

#ifdef DEBUG
        ps_name = _n;
#endif
    }
}

ICF void CBackend::set_GS(ID3DGeometryShader* _gs, LPCSTR _n)
{
    if (gs != _gs)
    {
        PGO(Msg("PGO:Gshader:%x", _ps));
        stat.gs++;
        gs = _gs;
#ifdef USE_DX11
        HW.get_context(context_id)->GSSetShader(gs, 0, 0);
#else
        HW.pContext->GSSetShader(gs);
#endif

#ifdef DEBUG
        gs_name = _n;
#endif
    }
}

#ifdef USE_DX11
ICF void CBackend::set_HS(ID3D11HullShader* _hs, LPCSTR _n)
{
    if (hs != _hs)
    {
        PGO(Msg("PGO:Hshader:%x", _ps));
        stat.hs++;
        hs = _hs;
        HW.get_context(context_id)->HSSetShader(hs, 0, 0);

#ifdef DEBUG
        hs_name = _n;
#endif
    }
}

ICF void CBackend::set_DS(ID3D11DomainShader* _ds, LPCSTR _n)
{
    if (ds != _ds)
    {
        PGO(Msg("PGO:Dshader:%x", _ps));
        stat.ds++;
        ds = _ds;
        HW.get_context(context_id)->DSSetShader(ds, 0, 0);

#ifdef DEBUG
        ds_name = _n;
#endif
    }
}

ICF void CBackend::set_CS(ID3D11ComputeShader* _cs, LPCSTR _n)
{
    if (cs != _cs)
    {
        PGO(Msg("PGO:Cshader:%x", _ps));
        stat.cs++;
        cs = _cs;
        HW.get_context(context_id)->CSSetShader(cs, 0, 0);

#ifdef DEBUG
        cs_name = _n;
#endif
    }
}

// [DA_PORT] Читает ли вершинный шейдер развёртку (TEXCOORD0).
//
// ЗАЧЕМ. В теневой проход геометрия идёт УРЕЗАННЫМ потоком: теневой карте нужна одна глубина,
// поэтому берётся `m_fast` — только положение, 12 байт на вершину. Но альфа-тестовые материалы
// (семейство `_aref`: листва, кроны, сетка, заборы) в теневом шейдере читают прозрачность из
// текстуры, а значит требуют развёртку. Для пары «поток без развёртки + шейдер с альфа-тестом»
// DirectX 11 разметку вершин НЕ СОЗДАЁТ — и такая геометрия молча выпадает из теневой карты.
//
// В 32-битном движке на DirectX 9 это было незаметно: недостающие входы там читались нулями, то
// есть прозрачность бралась из угла текстуры. Тот же класс, что уже разобранная невидимая мачта.
//
// Разбор сигнатуры ручной и намеренно осторожный: при любом неожиданном содержимом отвечаем
// «развёртка нужна». Это безопасная сторона — потеряем ускорение, но не тень.
static bool da_dxbc_needs_texcoord(ID3DBlob* sig)
{
    if (!sig)
        return true;

    const u8* base = (const u8*)sig->GetBufferPointer();
    const size_t size = (size_t)sig->GetBufferSize();
    if (!base || size <= 32 || base[0] != 'D' || base[1] != 'X' || base[2] != 'B' || base[3] != 'C')
        return true;

    const u32 chunks = *(const u32*)(base + 28);
    const u32* offsets = (const u32*)(base + 32);
    for (u32 c = 0; c < chunks && 32 + (c + 1) * 4 <= size; ++c)
    {
        const u32 off = offsets[c];
        if (off + 8 > size)
            continue;
        const u8* chunk = base + off;
        // ISG1 -- та же таблица с расширенной записью; отличается только шагом.
        if (chunk[0] != 'I' || chunk[1] != 'S' || chunk[2] != 'G')
            continue;

        const u32 stride = chunk[3] == 'N' ? 24u : 32u;
        const u8* body = chunk + 8;
        const u32 count = *(const u32*)body;
        if (count > 32)
            return true; // столько входов не бывает: содержимое не то, чем кажется

        for (u32 k = 0; k < count; ++k)
        {
            const u8* e = body + 8 + k * stride;
            if (size_t(e + stride - base) > size)
                return true;
            const u32 name_off = *(const u32*)e;
            if (size_t(body + name_off - base) >= size)
                return true;
            // Считается ТОЛЬКО то, что шейдер действительно читает: объявленный, но неиспользуемый
            // вход разметке не мешает — у DirectX 11 он в маске чтения пуст.
            const u8 read_mask = *(e + 21);
            if (read_mask && 0 == xr_strcmp((const char*)(body + name_off), "TEXCOORD"))
                return true;
        }
        return false;
    }
    return true;
}

// [DA_PORT] ⚠️ IC обязателен: функция определена В ЗАГОЛОВКЕ, который включают полтора десятка
// единиц трансляции рендера. Без inline каждая из них порождает своё определение, и линковка
// падает с multiple definition. В релизной сборке это не всплывало — там подстановка съедала
// определения, — и вскрылось только на конфигурации Mixed. Соседние функции файла помечены так же.
IC bool CBackend::da_vs_needs_uv()
{
    if (da_uv_sig_memo != m_pInputSignature)
    {
        da_uv_sig_memo = m_pInputSignature;
        da_uv_need_memo = da_dxbc_needs_texcoord(m_pInputSignature);
    }
    return da_uv_need_memo;
}

ICF bool CBackend::is_TessEnabled() { return HW.FeatureLevel >= D3D_FEATURE_LEVEL_11_0 && (ds != 0 || hs != 0); }
#endif

ICF void CBackend::set_VS(ID3DVertexShader* _vs, LPCSTR _n)
{
    if (vs != _vs)
    {
        PGO(Msg("PGO:Vshader:%x", _vs));
        stat.vs++;
        vs = _vs;
#ifdef USE_DX11
        HW.get_context(context_id)->VSSetShader(vs, 0, 0);
#else
        HW.pContext->VSSetShader(vs);
#endif

        vs_name = _n; // [DA_PORT] и в релизе тоже — см. R_Backend.h
    }
}

ICF void CBackend::set_Vertices(ID3DVertexBuffer* _vb, u32 _vb_stride)
{
    if ((vb != _vb) || (vb_stride != _vb_stride))
    {
        PGO(Msg("PGO:VB:%x,%d", _vb, _vb_stride));
        stat.vb++;
        vb = _vb;
        vb_stride = _vb_stride;
        // CHK_DX           (HW.pDevice->SetStreamSource(0,vb,0,vb_stride));
        // UINT StreamNumber,
        // ID3DVertexBuffer * pStreamData,
        // UINT OffsetInBytes,
        // UINT Stride

        // UINT StartSlot,
        // UINT NumBuffers,
        // ID3DxxBuffer *const *ppVertexBuffers,
        // const UINT *pStrides,
        // const UINT *pOffsets
        u32 iOffset = 0;
        HW.get_context(context_id)->IASetVertexBuffers(0, 1, &vb, &_vb_stride, &iOffset);
    }
}

ICF void CBackend::set_Indices(ID3DIndexBuffer* _ib)
{
    if (ib != _ib)
    {
        PGO(Msg("PGO:IB:%x", _ib));
        stat.ib++;
        ib = _ib;
        HW.get_context(context_id)->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
    }
}

IC D3D_PRIMITIVE_TOPOLOGY TranslateTopology(D3DPRIMITIVETYPE T)
{
    static D3D_PRIMITIVE_TOPOLOGY translateTable[] = {
        D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, // None
        D3D_PRIMITIVE_TOPOLOGY_POINTLIST, // D3DPT_POINTLIST = 1,
        D3D_PRIMITIVE_TOPOLOGY_LINELIST, // D3DPT_LINELIST = 2,
        D3D_PRIMITIVE_TOPOLOGY_LINESTRIP, // D3DPT_LINESTRIP = 3,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST, // D3DPT_TRIANGLELIST = 4,
        D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP, // D3DPT_TRIANGLESTRIP = 5,
        D3D_PRIMITIVE_TOPOLOGY_UNDEFINED, // D3DPT_TRIANGLEFAN = 6,
    };

    VERIFY(T < sizeof(translateTable) / sizeof(translateTable[0]));
    VERIFY(T >= 0);

    D3D_PRIMITIVE_TOPOLOGY result = translateTable[T];

    VERIFY(result != D3D_PRIMITIVE_TOPOLOGY_UNDEFINED);

    return result;
}

IC u32 GetIndexCount(D3DPRIMITIVETYPE T, u32 iPrimitiveCount)
{
    switch (T)
    {
    case D3DPT_POINTLIST: return iPrimitiveCount;
    case D3DPT_LINELIST: return iPrimitiveCount * 2;
    case D3DPT_LINESTRIP: return iPrimitiveCount + 1;
    case D3DPT_TRIANGLELIST: return iPrimitiveCount * 3;
    case D3DPT_TRIANGLESTRIP: return iPrimitiveCount + 2;
    default: NODEFAULT;
#ifdef DEBUG
        return 0;
#endif // #ifdef DEBUG
    }
}

IC void CBackend::ApplyPrimitieTopology(D3D_PRIMITIVE_TOPOLOGY Topology)
{
    if (m_PrimitiveTopology != Topology)
    {
        m_PrimitiveTopology = Topology;
        HW.get_context(context_id)->IASetPrimitiveTopology(m_PrimitiveTopology);
    }
}

#ifdef USE_DX11
IC void CBackend::Compute(u32 ThreadGroupCountX, u32 ThreadGroupCountY, u32 ThreadGroupCountZ)
{
    stat.compute.calls++;
    stat.compute.groups_x = ThreadGroupCountX;
    stat.compute.groups_y = ThreadGroupCountY;
    stat.compute.groups_z = ThreadGroupCountZ;

    SRVSManager.Apply(context_id);
    StateManager.Apply();
    //  State manager may alter constants
    constants.flush();
    HW.get_context(context_id)->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}
#endif

IC void CBackend::Render(D3DPRIMITIVETYPE T, u32 baseV, u32 startV, u32 countV, u32 startI, u32 PC)
{
    // VERIFY(vs);
    // HW.pDevice->VSSetShader(vs);
    // HW.pDevice->GSSetShader(0);

    D3D_PRIMITIVE_TOPOLOGY Topology = TranslateTopology(T);
    u32 iIndexCount = GetIndexCount(T, PC);

//!!! HACK !!!
#ifdef USE_DX11
    if (hs != 0 || ds != 0)
    {
        R_ASSERT(Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Topology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    }
#endif

    stat.render.calls++;
    stat.render.verts += countV;
    stat.render.polys += PC;

    ApplyPrimitieTopology(Topology);

    // CHK_DX(HW.pDevice->DrawIndexedPrimitive(T,baseV, startV, countV,startI,PC));
    // D3DPRIMITIVETYPE Type,
    // INT BaseVertexIndex,
    // UINT MinIndex,
    // UINT NumVertices,
    // UINT StartIndex,
    // UINT PriResmitiveCount

    // UINT IndexCount,
    // UINT StartIndexLocation,
    // INT BaseVertexLocation
    SRVSManager.Apply(context_id);
    ApplyRTandZB();
    ApplyVertexLayout();
    StateManager.Apply();
    //  State manager may alter constants
    constants.flush();
    //  Msg("DrawIndexed: Start");
    //  Msg("iIndexCount=%d, startI=%d, baseV=%d", iIndexCount, startI, baseV);
    HW.get_context(context_id)->DrawIndexed(iIndexCount, startI, baseV);
    //  Msg("DrawIndexed: End\n");

    PGO(Msg("PGO:DIP:%dv/%df", countV, PC));
}

// [DA_PORT] Инстансный вариант Render: та же геометрия, нарисованная instanceCount раз.
//
// Отличается ровно двумя вещами — вызовом DrawIndexedInstanced вместо DrawIndexed и тем, что
// счётчики вершин и полигонов умножаются на число экземпляров. Всё остальное (топология, ресурсы,
// цели, разметка вершин, состояния и сброс констант) делается тем же порядком, что и в Render:
// разойдись они — пакетные деревья рисовались бы в другом состоянии, чем одиночные.
IC void CBackend::RenderInstanced(
    D3DPRIMITIVETYPE T, u32 baseV, u32 /*startV*/, u32 countV, u32 startI, u32 PC, u32 instanceCount)
{
    VERIFY(instanceCount);

    D3D_PRIMITIVE_TOPOLOGY Topology = TranslateTopology(T);
    const u32 iIndexCount = GetIndexCount(T, PC);

    if (hs != 0 || ds != 0)
    {
        R_ASSERT(Topology == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        Topology = D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST;
    }

    stat.render.calls++;
    stat.render.verts += countV * instanceCount;
    stat.render.polys += PC * instanceCount;

    ApplyPrimitieTopology(Topology);
    SRVSManager.Apply(context_id);
    ApplyRTandZB();
    ApplyVertexLayout();
    StateManager.Apply();
    //  State manager may alter constants
    constants.flush();
    HW.get_context(context_id)->DrawIndexedInstanced(iIndexCount, instanceCount, startI, baseV, 0);

    PGO(Msg("PGO:DIP:%dv/%df", countV * instanceCount, PC * instanceCount));
}

IC void CBackend::Render(D3DPRIMITIVETYPE T, u32 startV, u32 PC)
{
    //  TODO: DX11: Remove triangle fan usage from the engine
    if (T == D3DPT_TRIANGLEFAN)
        return;

    // VERIFY(vs);
    // HW.pDevice->VSSetShader(vs);

    D3D_PRIMITIVE_TOPOLOGY Topology = TranslateTopology(T);
    u32 iVertexCount = GetIndexCount(T, PC);

    stat.render.calls++;
    stat.render.verts += 3 * PC;
    stat.render.polys += PC;

    ApplyPrimitieTopology(Topology);
    SRVSManager.Apply(context_id);
    ApplyRTandZB();
    ApplyVertexLayout();
    StateManager.Apply();
    //  State manager may alter constants
    constants.flush();
    //  Msg("Draw: Start");
    //  Msg("iVertexCount=%d, startV=%d", iVertexCount, startV);
    // CHK_DX               (HW.pDevice->DrawPrimitive(T, startV, PC));
    HW.get_context(context_id)->Draw(iVertexCount, startV);
    //  Msg("Draw: End\n");
    PGO(Msg("PGO:DIP:%dv/%df", 3 * PC, PC));
}

IC void CBackend::set_Geometry(SGeometry* _geom)
{
    set_Format(&*_geom->dcl);

    set_Vertices(_geom->vb, _geom->vb_stride);
    set_Indices(_geom->ib);
}

IC void CBackend::set_Scissor(const Irect* R)
{
    if (R)
    {
        // CHK_DX       (HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
        StateManager.EnableScissoring();
        RECT* clip = (RECT*)R;
        HW.get_context(context_id)->RSSetScissorRects(1, clip);
    }
    else
    {
        // CHK_DX       (HW.pDevice->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        StateManager.EnableScissoring(FALSE);
        HW.get_context(context_id)->RSSetScissorRects(0, 0);
    }
}

IC void CBackend::SetViewport(const D3D_VIEWPORT& viewport) const
{
    HW.get_context(context_id)->RSSetViewports(1, &viewport);
}

IC void CBackend::set_Stencil(
    u32 _enable, u32 _func, u32 _ref, u32 _mask, u32 _writemask, u32 _fail, u32 _pass, u32 _zfail)
{
    StateManager.SetStencil(_enable, _func, _ref, _mask, _writemask, _fail, _pass, _zfail);
    // Simple filter
    // if (stencil_enable       != _enable)     { stencil_enable=_enable;       CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILENABLE,     _enable             )); }
    // if (!stencil_enable)                 return;
    // if (stencil_func     != _func)       { stencil_func=_func;           CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILFUNC,
    // _func                )); }
    // if (stencil_ref          != _ref)        { stencil_ref=_ref;             CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILREF,
    // _ref
    // )); }
    // if (stencil_mask     != _mask)       { stencil_mask=_mask;           CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILMASK,
    // _mask                )); }
    // if (stencil_writemask    != _writemask)  { stencil_writemask=_writemask; CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILWRITEMASK,  _writemask          )); }
    // if (stencil_fail     != _fail)       { stencil_fail=_fail;           CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILFAIL,
    // _fail                )); }
    // if (stencil_pass     != _pass)       { stencil_pass=_pass;           CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILPASS,
    // _pass                )); }
    // if (stencil_zfail        != _zfail)      { stencil_zfail=_zfail;         CHK_DX(HW.pDevice->SetRenderState   (
    // D3DRS_STENCILZFAIL,
    // _zfail               )); }
}

IC void CBackend::set_Z(u32 _enable)
{
    StateManager.SetDepthEnable(_enable);
    // if (z_enable != _enable)
    //{
    //  z_enable=_enable;
    //  CHK_DX(HW.pDevice->SetRenderState   ( D3DRS_ZENABLE, _enable ));
    //}
}

IC void CBackend::set_ZFunc(u32 _func)
{
    StateManager.SetDepthFunc(_func);
    // if (z_func!=_func)
    //{
    //  z_func = _func;
    //  CHK_DX(HW.pDevice->SetRenderState( D3DRS_ZFUNC, _func));
    //}
}

IC void CBackend::set_AlphaRef(u32 _value)
{
    //  TODO: DX11: Implement rasterizer state update to support alpha ref
    VERIFY(!"Not implemented.");
    // if (alpha_ref != _value)
    //{
    //  alpha_ref = _value;
    //  CHK_DX(HW.pDevice->SetRenderState(D3DRS_ALPHAREF,_value));
    //}
}

IC void CBackend::set_ColorWriteEnable(u32 _mask)
{
    StateManager.SetColorWriteEnable(_mask);
    // if (colorwrite_mask      != _mask)       {
    //  colorwrite_mask=_mask;
    //  CHK_DX(HW.pDevice->SetRenderState   ( D3DRS_COLORWRITEENABLE,   _mask   ));
    //  CHK_DX(HW.pDevice->SetRenderState   ( D3DRS_COLORWRITEENABLE1,  _mask   ));
    //  CHK_DX(HW.pDevice->SetRenderState   ( D3DRS_COLORWRITEENABLE2,  _mask   ));
    //  CHK_DX(HW.pDevice->SetRenderState   ( D3DRS_COLORWRITEENABLE3,  _mask   ));
    //}
}
ICF void CBackend::set_CullMode(u32 _mode)
{
    StateManager.SetCullMode(_mode);
    cull_mode = _mode;
}

ICF void CBackend::set_FillMode(u32 _mode)
{
    StateManager.SetFillMode(_mode);
}

ICF void CBackend::SetTextureFactor(u32 /*factor*/) const
{
    // Not supported
}

ICF void CBackend::SetAmbient(u32 /*factor*/) const
{
    // Not supported
}

IC void CBackend::ApplyVertexLayout()
{
    VERIFY(vs);
    VERIFY(decl);
    VERIFY(m_pInputSignature);

    // [DA_PORT] Кэш берём СВОЙ ДЛЯ ЭТОГО КОНТЕКСТА. Разбор гонки — в SH_Atomic.h у vs_to_layout:
    // общее дерево перестраивалось вставкой из одного потока, пока по нему шёл другой.
    auto& layouts = decl->vs_to_layout[context_id];

    xr_map<ID3DBlob*, ID3DInputLayout*>::iterator it;

    it = layouts.find(m_pInputSignature);

    if (it == layouts.end())
    {
        // ⛔ [DA_PORT] Отказ создания разметки был НЕВИДИМ, и объект просто переставал рисоваться.
        //
        // `CHK_DX` в релизной сборке раскрывается в голый вызов (xrDebug_macros.h): результат
        // выбрасывается. Указатель при этом не инициализирован, а дальше он БЕЗУСЛОВНО клался в
        // кэш — то есть ноль оседал там навсегда для этой пары «объявление вершин + сигнатура
        // шейдера», и каждый следующий кадр привязывал пустую разметку. Слой проверки DirectX
        // называет это так: «Vertex Shader expects application provided input data, but no Input
        // Assembler object is bound», и вызов отрисовки тихо не рисует ничего.
        //
        // Замер на живой сцене: 127 таких сообщений за один заход, а причина — три отказа
        // CreateInputLayout: шейдер требует NORMAL/0, TANGENT/0, TEXCOORD/1, которых нет в
        // объявлении. Ровно тот же класс, что «VERIFY исчезает в релизе», только в рендере.
        ID3DInputLayout* pLayout = nullptr;

        const HRESULT hr = HW.pDevice->CreateInputLayout(&decl->dx11_dcl_code[0],
            decl->dx11_dcl_code.size() - 1, m_pInputSignature->GetBufferPointer(),
            m_pInputSignature->GetBufferSize(), &pLayout);

        // [DA_PORT] Достройка недостающей развёртки — ТОЛЬКО для семейства `_lmh`.
        //
        // Что не сходится: материал заявляет карту освещения (третья текстура начинается с `lmap`),
        // поэтому блендер выбирает вариант `_lmh`, а геометрия приходит в формате повершинно
        // освещённой — `COLOR0` и ОДНА развёртка. Вариант `-hq`, который берётся вблизи, читает
        // `TEXCOORD1`, которого в этом формате нет. DirectX 11 требует точного совпадения и
        // отказывает; DirectX 9 в оригинальном 32-битном движке недостающие входы читал нулями —
        // поэтому там мачта на месте, а у нас пропадала.
        //
        // ⭐ Выбор шейдера сверен с оригинальными исходниками (определение карты освещения, склейка
        // имени, суффикс `-hq`) — совпадает дословно. Расхождения в нашем коде нет, несогласованы
        // данные уровня. Поэтому чиним не выбор, а привязку.
        //
        // ⚠️ Ограничено `_lmh` намеренно: сломанных пар в кадре четыре, но остальные три (наш
        // `da_water_velocity` и теневой `shadow_direct_base_aref`) разбираются отдельно — общая
        // починка скрыла бы их.
        //
        // Довод в пользу безопасности: эти вызовы СЕЙЧАС не рисуют ничего. Хуже стать не может, а
        // затронуты только уже сломанные пары. Недостающая развёртка берётся с той же позиции, что
        // и базовая: карта освещения будет выбираться по базовой развёртке. Для повершинно
        // освещённой геометрии она смысла не несёт, зато форма и положение мачты верные.
        if ((FAILED(hr) || !pLayout) && vs_name && strstr(vs_name, "_lmh"))
        {
            xr_vector<D3D_INPUT_ELEMENT_DESC> patched;
            patched.reserve(decl->dx11_dcl_code.size() + 1);

            bool has_tc1 = false;
            const D3D_INPUT_ELEMENT_DESC* tc0 = nullptr;
            for (size_t i = 0; i + 1 < decl->dx11_dcl_code.size(); ++i)
            {
                const auto& e = decl->dx11_dcl_code[i];
                patched.push_back(e);
                if (!e.SemanticName || xr_strcmp(e.SemanticName, "TEXCOORD") != 0)
                    continue;
                if (e.SemanticIndex == 0)
                    tc0 = &decl->dx11_dcl_code[i];
                else if (e.SemanticIndex == 1)
                    has_tc1 = true;
            }

            if (!has_tc1 && tc0)
            {
                D3D_INPUT_ELEMENT_DESC extra = *tc0;
                extra.SemanticIndex = 1;
                patched.push_back(extra);

                ID3DInputLayout* patched_layout = nullptr;
                if (SUCCEEDED(HW.pDevice->CreateInputLayout(&patched[0], (UINT)patched.size(),
                        m_pInputSignature->GetBufferPointer(), m_pInputSignature->GetBufferSize(),
                        &patched_layout)) &&
                    patched_layout)
                {
                    static u32 da_fixed = 0;
                    if (++da_fixed <= 3)
                        Msg("* [DA] шейдеру «%s» дописана развёртка TEXCOORD1 поверх базовой — "
                            "геометрия без карты освещения снова рисуется (случаев %u)",
                            vs_name, da_fixed);
                    pLayout = patched_layout;
                }
            }
        }

        if (FAILED(hr) && !pLayout)
        {
            // Кэшируем и отказ тоже: повторять заведомо безнадёжный вызов каждый кадр дороже,
            // чем помнить о нём. Но теперь он НАЗВАН — с перечнем того, что объявление даёт.
            static u32 da_hits = 0;
            ++da_hits;
            if (da_hits <= 10)
            {
                string512 semantics{};
                for (size_t i = 0; i + 1 < decl->dx11_dcl_code.size(); ++i)
                {
                    const auto& e = decl->dx11_dcl_code[i];
                    if (!e.SemanticName)
                        continue;
                    string64 one;
                    xr_sprintf(one, "%s%u ", e.SemanticName, e.SemanticIndex);
                    xr_strcat(semantics, one);
                }
                // ⭐ Имя шейдера — самое важное в этой строке: по перечню семантик видно, чего НЕ
                // хватает, а по имени — КТО требует. Без него отказ приходится искать перебором.
                // [DA_PORT] Третье, чего не хватало: ЧЬЯ это геометрия. Имени визуала на этом уровне
                // нет — до бэкенда доходит только объявление вершин, — но его опознаёт связка
                // «шаг вершины + адрес объявления»: у каждого формата он свой и стабилен за сеанс.
                // По шагу сразу видно урезанный поток (12 байт — только положение).
                u32 da_stride = 0;
                for (size_t i = 0; i + 1 < decl->dx11_dcl_code.size(); ++i)
                {
                    const auto& e = decl->dx11_dcl_code[i];
                    switch (e.Format)
                    {
                    case DXGI_FORMAT_R32G32B32A32_FLOAT: da_stride += 16; break;
                    case DXGI_FORMAT_R32G32B32_FLOAT: da_stride += 12; break;
                    case DXGI_FORMAT_R32G32_FLOAT: da_stride += 8; break;
                    case DXGI_FORMAT_R32_FLOAT:
                    case DXGI_FORMAT_R8G8B8A8_UNORM:
                    case DXGI_FORMAT_R8G8B8A8_UINT:
                    case DXGI_FORMAT_R16G16_SINT:
                    case DXGI_FORMAT_R16G16_FLOAT: da_stride += 4; break;
                    default: break;
                    }
                }

                Msg("! [DA] разметка вершин НЕ СОЗДАНА (0x%08x): вызовы шейдера «%s» рисуют пустоту. "
                    "Объявление даёт: %s| шаг вершины %u байт, объявление [%p] (случаев %u)",
                    (u32)hr, vs_name ? vs_name : "(без имени)", semantics, da_stride,
                    (const void*)decl, da_hits);

                // [DA_PORT] Вторая строка: чего шейдер ТРЕБУЕТ, прочитано из его же сигнатуры.
                //
                // Зачем понадобилась. Первой строки хватало ровно до тех пор, пока требования шейдера
                // можно было прочитать в его исходнике. На `da_water_velocity` это подвело: исходник
                // объявлял шесть входов, читал один, и глазами было не понять, что DirectX сверяет
                // раскладку с ОБЪЯВЛЕННЫМИ, а не с используемыми. Разбираться пришлось разбором
                // двоичного файла из кэша — снаружи и вручную. Теперь это делает сам движок.
                //
                // Формат сигнатуры: контейнер DXBC, внутри кусок ISGN со списком входов. Разбор
                // ручной и намеренно осторожный: это отладочная ветка, она обязана молчать при любом
                // неожиданном содержимом, а не падать поверх уже случившейся беды.
                if (m_pInputSignature)
                {
                    const u8* base = (const u8*)m_pInputSignature->GetBufferPointer();
                    const size_t size = (size_t)m_pInputSignature->GetBufferSize();
                    string512 wants{};
                    bool parsed = false;

                    if (base && size > 32 && base[0] == 'D' && base[1] == 'X' && base[2] == 'B' &&
                        base[3] == 'C')
                    {
                        const u32 chunks = *(const u32*)(base + 28);
                        const u32* offsets = (const u32*)(base + 32);
                        for (u32 c = 0; c < chunks && 32 + (c + 1) * 4 <= size; ++c)
                        {
                            const u32 off = offsets[c];
                            if (off + 8 > size)
                                continue;
                            const u8* chunk = base + off;
                            // ISG1 -- та же таблица с расширенной записью; отличается только шагом.
                            const bool isgn = chunk[0] == 'I' && chunk[1] == 'S' && chunk[2] == 'G';
                            if (!isgn)
                                continue;
                            const u32 stride = chunk[3] == 'N' ? 24u : 32u;
                            const u8* body = chunk + 8;
                            const u32 count = *(const u32*)body;
                            if (count > 32)
                                break; // столько входов не бывает: содержимое не то, чем кажется
                            for (u32 k = 0; k < count; ++k)
                            {
                                const u8* e = body + 8 + k * stride;
                                if (size_t(e + stride - base) > size)
                                    break;
                                const u32 name_off = *(const u32*)e;
                                const u32 sem_index = *(const u32*)(e + 4);
                                const u8 read_mask = *(e + 21);
                                if (size_t(body + name_off - base) >= size)
                                    break;
                                string64 one;
                                // Помечаем те входы, которые шейдер объявил, но не читает: именно они
                                // и есть обычная причина отказа -- лишние требования на пустом месте.
                                xr_sprintf(one, "%s%u%s ", (const char*)(body + name_off), sem_index,
                                    read_mask ? "" : "(не читается)");
                                xr_strcat(wants, one);
                                parsed = true;
                            }
                            break;
                        }
                    }

                    if (parsed)
                        Msg("!   шейдер требует: %s", wants);
                }
            }
            pLayout = nullptr;
        }

        it = layouts.insert(std::pair<ID3DBlob*, ID3DInputLayout*>(m_pInputSignature, pLayout)).first;
    }

    if (m_pInputLayout != it->second)
    {
        m_pInputLayout = it->second;
        HW.get_context(context_id)->IASetInputLayout(m_pInputLayout);
    }
}

ICF void CBackend::set_VS(ref_vs& _vs)
{
    m_pInputSignature = _vs->signature->signature;
    set_VS(_vs->sh, _vs->cName.c_str());
}

ICF void CBackend::set_VS(SVS* _vs)
{
    m_pInputSignature = _vs->signature->signature;
    set_VS(_vs->sh, _vs->cName.c_str());
}

IC bool CBackend::CBuffersNeedUpdate(
    ref_cbuffer buf1[MaxCBuffers], dx11ConstantBuffer* const buf2[MaxCBuffers], u32& uiMin, u32& uiMax)
{
    bool bRes = false;
    int i = 0;
    while ((i < MaxCBuffers) && (buf1[i]._get() == buf2[i]))
        ++i;

    uiMin = i;

    for (; i < MaxCBuffers; ++i)
    {
        if (buf1[i]._get() != buf2[i])
        {
            bRes = true;
            uiMax = i;
        }
    }

    return bRes;
}

// [DA_PORT] Разбивка set_Constants (r__graph_prof 1). Замер показал: константы это 75% настройки
// прохода — больше, чем состояния, шейдеры, текстуры и матрицы вместе. Внутри три разных дела:
// сброс отображений, перетасовка массивов умных указателей (счётчик ссылок АТОМАРНЫЙ) и обход
// таблицы с привязкой. Лечатся по-разному, поэтому меряем порознь.
// [DA_PORT] Биты стадий для m_cbStageMask (см. R_Backend.h) и ручка сравнения r__cb_stage_skip.
enum : u32
{
    DA_CB_STAGE_PS = 1u << 0,
    DA_CB_STAGE_VS = 1u << 1,
    DA_CB_STAGE_GS = 1u << 2,
    DA_CB_STAGE_HS = 1u << 3,
    DA_CB_STAGE_DS = 1u << 4,
    DA_CB_STAGE_CS = 1u << 5,
    DA_CB_STAGE_ALL = 0x3Fu,
};
extern int ps_da_cb_stage_skip;

extern int ps_da_graph_prof;
extern float g_da_const_unmap, g_da_const_shuffle, g_da_const_bind, g_da_const_loaders;

IC void CBackend::set_Constants(R_constant_table* C)
{
    // caching
    if (ctable == C)
        return;
    ctable = C;

    CTimer da_ct;
    const bool da_cprof = ps_da_graph_prof != 0;
    if (da_cprof)
        da_ct.Start();
    xforms.unmap();
    hemi.unmap();
    tree.unmap();
#ifdef USE_DX11
    LOD.unmap();
#endif
    StateManager.UnmapConstants();
    if (da_cprof)
    {
        g_da_const_unmap += da_ct.GetElapsed_sec() * 1000.f;
        da_ct.Start();
    }
    if (0 == C)
        return;

    PGO(Msg("PGO:c-table"));

    //  Setup constant tables
    {
        // [DA_PORT] Снимок предыдущего набора буферов — СЫРЫМИ указателями, а не ref_cbuffer.
        //
        // ЗАЧЕМ. Замер (r__graph_prof) назвал эту перетасовку самой дорогой частью настройки
        // констант: 0.77 мс на кадр из 1.98 — больше, чем привязка буферов и обработчики по
        // отдельности. Работы здесь нет никакой, это чистая цена типа: копия 84 умных указателей
        // (шесть стадий по MaxCBuffers) — это 84 атомарных увеличения счётчика, зануление членов —
        // ещё 84 уменьшения, и столько же при выходе из области видимости. Около 250 атомарных
        // операций на КАЖДУЮ смену прохода, а их в кадре под восемь сотен.
        //
        // ⛔ Почему владеть снимком не нужно. Он живёт до конца функции и используется ровно для
        // одного — сравнения «изменился ли слот» в CBuffersNeedUpdate, без разыменования. Сами
        // буферы принадлежат таблицам констант проходов, а те живут, пока загружен уровень.
        //
        // 🪤 Единственный способ подорваться — если бы между занулением членов и сравнением кто-то
        // успел освободить буфер и получить НОВЫЙ по тому же адресу: сравнение сказало бы «не
        // изменилось» и слот остался бы непривязанным. Поэтому порядок важен и его нельзя менять:
        // между этим циклом и сравнением идёт только раскладка уже существующих ссылок из таблицы,
        // выделений памяти там нет.
        dx11ConstantBuffer* aPixelConstants[MaxCBuffers]{};
        dx11ConstantBuffer* aVertexConstants[MaxCBuffers]{};
        dx11ConstantBuffer* aGeometryConstants[MaxCBuffers]{};
#ifdef USE_DX11
        dx11ConstantBuffer* aHullConstants[MaxCBuffers]{};
        dx11ConstantBuffer* aDomainConstants[MaxCBuffers]{};
        dx11ConstantBuffer* aComputeConstants[MaxCBuffers]{};
#endif

        // [DA_PORT] Обходим только те стадии, которые хоть раз получали буфер (см. m_cbStageMask).
        // Ручка r__cb_stage_skip 0 возвращает обход всех шести — чтобы сравнить в одной сборке.
        const u32 da_stage_mask = ps_da_cb_stage_skip ? m_cbStageMask : DA_CB_STAGE_ALL;

        const auto da_snapshot = [](ref_cbuffer* members, dx11ConstantBuffer** snap)
        {
            for (int i = 0; i < MaxCBuffers; ++i)
            {
                snap[i] = members[i]._get();
                members[i] = 0;
            }
        };

        if (da_stage_mask & DA_CB_STAGE_PS)
            da_snapshot(m_aPixelConstants, aPixelConstants);
        if (da_stage_mask & DA_CB_STAGE_VS)
            da_snapshot(m_aVertexConstants, aVertexConstants);
        if (da_stage_mask & DA_CB_STAGE_GS)
            da_snapshot(m_aGeometryConstants, aGeometryConstants);
#ifdef USE_DX11
        if (da_stage_mask & DA_CB_STAGE_HS)
            da_snapshot(m_aHullConstants, aHullConstants);
        if (da_stage_mask & DA_CB_STAGE_DS)
            da_snapshot(m_aDomainConstants, aDomainConstants);
        if (da_stage_mask & DA_CB_STAGE_CS)
            da_snapshot(m_aComputeConstants, aComputeConstants);
#endif
        if (da_cprof)
        {
            g_da_const_shuffle += da_ct.GetElapsed_sec() * 1000.f;
            da_ct.Start();
        }

        R_constant_table::cb_table::iterator it = C->m_CBTable[context_id].begin();
        R_constant_table::cb_table::iterator end = C->m_CBTable[context_id].end();
        for (; it != end; ++it)
        {
            // ID3DxxBuffer*    pBuffer = (it->second)->GetBuffer();
            u32 uiBufferIndex = it->first;

            if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferPixelShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aPixelConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_PS;
            }
            else if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferVertexShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aVertexConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_VS;
            }
            else if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferGeometryShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aGeometryConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_GS;
            }
#ifdef USE_DX11
            else if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferHullShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aHullConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_HS;
            }
            else if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferDomainShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aDomainConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_DS;
            }
            else if ((uiBufferIndex & CB_BufferTypeMask) == CB_BufferComputeShader)
            {
                VERIFY((uiBufferIndex & CB_BufferIndexMask) < MaxCBuffers);
                m_aComputeConstants[uiBufferIndex & CB_BufferIndexMask] = it->second;
                m_cbStageMask |= DA_CB_STAGE_CS;
            }
#endif
            else
                VERIFY("Invalid enumeration");
        }

        ID3DBuffer* tempBuffer[MaxCBuffers];

        u32 uiMin;
        u32 uiMax;

        if ((da_stage_mask & DA_CB_STAGE_PS) && CBuffersNeedUpdate(m_aPixelConstants, aPixelConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aPixelConstants[i])
                    tempBuffer[i] = m_aPixelConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }

            HW.get_context(context_id)->PSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }

        if ((da_stage_mask & DA_CB_STAGE_VS) && CBuffersNeedUpdate(m_aVertexConstants, aVertexConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aVertexConstants[i])
                    tempBuffer[i] = m_aVertexConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }
            HW.get_context(context_id)->VSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }

        if ((da_stage_mask & DA_CB_STAGE_GS) && CBuffersNeedUpdate(m_aGeometryConstants, aGeometryConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aGeometryConstants[i])
                    tempBuffer[i] = m_aGeometryConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }
            HW.get_context(context_id)->GSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }

        if ((da_stage_mask & DA_CB_STAGE_HS) && CBuffersNeedUpdate(m_aHullConstants, aHullConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aHullConstants[i])
                    tempBuffer[i] = m_aHullConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }
            HW.get_context(context_id)->HSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }

        if ((da_stage_mask & DA_CB_STAGE_DS) && CBuffersNeedUpdate(m_aDomainConstants, aDomainConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aDomainConstants[i])
                    tempBuffer[i] = m_aDomainConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }
            HW.get_context(context_id)->DSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }

        if ((da_stage_mask & DA_CB_STAGE_CS) && CBuffersNeedUpdate(m_aComputeConstants, aComputeConstants, uiMin, uiMax))
        {
            ++uiMax;

            for (u32 i = uiMin; i < uiMax; ++i)
            {
                if (m_aComputeConstants[i])
                    tempBuffer[i] = m_aComputeConstants[i]->GetBuffer();
                else
                    tempBuffer[i] = 0;
            }
            HW.get_context(context_id)->CSSetConstantBuffers(uiMin, uiMax - uiMin, &tempBuffer[uiMin]);
        }
        /*
        for (int i=0; i<MaxCBuffers; ++i)
        {
            if (m_aPixelConstants[i])
                tempBuffer[i] = m_aPixelConstants[i]->GetBuffer();
            else
                tempBuffer[i] = 0;
        }
        HW.pDevice->PSSetConstantBuffers(0, MaxCBuffers, tempBuffer);

        for (int i=0; i<MaxCBuffers; ++i)
        {
            if (m_aVertexConstants[i])
                tempBuffer[i] = m_aVertexConstants[i]->GetBuffer();
            else
                tempBuffer[i] = 0;
        }
        HW.pDevice->VSSetConstantBuffers(0, MaxCBuffers, tempBuffer);

        for (int i=0; i<MaxCBuffers; ++i)
        {
            if (m_aGeometryConstants[i])
                tempBuffer[i] = m_aGeometryConstants[i]->GetBuffer();
            else
                tempBuffer[i] = 0;
        }
        HW.pDevice->GSSetConstantBuffers(0, MaxCBuffers, tempBuffer);
        */
    }

    if (da_cprof)
    {
        g_da_const_bind += da_ct.GetElapsed_sec() * 1000.f;
        da_ct.Start();
    }

    // process constant-loaders
    R_constant_table::c_table::iterator it = C->table.begin();
    R_constant_table::c_table::iterator end = C->table.end();
    for (; it != end; ++it)
    {
        R_constant* Cs = &**it;
        VERIFY(Cs);
        if (Cs && Cs->handler)
            Cs->handler->setup(*this, Cs);
    }

    if (da_cprof)
        g_da_const_loaders += da_ct.GetElapsed_sec() * 1000.f;
}

ICF void CBackend::ApplyRTandZB()
{
    if (m_bChangedRTorZB)
    {
        m_bChangedRTorZB = false;
        HW.get_context(context_id)->OMSetRenderTargets(sizeof(pRT) / sizeof(pRT[0]), pRT, pZB);
    }
}

IC void CBackend::get_ConstantDirect(const shared_str& n, size_t DataSize, void** pVData, void** pGData, void** pPData)
{
    ref_constant C = get_c(n);

    if (C)
        constants.access_direct(&*C, DataSize, pVData, pGData, pPData);
    else
    {
        if (pVData)
            *pVData = 0;
        if (pGData)
            *pGData = 0;
        if (pPData)
            *pPData = 0;
    }
}

// [DA_PORT] Проверка на пустоту обязательна. ID3DUserDefinedAnnotation — часть рантайма D3D11.1,
// на Windows 7 он есть только с Platform Update (KB2670838). Без него QueryInterface в
// CBackend::OnDeviceCreate не срабатывает и оставляет указатель пустым, а дальше первый же
// PIX_EVENT падал на разыменовании — игра умирала на первом кадре главного меню
// (CRender::RenderMenu -> PIX_EVENT(render_menu)), причём одинаково на любой машине без 11.1.
// Это метки для профилировщика: без них рендер работает ровно так же.
IC void CBackend::gpu_mark_begin(const wchar_t* name)
{
    if (pAnnotation)
        pAnnotation->BeginEvent(name);
}

IC void CBackend::gpu_mark_end()
{
    if (pAnnotation)
        pAnnotation->EndEvent();
}

IC void CBackend::set_pass_targets(const ref_rt& _1, const ref_rt& _2, const ref_rt& _3, const ref_rt& zb)
{
    if (_1)
    {
        curr_rt_width = _1->dwWidth;
        curr_rt_height = _1->dwHeight;
    }
    else
    {
        VERIFY(zb);
        curr_rt_width = zb->dwWidth;
        curr_rt_height = zb->dwHeight;
    }

    set_RT(_1 ? _1->pRT : nullptr, 0);
    set_RT(_2 ? _2->pRT : nullptr, 1);
    set_RT(_3 ? _3->pRT : nullptr, 2);
    set_ZB(zb ? zb->pZRT[context_id] : nullptr);

    const D3D_VIEWPORT viewport = { 0, 0, curr_rt_width, curr_rt_height, 0.f, 1.f };
    SetViewport(viewport);
}
} // namespace xray::render::RENDER_NAMESPACE
