// FProgressive.h: interface for the FProgressive class.
//
//////////////////////////////////////////////////////////////////////

#pragma once

#include "FVisual.h"

struct FSlideWindowItem;

namespace xray::render::RENDER_NAMESPACE
{
class FProgressive : public Fvisual
{
protected:
    FSlideWindowItem nSWI;
    FSlideWindowItem* xSWI;
    u32 last_lod;

public:
    FProgressive();
    virtual ~FProgressive();

    // [DA_PORT] Диапазон индексов, который РЕАЛЬНО рисуется при данном LOD.
    //
    // Зачем понадобилось: разбор очереди отрисовки оценивал, сколько вызовов сложилось бы
    // объединением соседних диапазонов, и брал постоянные iBase/iCount меша. Для прогрессивной
    // геометрии это неверно -- Render выбирает окно nSWI по LOD, и у двух соседних объектов с
    // разным LOD диапазоны не стыкуются, хотя по постоянным полям выглядели бы встык. Оценка по
    // ним завышала склейку там, где она самая заметная.
    //
    // Считается тем же выражением, что и в Render, ветка без быстрой геометрии (основной проход
    // рисует именно ею). Состояние last_lod не трогается: это запрос, а не отрисовка.
    void da_lod_window(float LOD, u32& out_ibase, u32& out_tris) const;
    virtual void Render(CBackend& cmd_list, float LOD, bool use_fast_geo) override; // LOD - Level Of Detail  [0.0f - min, 1.0f - max], -1 = Ignored
    virtual void Load(const char* N, IReader* data, u32 dwFlags);
    virtual void Copy(dxRender_Visual* pFrom);
    virtual void Release();

private:
    FProgressive(const FProgressive& other);
    void operator=(const FProgressive& other);
};
} // namespace xray::render::RENDER_NAMESPACE
