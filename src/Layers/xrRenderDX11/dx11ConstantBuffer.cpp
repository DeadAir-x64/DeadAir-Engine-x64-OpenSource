#include "stdafx.h"
#include "dx11ConstantBuffer.h"

#include "Layers/xrRender/BufferUtils.h"

// [DA_PORT] Global scope on purpose: the cvar lives in xrEngine, not in the render namespace.
extern ENGINE_API int ps_r__cb_skip_redundant;

namespace xray::render::RENDER_NAMESPACE
{
dx11ConstantBuffer::~dx11ConstantBuffer()
{
    for (int id = 0; id < R__NUM_CONTEXTS; ++id)
    {
        RImplementation.Resources->_DeleteConstantBuffer(id, this);
    }
    //	Flush();
    _RELEASE(m_pBuffer);
    xr_free(m_pBufferData);
    xr_free(m_pCommittedData);
}

dx11ConstantBuffer::dx11ConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable) : m_bChanged(true)
{
    D3D_SHADER_BUFFER_DESC Desc;

    CHK_DX(pTable->GetDesc(&Desc));

    m_strBufferName._set(Desc.Name);
    m_eBufferType = Desc.Type;
    m_uiBufferSize = Desc.Size;

    //	Fill member list with variable descriptions
    m_MembersList.resize(Desc.Variables);
    m_MembersNames.resize(Desc.Variables);
    for (u32 i = 0; i < Desc.Variables; ++i)
    {
        ID3DShaderReflectionVariable* pVar;
        ID3DShaderReflectionType* pType;

        D3D_SHADER_VARIABLE_DESC var_desc;

        pVar = pTable->GetVariableByIndex(i);
        VERIFY(pVar);
        pType = pVar->GetType();
        VERIFY(pType);
        pType->GetDesc(&m_MembersList[i]);
        //	Buffers with the same layout can contain totally different members
        CHK_DX(pVar->GetDesc(&var_desc));
        m_MembersNames[i] = var_desc.Name;
    }

    m_uiMembersCRC = crc32(&m_MembersList[0], Desc.Variables * sizeof(m_MembersList[0]));

    R_CHK(BufferUtils::CreateConstantBuffer(&m_pBuffer, Desc.Size));
    VERIFY(m_pBuffer);
    // [DA_PORT] xr_malloc does not zero memory. Constant buffers with the same layout are
    // shared/reused across different shaders (see comment above), and a given shader may only
    // set a subset of a buffer's members - any never-written bytes were leaking whatever
    // garbage happened to be in that heap allocation straight into shader constants (observed
    // as colorful "rainbow" noise, most visible right after a level load when shader/buffer
    // reuse patterns churn). Zero it once up front so unset members are always 0, not garbage.
    m_pBufferData = xr_malloc(Desc.Size);
    VERIFY(m_pBufferData);
    ZeroMemory(m_pBufferData, Desc.Size);

    // [DA_PORT] Shadow of the last upload; see r__cb_skip_redundant. Zeroed for the same reason as
    // the line above, though it is never compared until the first Flush has filled it.
    m_pCommittedData = xr_malloc(Desc.Size);
    VERIFY(m_pCommittedData);
    ZeroMemory(m_pCommittedData, Desc.Size);

#ifdef DEBUG
    if (m_pBuffer)
    {
        m_pBuffer->SetPrivateData(WKPDID_D3DDebugObjectName, xr_strlen(Desc.Name), Desc.Name);
    }
#endif
}

bool dx11ConstantBuffer::Similar(dx11ConstantBuffer& _in)
{
    if (m_strBufferName._get() != _in.m_strBufferName._get())
        return false;

    if (m_eBufferType != _in.m_eBufferType)
        return false;

    if (m_uiMembersCRC != _in.m_uiMembersCRC)
        return false;

    if (m_MembersList.size() != _in.m_MembersList.size())
        return false;

    if (memcmp(&m_MembersList[0], &_in.m_MembersList[0], m_MembersList.size() * sizeof(m_MembersList[0])))
        return false;

    VERIFY(m_MembersNames.size() == _in.m_MembersNames.size());

    int iMemberNum = m_MembersNames.size();
    for (int i = 0; i < iMemberNum; ++i)
    {
        if (m_MembersNames[i].c_str() != _in.m_MembersNames[i].c_str())
            return false;
    }

    return true;
}

void dx11ConstantBuffer::Flush(u32 context_id)
{
    if (m_bChanged)
    {
        // [DA_PORT] Access() flags the buffer on any write, whether or not the value differs (the
        // author's own "TODO: check if set actually changes" sits right there), so a constant that
        // is rewritten with the same value every frame still costs a map and a full copy. Compare
        // first - a memcmp is cheaper than a map, and these buffers are small.
        if (ps_r__cb_skip_redundant && m_bHasCommittedData &&
            0 == memcmp(m_pBufferData, m_pCommittedData, m_uiBufferSize))
        {
            m_bChanged = false;
            return;
        }

        void* pData;
#ifdef USE_DX11
        D3D11_MAPPED_SUBRESOURCE pSubRes;
        CHK_DX(HW.get_context(context_id)->Map(m_pBuffer, 0, D3D_MAP_WRITE_DISCARD, 0, &pSubRes));
        pData = pSubRes.pData;
#else
        CHK_DX(m_pBuffer->Map(D3D_MAP_WRITE_DISCARD, 0, &pData));
#endif
        VERIFY(pData);
        VERIFY(m_pBufferData);
        CopyMemory(pData, m_pBufferData, m_uiBufferSize);
#ifdef USE_DX11
        HW.get_context(context_id)->Unmap(m_pBuffer, 0);
#else
        m_pBuffer->Unmap();
#endif
        // [DA_PORT] Remember exactly what went to the GPU so the comparison above has something
        // truthful to work against.
        CopyMemory(m_pCommittedData, m_pBufferData, m_uiBufferSize);
        m_bHasCommittedData = true;
        m_bChanged = false;
    }
}
} // namespace xray::render::RENDER_NAMESPACE
