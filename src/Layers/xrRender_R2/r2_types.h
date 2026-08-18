#pragma once

namespace xray::render::RENDER_NAMESPACE
{
// Base targets
#define     r2_RT_base          "$user$base_"
#define     r2_RT_base_depth    "$user$base_depth"

// r3xx code-path (MRT)
#define     r2_RT_depth         "$user$depth"       // MRT
#define     r2_RT_MSAAdepth     "$user$msaadepth"   // MRT
#define     r2_RT_P             "$user$position"    // MRT
#define     r2_RT_N             "$user$normal"      // MRT
#define     r2_RT_SSR           "$user$ssr"         // DA: screen-space reflections (R4)
#define     r2_RT_taa_history   "$user$taa_history" // DA: previous resolved frame, for temporal AA (R4)
#define     r2_RT_taa_scratch   "$user$taa_scratch" // DA: un-sharpened resolve, on its way into the history
#define     r2_RT_taa_out       "$user$taa_out"     // DA: sharpened resolve, on its way back into rt_Color
#define     r2_RT_velocity      "$user$velocity"    // DA: per-pixel screen-space motion, for FSR 2 (R4)
#define     r2_RT_velocity_guard "$user$velocity_guard" // DA: velocity after the guard pass (R4)
#define     r2_RT_reactive      "$user$reactive"    // DA: reactive mask for the upscalers (R4)
#define     r2_RT_reactive_scratch "$user$reactive_scratch" // DA: reactive, first axis of the dilate (R4)
#define     r2_RT_reactive_scratch2 "$user$reactive_scratch2" // DA: reactive, motion in and result out (R4)
#define     r2_RT_fsr2_out      "$user$fsr2_out"    // DA: FSR 2 result, at OUTPUT resolution (R4)
#define     r2_RT_albedo        "$user$albedo"      // MRT

// other
#define     r2_RT_accum         "$user$accum"       // --- 16 bit fp or 16 bit fx
#define     r2_RT_accum_temp    "$user$accum_temp"  // --- 16 bit fp - only for HW which doesn't feature fp16 blend

#define     r2_T_envs0          "$user$env_s0"
#define     r2_T_envs1          "$user$env_s1"

#define     r2_T_sky0           "$user$sky0"
#define     r2_T_sky1           "$user$sky1"

#define     r2_RT_generic0      "$user$generic0"
#define     r2_RT_generic0_r    "$user$generic0_r"

#define     r2_RT_generic1      "$user$generic1"
#define     r2_RT_generic1_r    "$user$generic1_r"

#define     r2_RT_generic2      "$user$generic2"    // --- // Igor: for volumetric lights
#define     r2_RT_generic       "$user$generic"     // --- actually generic3

#define     r2_RT_ssao_temp     "$user$ssao_temp"   // temporary rt for ssao calculation
#define     r2_RT_half_depth    "$user$half_depth"  // temporary rt for ssao/hbao calculation

#define     r2_RT_bloom1        "$user$bloom1"
#define     r2_RT_bloom2        "$user$bloom2"

#define     r2_RT_luminance_t64 "$user$lum_t64"     // --- temp
#define     r2_RT_luminance_t8  "$user$lum_t8"      // --- temp

#define     r2_RT_luminance_src "$user$tonemap_src" // --- prev-frame-result
#define     r2_RT_luminance_cur "$user$tonemap"     // --- result
#define     r2_RT_luminance_pool "$user$luminance"  // --- pool

#define     r2_RT_smap_surf     "$user$smap_surf"   // --- directional
#define     r2_RT_smap_depth    "$user$smap_depth"  // --- directional
#define     r2_RT_smap_rain     "$user$smap_rain"
#define     r2_RT_smap_depth_minmax "$user$smap_depth_minmax"
// [DA_PORT] Вторая копия теневого атласа: в ней живёт ТОЛЬКО статика ламп. См. r2_rendertarget.cpp.
#define     r2_RT_da_smap_static "$user$da_smap_static"

#define     r2_async_ss         "$user$async_ss"

#define     r2_material         "$user$material"
#define     r2_ds2_fade         "$user$ds2_fade"

#define     r2_jitter           "$user$jitter_"     // --- dither
#define     r2_jitter_mipped    "$user$jitter_mipped" // --- dither
#define     r2_sunmask          "sunmask"

#define     r2_base             "$user$base"

static constexpr auto c_lmaterial = "L_material";
static constexpr auto c_sbase = "s_base";
static constexpr auto c_snoise = "s_noise";
static constexpr auto c_ssky0 = "s_sky0";
static constexpr auto c_ssky1 = "s_sky1";
static constexpr auto c_sclouds0 = "s_clouds0";
static constexpr auto c_sclouds1 = "s_clouds1";

#define JITTER(a) r2_jitter #a

const float SMAP_near_plane = .1f;

const u32 SMAP_adapt_min = 32;
const u32 SMAP_adapt_optimal = 768;
const u32 SMAP_adapt_max = 1536;

const u32 TEX_material_LdotN = 128; // diffuse, X, almost linear = small res
const u32 TEX_material_LdotH = 256; // specular, Y
const u32 TEX_material_Count = 4; // Number of materials, Z
const u32 TEX_jitter = 64;
const u32 TEX_jitter_count = 5; // for HBAO

const u32 BLOOM_size_X = 256;
const u32 BLOOM_size_Y = 256;
const u32 LUMINANCE_size = 16;

// deffer
#define SE_R2_NORMAL_HQ     0 // high quality/detail
#define SE_R2_NORMAL_LQ     1 // low quality
#define SE_R2_SHADOW        2 // shadow generation

// spot
#define SE_L_FILL           0
#define SE_L_UNSHADOWED     1
#define SE_L_NORMAL         2 // typical, scaled
#define SE_L_FULLSIZE       3 // full texture coverage
#define SE_L_TRANSLUENT     4 // with opacity/color mask

// mask
#define SE_MASK_SPOT        0
#define SE_MASK_POINT       1
#define SE_MASK_DIRECT      2
#define SE_MASK_ACCUM_VOL   3
#define SE_MASK_ACCUM_2D    4
#define SE_MASK_ALBEDO      5

// sun
#define SE_SUN_NEAR         0
#define SE_SUN_MIDDLE       1
#define SE_SUN_FAR          2
#define SE_SUN_LUMINANCE    3
#define SE_SUN_NEAR_MINMAX  4
// For rain R3 rendering
#define SE_SUN_RAIN_SMAP    5

extern float ps_r2_gloss_factor;
extern float ps_r2_gloss_min;

// [DA_PORT] Пол под зеркальной отдачей источника света. Перенесено из xray-monolith/OGSR, где эта
// ручка (r2_gloss_min) заведена давно и используется шейдерными сборками сообщества — в Enhanced
// Shaders она прямо названа управлением шероховатостью их псевдо-PBR.
//
// Смысл: без пола тусклый источник даёт зеркальную составляющую около нуля, и блик на поверхности
// то есть, то нет. Ноль по умолчанию — поведение в точности прежнее.
IC float u_diffuse2s(float x, float y, float z)
{
    float v = (x + y + z) / 3.f;
    return ps_r2_gloss_min + ps_r2_gloss_factor * ((v < 1) ? powf(v, 2.f / 3.f) : v);
}

// [DA_PORT] Тот же расчёт БЕЗ пола — только для решений «а нужен ли вообще этот проход».
// Фаза солнца включается по условию u_diffuse2s(цвет солнца) > EPS: с ненулевым полом оно стало бы
// истинным ВСЕГДА, и проход солнца работал бы даже в кромешной темноте. Пол задуман про яркость
// блика, а не про то, светит ли солнце.
IC float u_diffuse2s_nofloor(float x, float y, float z)
{
    float v = (x + y + z) / 3.f;
    return ps_r2_gloss_factor * ((v < 1) ? powf(v, 2.f / 3.f) : v);
}

IC float u_diffuse2s(Fvector3& c)
{
    return u_diffuse2s(c.x, c.y, c.z);
}
} // namespace xray::render::RENDER_NAMESPACE
