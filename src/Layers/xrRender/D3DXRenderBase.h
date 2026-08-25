#pragma once

#include "xrEngine/Render.h"
#include "xrCDB/ISpatial.h"
#include "r__dsgraph_types.h"
#include "r__dsgraph_structure.h"
#include "r__sector.h"
#include "xr_effgamma.h"

namespace xray::render::RENDER_NAMESPACE
{
// Common part of interface implementation for all D3D renderers
class D3DXRenderBase : public IRender, public pureFrame
{
public:
    //friend class CSkeletonX; // Stats.Skinning
    //friend class CKinematics; // Stats.Animation
    RenderStatistics BasicStats;

public:
    //	Gamma correction functions
    virtual void setGamma(float fGamma) override;
    virtual void setBrightness(float fGamma) override;
    virtual void setContrast(float fGamma) override;
    virtual void updateGamma() override;

    //	Destroy
    virtual void OnDeviceDestroy(bool bKeepTextures) override;
    virtual void Destroy() override;
    virtual void Reset(SDL_Window* hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) override;

    //	Init
    virtual void ObtainRequiredWindowFlags(u32& /*windowFlags*/) override;
    virtual void SetupStates() override;
    virtual void OnDeviceCreate(const char* shName) override;
    virtual void Create(SDL_Window* hWnd, u32& dwWidth, u32& dwHeight, float& fWidth_2, float& fHeight_2) override;

    //	Overdraw
    virtual void overdrawBegin() override;
    virtual void overdrawEnd() override;
    //	Resources control
    virtual void DeferredLoad(bool E) override;
    virtual void ResourcesDeferredUpload() override;
    virtual void ResourcesDeferredUnload() override;
    virtual void ResourcesGetMemoryUsage(u32& m_base, u32& c_base, u32& m_lmaps, u32& c_lmaps) override;
    virtual void ResourcesDestroyNecessaryTextures() override;
    virtual void ResourcesStoreNecessaryTextures() override;
    virtual void ResourcesDumpMemoryUsage() override;
    //	HWSupport
    virtual bool HWSupportsShaderYUV2RGB() override;
    //	Device state
    virtual DeviceState GetDeviceState() override;
    virtual bool GetForceGPU_REF() override;
    virtual u32 GetCacheStatPolys() override;
    virtual void Begin() override;
    virtual void Clear() override;
    virtual void End() override;
    virtual void ClearTarget() override;
    virtual void SetCacheXform(Fmatrix& mView, Fmatrix& mProject) override;
    virtual void OnAssetsChanged() override;
    virtual void DumpStatistics(class IGameFont& font, class IPerformanceAlert* alert) override;

    xrImTextureData GetImGuiTextureId(pcstr texture_name) override;

    RenderContext GetCurrentContext() const override { return IRender::PrimaryContext; }
    void MakeContextCurrent(RenderContext /*context*/) override {}

    CBackend& get_imm_command_list()
    {
        return get_imm_context().cmd_list;
    }

#if RENDER != R_R1
    ICF u32 alloc_context(bool alloc_cmd_list = true)
    {
        // [DA_PORT] Свободный контекст ищем ТОЛЬКО среди параллельных. Взято у Dead-Air-Refined,
        // они нашли это раньше нас.
        //
        // Было: `if (contexts_used.all())` — проверка по ВСЕМ битам, включая бит непосредственного
        // контекста, потому что он лежит в том же массиве последним слотом
        // (`IMM_CTX_ID = R__NUM_PARALLEL_CONTEXTS`). Если этот бит в момент вызова не выставлен, а
        // все параллельные контексты уже заняты, проверка пропускает, цикл поиска добегает до конца
        // не найдя свободного, и `id` остаётся равным `R__NUM_PARALLEL_CONTEXTS` — то есть номеру
        // НЕПОСРЕДСТВЕННОГО контекста. Дальше он честно выдаётся рабочему потоку, а `reset()` перед
        // выдачей обнуляет граф основного контекста прямо посреди кадра.
        //
        // Занять все параллельные — обычное дело: солнце берёт три (по каскаду), дождь один, и оба
        // переспрашивают их каждый кадр из CRender::Calculate(); следом за контекстами приходят
        // батчи теневых ламп.
        //
        // Проявлялось как одиночные испорченные кадры: пятнадцать выбросов на три тысячи кадров,
        // свет в пикселе уходил и впятеро вверх, и втрое вниз, а следующий кадр возвращался к
        // прежнему значению. Глазом — мерцание тени на кусте при полностью неподвижной камере.
        // Отсюда же и то, что дефект не ловился ни одной настройкой рендера: он не в рендере,
        // а в раздаче контекстов.
        // Поиск оставлен обычным циклом: у конкурента здесь std::countr_zero, но <bit> в этой
        // единице трансляции недоступен, а перебор четырёх битов ничего не стоит.
        // [DA_PORT] r__max_parallel_ctx — сколько контекстов РАЗДАВАТЬ, при том что собрано их
        // R__NUM_PARALLEL_CONTEXTS. Нужна для честного сравнения: число контекстов задаётся на
        // компиляции, и без этой ручки каждый замер требовал бы полной пересборки всего движка
        // (макрос меняет размер vis_data, лежащей внутри каждого визуала).
        //
        // ⚠️ Ниже трёх опускать нельзя: три контекста забирают каскады солнца. Зажимаем снизу.
        extern int ps_da_max_parallel_ctx;
        const u32 limit = (ps_da_max_parallel_ctx <= 0)
            ? R__NUM_PARALLEL_CONTEXTS
            : u32(std::min<int>(ps_da_max_parallel_ctx, R__NUM_PARALLEL_CONTEXTS));
        const auto parallel_mask = (1ul << (limit < 4u ? 4u : limit)) - 1;
        const auto available = ~contexts_used.to_ulong() & parallel_mask;
        if (!available)
            return R_dsgraph_structure::INVALID_CONTEXT_ID;

        u32 id = 0;
        while (!(available & (1ul << id)))
            ++id;

        contexts_used.set(id, true);
        contexts_pool[id].reset();
        contexts_pool[id].context_id = id;
        contexts_pool[id].cmd_list.context_id = alloc_cmd_list ? id : CHW::IMM_CTX_ID;
        return id;
    }

    ICF R_dsgraph_structure& get_context(u32 id)
    {
        VERIFY(id < R__NUM_CONTEXTS);
        if (id == R_dsgraph_structure::IMM_CTX_ID)
        {
            return get_imm_context();
        }
        VERIFY(contexts_used.test(id));
        VERIFY(contexts_pool[id].context_id == id);
        return contexts_pool[id];
    }

    ICF void release_context(u32 id)
    {
        // [DA_PORT] Отказ настоящий, а не только под VERIFY.
        //
        // Ниже стоит `VERIFY(id != IMM_CTX_ID)`, но в релизной сборке VERIFY разворачивается в
        // пустоту, и освобождение непосредственного контекста проходило молча — снимая его бит.
        // А снятый бит и есть то условие, при котором выдача выше отдавала рабочему потоку
        // непосредственный контекст. То есть одна поломка кормила другую.
        if (id >= R__NUM_PARALLEL_CONTEXTS)
            return;

        VERIFY(id != R_dsgraph_structure::IMM_CTX_ID); // never release immediate context
        VERIFY(id < R__NUM_PARALLEL_CONTEXTS);
        VERIFY(contexts_used.test(id));
        VERIFY(contexts_pool[id].context_id != R_dsgraph_structure::INVALID_CONTEXT_ID);
        contexts_used.set(id, false);
    }

    ICF R_dsgraph_structure& get_imm_context()
    {
        auto& ctx = contexts_pool[R_dsgraph_structure::IMM_CTX_ID];
        ctx.context_id = R_dsgraph_structure::IMM_CTX_ID;
        contexts_used.set(ctx.context_id, true);
        return ctx;
    }

    ICF void cleanup_contexts()
    {
        for (int id = 0; id < R__NUM_CONTEXTS; ++id)
        {
            contexts_pool[id].reset();
        }
        contexts_used.reset();
    }

    // [DA_PORT] Счётчики отрисовки ОТДЕЛЬНОГО контекста, только для замеров.
    //
    // Зачем: показатель DIP в отчёте рендера снимается с непосредственного контекста, а теневые
    // проходы идут через параллельные -- их вызовы отрисовки в DIP не попадают вовсе. По одному
    // этому числу фаза каскадов солнца выглядела как ноль вызовов при почти миллисекунде работы
    // видеокарты, чего быть не может. Здесь счётчики доступны по номеру контекста, и картина
    // складывается целиком.
    ICF const CBackend& da_context_cmd_list(u32 id) const
    {
        VERIFY(id < R__NUM_CONTEXTS);
        return contexts_pool[id].cmd_list;
    }
#else
    ICF R_dsgraph_structure& get_imm_context()
    {
        context_imm.context_id = R_dsgraph_structure::IMM_CTX_ID;
        return context_imm;
    }

    ICF R_dsgraph_structure& get_context(u32 id)
    {
        VERIFY(id == R_dsgraph_structure::IMM_CTX_ID); // be sure R1 doesn't go crazy
        return get_imm_context();
    }

    ICF void cleanup_contexts()
    {
        context_imm.reset();
    }
#endif

    void CreateQuadIB();

public:
    CResourceManager* Resources{};
    ref_shader m_WireShader;
    ref_shader m_SelectionShader;
    ref_shader m_PortalFadeShader;
    ref_geom   m_PortalFadeGeom;

    // Dynamic geometry streams
    _VertexStream Vertex;
    _IndexStream Index;

    IndexStagingBuffer QuadIB;
    IndexBufferHandle old_QuadIB;

protected:
#if RENDER == R_R1
    R_dsgraph_structure context_imm;
#else
    R_dsgraph_structure contexts_pool[R__NUM_CONTEXTS];
    std::bitset<R__NUM_CONTEXTS> contexts_used{};
#endif
private:
    CGammaControl m_Gamma;

protected:
    bool b_loaded{};
};
} // namespace xray::render::RENDER_NAMESPACE
