#pragma once

namespace xray::render::RENDER_NAMESPACE
{
IC Fvector4* dx11ConstantBuffer::Access(u16 offset)
{
    //	TODO: DX11: Implement code which will check if set actually changes code.
    m_bChanged = true;

    //	Check buffer size in client code: don't know if actual data will cross
    //	buffer boundaries.
    VERIFY(offset < (int)m_uiBufferSize);
    u8* res = ((u8*)m_pBufferData) + offset;
    return (Fvector4*)res;
}

IC void dx11ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fmatrix& A)
{
    VERIFY(RC_float == C->type);
    //	TEST
    // return;

    // Fvector4*	it	= c_f.access	(L.index);
    Fvector4* it = Access(L.index);
    switch (L.cls)
    {
    case RC_2x4:
        // c_f.dirty			(L.index,L.index+2);
        VERIFY(u32((u32)L.index + 2 * lineSize) <= m_uiBufferSize);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        break;
    case RC_3x4:
        // c_f.dirty			(L.index,L.index+3);
        VERIFY(u32((u32)L.index + 3 * lineSize) <= m_uiBufferSize);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        it[2].set(A._13, A._23, A._33, A._43);
        break;
    case RC_4x4:
        // c_f.dirty			(L.index,L.index+4);
        VERIFY(u32((u32)L.index + 4 * lineSize) <= m_uiBufferSize);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        it[2].set(A._13, A._23, A._33, A._43);
        it[3].set(A._14, A._24, A._34, A._44);
        break;
    default:
#ifdef DEBUG
        xrDebug::Fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", C->name.c_str());
#else
        NODEFAULT;
#endif
    }
}

IC void dx11ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fvector4& A)
{
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);
    // Fvector4*	it	= Access(L.index);
    // it->set	(A);

    VERIFY(u32((u32)L.index + lineSize) <= m_uiBufferSize);
    float* it = (float*)Access(L.index);

    size_t count = 4;
    switch (L.cls)
    {
    case RC_1x2: count = 2; break;
    case RC_1x3: count = 3; break;
    case RC_1x4: count = 4; break;
    default: break;
    }

    CopyMemory(it, &A[0], count * sizeof(float));

    // c_f.access	(L.index)->set	(A);
    // c_f.dirty	(L.index,L.index+1);
}

IC void dx11ConstantBuffer::set(R_constant* C, R_constant_load& L, float A)
{
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x1 == L.cls);
    float* it = (float*)Access(L.index);
    VERIFY(u32((u32)L.index + sizeof(float)) <= m_uiBufferSize);
    *it = A;

    // c_f.access	(L.index)->set	(A);
    // c_f.dirty	(L.index,L.index+1);
}

IC void dx11ConstantBuffer::set(R_constant* C, R_constant_load& L, int A)
{
    VERIFY(RC_int == C->type);
    VERIFY(RC_1x1 == L.cls);
    int* it = (int*)Access(L.index);
    VERIFY(u32((u32)L.index + sizeof(int)) <= m_uiBufferSize);
    *it = A;

    // c_f.access	(L.index)->set	(A);
    // c_f.dirty	(L.index,L.index+1);
}

IC void dx11ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A)
{
    //	TEST
    // return;
    VERIFY(RC_float == C->type);
    // [DA_PORT] seta() — путь МАССИВОВ: e это индекс элемента из РАНТАЙМА (палитра костей, массив
    // источников света и т.п.), а не из рефлексии. В релизе VERIFY ниже вырезан, и base мог уйти за
    // буфер — плюс скрытая беда: base это u32, а Access() берёт u16, поэтому старая
    // последовательность «Access((u16)base) ... потом VERIFY(base ...)» и усекала адрес, и проверяла
    // уже поздно. Считаем полный u32 base, проверяем ДО обращения к буферу; за границей молча
    // пропускаем запись (лучше устаревшая матрица, чем порча кучи). Это единственная ветка на
    // горячем пути — только на массивах; скалярный set() не тронут (он безопасен по построению).
    u32 base;
    Fvector4* it;
    switch (L.cls)
    {
    case RC_2x4:
        base = (u32)L.index + 2 * lineSize * e;
        VERIFY((base + 2 * lineSize) <= m_uiBufferSize);
        if (base + 2 * lineSize > m_uiBufferSize)
            return;
        it = Access((u16)base);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        break;
    case RC_3x4:
        base = (u32)L.index + 3 * lineSize * e;
        VERIFY((base + 3 * lineSize) <= m_uiBufferSize);
        if (base + 3 * lineSize > m_uiBufferSize)
            return;
        it = Access((u16)base);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        it[2].set(A._13, A._23, A._33, A._43);
        break;
    case RC_4x4:
        base = (u32)L.index + 4 * lineSize * e;
        VERIFY((base + 4 * lineSize) <= m_uiBufferSize);
        if (base + 4 * lineSize > m_uiBufferSize)
            return;
        it = Access((u16)base);
        it[0].set(A._11, A._21, A._31, A._41);
        it[1].set(A._12, A._22, A._32, A._42);
        it[2].set(A._13, A._23, A._33, A._43);
        it[3].set(A._14, A._24, A._34, A._44);
        break;
    default:
#ifdef DEBUG
        xrDebug::Fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", C->name.c_str());
#else
        NODEFAULT;
#endif
    }
}

IC void dx11ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fvector4& A)
{
    //	TEST
    // return;
    VERIFY(RC_float == C->type);
    VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);

    static const u16 lineSize = 4 * sizeof(float);
    u32 base = (u32)L.index + lineSize * e;
    // [DA_PORT] см. seta(Fmatrix): e — рантайм-индекс массива; проверяем полным u32 base ДО обращения
    // к буферу (Access усекает до u16), за границей молча пропускаем — куча не портится.
    VERIFY((base + lineSize) <= m_uiBufferSize);
    if (base + lineSize > m_uiBufferSize)
        return;
    Fvector4* it = Access((u16)base);
    it->set(A);

    // u32			base	= L.index + e;
    // c_f.access	(base)->set	(A);
    // c_f.dirty	(base,base+1);
}

IC void* dx11ConstantBuffer::AccessDirect(R_constant_load& L, size_t DataSize)
{
    //	Check buffer size in client code: don't know if actual data will cross
    //	buffer boundaries.
    VERIFY(L.index < (int)m_uiBufferSize);
    u8* res = ((u8*)m_pBufferData) + L.index;

    if ((size_t)L.index + DataSize <= m_uiBufferSize)
    {
        m_bChanged = true;
        return res;
    }
    else
        return 0;
}
} // namespace xray::render::RENDER_NAMESPACE
