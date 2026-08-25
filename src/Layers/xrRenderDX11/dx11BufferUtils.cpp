#include "stdafx.h"
#include "Layers/xrRender/BufferUtils.h"

#include <FlexibleVertexFormat.h>

namespace xray::render::RENDER_NAMESPACE
{
u32 GetFVFVertexSize(u32 FVF)
{
    return static_cast<u32>(::FVF::ComputeVertexSize(FVF));
}

u32 GetDeclVertexSize(const VertexElement* decl, u32 Stream)
{
    return static_cast<u32>(::FVF::ComputeVertexSize(decl, Stream));
}

u32 GetDeclLength(const VertexElement* decl)
{
    return static_cast<u32>(::FVF::GetDeclLength(decl));
}

static HRESULT CreateBuffer(ID3DBuffer** ppBuffer, const void* pData, u32 dataSize,
    bool bDynamic, D3D_BIND_FLAG bufferType, bool shaderReadable = false)
{
    // [DA_PORT] Сырое представление требует размер, кратный четырём, и просить его можно только на
    // создании. Некратный размер (индексный буфер с нечётным числом индексов) — не ошибка вызова, а
    // штатный случай: молча отказываемся от чтения из шейдера, буфер создаётся как обычно.
    if (shaderReadable && (dataSize % 4) != 0)
        shaderReadable = false;

    D3D_BUFFER_DESC desc;
    desc.ByteWidth      = dataSize;
    desc.Usage          = bDynamic ? D3D_USAGE_DYNAMIC : D3D_USAGE_DEFAULT;
    desc.BindFlags      = shaderReadable ? (bufferType | D3D11_BIND_SHADER_RESOURCE) : bufferType;
    desc.CPUAccessFlags = bDynamic ? D3D_CPU_ACCESS_WRITE : 0;
    desc.MiscFlags      = shaderReadable ? D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS : 0;

    D3D_SUBRESOURCE_DATA subData;
    subData.pSysMem = pData;

    HRESULT res = HW.pDevice->CreateBuffer(
        &desc,
        pData ? &subData : nullptr,
        ppBuffer);

    return res;
}

static inline HRESULT CreateVertexBuffer(
    VertexBufferHandle* ppBuffer, const void* pData, u32 dataSize, bool bDynamic, bool shaderReadable = false)
{
    return CreateBuffer(ppBuffer, pData, dataSize, bDynamic, D3D_BIND_VERTEX_BUFFER, shaderReadable);
}

static inline HRESULT CreateIndexBuffer(
    IndexBufferHandle* ppBuffer, const void* pData, u32 dataSize, bool bDynamic, bool shaderReadable = false)
{
    return CreateBuffer(ppBuffer, pData, dataSize, bDynamic, D3D_BIND_INDEX_BUFFER, shaderReadable);
}

// [DA_PORT] Сырое (ByteAddressBuffer) представление на уже созданный буфер геометрии.
//
// Формат ОБЯЗАН быть R32_TYPELESS с флагом RAW: структурное представление здесь не годится, потому
// что тот же буфер одновременно привязан как вершинный, а структурное с этим несовместимо. В шейдере
// это ByteAddressBuffer, читается Load/Load4 по БАЙТОВОМУ смещению — отсюда и точная раскладка полей
// вершины в 05_ROADMAP.md: ошибка в смещении прочитает не те байты молча.
static BufferSRVHandle CreateRawSRV(ID3DBuffer* pBuffer, u32 dataSize)
{
    if (!pBuffer || (dataSize % 4) != 0)
        return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = DXGI_FORMAT_R32_TYPELESS;
    desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    desc.BufferEx.FirstElement = 0;
    desc.BufferEx.NumElements = dataSize / 4;
    desc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

    BufferSRVHandle srv = nullptr;
    const HRESULT hr = HW.pDevice->CreateShaderResourceView(pBuffer, &desc, &srv);
    if (FAILED(hr))
    {
        // Не фатально: путь выборки из шейдера просто не включится для этого буфера.
        Msg("! [DA_PORT] сырое представление буфера не создано (0x%08x), размер %u", u32(hr), dataSize);
        return nullptr;
    }
    return srv;
}

static inline HRESULT CreateConstantBuffer(ConstantBufferHandle* ppBuffer, u32 dataSize)
{
    return CreateBuffer(ppBuffer, nullptr, dataSize, true, D3D_BIND_CONSTANT_BUFFER);
}

namespace BufferUtils
{
// TODO: replace by streaming buffer instance in `dx11ConstantBuffer`
HRESULT CreateConstantBuffer(ConstantBufferHandle* ppBuffer, u32 DataSize)
{
    return RENDER_NAMESPACE::CreateConstantBuffer(ppBuffer, DataSize);
}
};

struct VertexFormatPairs
{
    D3DDECLTYPE m_dx9FMT;
    DXGI_FORMAT m_dx11FMT;
};

VertexFormatPairs VertexFormatList[] = {{D3DDECLTYPE_FLOAT1, DXGI_FORMAT_R32_FLOAT},
    {D3DDECLTYPE_FLOAT2, DXGI_FORMAT_R32G32_FLOAT}, {D3DDECLTYPE_FLOAT3, DXGI_FORMAT_R32G32B32_FLOAT},
    {D3DDECLTYPE_FLOAT4, DXGI_FORMAT_R32G32B32A32_FLOAT},
    {D3DDECLTYPE_D3DCOLOR,
        DXGI_FORMAT_R8G8B8A8_UNORM}, // Warning. Explicit RGB component swizzling is nesessary	//	Not available
    {D3DDECLTYPE_UBYTE4, DXGI_FORMAT_R8G8B8A8_UINT}, // Note: Shader gets UINT values, but if Direct3D 9 style integral
    // floats are needed (0.0f, 1.0f... 255.f), UINT can just be converted
    // to float32 in shader.
    {D3DDECLTYPE_SHORT2,
        DXGI_FORMAT_R16G16_SINT}, // Note: Shader gets SINT values, but if Direct3D 9 style integral floats
    // are needed, SINT can just be converted to float32 in shader.
    {D3DDECLTYPE_SHORT4,
        DXGI_FORMAT_R16G16B16A16_SINT}, // Note: Shader gets SINT values, but if Direct3D 9 style integral
    // floats are needed, SINT can just be converted to float32 in
    // shader.
    {D3DDECLTYPE_UBYTE4N, DXGI_FORMAT_R8G8B8A8_UNORM},
    {D3DDECLTYPE_SHORT2N, DXGI_FORMAT_R16G16_SNORM}, {D3DDECLTYPE_SHORT4N, DXGI_FORMAT_R16G16B16A16_SNORM},
    {D3DDECLTYPE_USHORT2N, DXGI_FORMAT_R16G16_UNORM}, {D3DDECLTYPE_USHORT4N, DXGI_FORMAT_R16G16B16A16_UNORM},
    // D3DDECLTYPE_UDEC3 Not available
    // D3DDECLTYPE_DEC3N Not available
    {D3DDECLTYPE_FLOAT16_2, DXGI_FORMAT_R16G16_FLOAT}, {D3DDECLTYPE_FLOAT16_4, DXGI_FORMAT_R16G16B16A16_FLOAT}};

DXGI_FORMAT ConvertVertexFormat(D3DDECLTYPE dx9FMT)
{
    size_t arrayLength = sizeof(VertexFormatList) / sizeof(VertexFormatList[0]);
    for (size_t i = 0; i < arrayLength; ++i)
    {
        if (VertexFormatList[i].m_dx9FMT == dx9FMT)
            return VertexFormatList[i].m_dx11FMT;
    }

    VERIFY(!"ConvertVertexFormat didn't find appropriate dx11 vertex format!");
    return DXGI_FORMAT_UNKNOWN;
}

struct VertexSemanticPairs
{
    D3DDECLUSAGE m_dx9Semantic;
    LPCSTR m_dx11Semantic;
};

VertexSemanticPairs VertexSemanticList[] = {
    {D3DDECLUSAGE_POSITION, "POSITION"}, //	0
    {D3DDECLUSAGE_BLENDWEIGHT, "BLENDWEIGHT"}, // 1
    {D3DDECLUSAGE_BLENDINDICES, "BLENDINDICES"}, // 2
    {D3DDECLUSAGE_NORMAL, "NORMAL"}, // 3
    {D3DDECLUSAGE_PSIZE, "PSIZE"}, // 4
    {D3DDECLUSAGE_TEXCOORD, "TEXCOORD"}, // 5
    {D3DDECLUSAGE_TANGENT, "TANGENT"}, // 6
    {D3DDECLUSAGE_BINORMAL, "BINORMAL"}, // 7
    // D3DDECLUSAGE_TESSFACTOR,    // 8
    {D3DDECLUSAGE_POSITIONT, "POSITIONT"}, // 9
    {D3DDECLUSAGE_COLOR, "COLOR"}, // 10
    // D3DDECLUSAGE_FOG,           // 11
    // D3DDECLUSAGE_DEPTH,         // 12
    // D3DDECLUSAGE_SAMPLE,        // 13
};

LPCSTR ConvertSemantic(D3DDECLUSAGE Semantic)
{
    size_t arrayLength = sizeof(VertexSemanticList) / sizeof(VertexSemanticList[0]);
    for (size_t i = 0; i < arrayLength; ++i)
    {
        if (VertexSemanticList[i].m_dx9Semantic == Semantic)
            return VertexSemanticList[i].m_dx11Semantic;
    }

    VERIFY(!"ConvertSemantic didn't find appropriate dx11 input semantic!");
    return 0;
}

void ConvertVertexDeclaration(const xr_vector<D3DVERTEXELEMENT9>& declIn, xr_vector<D3D_INPUT_ELEMENT_DESC>& declOut)
{
    s32 iDeclSize = declIn.size() - 1;
    declOut.resize(iDeclSize + 1);

    for (s32 i = 0; i < iDeclSize; ++i)
    {
        const D3DVERTEXELEMENT9& descIn = declIn[i];
        D3D_INPUT_ELEMENT_DESC& descOut = declOut[i];

        descOut.SemanticName = ConvertSemantic((D3DDECLUSAGE)descIn.Usage);
        descOut.SemanticIndex = descIn.UsageIndex;
        descOut.Format = ConvertVertexFormat((D3DDECLTYPE)descIn.Type);
        descOut.InputSlot = descIn.Stream;
        descOut.AlignedByteOffset = descIn.Offset;
        descOut.InputSlotClass = D3D_INPUT_PER_VERTEX_DATA;
        descOut.InstanceDataStepRate = 0;
    }

    if (iDeclSize >= 0)
        ZeroMemory(&declOut[iDeclSize], sizeof(declOut[iDeclSize]));
}

//-----------------------------------------------------------------------------
VertexStagingBuffer::~VertexStagingBuffer()
{
    Destroy();
}

void VertexStagingBuffer::Create(size_t size, bool allowReadBack /*= false*/, bool shaderReadable /*= false*/)
{
    m_Size = size;
    m_AllowReadBack = allowReadBack;
    m_ShaderReadable = shaderReadable;

    m_HostBuffer = xr_alloc<u8>(size);
    AddRef();
}

bool VertexStagingBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

void* VertexStagingBuffer::Map(
    size_t offset /*= 0*/,
    size_t size /*= 0*/,
    bool read /*= false*/)
{
    VERIFY2(m_HostBuffer, "Buffer wasn't created or already discarded");
    VERIFY2(!read || m_AllowReadBack, "Can't read from write only buffer");
    VERIFY2((size + offset) <= m_Size, "Map region is too large");

    return static_cast<u8*>(m_HostBuffer) + offset;
}

void VertexStagingBuffer::Unmap(bool doFlush /*= false*/)
{
    if (!doFlush)
    {
        /* Do nothing*/
        return;
    }

    VERIFY2(!m_DeviceBuffer, "Attempting to upload buffer twice");
    VERIFY(m_HostBuffer && m_Size);

    // Upload data to device
    R_CHK(CreateVertexBuffer(&m_DeviceBuffer, m_HostBuffer, m_Size, false, m_ShaderReadable));
    VERIFY(m_DeviceBuffer);
    HW.stats_manager.increment_stats_vb(m_DeviceBuffer);
    if (m_ShaderReadable)
        m_SRV = CreateRawSRV(m_DeviceBuffer, u32(m_Size)); // [DA_PORT] выборка вершин из шейдера

    if (!m_AllowReadBack)
    {
        // Cache buffer isn't required anymore. Free host memory
        DiscardHostBuffer();
    }
}

VertexBufferHandle VertexStagingBuffer::GetBufferHandle() const
{
    return m_DeviceBuffer;
}

void VertexStagingBuffer::Destroy()
{
    DiscardHostBuffer();
    m_Size = 0;

    _RELEASE(m_SRV); // [DA_PORT] представление держит ссылку на буфер -- отпускать ПЕРВЫМ
    HW.stats_manager.decrement_stats_vb(m_DeviceBuffer);
    _RELEASE(m_DeviceBuffer);
}

void VertexStagingBuffer::DiscardHostBuffer()
{
    if (m_HostBuffer)
        xr_free(m_HostBuffer);
}

size_t VertexStagingBuffer::GetSystemMemoryUsage() const
{
    return m_HostBuffer ? m_Size : 0;
}

size_t VertexStagingBuffer::GetVideoMemoryUsage() const
{
    if (m_DeviceBuffer)
    {
        D3D_BUFFER_DESC desc;
        m_DeviceBuffer->GetDesc(&desc);
        return desc.ByteWidth;
    }

    return 0;
}

//-----------------------------------------------------------------------------
IndexStagingBuffer::~IndexStagingBuffer()
{
    Destroy();
}

void IndexStagingBuffer::Create(
    size_t size, bool allowReadBack /*= false*/, bool /*managed = true*/, bool shaderReadable /*= false*/)
{
    m_ShaderReadable = shaderReadable;
    m_Size = size;
    m_AllowReadBack = allowReadBack;

    m_HostBuffer = xr_alloc<u8>(size);
    AddRef();
}

bool IndexStagingBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

void* IndexStagingBuffer::Map(
    size_t offset /*= 0*/,
    size_t size /*= 0*/,
    bool read /*= false*/)
{
    VERIFY2(m_HostBuffer, "Buffer wasn't created or already discarded");
    VERIFY2(!read || m_AllowReadBack, "Can't read from write only buffer");
    VERIFY2((size + offset) <= m_Size, "Map region is too large");

    return static_cast<u8*>(m_HostBuffer) + offset;
}

void IndexStagingBuffer::Unmap(bool doFlush /*= false*/)
{
    if (!doFlush)
    {
        /* Do nothing*/
        return;
    }

    VERIFY2(!m_DeviceBuffer, "Attempting to upload buffer twice");
    VERIFY(m_HostBuffer && m_Size);

    // Upload data to device
    R_CHK(CreateIndexBuffer(&m_DeviceBuffer, m_HostBuffer, m_Size, false, m_ShaderReadable));
    if (m_ShaderReadable)
        m_SRV = CreateRawSRV(m_DeviceBuffer, u32(m_Size)); // [DA_PORT] выборка индексов из шейдера
    VERIFY(m_DeviceBuffer);
    HW.stats_manager.increment_stats_ib(m_DeviceBuffer);

    if (!m_AllowReadBack)
    {
        // Cache buffer isn't required anymore. Free host memory
        DiscardHostBuffer();
    }
}

IndexBufferHandle IndexStagingBuffer::GetBufferHandle() const
{
    return m_DeviceBuffer;
}

void IndexStagingBuffer::Destroy()
{
    _RELEASE(m_SRV); // [DA_PORT] см. VertexStagingBuffer::Destroy
    DiscardHostBuffer();
    m_Size = 0;

    HW.stats_manager.decrement_stats_ib(m_DeviceBuffer);
    _RELEASE(m_DeviceBuffer);
}

void IndexStagingBuffer::DiscardHostBuffer()
{
    if (m_HostBuffer)
        xr_free(m_HostBuffer);
}

size_t IndexStagingBuffer::GetSystemMemoryUsage() const
{
    return m_HostBuffer ? m_Size : 0;
}

size_t IndexStagingBuffer::GetVideoMemoryUsage() const
{
    if (m_DeviceBuffer)
    {
        D3D_BUFFER_DESC desc;
        m_DeviceBuffer->GetDesc(&desc);
        return desc.ByteWidth;
    }

    return 0;
}

//-----------------------------------------------------------------------------
VertexStreamBuffer::~VertexStreamBuffer()
{
    Destroy();
}

void VertexStreamBuffer::Create(size_t size)
{
    R_CHK(CreateVertexBuffer(&m_DeviceBuffer, nullptr, size, true));
    VERIFY(m_DeviceBuffer);
    AddRef();
    HW.stats_manager.increment_stats_vb(m_DeviceBuffer);
}

void VertexStreamBuffer::Destroy()
{
    if (m_DeviceBuffer == nullptr)
        return;

    HW.stats_manager.decrement_stats_vb(m_DeviceBuffer);
    _RELEASE(m_DeviceBuffer);
}

void* VertexStreamBuffer::Map(size_t offset, size_t /*size*/, bool flush /*= false*/) // TODO: this should be moved into backend
{
    VERIFY(m_DeviceBuffer);

    const auto flag = flush ? D3D_MAP_WRITE_DISCARD : D3D_MAP_WRITE_NO_OVERWRITE;

    D3D11_MAPPED_SUBRESOURCE MappedSubRes;
    HW.get_context(CHW::IMM_CTX_ID)->Map(m_DeviceBuffer, 0, flag, 0, &MappedSubRes); // TODO: proper context id + check for flush & imm

    u8* pData = static_cast<u8*>(MappedSubRes.pData);
    pData += offset;

    return static_cast<void*>(pData);
}

void VertexStreamBuffer::Unmap() // TODO: this should be moved into backend
{
    VERIFY(m_DeviceBuffer);
    HW.get_context(CHW::IMM_CTX_ID)->Unmap(m_DeviceBuffer, 0); // TODO: proper context id
}

bool VertexStreamBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}

//-----------------------------------------------------------------------------
IndexStreamBuffer::~IndexStreamBuffer()
{
    Destroy();
}

void IndexStreamBuffer::Create(size_t size)
{
    R_CHK(CreateIndexBuffer(&m_DeviceBuffer, nullptr, size, true));
    VERIFY(m_DeviceBuffer);
    AddRef();
    HW.stats_manager.increment_stats_ib(m_DeviceBuffer);
}

void IndexStreamBuffer::Destroy()
{
    if (m_DeviceBuffer == nullptr)
        return;

    HW.stats_manager.decrement_stats_ib(m_DeviceBuffer);
    _RELEASE(m_DeviceBuffer);
}

void* IndexStreamBuffer::Map(size_t offset, size_t /*size*/, bool flush /*= false*/)
{
    VERIFY(m_DeviceBuffer);

    const auto flag = flush ? D3D_MAP_WRITE_DISCARD : D3D_MAP_WRITE_NO_OVERWRITE;

    D3D11_MAPPED_SUBRESOURCE MappedSubRes;
    HW.get_context(CHW::IMM_CTX_ID)->Map(m_DeviceBuffer, 0, flag, 0, &MappedSubRes); // TODO: see above comms for vertex

    u8* pData = static_cast<u8*>(MappedSubRes.pData);
    pData += offset;

    return static_cast<void*>(pData);
}

void IndexStreamBuffer::Unmap()
{
    VERIFY(m_DeviceBuffer);
    HW.get_context(CHW::IMM_CTX_ID)->Unmap(m_DeviceBuffer, 0); // TODO: see above comms for vertex
}

bool IndexStreamBuffer::IsValid() const
{
    return !!m_DeviceBuffer;
}
} // namespace xray::render::RENDER_NAMESPACE
