#pragma once

// [DA_PORT] NVIDIA DLSS — третий временной апскейлер, рядом с FSR 2 и XeSS.
//
// Зачем, если FSR 2 уже работает: XeSS на DX11 живёт только на Intel Arc (см. da_xess.cpp), то есть
// универсальный апскейлер у нас ровно один. DLSS закрывает владельцев RTX — а их среди играющих
// заметно больше, чем владельцев Arc.
//
// ЧЕМ ЭТОТ БЭКЕНД ОТЛИЧАЕТСЯ ОТ ДВУХ ДРУГИХ
//
// NGX нельзя слинковать нашим тулчейном вообще: NVIDIA отдаёт статические библиотеки MSVC, и MinGW
// упирается в 77-123 неразрешённых символа MSVC-STL и vcruntime. Поэтому NGX живёт в отдельной
// da_ngx.dll (Externals/nvngx_shim), собранной MSVC, а здесь — только загрузка её по имени и вызовы
// через плоский C. Подробности и полный разбор — в Externals/nvngx_shim/da_ngx_api.h.
//
// Библиотека грузится ЧЕРЕЗ GetProcAddress, а не линкуется. Это принципиально: если da_ngx.dll
// потеряется или её съест антивирус, игра останется без DLSS и продолжит работать. При линковке на
// этапе загрузки пропавший файл убил бы весь модуль рендера, то есть чёрный экран вместо игры.
//
// Входы те же, что у FSR 2 и XeSS — ради этого и делались честные векторы движения. Читать перед
// правкой: docs/09_UPSCALERS.md и заметку про молчаливые отказы рендера.

#include <nvngx_shim/da_ngx_api.h>

namespace xray::render::RENDER_NAMESPACE
{
class da_dlss
{
public:
    struct init_params
    {
        // ⚠️ Размер рендера обязан быть РОВНО тем, в котором рисуется сцена (Device.dwRenderWidth),
        // а НЕ тем, что NGX называет оптимальным для этого режима качества. Числа не совпадают:
        // масштаб рендера задаётся целыми процентами, и на 2560 «качество» даёт 1715 против
        // оптимальных 1707. Фича, созданная под 1707, получала бы кадр шире себя.
        u32 render_width{};
        u32 render_height{};
        u32 display_width{};
        u32 display_height{};
        u32 quality{}; // r__dlss, те же пять ступеней, что у остальных
        ID3D11Device* device{};
    };

    struct draw_params
    {
        ID3D11DeviceContext* context{};

        ID3D11Resource* colour{};   // сцена в разрешении рендера, HDR, до тонемапа
        ID3D11Resource* depth{};    // настоящий буфер глубины, НЕ rt_Position
        ID3D11Resource* velocity{}; // rt_Velocity, векторы движения в NDC
        ID3D11Resource* output{};   // разрешение экрана, с неупорядоченным доступом
        ID3D11Resource* reactive{}; // NVIDIA зовёт её маской смещения цвета — текстура та же

        u32 render_width{};
        u32 render_height{};

        // В ПИКСЕЛЯХ разрешения рендера, ровно как выдаёт CCameraManager.
        float jitter_x{};
        float jitter_y{};

        bool reset{}; // выбросить историю: загрузка уровня, телепорт, склейка камеры
    };

    ~da_dlss() { destroy(); }

    bool create(const init_params& p);
    void destroy();
    bool draw(const draw_params& p);

    bool ready() const { return m_created; }

    // Поддерживает ли машина DLSS вообще. Осмысленно только после успешного create.
    static bool supported();

    // Разрешение рендера для выбранного режима качества.
    static void render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h);

    // [DA_PORT] Перевод векторов из NDC в пиксели. ОДНО место на весь порт: замер и отрисовка обязаны
    // считать одинаково, иначе лог отчитывается об одних числах, а в NGX уходят другие — на этом мы
    // уже потеряли заход.
    static void mv_scale_for(u32 render_w, u32 render_h, float& out_x, float& out_y);

private:
    bool m_created{};
    ID3D11Device* m_device{};
};

extern da_dlss g_da_dlss;
} // namespace xray::render::RENDER_NAMESPACE
