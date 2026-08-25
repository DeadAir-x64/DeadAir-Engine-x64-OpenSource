#ifndef srgb_h_included
#define srgb_h_included

//=================================================================================================
// [DA_PORT] Linear Workflow — color space conversions
// Based on: NVIDIA GPU Gems 3 "The Importance of Being Linear"
//           https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-24-importance-being-linear
//
// Rule: EVERY color texture (albedo, diffuse, light colors, env maps) → SRGBToLinear on read.
//       EVERY lighting calculation → in linear space.
//       ONLY final output to monitor → LinearToSRGB (in combine_2 or post-process).
//=================================================================================================

// [DA_PORT] ⚠️ Почему дефолт — ДЕШЁВЫЙ pow(2.2), хотя Frostbite (PBR 2.0, §5.1.5) и зовёт его
// промахом. Frostbite переходил на точный sRGB ВМЕСТЕ с полным перевыпуском контента под PBR.
// Контент Dead Air (текстуры с DXT-шумом у чёрного, авторские значения света, 1113 погодных
// записей) 15 лет подбирался под кривую 2.2: она давит подчёркнутый шум к нулю. Точный
// piecewise в dim-свете поднимает свет в линейном в 1.5-2 раза (0.08 sRGB: 0.0038 → 0.0072) —
// тёмные комнаты «подсвечиваются», и вместе с яркостью над порогом видимости всплывает
// DXT-шум текстур цветной крошкой. Проверено в игре 21.08: на легаси-контенте точная кривая
// вредит. Strict оставлены для скриншотов/сравнения.
float3 da_SRGBToLinear_fast(float3 c) { return pow(max(c, 0.0h), 2.2h); }
float3 da_LinearToSRGB_fast(float3 c) { return pow(max(c, 0.0h), 0.45454545h); }

// Strict sRGB — piecewise exact per IEC 61966-2-1. НЕ дефолт: см. предупреждение выше.
float3 da_SRGBToLinear_strict(float3 c)
{
    c = max(c, 0.0h);
    float3 lo = c / 12.92h;
    float3 hi = pow((c + 0.055h) / 1.055h, 2.4h);
    return (c <= 0.04045h) ? lo : hi;
}
float3 da_LinearToSRGB_strict(float3 c)
{
    c = max(c, 0.0h);
    float3 lo = c * 12.92h;
    float3 hi = 1.055h * pow(c, 0.41666667h) - 0.055h;
    return (c <= 0.0031308h) ? lo : hi;
}

// [DA_PORT] Дефолт — кривая, под которую authored весь контент мода. Менять ТОЛЬКО парой:
// если прямая и обратная разъедутся, round-trip перестаёт смыкаться и тёмные тона гуляют.
#define SRGBToLinear da_SRGBToLinear_fast
#define LinearToSRGB da_LinearToSRGB_fast

//=================================================================================================
// Luminance coefficients (Rec. 709) — for saturation/desaturation in linear space
//=================================================================================================
static const float3 LUMINANCE_VECTOR_LINEAR = float3(0.2126h, 0.7152h, 0.0722h);

#endif // srgb_h_included
