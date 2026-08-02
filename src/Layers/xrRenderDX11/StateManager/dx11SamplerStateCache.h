#pragma once

namespace xray::render::RENDER_NAMESPACE
{
class dx11SamplerStateCache
{
public:
    enum
    {
        hInvalidHandle = 0xFFFFFFFF
    };

    //	State handle
    typedef u32 SHandle;
    typedef xr_vector<SHandle> HArray;

public:
    dx11SamplerStateCache();
    ~dx11SamplerStateCache();

    void ClearStateArray();

    SHandle GetState(D3D_SAMPLER_DESC& desc);

    void VSApplySamplers(u32 context_id, HArray& samplers);
    void PSApplySamplers(u32 context_id, HArray& samplers);
    void GSApplySamplers(u32 context_id, HArray& samplers);
    void HSApplySamplers(u32 context_id, HArray& samplers);
    void DSApplySamplers(u32 context_id, HArray& samplers);
    void CSApplySamplers(u32 context_id, HArray& samplers);

    void SetMaxAnisotropy(u32 uiMaxAniso);
    void SetMipLODBias(float uiMipLODBias);

private:
    typedef ID3DSamplerState IDeviceState;
    typedef D3D_SAMPLER_DESC StateDecs;

    struct StateRecord
    {
        u32 m_crc;
        IDeviceState* m_pState;
        // [DA_PORT] Keep the description alongside the state. FindState used to ask D3D for it
        // (m_pState->GetDesc) on every crc match, i.e. a driver round-trip inside a lookup that
        // runs for each shader bind. Spotted in Dead Air: Refined
        // (github.com/MMadmer/Dead-Air-Refined), MIT like the rest of OpenXRay.
        StateDecs m_desc;
    };

private:
    void CreateState(StateDecs desc, IDeviceState** ppIState);
    SHandle FindState(const StateDecs& desc, u32 StateCRC);

    // [DA_PORT] Returns how many slots actually have to be handed to D3D: the ones this shader uses,
    // plus whatever the previous bind on this context/stage left behind so those get cleared. The
    // engine used to set all 16 every time regardless.
    enum class ShaderStage : u32
    {
        Vertex = 0,
        Pixel,
        Geometry,
        Hull,
        Domain,
        Compute,
        COUNT
    };

    u32 PrepareSamplerStates(u32 context_id, ShaderStage stage, HArray& samplers,
        ID3DSamplerState* pSS[D3D_COMMONSHADER_SAMPLER_SLOT_COUNT]);

    //	Private data
private:
    //	This must be cleared on device destroy
    xr_vector<StateRecord> m_StateArray;

    // [DA_PORT] How many sampler slots the previous bind occupied, per context and shader stage.
    u8 m_boundSamplerCounts[R__NUM_CONTEXTS][static_cast<u32>(ShaderStage::COUNT)]{};

    u32 m_uiMaxAnisotropy;
    float m_uiMipLODBias;
};

extern dx11SamplerStateCache SSManager;
} // namespace xray::render::RENDER_NAMESPACE
