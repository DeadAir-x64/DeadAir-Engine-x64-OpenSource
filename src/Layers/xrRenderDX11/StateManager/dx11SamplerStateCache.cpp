#include "stdafx.h"
#include "dx11SamplerStateCache.h"
#include "Layers/xrRenderDX11/dx11StateUtils.h"

namespace xray::render::RENDER_NAMESPACE
{
using dx11StateUtils::operator==;

dx11SamplerStateCache SSManager;

dx11SamplerStateCache::dx11SamplerStateCache() : m_uiMaxAnisotropy(1), m_uiMipLODBias(0.0f)
{
    static const int iMaxRSStates = 10;
    m_StateArray.reserve(iMaxRSStates);
}

dx11SamplerStateCache::~dx11SamplerStateCache() { ClearStateArray(); }
dx11SamplerStateCache::SHandle dx11SamplerStateCache::GetState(D3D_SAMPLER_DESC& desc)
{
    SHandle hResult;

    //	MaxAnisitropy is reset by ValidateState if not aplicable
    //	to the filter mode used.
    desc.MaxAnisotropy = m_uiMaxAnisotropy;
    // RZ
    desc.MipLODBias = m_uiMipLODBias;

    dx11StateUtils::ValidateState(desc);

    u32 crc = dx11StateUtils::GetHash(desc);

    hResult = FindState(desc, crc);

    if (hResult == hInvalidHandle)
    {
        StateRecord rec{};
        rec.m_crc = crc;
        rec.m_desc = desc; // [DA_PORT] see StateRecord: spares FindState a GetDesc per lookup
        CreateState(desc, &rec.m_pState);
        hResult = m_StateArray.size();
        m_StateArray.push_back(rec);
    }

    return hResult;
}

void dx11SamplerStateCache::CreateState(StateDecs desc, IDeviceState** ppIState)
{
    CHK_DX(HW.pDevice->CreateSamplerState(&desc, ppIState));
}

dx11SamplerStateCache::SHandle dx11SamplerStateCache::FindState(const StateDecs& desc, u32 StateCRC)
{
    u32 res = 0xffffffff;
    u32 i = 0;
    while (i < m_StateArray.size())
    {
        if (m_StateArray[i].m_crc == StateCRC)
        {
            if (m_StateArray[i].m_desc == desc)
            // return i;
            //	TEST
            {
                // return i;
                res = i;
                break;
            }
            // else
            //{
            //	VERIFY(0);
            //}
        }
        i++;
    }

    return res != 0xffffffff ? i : (u32)hInvalidHandle;
}

void dx11SamplerStateCache::ClearStateArray()
{
    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        _RELEASE(m_StateArray[i].m_pState);
    }

    m_StateArray.clear();

    // [DA_PORT] The device is going away, so nothing is bound any more. Leaving the counts behind
    // would make the next bind clear slots that no longer exist - and this runs on every device
    // reset, which is a path we already know is touchy.
    ZeroMemory(m_boundSamplerCounts, sizeof(m_boundSamplerCounts));
}

u32 dx11SamplerStateCache::PrepareSamplerStates(u32 context_id, ShaderStage stage, HArray& samplers,
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT])
{
    VERIFY(samplers.size() <= D3D_COMMONSHADER_SAMPLER_SLOT_COUNT);
    VERIFY(context_id < R__NUM_CONTEXTS);

    for (u32 i = 0; i < samplers.size(); ++i)
    {
        if (samplers[i] != hInvalidHandle)
        {
            VERIFY(samplers[i] < m_StateArray.size());
            pSS[i] = m_StateArray[samplers[i]].m_pState;
        }
    }

    // [DA_PORT] Hand D3D this shader's slots plus the tail the previous bind used, so the leftovers
    // are nulled and nothing stale stays bound. pSS is zero-initialised by the caller, so the tail
    // entries are already null.
    auto& previous = m_boundSamplerCounts[context_id][static_cast<u32>(stage)];
    const u32 count = _max(static_cast<u32>(samplers.size()), static_cast<u32>(previous));
    previous = static_cast<u8>(samplers.size());
    return count;
}

void dx11SamplerStateCache::VSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Vertex, samplers, pSS);
    if (count)
        HW.get_context(context_id)->VSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::PSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Pixel, samplers, pSS);
    if (count)
        HW.get_context(context_id)->PSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::GSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Geometry, samplers, pSS);
    if (count)
        HW.get_context(context_id)->GSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::HSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Hull, samplers, pSS);
    if (count)
        HW.get_context(context_id)->HSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::DSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Domain, samplers, pSS);
    if (count)
        HW.get_context(context_id)->DSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::CSApplySamplers(u32 context_id, HArray& samplers)
{
    ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    const u32 count = PrepareSamplerStates(context_id, ShaderStage::Compute, samplers, pSS);
    if (count)
        HW.get_context(context_id)->CSSetSamplers(0, count, pSS);
}

void dx11SamplerStateCache::SetMaxAnisotropy(u32 uiMaxAniso)
{
    clamp(uiMaxAniso, (u32)1, (u32)16);

    if (m_uiMaxAnisotropy == uiMaxAniso)
        return;

    m_uiMaxAnisotropy = uiMaxAniso;

    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        StateRecord& rec = m_StateArray[i];
        StateDecs desc = rec.m_desc;

        //	MaxAnisitropy is reset by ValidateState if not aplicable
        //	to the filter mode used.
        //	Reason: all checks for aniso applicability are done
        //	in ValidateState.
        desc.MaxAnisotropy = m_uiMaxAnisotropy;
        dx11StateUtils::ValidateState(desc);

        //	This can cause fragmentation if called too often
        rec.m_pState->Release();
        CreateState(desc, &rec.m_pState);

        // [DA_PORT] Keep the cached description AND the hash in step with the state we just rebuilt.
        // GetState() hashes with the current anisotropy applied, so leaving the old hash in place
        // made every record unmatchable after a change - the array simply grew a second copy of each
        // sampler. Anisotropy is not a rare setting for us: it is bound to the upscaler's mip bias.
        rec.m_desc = desc;
        rec.m_crc = dx11StateUtils::GetHash(desc);
    }
}

void dx11SamplerStateCache::SetMipLODBias(float uiMipLODBias)
{
    if (m_uiMipLODBias == uiMipLODBias)
        return;

    m_uiMipLODBias = uiMipLODBias;

    for (u32 i = 0; i < m_StateArray.size(); ++i)
    {
        StateRecord& rec = m_StateArray[i];
        StateDecs desc = rec.m_desc;

        desc.MipLODBias = m_uiMipLODBias;
        dx11StateUtils::ValidateState(desc);

        // This can cause fragmentation if called too often
        rec.m_pState->Release();
        CreateState(desc, &rec.m_pState);

        // [DA_PORT] See SetMaxAnisotropy: description and hash must follow the rebuilt state.
        rec.m_desc = desc;
        rec.m_crc = dx11StateUtils::GetHash(desc);
    }
}
} // namespace xray::render::RENDER_NAMESPACE
