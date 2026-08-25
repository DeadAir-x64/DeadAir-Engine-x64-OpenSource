#pragma once

#include "FBasicVisual.h"

struct FSlideWindowItem;

namespace xray::render::RENDER_NAMESPACE
{
#ifdef USE_DX11
// [DA_PORT] Девять векторов на дерево — ровно столько, сколько у него своего: мировая и видовая
// матрицы (по три вектора) плюс масштаб, смещение и солнце. Ветер и волна общие, они уходят
// обычными константами один раз на пачку.
//
// ⚠️ Число и порядок обязаны совпадать с tree_instance.h. Не совпадут — шейдер молча прочитает
// не те числа, и деревья разъедутся без единого сообщения.
constexpr u32 FTreeVisualInstanceVectorCount = 9;

struct FTreeVisualInstanceData
{
    Fvector4 vectors[FTreeVisualInstanceVectorCount];
};
static_assert(sizeof(FTreeVisualInstanceData) == FTreeVisualInstanceVectorCount * sizeof(Fvector4));

// [DA_PORT] Всё, что должно совпасть у двух деревьев, чтобы их можно было нарисовать одним вызовом.
struct FTreeVisualInstancedDraw
{
    SGeometry* geometry{};
    u32 base_vertex{};
    u32 vertex_count{};
    u32 start_index{};
    u32 primitive_count{};
};
#endif

class FTreeVisual : public dxRender_Visual, public IRender_Mesh
{
private:
    struct _5color
    {
        Fvector rgb; // - all static lighting
        float hemi; // - hemisphere
        float sun; // - sun
    };

protected:
    _5color c_scale;
    _5color c_bias;
    Fmatrix xform;

#ifdef USE_DX11
    // [DA_PORT] Кэш для FillInstanceData — тот же приём, что вывел траву на dt_rend 0.58→0.43 мс
    // (см. [[grass-submission-cost]]). xform выставляется РАЗ в Load() и не меняется никогда;
    // vectors[0..2] от него — чистое копирование полей, без счёта, кэшируем без инвалидации вовсе.
    // vectors[6..8] зависят от ps_r__Tree_SBC (крутится игроком редко) — инвалидация сравнением с
    // прошлым значением множителя. vectors[3..5] (mul_43 с матрицей вида) кэшу не подлежат — камера
    // движется каждый кадр, это и есть единственная настоящая работа, что здесь осталась.
    mutable Fvector4 m_da_cached_world[3];
    mutable bool m_da_cached_world_valid{false};
    mutable Fvector4 m_da_cached_light[3];
    mutable float m_da_cached_sbc{-1.f};
#endif

public:
    virtual void Render(CBackend& cmd_list, float LOD, bool use_fast_geo) override; // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
#ifdef USE_DX11
    virtual bool GetInstancedDraw(float LOD, FTreeVisualInstancedDraw& draw); // [DA_PORT] инстансинг
    void FillInstanceData(CBackend& cmd_list, FTreeVisualInstanceData& data) const;
    static void SetupInstancedGlobals(CBackend& cmd_list);
#endif
    virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
    virtual void Copy(dxRender_Visual* pFrom);
    virtual void Release();

    FTreeVisual(void);
    virtual ~FTreeVisual(void);
};

class FTreeVisual_ST : public FTreeVisual
{
    typedef FTreeVisual inherited;

public:
    FTreeVisual_ST(void);
    virtual ~FTreeVisual_ST(void);

    virtual void Render(CBackend& cmd_list, float LOD, bool use_fast_geo) override; // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
#ifdef USE_DX11
    virtual bool GetInstancedDraw(float LOD, FTreeVisualInstancedDraw& draw) override; // [DA_PORT]
#endif
    virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
    virtual void Copy(dxRender_Visual* pFrom);
    virtual void Release();

private:
    FTreeVisual_ST(const FTreeVisual_ST& other);
    void operator=(const FTreeVisual_ST& other);
};

class FTreeVisual_PM : public FTreeVisual
{
    typedef FTreeVisual inherited;

private:
    FSlideWindowItem* pSWI;
    u32 last_lod;
    u32 SelectLOD(float LOD); // [DA_PORT] нужен пакетной отрисовке отдельно от Render

public:
    FTreeVisual_PM(void);
    virtual ~FTreeVisual_PM(void);

    virtual void Render(CBackend& cmd_list, float LOD, bool use_fast_geo) override; // LOD - Level Of Detail  [0.0f - min, 1.0f - max], Ignored
#ifdef USE_DX11
    virtual bool GetInstancedDraw(float LOD, FTreeVisualInstancedDraw& draw) override; // [DA_PORT]
#endif
    virtual void Load(LPCSTR N, IReader* data, u32 dwFlags);
    virtual void Copy(dxRender_Visual* pFrom);
    virtual void Release();

private:
    FTreeVisual_PM(const FTreeVisual_PM& other);
    void operator=(const FTreeVisual_PM& other);
};

const int FTreeVisual_tile = 16;
const int FTreeVisual_quant = 32768 / FTreeVisual_tile;
} // namespace xray::render::RENDER_NAMESPACE
