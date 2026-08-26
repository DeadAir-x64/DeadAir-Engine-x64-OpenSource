// [DA_PORT] Сторона MSVC: линкует NGX по-родному, наружу отдаёт плоский C (см. da_ngx_api.h).
//
// Собирается build.bat. Пересобирать нужно ТОЛЬКО при правке этого файла - готовая da_ngx.dll лежит
// рядом в репозитории, поэтому обычная сборка игры (_build_fast.sh, MinGW) о MSVC ничего не знает.

#include <windows.h>
#include <d3d11.h>
#include <cstdio>
#include <cwchar>

#include "nvsdk_ngx.h"
#include "nvsdk_ngx_helpers.h"

#include "da_ngx_api.h"

namespace
{
da_ngx_log_fn g_log = nullptr;
NVSDK_NGX_Parameter* g_caps = nullptr; // параметры возможностей, живут между init и shutdown
NVSDK_NGX_Handle* g_feature = nullptr;
bool g_inited = false;

void say(const char* fmt, ...)
{
    if (!g_log)
        return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, args);
    va_end(args);
    g_log(buf);
}

// Лог самой NGX. Включён всегда: три причины «DLSS недоступен» он называет прямым текстом, а без
// него они неотличимы друг от друга и выглядят как молчаливый отказ.
void NVSDK_CONV ngx_log(const char* message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature)
{
    if (g_log && message)
        g_log(message);
}

NVSDK_NGX_PerfQuality_Value quality_for(int quality)
{
    // Те же пять ступеней, что у FSR 2 и XeSS, в том же порядке: от качества к скорости.
    //
    // ⛔ NVSDK_NGX_PerfQuality_Value_UltraQuality НЕ ИСПОЛЬЗОВАТЬ. Он объявлен в заголовках,NGX
    // отвечает на него осмысленным разрешением в GET_OPTIMAL_SETTINGS, и его же перечисляет в своём
    // логе среди пресетов — но создание фичи с ним падает с 0xBAD00010 (FAIL_UnsupportedParameter).
    // Замерено перебором всех шести значений на живом устройстве: отвергается ровно это одно.
    //
    // Проверка «спросить оптимальный размер» такую поломку НЕ ловит: там он проходит.
    //
    // Ступень 1 поэтому отдаётся в MaxQuality. Потери нет: на оба значения NGX возвращает одно и то
    // же разрешение (1.5x), то есть отдельным режимом он ультра-качество всё равно не считает.
    switch (quality)
    {
    case 1: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case 2: return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case 3: return NVSDK_NGX_PerfQuality_Value_Balanced;
    case 4: return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case 5: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    default: return NVSDK_NGX_PerfQuality_Value_DLAA; // 1.0x, реконструкция в родном разрешении
    }
}
} // namespace

extern "C" {

int da_ngx_abi_version(void) { return DA_NGX_ABI_VERSION; }

void da_ngx_set_log(da_ngx_log_fn fn) { g_log = fn; }

int da_ngx_init(void* d3d11_device, const wchar_t* data_dir)
{
    if (g_inited)
        return 1;
    if (!d3d11_device || !data_dir || !*data_dir)
        return 0;

    const wchar_t* paths[] = { data_dir };

    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = paths;
    info.PathListInfo.Length = 1;
    info.LoggingInfo.LoggingCallback = &ngx_log;
    info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON;
    info.LoggingInfo.DisableOtherLoggingSinks = true;

    // Init_with_ProjectID, а не числовой AppId. С произвольным числовым идентификатором NGX
    // возвращает «успех» и при этом сообщает, что DLSS недоступен - отказ без единой жалобы.
    // GUID проверяется посимвольно: любая не-шестнадцатеричная буква и инициализация не пройдёт.
    NVSDK_NGX_Result r = NVSDK_NGX_D3D11_Init_with_ProjectID(
        "a1b2c3d4-5e6f-4a0b-9c8d-7e6f5a4b3c2d", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "1.0", data_dir,
        static_cast<ID3D11Device*>(d3d11_device), &info, NVSDK_NGX_Version_API);

    if (NVSDK_NGX_FAILED(r))
    {
        say("! [DLSS] NGX init failed (0x%08X)", unsigned(r));
        return 0;
    }

    r = NVSDK_NGX_D3D11_GetCapabilityParameters(&g_caps);
    if (NVSDK_NGX_FAILED(r) || !g_caps)
    {
        say("! [DLSS] cannot read capability parameters (0x%08X)", unsigned(r));
        NVSDK_NGX_D3D11_Shutdown1(static_cast<ID3D11Device*>(d3d11_device));
        g_caps = nullptr;
        return 0;
    }

    g_inited = true;
    return 1;
}

void da_ngx_shutdown(void* d3d11_device)
{
    da_ngx_destroy(nullptr);
    if (g_caps)
    {
        NVSDK_NGX_D3D11_DestroyParameters(g_caps);
        g_caps = nullptr;
    }
    if (g_inited)
    {
        NVSDK_NGX_D3D11_Shutdown1(static_cast<ID3D11Device*>(d3d11_device));
        g_inited = false;
    }
}

int da_ngx_available(void)
{
    if (!g_inited || !g_caps)
        return 0;

    int available = 0;
    g_caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!available)
    {
        int needs_driver = 0;
        g_caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needs_driver);
        if (needs_driver)
            say("* [DLSS] the driver is too old for this DLSS version - update it");
        else
            say("* [DLSS] not supported by this GPU");
    }
    return available;
}

int da_ngx_optimal_size(int quality, unsigned display_w, unsigned display_h, unsigned* out_render_w,
                        unsigned* out_render_h)
{
    if (!g_inited || !g_caps || !out_render_w || !out_render_h)
        return 0;

    unsigned rw = 0, rh = 0, max_w = 0, max_h = 0, min_w = 0, min_h = 0;
    float sharpness = 0.f;

    NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(g_caps, display_w, display_h,
                                                       quality_for(quality), &rw, &rh, &max_w, &max_h,
                                                       &min_w, &min_h, &sharpness);
    if (NVSDK_NGX_FAILED(r))
    {
        say("! [DLSS] optimal settings query failed (0x%08X)", unsigned(r));
        return 0;
    }

    // Ноль здесь означает «этот режим на этой карте не поддерживается» - так NGX отвечает, например,
    // про UltraQuality, который заявлен не везде. Молча откатываемся на ближайший рабочий, иначе
    // сцена получит нулевой размер и кадр будет чёрным без единого сообщения.
    if (!rw || !rh)
    {
        say("* [DLSS] quality mode %d is unavailable here, falling back to quality", quality);
        r = NGX_DLSS_GET_OPTIMAL_SETTINGS(g_caps, display_w, display_h,
                                          NVSDK_NGX_PerfQuality_Value_MaxQuality, &rw, &rh, &max_w,
                                          &max_h, &min_w, &min_h, &sharpness);
        if (NVSDK_NGX_FAILED(r) || !rw || !rh)
            return 0;
    }

    *out_render_w = rw;
    *out_render_h = rh;
    return 1;
}

// [DA_PORT] Выбранный пресет модели, 0 - не задавать. Разбор - у da_ngx_set_preset в заголовке.
static int g_preset = 0;

void da_ngx_set_preset(int preset) { g_preset = preset; }

int da_ngx_create(void* d3d11_context, unsigned render_w, unsigned render_h, unsigned display_w,
                  unsigned display_h, int quality)
{
    if (!g_inited || !g_caps || !d3d11_context || !render_w || !render_h)
        return 0;

    da_ngx_destroy(d3d11_context);

    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = render_w;
    create.Feature.InHeight = render_h;
    create.Feature.InTargetWidth = display_w;
    create.Feature.InTargetHeight = display_h;
    create.Feature.InPerfQualityValue = quality_for(quality);

    // [DA_PORT] ⚠️ IsHDR СНЯТ: кадр к моменту апскейла уже тонемаплен.
    //
    // Прежняя строка тут утверждала «сцена приходит в HDR до тонемапа» — это было верно, пока диспетч
    // стоял в другом месте кадра. Сейчас он внутри phase_combine, ПОСЛЕ того как combine_1 свёл сцену
    // в отображаемый диапазон (`tonemap(o.low, o.high, ...)`). Библиотеке говорили «линейный HDR», а
    // давали 0..1 — и вся её работа с яркостью шла в чужой шкале. Видно это на самом ярком предмете
    // кадра: светящаяся палочка на тёмном фоне шла ступенчатой кромкой при любом качестве. Ошибка была
    // одинаковой у FSR 2, XeSS и DLSS, поэтому и симптом был одинаковый на всех трёх.
    //
    // MVLowRes: вектора лежат в разрешении рендера, а не экрана.
    // DepthInverted НЕ ставим: проекция X-Ray обычная, как и для FSR 2.
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;

    // [DA_PORT] Пресет ставится СРАЗУ ВО ВСЕ шесть режимов, а не только в текущий.
    //
    // Так надёжнее и дешевле, чем угадывать имя параметра по режиму. Наша ступень качества и режим
    // NGX связаны через quality_for(), где две разные ступени дают один MaxQuality; ошибиться в
    // соответствии легко, а цена ошибки - молчаливое бездействие: NGX не жалуется на подсказку,
    // адресованную чужому режиму, он её просто не читает. Задав все шесть одинаково, мы получаем
    // выбранную модель при любом режиме.
    if (g_preset > 0)
    {
        const int v = g_preset;
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, v);
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality, v);
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced, v);
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance, v);
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance, v);
        g_caps->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraQuality, v);
        say("* [DLSS] render preset %d ('%c') requested", v, char('A' + v - 1));
    }
    else
        say("* [DLSS] render preset: driver default");

    NVSDK_NGX_Result r = NGX_D3D11_CREATE_DLSS_EXT(static_cast<ID3D11DeviceContext*>(d3d11_context),
                                                   &g_feature, g_caps, &create);
    if (NVSDK_NGX_FAILED(r))
    {
        say("! [DLSS] feature creation failed (0x%08X)", unsigned(r));
        g_feature = nullptr;
        return 0;
    }

    say("* [DLSS] ready: %ux%u -> %ux%u, quality mode %d", render_w, render_h, display_w, display_h,
        quality);
    return 1;
}

void da_ngx_destroy(void* d3d11_context)
{
    if (!g_feature)
        return;
    NVSDK_NGX_D3D11_ReleaseFeature(g_feature);
    g_feature = nullptr;
    (void)d3d11_context;
}

int da_ngx_evaluate(void* d3d11_context, void* colour, void* depth, void* velocity, void* output,
                    void* reactive, float jitter_x, float jitter_y, float mv_scale_x,
                    float mv_scale_y, int reset, unsigned render_w, unsigned render_h)
{
    if (!g_feature || !d3d11_context || !colour || !depth || !velocity || !output)
        return 0;

    NVSDK_NGX_D3D11_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = static_cast<ID3D11Resource*>(colour);
    eval.Feature.pInOutput = static_cast<ID3D11Resource*>(output);
    eval.pInDepth = static_cast<ID3D11Resource*>(depth);
    eval.pInMotionVectors = static_cast<ID3D11Resource*>(velocity);

    // NVIDIA называет реактивную маску иначе, но берёт ту же текстуру, что FSR 2 и XeSS.
    eval.pInBiasCurrentColorMask = static_cast<ID3D11Resource*>(reactive);

    eval.InJitterOffsetX = jitter_x;
    eval.InJitterOffsetY = jitter_y;
    eval.InMVScaleX = mv_scale_x;
    eval.InMVScaleY = mv_scale_y;
    eval.InReset = reset;
    eval.InRenderSubrectDimensions.Width = render_w;
    eval.InRenderSubrectDimensions.Height = render_h;
    eval.InExposureScale = 1.0f;
    eval.InPreExposure = 1.0f;

    const NVSDK_NGX_Result r = NGX_D3D11_EVALUATE_DLSS_EXT(
        static_cast<ID3D11DeviceContext*>(d3d11_context), g_feature, g_caps, &eval);
    if (NVSDK_NGX_FAILED(r))
    {
        say("! [DLSS] evaluate failed (0x%08X)", unsigned(r));
        return 0;
    }
    return 1;
}

} // extern "C"
