#include "stdafx.h"
#pragma hdrstop

#include "xrRender_console.h"
#include "xrCore/xr_token.h"
#include "xrCore/Animation/SkeletonMotions.hpp"

#include "xrEngine/XR_IOConsole.h"
#include "xrEngine/xr_ioc_cmd.h"

#if RENDER != R_R1
#include "r__pixel_calculator.h"
#endif

#if defined(USE_DX11)
#include "Layers/xrRenderDX11/StateManager/dx11SamplerStateCache.h"
#endif

#if (RENDER == R_R3) || (RENDER == R_R4)
#   ifndef MASTER_GOLD
#   include "Layers/xrRenderDX11/3DFluid/dx113DFluidManager.h"
#   endif // MASTER_GOLD
#endif // (RENDER == R_R3) || (RENDER == R_R4)


// [DA_PORT] Which entry the upscaler list stands on; needed by CCC_MSAA further down. Declared HERE,
// outside the namespace, and not next to its use: an extern written inside
// xray::render::RENDER_NAMESPACE looks for the symbol in that namespace and the link fails with an
// undefined reference to a mangled name that still reads like the right variable. Same reason the
// engine externs at the top of r2.cpp sit outside the namespace.
extern ENGINE_API u32 ps_r__upscaler;

namespace xray::render::RENDER_NAMESPACE
{
u32 ps_Preset = 2;
const xr_token qpreset_token[] =
{
    { "Minimum", 0 },
    { "Low", 1 },
    { "Default", 2 },
    { "High", 3 },
    { "Extreme", 4 },
    { nullptr, 0 }
};

u32 ps_r2_smapsize = 2048;
const xr_token qsmapsize_token[] =
{
#if !defined(MASTER_GOLD) || RENDER == R_R1
    { "256", 256 }, // Too bad for R2+
    { "512", 512 }, // But works
#endif
    { "1024", 1024 },
    { "1032", 1032 },
    { "1536", 1536 },
    { "2048", 2048 },
    { "2560", 2560 },
    { "3072", 3072 },
    { "3584", 3584 },
    { "4096", 4096 }, // XXX: runtime check for maximum smap-size on OpenGL
    { "5120", 5120 },
    { "6144", 6144 },
    { "7168", 7168 },
    { "8192", 8192 },
    { "9216", 9216 },
    { "10240", 10240 },
    { "11264", 11264 },
    { "12288", 12288 },
    { "13312", 13312 },
    { "14336", 14336 },
    { "15360", 15360 },
    { "16384", 16384 },
    { nullptr, 0 }
};

u32 ps_r_ssao_mode = ssao_mode_default;
const xr_token qssao_mode_token[] =
{
    { "disabled", ssao_mode_off },
    { "default",  ssao_mode_default },
    { "hdao",     ssao_mode_hdao },
    { "hbao",     ssao_mode_hbao },
    { nullptr,    0 }
};

u32 ps_r_sun_shafts = 2;
const xr_token qsun_shafts_token[] = {{"st_opt_off", 0}, {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3}, {nullptr, 0}};

u32 ps_r_ssao = 3;
const xr_token qssao_token[] = {{"st_opt_off", 0}, {"st_opt_low", 1}, {"st_opt_medium", 2}, {"st_opt_high", 3},
    {"st_opt_ultra", 4},
{nullptr, 0}};

u32 ps_r_sun_quality = 1; // = 0;
const xr_token qsun_quality_token[] = {{"st_opt_low", 0}, {"st_opt_medium", 1}, {"st_opt_high", 2},
#if defined(USE_DX11) // TODO: OGL: fix ultra and extreme settings
    {"st_opt_ultra", 3}, {"st_opt_extreme", 4},
#endif // USE_DX11
    {nullptr, 0}};

u32 ps_r_water_reflection = 3;
const xr_token qwater_reflection_quality_token[] =
{
    { "st_opt_off", 0 },
    { "st_opt_low", 1 },
    { "st_opt_medium", 2 },
    { "st_opt_high", 3 },
    { "st_opt_ultra", 4 },
    { nullptr, -1 }
};

u32 ps_r3_msaa = 0; // = 0;
// [DA_PORT] 8x убран из списка (значение 3 в стоковой таблице). Три причины, и все три — про то, что
// пункт обещал больше, чем движок может обеспечить.
//
// 1. Рендер отложенный, поэтому цена памяти умножается БУКВАЛЬНО: весь G-буфер создаётся с восемью
//    сэмплами. На 1920x1080 это порядка 400 МБ на целях сцены против примерно 50 без MSAA.
// 2. Поддержку никто не проверяет: во всём рендере нет ни одного CheckMultisampleQualityLevels, а
//    DirectX 11 гарантирует для обычных форматов целей только 4x — 8x опционален и зависит от
//    формата и видеокарты. Отката на меньший множитель тоже нет: отказ драйвера приводит к падению
//    на CHK_DX при создании цели.
// 3. Сглаживаются только края геометрии, и поверх обычно работает TAA, которая их и так разбирает.
//    Разница 4x против 8x на этом фоне не стоит своей цены.
//
// Значения остальных пунктов НЕ сдвинуты: 2x остаётся 1, 4x остаётся 2 — по ним считается
// o.msaa_samples = (1 << ps_r3_msaa), и сдвиг сломал бы сохранённые настройки. Старый user.ltx с
// "r3_msaa 8x" просто не найдёт токен: команда сообщит о неверном аргументе и оставит прежнее
// значение, а не выставит мусор.
const xr_token qmsaa_token[] = {{"st_opt_off", 0}, {"2x", 1}, {"4x", 2},
    {nullptr, 0}};

u32 ps_r3_msaa_atest = 0; // = 0;
const xr_token qmsaa__atest_token[] = {
    {"st_opt_off", 0}, {"st_opt_atest_msaa_dx10_0", 1}, {"st_opt_atest_msaa_dx10_1", 2}, {nullptr, 0}};

u32 ps_r3_minmax_sm = 3; // = 0;
const xr_token qminmax_sm_token[] = {{"off", 0}, {"on", 1}, {"auto", 2}, {"autodetect", 3}, {nullptr, 0}};

// “Off”
// “DX10.0 style [Standard]”
// “DX10.1 style [Higher quality]”

// Common
extern int psSkeletonUpdate;
extern float r__dtex_range;

Flags32 ps_r__common_flags = { RFLAG_ACTOR_SHADOW }; // All renders

//int ps_r__Supersample = 1;
int ps_r__LightSleepFrames = 10;

float ps_r__Detail_l_ambient = 0.9f;
float ps_r__Detail_l_aniso = 0.25f;
float ps_r__Detail_density = 0.3f;
float ps_r__Detail_height = 1.f;
float ps_r__Detail_rainbow_hemi = 0.75f;

float ps_r__Tree_SBC = 1.5f; // scale bias correct

float ps_r__WallmarkTTL = 50.f;
float ps_r__WallmarkSHIFT = 0.0001f;
float ps_r__WallmarkSHIFT_V = 0.0001f;

float ps_r__GLOD_ssa_start = 256.f;
float ps_r__GLOD_ssa_end = 64.f;
// [DA_PORT] The author's value; governs STATIC visuals - trees and bushes, grass is unaffected by it.
// Higher means models hold their detail further out before the simplified version takes over.
//
// This spent a day at the stock 0.75 because trees were snapping into their rest pose as they crossed
// the LOD boundary, and lowering it made that rarer. It was treating a symptom: the real cause was the
// race for the shared phase object in FTreeVisual, where a second command list overwrote the previous
// frame's wind. With that fixed, 1.5 was verified clean in game, so the author's default is safe and
// the detail loss at range was being paid for nothing.
//
// Not raised to the 1.5/2.0 found in the reference x32 installs: those are values the engine wrote out
// on exit, they disagree with each other and match no preset, so they show that high values are safe
// rather than what the author intended. 1.0 also sits inside the range the quality presets use
// (0.5 to 1.2), which keeps the menu from silently lowering detail the first time it is touched.
float ps_r__LOD = 1.0f;
//float ps_r__LOD_Power = 1.5f;
float ps_r__ssaDISCARD = 3.5f; // RO
float ps_r__ssaDONTSORT = 32.f; // RO
float ps_r__ssaHZBvsTEX = 96.f; // RO

// [DA_PORT] 16 rather than the stock 8, and this is not a "more is prettier" bump. Under any upscaler
// SetupStates (R_Backend_Runtime.cpp) shifts the mip LOD bias into the minus by log2(render / output),
// otherwise the hardware picks mips for the reduced frame and textures arrive pre-blurred. With a
// negative bias trilinear filtering UNDERSAMPLES, so the symptom flips over: instead of the familiar
// mush you get shimmer, worst on ground planes seen at a grazing angle. Anisotropy takes its samples
// along the long axis of the footprint and is what makes the negative bias safe again — measured in
// game, 16 removes the shimmer under DLSS. Hence also: a quality ladder on this setting is HARMFUL,
// every rspec_*.ltx asks for 16.
int ps_r__tf_Anisotropic = 16;
float ps_r__tf_Mipbias = 0.0f;

int ps_r__clear_models_on_unload = 0; // Alundaio

// R1
float ps_r1_ssaLOD_A = 64.f;
float ps_r1_ssaLOD_B = 48.f;
Flags32 ps_r1_flags = {R1FLAG_DLIGHTS}; // r1-only
float ps_r1_lmodel_lerp = 0.1f;
float ps_r1_dlights_clip = 40.f;
float ps_r1_pps_u = 0.f;
float ps_r1_pps_v = 0.f;
int ps_r1_force_geomx = 0;

// R1-specific
int ps_r1_GlowsPerFrame = 16; // r1-only
float ps_r1_fog_luminance = 1.1f; // r1-only
int ps_r1_SoftwareSkinning = 0; // r1-only

// R2
bool ps_r2_sun_static = false;
bool ps_r2_advanced_pp = true; // advanced post process and effects

// [DA_PORT] 64 is what the original actually rendered with, not merely the stock value: the author's
// code default of 96 never took effect, because his own user.ltx wrote 64 back over it on every exit.
// Verified against the reference x32 installs. Grass, unlike trees, keys off this one - at 96 the LOD
// switch landed where the grass lives and it flickered.
float ps_r2_ssaLOD_A = 64.f;
float ps_r2_ssaLOD_B = 48.f;

// R2-specific
Flags32 ps_r2_ls_flags = {R2FLAG_SUN
    //| R2FLAG_SUN_IGNORE_PORTALS
    | R2FLAG_EXP_DONT_TEST_UNSHADOWED | R2FLAG_USE_NVSTENCIL | R2FLAG_EXP_SPLIT_SCENE | R2FLAG_EXP_MT_CALC |
    R3FLAG_DYN_WET_SURF | R3FLAG_VOLUMETRIC_SMOKE
    //| R3FLAG_MSAA
    //| R3FLAG_MSAA_OPT
    // [DA_PORT] R2FLAG_DOF убран из значений по умолчанию.
    //
    // Глубина резкости в этом моде размывает при прицеливании и перезарядке и мешает целиться;
    // ничего взамен она не даёт, потому что настроена под неподвижные планы, а не под бой. Три
    // источника размытия у оружия закрыты ручкой g_weapon_dof, но остаётся ещё погодная глубина
    // резкости из level_weathers.script — она идёт через этот же флаг, и гасится только им.
    //
    // Флаг никуда не делся: r2_dof_enable on включает всё обратно, строка в настройках на месте.
    // Убрано ровно то, из-за чего он возвращался сам: значение по умолчанию и три набора качества
    // (rspec_default, rspec_high, rspec_extreme), где стояло on.
    | R3FLAG_GBUFFER_OPT | R2FLAG_DETAIL_BUMP | R2FLAG_SOFT_PARTICLES | R2FLAG_SOFT_WATER |
    R2FLAG_STEEP_PARALLAX | R2FLAG_SUN_FOCUS | R2FLAG_SUN_TSM | R2FLAG_TONEMAP | R2FLAG_VOLUMETRIC_LIGHTS}; // r2-only

Flags32 ps_r2_ls_flags_ext = {
    /*R2FLAGEXT_SSAO_OPT_DATA |*/ R2FLAGEXT_SSAO_HALF_DATA | R2FLAGEXT_ENABLE_TESSELLATION | R3FLAGEXT_SSR_HALF_DEPTH |
    R3FLAGEXT_SSR_JITTER};

float ps_r2_df_parallax_h = 0.02f;
float ps_r2_df_parallax_range = 75.f;
// [DA_PORT] Tone mapping and bloom below are the author's values, taken from the Dead Air sources
// (_engine_diff/DA_render_R2.patch), not OpenXRay's stock ones. His exposure curve is flatter: brighter
// outdoors in daylight, darker indoors, and it adapts twice as fast. Keeping the stock curve made the
// port noticeably duller than the original everywhere the player actually spends time.
float ps_r2_tonemap_middlegray = 1.f; // r2-only
float ps_r2_tonemap_adaptation = 2.f; // r2-only  [DA_PORT] was 1.f
float ps_r2_tonemap_low_lum = 0.2f; // r2-only  [DA_PORT] was 0.0001f
float ps_r2_tonemap_amount = 0.4f; // r2-only  [DA_PORT] was 0.7f
float ps_r2_ls_bloom_kernel_g = 3.f; // r2-only
float ps_r2_ls_bloom_kernel_b = .5f; // r2-only  [DA_PORT] was .7f
float ps_r2_ls_bloom_speed = 100.f; // r2-only
float ps_r2_ls_bloom_kernel_scale = 0.55f; // r2-only // gauss  [DA_PORT] was .7f
float ps_r2_ls_dsm_kernel = .7f; // r2-only
float ps_r2_ls_psm_kernel = .7f; // r2-only
float ps_r2_ls_ssm_kernel = .7f; // r2-only
float ps_r2_ls_bloom_threshold = 0.f; // r2-only  [DA_PORT] was .00001f (author's value)
Fvector ps_r2_aa_barier = {.8f, .1f, 0}; // r2-only
Fvector ps_r2_aa_weight = {.25f, .25f, 0}; // r2-only
float ps_r2_aa_kernel = .5f; // r2-only
float ps_r2_mblur = .0f; // .5f
int ps_r2_GI_depth = 1; // 1..5
int ps_r2_GI_photons = 16; // 8..64
float ps_r2_GI_clip = EPS_L; // EPS
float ps_r2_GI_refl = .9f; // .9f
float ps_r2_ls_depth_scale = 1.00001f; // 1.00001f
float ps_r2_ls_depth_bias = -0.0003f; // -0.0001f
float ps_r2_ls_squality = 1.0f; // 1.00f
float ps_r2_sun_tsm_projection = 0.3f; // 0.18f
float ps_r2_sun_tsm_bias = -0.2f;
float ps_r2_sun_near = 20.f; // 12.0f
float ps_r2_sun_near_border = 1.0f; // [DA_PORT] was 0.75f (author's value)
// [DA_PORT] 180, the value beside it, which is the author's and also the console maximum. At 100 the
// far shadow cascade ends well inside open country, and the step in brightness where it ends reads as
// a flickering line across a distant slope that slides away as the player walks towards it. Verified
// in game: raising it to 180 removes that line outright.
// [DA_PORT] 51 instead of the stock 180.
//
// The sun renders the whole scene again into each of its shadow cascades, and the far cascade is by far
// the most expensive: on Cordon it was 5.1 ms of a 6.0 ms GPU frame at 180. At 51 the shadows look the
// same in play - the difference only shows on terrain hundreds of metres out - and the level went from
// 130 to 240-500 fps. Chosen deliberately after measuring; going above 51 is not worth it.
float ps_r2_sun_far = 51.f;
float ps_r2_sun_depth_far_scale = 1.00000f; // 1.00001f
float ps_r2_sun_depth_far_bias = -0.00002f; // -0.0000f
float ps_r2_sun_depth_near_scale = 1.0000f; // 1.00001f
float ps_r2_sun_depth_near_bias = 0.00001f; // -0.00005f
// [DA_PORT] The author's lighting balance: a much stronger sun against a heavily damped ambient and
// hemisphere term. This is what gives Dead Air its hard, contrasty daylight and genuinely dark shade;
// with the stock 1/1/1 everything is lit evenly from all sides and the picture goes flat.
float ps_r2_sun_lumscale = 1.6f; // [DA_PORT] was 1.0f
float ps_r2_sun_lumscale_hemi = 0.6f; // [DA_PORT] was 1.0f
float ps_r2_sun_lumscale_amb = 0.4f; // [DA_PORT] was 1.0f
float ps_r2_gmaterial = 2.2f; //
float ps_r2_zfill = 0.25f; // .1f

float ps_r2_dhemi_sky_scale = 0.08f; // 1.5f
float ps_r2_dhemi_light_scale = 0.2f;
float ps_r2_dhemi_light_flow = 0.1f;
int ps_r2_dhemi_count = 5; // 5
int ps_r2_wait_sleep = 0;
int ps_r2_wait_timeout = 500;

float ps_r2_lt_smooth = 1.f; // 1.f
float ps_r2_slight_fade = 0.5f; // 1.f

//  x - min (0), y - focus (1.4), z - max (100)
Fvector3 ps_r2_dof = Fvector3().set(-1.25f, 1.4f, 600.f);
float ps_r2_dof_sky = 30; //    distance to sky
float ps_r2_dof_kernel_size = 5.0f; //  7.0f

float ps_r3_dyn_wet_surf_near = 5.f; // 10.0f
float ps_r3_dyn_wet_surf_far = 20.f; // 30.0f
int ps_r3_dyn_wet_surf_sm_res = 256; // 256

// [DA_PORT] Dead Air compatibility stubs (x32 mod commands)
u32 ps_r2_shadow_map_size = 2048;
const xr_token qshadow_map_size_token[] = {
    {"512", 512}, {"1024", 1024}, {"1536", 1536}, {"2048", 2048}, {"3072", 3072}, {"4096", 4096}, {nullptr, 0}};
float ps_r2_sun_shafts_value = 0.5f;
float ps_r2_aberration_val = 0.f;
float ps_r2_dof_diff_far = 0.f;
float ps_r2_dof_diff_near = 0.f;
float ps_r2_dof_pickable = 0.f;
float ps_r2_dof_time = 0.2f;
float ps_r2_fxaa = 0.f;
float ps_r2_lensdirt = 0.f;
float ps_r2_lensdirt_val = 0.5f;
float ps_r2_lenswater = 0.f;
float ps_r2_lenswater_val = 0.5f;
float ps_r2_lumasharpen = 0.f;
float ps_r2_reflections = 0.f;
float ps_r2_sss_blend = 0.3f;
float ps_r2_sss_enable = 0.f;
float ps_r2_sss_intensity = 0.5f;
float ps_r2_sss_phase1 = 0.1f;
float ps_r2_sss_phase2 = 0.1f;
float ps_r2_sss_radius = 0.05f;
float ps_r2_technicolor = 0.f;
float ps_r2_tmp_w = 0.f;
float ps_r2_tmp_x = 0.f;
float ps_r2_tmp_y = 0.f;
float ps_r2_tmp_z = 0.f;
// [DA_PORT] Vibrance, not saturation: it lifts muted colours and leaves vivid ones alone, so night
// scenes keep their blue cast while grass and brick gain depth. 0.18 is felt without looking graded.
float ps_r2_vibrance_val = 0.18f;
float ps_r2_vignette = 0.f;
float ps_r2_zoom_dof = 0.f;
float ps_r1_dynamic_lights = 1.f;
float ps_r2_actor_body = 0.f;
float ps_r_color_add_r = 0.f;
float ps_r_color_add_g = 0.f;
float ps_r_color_add_b = 0.f;
// [DA_PORT] Ready-made grading profiles, so a player can pick a look instead of hunting for numbers.
// The console command applies the four values; the sliders stay live afterwards, so a profile is a
// starting point rather than a lock.
u32 ps_r_grading_preset = 1; // 1 = the port default below
const xr_token qgrading_preset_token[] =
{
    { "ui_mm_grade_original", 0 },
    { "ui_mm_grade_default",  1 },
    { "ui_mm_grade_autumn",   2 },
    { "ui_mm_grade_cold",     3 },
    { "ui_mm_grade_faded",    4 },
    { "ui_mm_grade_vivid",    5 },
    { nullptr, 0 }
};

// [DA_PORT] Default grading for Dead Air's palette: a slight push towards warm. The mod's world is
// burnt-out autumn - ochre, rust, grey-green - and lifting red while easing blue makes rust and dry
// grass read better without touching the sky, which stays cold because it is bright and gets pushed
// least. Deliberately small: grading is a matter of taste, and the sliders are there for whoever
// disagrees. 1/1/1 restores stock behaviour exactly.
float ps_r_color_base_r = 1.04f;
float ps_r_color_base_g = 1.00f;
float ps_r_color_base_b = 0.96f;

u32 ps_steep_parallax = 0;
int ps_r__detail_radius = 49;

// [DA_PORT] Ceiling on shadow-casting lights per frame. 0 restores the stock "no limit".
//
// Every shadowed light re-submits the whole scene into its own shadow map (r2_R_lights.cpp:
// phase_smap_spot -> render_graph), so the scene is drawn 1 + N times per frame. Measured in the Bar:
// 87 lights, 84 visible, 61 shadowed, only 4 clipped - the level's 345k polygons went through the
// pipeline nearly sixty times. Lights ate 3.7 ms of the 4.5 ms GPU frame there.
// Lights beyond the budget are demoted to the unshadowed path (they still light the scene, they just
// stop casting), keeping the ones that matter most on screen.
u32 ps_r__light_shadow_budget = 12;

u32 dm_size = 24;
u32 dm_cache1_line = 12; //dm_size*2/dm_cache1_count
u32 dm_cache_line = 49; //dm_size+1+dm_size
u32 dm_cache_size = 2401; //dm_cache_line*dm_cache_line
float dm_fade = 47.5; //float(2*dm_size)-.5f;
u32 dm_current_size = 24;
u32 dm_current_cache1_line = 12; //dm_current_size*2/dm_cache1_count
u32 dm_current_cache_line = 49; //dm_current_size+1+dm_current_size
u32 dm_current_cache_size = 2401; //dm_current_cache_line*dm_current_cache_line
float dm_current_fade = 47.5; //float(2*dm_current_size)-.5f;

float ps_current_detail_density = 0.6f;
float ps_current_detail_height = 1.f;

int ps_r2_mt_calculate = 1;
int ps_r2_mt_render = 1;

xr_token ext_quality_token[] = {{"qt_off", 0}, {"qt_low", 1}, {"qt_medium", 2},
    {"qt_high", 3}, {"qt_extreme", 4}, {nullptr, 0}};
//-AVO

//- Mad Max
float ps_r2_gloss_factor = 6.0f; // [DA_PORT] was 4.0f (author's value)
//- Mad Max

//AVO: detail draw radius
class CCC_detail_radius : public CCC_Integer
{
public:
    void apply()
    {
        dm_current_size = iFloor((float)ps_r__detail_radius / 4) * 2;
        dm_current_cache1_line = dm_current_size * 2 / 4; // assuming cache1_count = 4
        dm_current_cache_line = dm_current_size + 1 + dm_current_size;
        dm_current_cache_size = dm_current_cache_line * dm_current_cache_line;
        dm_current_fade = float(2 * dm_current_size) - .5f;
    }

    CCC_detail_radius(LPCSTR N, int* V, int _min = 0, int _max = 999) : CCC_Integer(N, V, _min, _max) {};

    void Execute(LPCSTR args) override
    {
        CCC_Integer::Execute(args);
        apply();
    }

    void GetStatus(TStatus& S) override
    {
        CCC_Integer::GetStatus(S);
    }
};
//-AVO

class CCC_tf_Aniso : public CCC_Integer
{
public:
    void apply()
    {
#if defined(USE_DX11)
        if (nullptr == HW.pDevice)
            return;
#endif
        int val = *value;
        clamp(val, 1, 16);
#if defined(USE_DX11)
        SSManager.SetMaxAnisotropy(val);
#elif defined(USE_OGL)
        // OGL: don't set aniso here because it will be updated after vid restart
#else
#   error No graphics API selected or enabled!
#endif
    }
    CCC_tf_Aniso(LPCSTR N, int* v) : CCC_Integer(N, v, 1, 16){};
    virtual void Execute(LPCSTR args)
    {
        CCC_Integer::Execute(args);
        apply();
    }
    virtual void GetStatus(TStatus& S)
    {
        CCC_Integer::GetStatus(S);
        apply();
    }
};
class CCC_tf_MipBias : public CCC_Float
{
public:
    void apply()
    {
#if defined(USE_DX11)
        if (nullptr == HW.pDevice)
            return;

        SSManager.SetMipLODBias(*value);
#endif
    }

    CCC_tf_MipBias(LPCSTR N, float* v) : CCC_Float(N, v, -3.f, +3.f) {}
    virtual void Execute(LPCSTR args)
    {
        CCC_Float::Execute(args);
        apply();
    }
    virtual void GetStatus(TStatus& S)
    {
        CCC_Float::GetStatus(S);
        apply();
    }
};
class CCC_R2GM : public CCC_Float
{
public:
    CCC_R2GM(LPCSTR N, float* v) : CCC_Float(N, v, 0.f, 4.f) { *v = 0; };
    virtual void Execute(LPCSTR args)
    {
        if (0 == xr_strcmp(args, "on"))
        {
            ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL, TRUE);
        }
        else if (0 == xr_strcmp(args, "off"))
        {
            ps_r2_ls_flags.set(R2FLAG_GLOBALMATERIAL, FALSE);
        }
        else
        {
            CCC_Float::Execute(args);
            if (ps_r2_ls_flags.test(R2FLAG_GLOBALMATERIAL))
            {
                static LPCSTR name[4] = {"oren", "blin", "phong", "metal"};
                float mid = *value;
                int m0 = iFloor(mid) % 4;
                int m1 = (m0 + 1) % 4;
                float frc = mid - float(iFloor(mid));
                Msg("* material set to [%s]-[%s], with lerp of [%f]", name[m0], name[m1], frc);
            }
        }
    }
};
class CCC_Screenshot : public IConsole_Command
{
public:
    CCC_Screenshot(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if (GEnv.isDedicatedServer)
            return;

        string_path name;
        name[0] = 0;
        sscanf(args, "%s", name);
        LPCSTR image = xr_strlen(name) ? name : 0;
        RImplementation.Screenshot(IRender::SM_NORMAL, image);
    }
};

class CCC_ModelPoolStat : public IConsole_Command
{
public:
    CCC_ModelPoolStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR /*args*/) { RImplementation.Models->dump(); }
};

// [DA_PORT] ---- Geometry cut-off (author's optimisation, see xrRender_console.h) -----------------
// Selecting a level rewrites the working thresholds; the render graph itself only ever reads the
// o_optimize_* values, so switching quality costs nothing per frame.
// NB the author's own inconsistency, kept deliberately: the level reads as 1 ("low") while the working
// thresholds start at MED. Until the console command runs, MED is what his build rendered with, so
// matching his behaviour means keeping the initialisers as they are rather than "fixing" them.
//
// [DA_PORT] Both levels default to off, unlike the author's 1/1. This subsystem comes from his
// unfinished x64 alpha and never shipped: the reference x32 installs have no r__optimize_* keys in
// user.ltx at all, so the released mod drew everything the frustum passed. Turning it on drops small
// geometry outright once it is far enough, which read as plants blinking out and back in.
//
// ⚠ The two levels do NOT switch the whole thing off. The shadow-map branch is gated on
// ps_r_high_optimize_sun_shad instead, so with the defaults below a player who sees "cut-off: off"
// still gets the harshest (ULT) thresholds applied to the sun cascades. That is the author's own
// coupling, faithfully ported - but he shipped 1/1/1, whereas 0/0/1 is a state he never had. It is
// kept because the shadow pass is where nearly all of the saving is and the audience runs weak GPUs;
// for behaviour identical to the released x32, set this to 0 as well.
//
// The far-cascade cut-off is safe by construction: r2_sun_far is clamped to 180 by its console
// command, so the 192 below can never clip a cascade that is actually being rendered.
u32 ps_r_optimize_static = 0;
u32 ps_r_optimize_dynamic = 0;
int ps_r_high_optimize_sun_shad = 1;
float ps_r2_sun_shadows_far_casc = 192.f;

// [DA_PORT] Счётчик кадров для da_sun_log (замер по каскадам солнца). 0 = молчит.
int ps_da_sun_log = 0;

// [DA_PORT] da_sun_only N: накапливать солнечный свет только от каскада N (1..3), 0 = все.
// Замер под мерцание тени; см. render_sun::accumulate_cascade.
int ps_da_sun_only = 0;


float o_optimize_static_l1_dist = O_S_L1_D_MED;
float o_optimize_static_l1_size = O_S_L1_S_MED;
float o_optimize_static_l2_dist = O_S_L2_D_MED;
float o_optimize_static_l2_size = O_S_L2_S_MED;
float o_optimize_static_l3_dist = O_S_L3_D_MED;
float o_optimize_static_l3_size = O_S_L3_S_MED;
float o_optimize_static_l4_dist = O_S_L4_D_MED;
float o_optimize_static_l4_size = O_S_L4_S_MED;
float o_optimize_static_l5_dist = O_S_L5_D_MED;
float o_optimize_static_l5_size = O_S_L5_S_MED;

float o_optimize_dynamic_l1_dist = O_D_L1_D_MED;
float o_optimize_dynamic_l1_size = O_D_L1_S_MED;
float o_optimize_dynamic_l2_dist = O_D_L2_D_MED;
float o_optimize_dynamic_l2_size = O_D_L2_S_MED;
float o_optimize_dynamic_l3_dist = O_D_L3_D_MED;
float o_optimize_dynamic_l3_size = O_D_L3_S_MED;

// [DA_PORT] Presets for the shadow-casting light ceiling, exposed in the video options.
// The token value IS the budget, so the console still reads as a plain number.
// [DA_PORT] One-touch performance preset for the Performance tab.
//
// Applies the whole tab at once by running each setting's own console command, not by poking the
// variables: r__optimize_* recompute their distance thresholds inside Execute, and r2_smap_size has to
// go through its own handler too. Writing the variables directly would set the number and skip the work.
// The ordering of the rows below matches what was actually measured - the shadow settings are the ones
// that move the frame, the rest is trim.
//
// The ten commands this runs belong to THIS tab and to nothing else. The mod's own quality presets
// (gamedata/configs/rspec_*.ltx, reached through `_preset` / CCC_Preset below) deliberately do not
// name any of them: two controls writing the same variable overwrite each other, and the player has
// no way to tell which one won. Adding a knob here means removing it from those five files.
//
// [DA_PORT] Пятый пункт («Ультра-производительность», st_opt_perf_max_fps) убран намеренно:
// он гасил тени у ВСЕХ источников разом и обрезал видимость до предела — картинка ломалась заметнее,
// чем росли кадры, а «Низкие» и так покрывают слабые машины. Строка st_opt_perf_max_fps в
// text/*/st_da_port_ui.xml оставлена намеренно: вернуть пункт = одна строка здесь плюс шестой ряд ниже.
// [DA_PORT] Пятый пункт списка — «Своё». Он ничего не применяет, и это не заглушка, а починка.
//
// Набор сохранялся в user.ltx наравне с остальными строками и при следующем запуске выполнялся
// СНОВА, переписывая всё, что игрок настроил вручную. Причём последним: строки конфига идут по
// алфавиту, а `r__perf_preset` стоит после `r__detail_*`, `r__geometry_lod` и `r2_*`. Со стороны это
// выглядело как «настройки производительности не сохраняются» — хотя сохранялись они исправно, их
// затирал набор.
//
// Теперь набор — это ДЕЙСТВИЕ, а не состояние: как только текущие значения перестают совпадать с
// каким-либо из наборов, список показывает «Своё», в конфиг уходит оно же, и при загрузке не
// применяется ничего. Выбор набора руками работает как раньше.
constexpr u32 PERF_PRESET_CUSTOM = 4;

xr_token q_perf_preset[] = {
    { "st_opt_perf_low", 0 },
    { "st_opt_perf_medium", 1 },
    { "st_opt_perf_high", 2 },
    { "st_opt_perf_ultra", 3 },
    { "st_opt_perf_custom", PERF_PRESET_CUSTOM },
    { nullptr, 0 },
};
u32 ps_r__perf_preset = 1;

namespace
{
struct Preset
{
    pcstr shadow_lights, sun_far, smap, opt_static, opt_dyn;
    pcstr vis_dist, detail_radius, detail_density, geometry_lod, detail_height;
};

// low, medium, high, ultra
const Preset s_perf_presets[4] = {
    { "st_opt_shadow_lights_low",    "51",  "1024", "st_optimize_high", "st_optimize_high",
      "0.8", "60",  "0.75", "0.7", "1.0" },
    { "st_opt_shadow_lights_medium", "51",  "1024", "st_optimize_med",  "st_optimize_med",
      "1.0", "100", "0.5",  "1.0", "1.3" },
    { "st_opt_shadow_lights_high",   "80",  "2048", "st_optimize_low",  "st_optimize_low",
      "1.0", "150", "0.4",  "1.3", "1.5" },
    { "st_opt_shadow_lights_high",   "120", "2048", "st_optimize_off",  "st_optimize_off",
      "1.0", "200", "0.3",  "1.6", "1.8" },
};

// Сверяем через саму консоль, а не через десяток внешних переменных: так список настроек набора
// остаётся в одном месте, и добавить в него строку — значит дописать её только в таблицу выше.
bool da_perf_value_matches(pcstr command, pcstr expected)
{
    IConsole_Command* cc = Console->GetCommand(command);
    if (!cc)
        return false;

    IConsole_Command::TStatus current;
    current[0] = 0;
    cc->GetStatus(current);

    // Числа сравниваем числами: «0.75» и «0.750000» — одно значение, а строки разные.
    const bool numeric = expected[0] == '-' || expected[0] == '.' || (expected[0] >= '0' && expected[0] <= '9');
    if (numeric)
        return _abs(float(atof(current)) - float(atof(expected))) < 0.001f;

    return 0 == xr_stricmp(current, expected);
}

// Какому набору соответствуют текущие значения; PERF_PRESET_CUSTOM — ни одному.
u32 da_perf_detect_preset()
{
    for (u32 i = 0; i < 4; ++i)
    {
        const Preset& p = s_perf_presets[i];
        if (da_perf_value_matches("r__light_shadow_budget", p.shadow_lights) &&
            da_perf_value_matches("r2_sun_far", p.sun_far) &&
            da_perf_value_matches("r2_smap_size", p.smap) &&
            da_perf_value_matches("r__optimize_static_geom", p.opt_static) &&
            da_perf_value_matches("r__optimize_dynamic_geom", p.opt_dyn) &&
            da_perf_value_matches("rs_vis_distance", p.vis_dist) &&
            da_perf_value_matches("r__detail_radius", p.detail_radius) &&
            da_perf_value_matches("r__detail_density", p.detail_density) &&
            da_perf_value_matches("r__geometry_lod", p.geometry_lod) &&
            da_perf_value_matches("r__detail_height", p.detail_height))
            return i;
    }
    return PERF_PRESET_CUSTOM;
}
} // namespace

class CCC_PerfPreset : public CCC_Token
{
public:
    CCC_PerfPreset(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    // [DA_PORT] Отсюда значение и берут: и список настроек для показа, и Save для записи в конфиг
    // (IConsole_Command::Save печатает именно GetStatus). Поэтому пересчёт стоит здесь — одно место
    // на оба случая, и «Своё» попадает в конфиг само, без отдельного обработчика на каждую строку.
    void GetStatus(TStatus& S) override
    {
        *value = da_perf_detect_preset();
        CCC_Token::GetStatus(S);
    }

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);

        // «Своё» не применяет ничего - см. пояснение у PERF_PRESET_CUSTOM. Именно эта ветка и
        // отрабатывает при загрузке user.ltx у того, кто настраивал значения вручную.
        if (*value >= PERF_PRESET_CUSTOM)
            return;

        const Preset& p = s_perf_presets[*value];
        string256 cmd;

        xr_sprintf(cmd, "r__light_shadow_budget %s", p.shadow_lights);   Console->Execute(cmd);
        xr_sprintf(cmd, "r2_sun_far %s", p.sun_far);                     Console->Execute(cmd);

        // [DA_PORT] Размер теневой карты - единственное в наборе, что не применяется до перезапуска
        // ИГРЫ, и не по недосмотру: он попадает в шейдеры как дефайн (r4_shaders.cpp, c_smap), то
        // есть меняет их сборку. Сброс устройства пересоздаёт цели рендера, но шейдеры берутся из
        // кэша - получились бы цели одного размера и шейдеры под другой, а это тихое расхождение,
        // которое проявится кривыми тенями, а не сообщением.
        //
        // Поэтому строка остаётся, но о ней говорится вслух: остальное из набора применится сразу,
        // это - со следующего запуска.
        if (ps_r2_smapsize != (u32)atoi(p.smap))
            Msg("~ [DA_PORT] набор настроек: размер теневой карты %s применится после перезапуска игры",
                p.smap);
        xr_sprintf(cmd, "r2_smap_size %s", p.smap);                      Console->Execute(cmd);
        xr_sprintf(cmd, "r__optimize_static_geom %s", p.opt_static);     Console->Execute(cmd);
        xr_sprintf(cmd, "r__optimize_dynamic_geom %s", p.opt_dyn);       Console->Execute(cmd);
        xr_sprintf(cmd, "rs_vis_distance %s", p.vis_dist);               Console->Execute(cmd);
        xr_sprintf(cmd, "r__detail_radius %s", p.detail_radius);         Console->Execute(cmd);
        xr_sprintf(cmd, "r__detail_density %s", p.detail_density);       Console->Execute(cmd);
        xr_sprintf(cmd, "r__geometry_lod %s", p.geometry_lod);           Console->Execute(cmd);
        xr_sprintf(cmd, "r__detail_height %s", p.detail_height);         Console->Execute(cmd);
    }
};

// [DA_PORT] Note the first entry, and that it is 0 rather than a count: 0 means "no budget at all",
// the stock behaviour where every shadowing light gets its own pass. Without it the menu could not
// express what the engine did before this setting existed - the list started at 1, so a player whose
// lamps had stopped casting had no way back to the original behaviour, and no way to tell our cap
// apart from a broken level. That is a diagnostic dead end, which is why the entry exists.
//
// "off" is kept but it is NOT the absence of a limit - it is a budget of one, i.e. the harshest
// setting in the list. The two sit at opposite ends and their names are one letter apart in meaning,
// so the label matters: it reads "Минимум", not "Отключить".
xr_token q_light_shadow_budget[] = {
    { "st_opt_shadow_lights_unlimited", 0 },
    { "st_opt_shadow_lights_off", 1 },
    { "st_opt_shadow_lights_low", 6 },
    { "st_opt_shadow_lights_medium", 12 },
    { "st_opt_shadow_lights_high", 18 },
    { nullptr, 0 },
};

// [DA_PORT] r2_sun_details: у автора это ТРИ состояния, у нас остаётся флаг — и это осознанно.
//
// В альфе DA он заменён на CCC_Token с ps_r_sun_details (st_opt_off/medium/high), и разница между
// «средне» и «высоко» — в том, КТО чистит списки видимой травы: при 2 их чистит UpdateVisibleM раз в
// кадр, при 1 — сам проход отрисовки, и только вне теневой фазы.
//
// Не переносим по двум причинам. Первая: нашего DetailManager_VS это не касается вовсе — там другой
// путь, без clear_not_free, а трава попадает в теневую карту по тесту R2FLAG_SUN_DETAILS. Вторая, и
// решающая: пресеты качества самого мода пишут сюда `on`, то есть булеву форму. Токен сломал бы их
// собственные данные, а авторская альфа, судя по этому, отвергала бы свои же пресеты — она здесь в
// середине переделки, как это уже было с power_loss у шлемов.
//
// Строка `r2_sun_details on` из rspec_extreme.ltx действительно не применялась, но виноват был
// хвостовой пробел в конфиге, а не тип команды. Починено в remove_spaces (line_edit_control.cpp).

xr_token q_optimize_geom[] = {
    { "st_optimize_off", 0 },
    { "st_optimize_low", 1 },
    { "st_optimize_med", 2 },
    { "st_optimize_high", 3 },
    { nullptr, 0 },
};

class CCC_OptimizeStatic : public CCC_Token
{
public:
    CCC_OptimizeStatic(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        switch (*value)
        {
        case 0:
            break; // culling disabled entirely — the checks bail out on ps_r_optimize_static < 1
        case 1:
            o_optimize_static_l1_dist = O_S_L1_D_LOW; o_optimize_static_l1_size = O_S_L1_S_LOW;
            o_optimize_static_l2_dist = O_S_L2_D_LOW; o_optimize_static_l2_size = O_S_L2_S_LOW;
            o_optimize_static_l3_dist = O_S_L3_D_LOW; o_optimize_static_l3_size = O_S_L3_S_LOW;
            o_optimize_static_l4_dist = O_S_L4_D_LOW; o_optimize_static_l4_size = O_S_L4_S_LOW;
            o_optimize_static_l5_dist = O_S_L5_D_LOW; o_optimize_static_l5_size = O_S_L5_S_LOW;
            break;
        case 2:
            o_optimize_static_l1_dist = O_S_L1_D_MED; o_optimize_static_l1_size = O_S_L1_S_MED;
            o_optimize_static_l2_dist = O_S_L2_D_MED; o_optimize_static_l2_size = O_S_L2_S_MED;
            o_optimize_static_l3_dist = O_S_L3_D_MED; o_optimize_static_l3_size = O_S_L3_S_MED;
            o_optimize_static_l4_dist = O_S_L4_D_MED; o_optimize_static_l4_size = O_S_L4_S_MED;
            o_optimize_static_l5_dist = O_S_L5_D_MED; o_optimize_static_l5_size = O_S_L5_S_MED;
            break;
        default:
            o_optimize_static_l1_dist = O_S_L1_D_HII; o_optimize_static_l1_size = O_S_L1_S_HII;
            o_optimize_static_l2_dist = O_S_L2_D_HII; o_optimize_static_l2_size = O_S_L2_S_HII;
            o_optimize_static_l3_dist = O_S_L3_D_HII; o_optimize_static_l3_size = O_S_L3_S_HII;
            o_optimize_static_l4_dist = O_S_L4_D_HII; o_optimize_static_l4_size = O_S_L4_S_HII;
            o_optimize_static_l5_dist = O_S_L5_D_HII; o_optimize_static_l5_size = O_S_L5_S_HII;
            break;
        }
    }
};

class CCC_OptimizeDynamic : public CCC_Token
{
public:
    CCC_OptimizeDynamic(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        switch (*value)
        {
        case 0:
            break; // culling disabled entirely — the checks bail out on ps_r_optimize_dynamic < 1
        case 1:
            o_optimize_dynamic_l1_dist = O_D_L1_D_LOW; o_optimize_dynamic_l1_size = O_D_L1_S_LOW;
            o_optimize_dynamic_l2_dist = O_D_L2_D_LOW; o_optimize_dynamic_l2_size = O_D_L2_S_LOW;
            o_optimize_dynamic_l3_dist = O_D_L3_D_LOW; o_optimize_dynamic_l3_size = O_D_L3_S_LOW;
            break;
        case 2:
            o_optimize_dynamic_l1_dist = O_D_L1_D_MED; o_optimize_dynamic_l1_size = O_D_L1_S_MED;
            o_optimize_dynamic_l2_dist = O_D_L2_D_MED; o_optimize_dynamic_l2_size = O_D_L2_S_MED;
            o_optimize_dynamic_l3_dist = O_D_L3_D_MED; o_optimize_dynamic_l3_size = O_D_L3_S_MED;
            break;
        default:
            o_optimize_dynamic_l1_dist = O_D_L1_D_HII; o_optimize_dynamic_l1_size = O_D_L1_S_HII;
            o_optimize_dynamic_l2_dist = O_D_L2_D_HII; o_optimize_dynamic_l2_size = O_D_L2_S_HII;
            o_optimize_dynamic_l3_dist = O_D_L3_D_HII; o_optimize_dynamic_l3_size = O_D_L3_S_HII;
            break;
        }
    }
};

class CCC_SSAO_Mode : public CCC_Token
{
public:
    CCC_SSAO_Mode(LPCSTR N, u32* V, const xr_token* T) : CCC_Token(N, V, T){};

    virtual void Execute(LPCSTR args)
    {
        CCC_Token::Execute(args);

        switch (*value)
        {
        case ssao_mode_off:
        {
            ps_r_ssao = 0;
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            break;
        }
        case ssao_mode_default:
        {
            if (ps_r_ssao == 0)
            {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
            break;
        }
        case ssao_mode_hdao:
        {
            if (ps_r_ssao == 0)
            {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 1);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HALF_DATA, 0);
            break;
        }
        case ssao_mode_hbao:
        {
            if (ps_r_ssao == 0)
            {
                ps_r_ssao = 1;
            }
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HBAO, 1);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_HDAO, 0);
            ps_r2_ls_flags_ext.set(R2FLAGEXT_SSAO_OPT_DATA, 1);
            break;
        }
        }
    }
};

//-----------------------------------------------------------------------
class CCC_Preset : public CCC_Token
{
public:
    CCC_Preset(LPCSTR N, u32* V, const xr_token* T) : CCC_Token(N, V, T){};

    virtual void Execute(LPCSTR args)
    {
        CCC_Token::Execute(args);
        string_path _cfg;
        string_path cmd;

        switch (*value)
        {
        case 0: xr_strcpy(_cfg, "rspec_minimum.ltx"); break;
        case 1: xr_strcpy(_cfg, "rspec_low.ltx"); break;
        case 2: xr_strcpy(_cfg, "rspec_default.ltx"); break;
        case 3: xr_strcpy(_cfg, "rspec_high.ltx"); break;
        case 4: xr_strcpy(_cfg, "rspec_extreme.ltx"); break;
        }
        FS.update_path(_cfg, "$game_config$", _cfg);
        strconcat(sizeof(cmd), cmd, "cfg_load", " ", _cfg);
        Console->Execute(cmd);
    }
};

// [DA_PORT] Multisampling may not run next to a RECONSTRUCTING upscaler, so picking it clears those -
// exactly as picking one of them clears this (da_apply_upscaler, xr_ioc_cmd.cpp).
//
// Why they cannot share a frame: FSR/XeSS/DLSS reconstruct edges FROM the jittered samples of several
// frames, and MSAA resolves those same edges inside one frame before they ever see them - the
// sub-pixel information they exist to use is gone, and the extra samples were paid for anyway. Worse,
// rt_Velocity and rt_Reactive are single-sample by construction while the rest of the G-buffer follows
// the MSAA count; with vectors on they are bound together, and D3D11 refuses a mismatched set without
// a word.
//
// ⚠ Our own temporal AA (entry 1 in the list) is NOT one of them. It is absent from
// da_upscaler_active(), so it never switches the velocity buffer on, the scene pass binds no extra
// targets, and MSAA is free to run beside it - the two cover different things, geometry edges against
// shading and specular. Hence the `> 1` below rather than a bare non-zero test.
//
// The menu already hides this row for the reconstructing choices, so through the interface the
// conflict cannot arise. This exists for the console, where it can.
//
// No loop is possible: each side acts only when ITS OWN setting is set, and each hands the other a
// zero. "r3_msaa 4x" clears the upscaler, whose handler then sees zero and stops.
class CCC_MSAA : public CCC_Token
{
public:
    CCC_MSAA(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        // Global scope: the declaration lives outside this namespace, see the top of the file.
        if (*value != 0 && ::ps_r__upscaler > 1)
            Console->Execute("r__upscaler ui_mm_upscaler_off");
    }
};

// [DA_PORT] Applies one of the grading profiles. Values mirror the .ltx profiles shipped in
// gamedata/configs/da_grade_*.ltx, so console, menu and files never disagree.
class CCC_GradingPreset : public CCC_Token
{
public:
    CCC_GradingPreset(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);

        struct grade { float r, g, b, v; };
        static const grade profiles[] =
        {
            { 1.00f, 1.00f, 1.00f,  0.00f }, // original - exactly as the mod shipped
            { 1.04f, 1.00f, 0.96f,  0.18f }, // port default - gently warm
            { 1.10f, 1.02f, 0.90f,  0.30f }, // golden autumn
            { 0.96f, 1.00f, 1.08f,  0.10f }, // cold Zone
            { 1.02f, 1.00f, 0.98f, -0.25f }, // faded film
            { 1.06f, 1.02f, 0.98f,  0.40f }, // vivid
        };

        const u32 idx = *value < std::size(profiles) ? *value : 1;
        ps_r_color_base_r = profiles[idx].r;
        ps_r_color_base_g = profiles[idx].g;
        ps_r_color_base_b = profiles[idx].b;
        ps_r2_vibrance_val = profiles[idx].v;
    }
};

class CCC_memory_stats : public IConsole_Command
{
public:
    CCC_memory_stats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR /*args*/)
    {
        // TODO: OGL: Implement memory usage statistics.
#if defined(USE_DX11)
        u32 m_base = 0;
        u32 c_base = 0;
        u32 m_lmaps = 0;
        u32 c_lmaps = 0;

        RImplementation.ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);

        Msg("memory usage  mb \t \t video    \t managed      \t system \n");

        const float MiB = 1024*1024; // XXX: use it as common enum value (like in X-Ray 2.0)
        const u32* mem_usage = HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_vertex];

        float vb_video = mem_usage[D3DPOOL_DEFAULT] / MiB;
        float vb_managed = mem_usage[D3DPOOL_MANAGED] / MiB;
        float vb_system = mem_usage[D3DPOOL_SYSTEMMEM] / MiB;
        Msg("vertex buffer      \t \t %f \t %f \t %f ", vb_video, vb_managed, vb_system);

        float ib_video = mem_usage[D3DPOOL_DEFAULT] / MiB;
        float ib_managed = mem_usage[D3DPOOL_MANAGED] / MiB;
        float ib_system = mem_usage[D3DPOOL_SYSTEMMEM] / MiB;
        Msg("index buffer      \t \t %f \t %f \t %f ", ib_video, ib_managed, ib_system);

        float textures_video = (m_base+m_lmaps)/MiB;
        Msg("textures          \t \t %f \t %f \t %f ", textures_video, 0.f, 0.f);

        mem_usage = HW.stats_manager.memory_usage_summary[enum_stats_buffer_type_rtarget];
        float rt_video = mem_usage[D3DPOOL_DEFAULT] / MiB;
        float rt_managed = mem_usage[D3DPOOL_MANAGED] / MiB;
        float rt_system = mem_usage[D3DPOOL_SYSTEMMEM] / MiB;
        Msg("R-Targets         \t \t %f \t %f \t %f ", rt_video, rt_managed, rt_system);

        Msg("\nTotal             \t \t %f \t %f \t %f ", vb_video + ib_video + textures_video + rt_video,
            vb_managed + ib_managed + rt_managed, vb_system + ib_system + rt_system);
#endif // !USE_OGL
    }
};

class CCC_DumpResources final : public IConsole_Command
{
public:
    CCC_DumpResources(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        RImplementation.Models->dump();
        RImplementation.Resources->Dump(false);
    }
};

class CCC_MotionsStat final : public IConsole_Command
{
public:
    CCC_MotionsStat(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        g_pMotionsContainer->dump();
    }
};

class CCC_TexturesStat final : public IConsole_Command
{
public:
    CCC_TexturesStat(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        RImplementation.Resources->_DumpMemoryUsage();
    }
};

#if RENDER != R_R1
class CCC_BuildSSA : public IConsole_Command
{
public:
    CCC_BuildSSA(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR /*args*/)
    {
        r_pixel_calculator c;
        c.run();
    }
};
#endif

class CCC_DofFar : public CCC_Float
{
public:
    CCC_DofFar(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f) : CCC_Float(N, V, _min, _max) {}
    virtual void Execute(LPCSTR args)
    {
        float v = float(atof(args));

        if (v < ps_r2_dof.y + 0.1f)
        {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value greater or equal to r2_dof_focus+0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_focus");
        }
        else
        {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    //  CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* /*F*/) { ; }
};

class CCC_DofNear : public CCC_Float
{
public:
    CCC_DofNear(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f) : CCC_Float(N, V, _min, _max) {}
    virtual void Execute(LPCSTR args)
    {
        float v = float(atof(args));

        if (v > ps_r2_dof.y - 0.1f)
        {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value less or equal to r2_dof_focus-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_focus");
        }
        else
        {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    // CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* /*F*/) { ; }
};

class CCC_DofFocus : public CCC_Float
{
public:
    CCC_DofFocus(LPCSTR N, float* V, float _min = 0.0f, float _max = 10000.0f) : CCC_Float(N, V, _min, _max) {}
    virtual void Execute(LPCSTR args)
    {
        float v = float(atof(args));

        if (v > ps_r2_dof.z - 0.1f)
        {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value less or equal to r2_dof_far-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_far");
        }
        else if (v < ps_r2_dof.x + 0.1f)
        {
            char pBuf[256];
            _snprintf(pBuf, sizeof(pBuf) / sizeof(pBuf[0]), "float value greater or equal to r2_dof_far-0.1");
            Msg("~ Invalid syntax in call to '%s'", cName);
            Msg("~ Valid arguments: %s", pBuf);
            Console->Execute("r2_dof_near");
        }
        else
        {
            CCC_Float::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }

    //  CCC_Dof should save all data as well as load from config
    virtual void Save(IWriter* /*F*/) { ; }
};

class CCC_Dof : public CCC_Vector3
{
public:
    CCC_Dof(LPCSTR N, Fvector* V, const Fvector _min, const Fvector _max) : CCC_Vector3(N, V, _min, _max) { ; }
    virtual void Execute(LPCSTR args)
    {
        Fvector v;
        if (3 != sscanf(args, "%f,%f,%f", &v.x, &v.y, &v.z))
            InvalidSyntax();
        else if ((v.x > v.y - 0.1f) || (v.z < v.y + 0.1f))
        {
            InvalidSyntax();
            Msg("x <= y - 0.1");
            Msg("y <= z - 0.1");
        }
        else
        {
            CCC_Vector3::Execute(args);
            if (g_pGamePersistent)
                g_pGamePersistent->SetBaseDof(ps_r2_dof);
        }
    }
    virtual void GetStatus(TStatus& S) { xr_sprintf(S, "%f,%f,%f", value->x, value->y, value->z); }
    virtual void Info(TInfo& I)
    {
        xr_sprintf(I, "vector3 in range [%f,%f,%f]-[%f,%f,%f]", min.x, min.y, min.z, max.x, max.y, max.z);
    }
};

//  Allow real-time fog config reload
#if (RENDER == R_R3) || (RENDER == R_R4)
#   ifndef MASTER_GOLD
class CCC_Fog_Reload : public IConsole_Command
{
public:
    CCC_Fog_Reload(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR /*args*/) { FluidManager.UpdateProfiles(); }
};
#   endif // MASTER_GOLD
#endif // (RENDER == R_R3) || (RENDER == R_R4)

//-----------------------------------------------------------------------
void xrRender_initconsole()
{
    ZoneScoped;

    CMD3(CCC_Preset, "_preset", &ps_Preset, qpreset_token);

    CMD4(CCC_Integer, "rs_skeleton_update", &psSkeletonUpdate, 2, 128);
#ifndef MASTER_GOLD
    CMD1(CCC_DumpResources, "dump_resources");
    CMD1(CCC_MotionsStat, "stat_motions");
    CMD1(CCC_TexturesStat, "stat_textures");
#endif

    CMD4(CCC_Float, "r__dtex_range", &r__dtex_range, 5, 175);

    // Common
    CMD1(CCC_Screenshot, "screenshot");

#ifdef DEBUG
#if RENDER != R_R1
    CMD1(CCC_BuildSSA, "build_ssa");
#endif
    CMD4(CCC_Integer, "r__lsleep_frames", &ps_r__LightSleepFrames, 4, 30);
    CMD4(CCC_Float, "r__ssa_glod_start", &ps_r__GLOD_ssa_start, 128, 512);
    CMD4(CCC_Float, "r__ssa_glod_end", &ps_r__GLOD_ssa_end, 16, 96);
    CMD4(CCC_Float, "r__wallmark_shift_pp", &ps_r__WallmarkSHIFT, 0.0f, 1.f);
    CMD4(CCC_Float, "r__wallmark_shift_v", &ps_r__WallmarkSHIFT_V, 0.0f, 1.f);
    CMD1(CCC_ModelPoolStat, "stat_models");
#endif // DEBUG
    CMD4(CCC_Float, "r__wallmark_ttl", &ps_r__WallmarkTTL, 1.0f, 10.f * 60.f);

    CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 8);

    CMD4(CCC_Float, "r__geometry_lod", &ps_r__LOD, 0.1f, 2.f);

    // [DA_PORT] Geometry cut-off, ported from the author's build. Defaults match his: both levels on
    // "low" (i.e. the gentlest culling) and the harsher shadow-map pass enabled.
    // [DA_PORT] Render breakdown into the log, N frames. Needs rs_stats 1 as well: the sub-counters are
    // only totalled up inside DumpStatistics, so with the overlay off there is nothing to print.
    {
        extern int ps_da_render_log;
        CMD4(CCC_Integer, "da_render_log", &ps_da_render_log, 0, 2000);
        // [DA_PORT] Замер по каскадам солнца: da_sun_log N печатает N кадров подряд. Против
        // мерцания целой тени на улице — см. комментарий в render_phase_sun.cpp::calculate.
        CMD4(CCC_Integer, "da_sun_log", &ps_da_sun_log, 0, 2000);
        CMD4(CCC_Integer, "da_sun_only", &ps_da_sun_only, 0, 3);
    }
#if RENDER == R_R4
    // [DA_PORT] GPU time per render phase, N frames into the log. See da_gpu_timer.h.
    {
        extern int ps_da_gpu_log;
        CMD4(CCC_Integer, "da_gpu_log", &ps_da_gpu_log, 0, 2000);
    }
#endif
    CMD3(CCC_OptimizeStatic, "r__optimize_static_geom", &ps_r_optimize_static, q_optimize_geom);
    CMD3(CCC_OptimizeDynamic, "r__optimize_dynamic_geom", &ps_r_optimize_dynamic, q_optimize_geom);
    CMD4(CCC_Integer, "r__optimize_sun_shad", &ps_r_high_optimize_sun_shad, 0, 1);
    //CMD4(CCC_Float, "r__geometry_lod_pow", &ps_r__LOD_Power, 0, 2);

    CMD4(CCC_Float, "r__detail_density", &ps_current_detail_density/*&ps_r__Detail_density*/, 0.1f, 0.99f);
    CMD3(CCC_PerfPreset, "r__perf_preset", &ps_r__perf_preset, q_perf_preset); // [DA_PORT]
    CMD4(CCC_detail_radius, "r__detail_radius", &ps_r__detail_radius, 49, 300);
    CMD3(CCC_Token, "r__light_shadow_budget", &ps_r__light_shadow_budget, q_light_shadow_budget); // [DA_PORT]
    CMD4(CCC_Float, "r__detail_height", &ps_r__Detail_height, 1, 2);

#ifdef DEBUG
    CMD4(CCC_Float, "r__detail_l_ambient", &ps_r__Detail_l_ambient, .5f, .95f);
    CMD4(CCC_Float, "r__detail_l_aniso", &ps_r__Detail_l_aniso, .1f, .5f);
#endif // DEBUG

    CMD3(CCC_Mask, "r__actor_shadow", &ps_r__common_flags, RFLAG_ACTOR_SHADOW);

    CMD2(CCC_tf_Aniso, "r__tf_aniso", &ps_r__tf_Anisotropic); // {1..16}
    CMD2(CCC_tf_MipBias, "r1_tf_mipbias", &ps_r__tf_Mipbias); // {-3 +3}
    CMD2(CCC_tf_MipBias, "r2_tf_mipbias", &ps_r__tf_Mipbias); // {-3 +3}

    CMD4(CCC_Integer, "r__clear_models_on_unload", &ps_r__clear_models_on_unload, 0, 1); // Alundaio

    // R1
    CMD4(CCC_Float, "r1_ssa_lod_a", &ps_r1_ssaLOD_A, 16, 96);
    CMD4(CCC_Float, "r1_ssa_lod_b", &ps_r1_ssaLOD_B, 16, 64);
    CMD4(CCC_Float, "r1_lmodel_lerp", &ps_r1_lmodel_lerp, 0, 0.333f);
    CMD3(CCC_Mask, "r1_dlights", &ps_r1_flags, R1FLAG_DLIGHTS);
    CMD4(CCC_Float, "r1_dlights_clip", &ps_r1_dlights_clip, 10.f, 150.f);
    CMD4(CCC_Float, "r1_pps_u", &ps_r1_pps_u, -1.f, +1.f);
    CMD4(CCC_Float, "r1_pps_v", &ps_r1_pps_v, -1.f, +1.f);
    CMD4(CCC_Integer, "r1_force_geomx", &ps_r1_force_geomx, 0, 1);

    // R1-specific
    CMD4(CCC_Integer, "r1_glows_per_frame", &ps_r1_GlowsPerFrame, 2, 32);
    CMD3(CCC_Mask, "r1_detail_textures", &ps_r2_ls_flags, R1FLAG_DETAIL_TEXTURES);

    CMD4(CCC_Float, "r1_fog_luminance", &ps_r1_fog_luminance, 0.2f, 5.f);

    // Software Skinning
    // 0 - disabled (renderer can override)
    // 1 - enabled
    // 2 - forced hardware skinning (renderer can not override)
    CMD4(CCC_Integer, "r1_software_skinning", &ps_r1_SoftwareSkinning, 0, 2);

    CMD3(CCC_Mask, "r1_ffp", &ps_r1_flags, R1FLAG_FFP);
    CMD3(CCC_Mask, "r1_ffp_lightmaps", &ps_r1_flags, R1FLAG_FFP_LIGHTMAPS);

    // R2
    CMD4(CCC_Float, "r2_ssa_lod_a", &ps_r2_ssaLOD_A, 16, 96);
    CMD4(CCC_Float, "r2_ssa_lod_b", &ps_r2_ssaLOD_B, 32, 64);

    // R2-specific
    CMD2(CCC_R2GM, "r2em", &ps_r2_gmaterial);
    CMD3(CCC_Mask, "r2_tonemap", &ps_r2_ls_flags, R2FLAG_TONEMAP);
    CMD4(CCC_Float, "r2_tonemap_middlegray", &ps_r2_tonemap_middlegray, 0.0f, 2.0f);
    CMD4(CCC_Float, "r2_tonemap_adaptation", &ps_r2_tonemap_adaptation, 0.01f, 10.0f);
    CMD4(CCC_Float, "r2_tonemap_lowlum", &ps_r2_tonemap_low_lum, 0.0001f, 1.0f);
    CMD4(CCC_Float, "r2_tonemap_amount", &ps_r2_tonemap_amount, 0.0000f, 1.0f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_scale", &ps_r2_ls_bloom_kernel_scale, 0.5f, 2.f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_g", &ps_r2_ls_bloom_kernel_g, 1.f, 7.f);
    CMD4(CCC_Float, "r2_ls_bloom_kernel_b", &ps_r2_ls_bloom_kernel_b, 0.01f, 1.f);
    CMD4(CCC_Float, "r2_ls_bloom_threshold", &ps_r2_ls_bloom_threshold, 0.f, 1.f);
    CMD4(CCC_Float, "r2_ls_bloom_speed", &ps_r2_ls_bloom_speed, 0.f, 100.f);
    CMD3(CCC_Mask, "r2_ls_bloom_fast", &ps_r2_ls_flags, R2FLAG_FASTBLOOM);
    CMD4(CCC_Float, "r2_ls_dsm_kernel", &ps_r2_ls_dsm_kernel, .1f, 3.f);
    CMD4(CCC_Float, "r2_ls_psm_kernel", &ps_r2_ls_psm_kernel, .1f, 3.f);
    CMD4(CCC_Float, "r2_ls_ssm_kernel", &ps_r2_ls_ssm_kernel, .1f, 3.f);
    CMD4(CCC_Float, "r2_ls_squality", &ps_r2_ls_squality, .5f, 1.f);

    CMD3(CCC_Mask, "r2_zfill", &ps_r2_ls_flags, R2FLAG_ZFILL);
    CMD4(CCC_Float, "r2_zfill_depth", &ps_r2_zfill, .001f, .5f);
    CMD3(CCC_Mask, "r2_allow_r1_lights", &ps_r2_ls_flags, R2FLAG_R1LIGHTS);

    //- Mad Max
    // [DA_PORT] The author's bounds, not the stock ones: he raised the ceiling to 100 and, more to the
    // point, set the floor at 1. Zero is not a darker setting but a broken one - it collapses the
    // specular term and the whole scene reads as flat and dead, which cost half a session once already.
    CMD4(CCC_Float, "r2_gloss_factor", &ps_r2_gloss_factor, 1.0f, 100.f);
//- Mad Max

#ifdef DEBUG
    CMD3(CCC_Mask, "r2_use_nvdbt", &ps_r2_ls_flags, R2FLAG_USE_NVDBT);
    CMD3(CCC_Mask, "r2_mt", &ps_r2_ls_flags, R2FLAG_EXP_MT_CALC);
#endif // DEBUG

    CMD3(CCC_Mask, "r2_sun", &ps_r2_ls_flags, R2FLAG_SUN);
    CMD3(CCC_Mask, "r2_sun_details", &ps_r2_ls_flags, R2FLAG_SUN_DETAILS);
    CMD3(CCC_Mask, "r2_sun_focus", &ps_r2_ls_flags, R2FLAG_SUN_FOCUS);
    //CMD3(CCC_Mask, "r2_sun_static", &ps_r2_ls_flags, R2FLAG_SUN_STATIC);
    //CMD3(CCC_Mask, "r2_exp_splitscene", &ps_r2_ls_flags, R2FLAG_EXP_SPLIT_SCENE);
    //CMD3(CCC_Mask, "r2_exp_donttest_uns", &ps_r2_ls_flags, R2FLAG_EXP_DONT_TEST_UNSHADOWED);
    CMD3(CCC_Mask, "r2_exp_donttest_shad", &ps_r2_ls_flags, R2FLAG_EXP_DONT_TEST_SHADOWED);

    CMD3(CCC_Mask, "r2_sun_tsm", &ps_r2_ls_flags, R2FLAG_SUN_TSM);
    CMD4(CCC_Float, "r2_sun_tsm_proj", &ps_r2_sun_tsm_projection, .001f, 0.8f);
    CMD4(CCC_Float, "r2_sun_tsm_bias", &ps_r2_sun_tsm_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_near", &ps_r2_sun_near, 1.f, 150.f); //AVO: extended from 50.f to 150.f
#if RENDER != R_R1
    CMD4(CCC_Float, "r2_sun_far", &ps_r2_sun_far, 51.f, 180.f);
#endif
    CMD4(CCC_Float, "r2_sun_near_border", &ps_r2_sun_near_border, .5f, 1.0f);
    CMD4(CCC_Float, "r2_sun_depth_far_scale", &ps_r2_sun_depth_far_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_sun_depth_far_bias", &ps_r2_sun_depth_far_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_depth_near_scale", &ps_r2_sun_depth_near_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_sun_depth_near_bias", &ps_r2_sun_depth_near_bias, -0.5, +0.5);
    CMD4(CCC_Float, "r2_sun_lumscale", &ps_r2_sun_lumscale, -1.0, +3.0);
    CMD4(CCC_Float, "r2_sun_lumscale_hemi", &ps_r2_sun_lumscale_hemi, 0.0, +3.0);
    CMD4(CCC_Float, "r2_sun_lumscale_amb", &ps_r2_sun_lumscale_amb, 0.0, +3.0);

    CMD3(CCC_Mask, "r2_aa", &ps_r2_ls_flags, R2FLAG_AA);
    CMD4(CCC_Float, "r2_aa_kernel", &ps_r2_aa_kernel, 0.3f, 0.7f);
    CMD4(CCC_Float, "r2_mblur", &ps_r2_mblur, 0.0f, 1.0f);

    CMD3(CCC_Mask, "r2_gi", &ps_r2_ls_flags, R2FLAG_GI);
    CMD4(CCC_Float, "r2_gi_clip", &ps_r2_GI_clip, EPS, 0.1f);
    CMD4(CCC_Integer, "r2_gi_depth", &ps_r2_GI_depth, 1, 5);
    CMD4(CCC_Integer, "r2_gi_photons", &ps_r2_GI_photons, 8, 256);
    CMD4(CCC_Float, "r2_gi_refl", &ps_r2_GI_refl, EPS_L, 0.99f);

    CMD4(CCC_Integer, "r2_wait_sleep", &ps_r2_wait_sleep, 0, 1);
    CMD4(CCC_Integer, "r2_wait_timeout", &ps_r2_wait_timeout, 100, 1000);

#ifndef MASTER_GOLD
    CMD4(CCC_Integer, "r2_dhemi_count", &ps_r2_dhemi_count, 4, 25);
    CMD4(CCC_Float, "r2_dhemi_sky_scale", &ps_r2_dhemi_sky_scale, 0.0f, 100.f);
    CMD4(CCC_Float, "r2_dhemi_light_scale", &ps_r2_dhemi_light_scale, 0, 100.f);
    CMD4(CCC_Float, "r2_dhemi_light_flow", &ps_r2_dhemi_light_flow, 0, 1.f);
    CMD4(CCC_Float, "r2_dhemi_smooth", &ps_r2_lt_smooth, 0.f, 10.f);
    CMD3(CCC_Mask, "rs_hom_depth_draw", &ps_r2_ls_flags_ext, R_FLAGEXT_HOM_DEPTH_DRAW);
    CMD3(CCC_Mask, "r2_shadow_cascede_zcul", &ps_r2_ls_flags_ext, R2FLAGEXT_SUN_ZCULLING);
    CMD3(CCC_Mask, "r2_shadow_cascede_old", &ps_r2_ls_flags_ext, R2FLAGEXT_SUN_OLD);

#endif // DEBUG

    CMD4(CCC_Float, "r2_ls_depth_scale", &ps_r2_ls_depth_scale, 0.5, 1.5);
    CMD4(CCC_Float, "r2_ls_depth_bias", &ps_r2_ls_depth_bias, -0.5, +0.5);

    CMD4(CCC_Float, "r2_parallax_h", &ps_r2_df_parallax_h, .0f, .5f);
    // [DA_PORT] Un-commented: this drives r_dtex_range, the distance over which the detail texture
    // is blended in (see r2_R_calculate.cpp), and that is the current suspect for the woven
    // pattern crawling over large surfaces under FSR 2. Cannot be measured without a way to
    // change it. Minimum stays 5, NOT 0: the value is used as a DIVISOR - binder_parallax hands the
    // shader 1/r_dtex_range - so zero becomes infinity and then NaN, which smears the ground into flat
    // bands. The original lower bound was there for that reason, not arbitrarily.
    CMD4(CCC_Float, "r2_parallax_range", &ps_r2_df_parallax_range, 5.0f, 175.0f);

    CMD4(CCC_Float, "r2_slight_fade", &ps_r2_slight_fade, .2f, 1.f);
    CMD3(CCC_Token, "r2_smap_size", &ps_r2_smapsize, qsmapsize_token);

    Fvector tw_min, tw_max;
    tw_min.set(0, 0, 0);
    tw_max.set(1, 1, 1);
    CMD4(CCC_Vector3, "r2_aa_break", &ps_r2_aa_barier, tw_min, tw_max);

    tw_min.set(0, 0, 0);
    tw_max.set(1, 1, 1);
    CMD4(CCC_Vector3, "r2_aa_weight", &ps_r2_aa_weight, tw_min, tw_max);

    // Igor: Depth of field
    tw_min.set(-10000, -10000, 0);
    tw_max.set(10000, 10000, 10000);
    CMD4(CCC_Dof, "r2_dof", &ps_r2_dof, tw_min, tw_max);
    CMD4(CCC_DofNear, "r2_dof_near", &ps_r2_dof.x, tw_min.x, tw_max.x);
    CMD4(CCC_DofFocus, "r2_dof_focus", &ps_r2_dof.y, tw_min.y, tw_max.y);
    CMD4(CCC_DofFar, "r2_dof_far", &ps_r2_dof.z, tw_min.z, tw_max.z);

    CMD4(CCC_Float, "r2_dof_kernel", &ps_r2_dof_kernel_size, .0f, 10.f);
    CMD4(CCC_Float, "r2_dof_sky", &ps_r2_dof_sky, -10000.f, 10000.f);
    CMD3(CCC_Mask, "r2_dof_enable", &ps_r2_ls_flags, R2FLAG_DOF);

    //float ps_r2_dof_near = 0.f; // 0.f
    //float ps_r2_dof_focus = 1.4f; // 1.4f

    CMD3(CCC_Mask, "r2_volumetric_lights", &ps_r2_ls_flags, R2FLAG_VOLUMETRIC_LIGHTS);
    //CMD3(CCC_Mask, "r2_sun_shafts", &ps_r2_ls_flags, R2FLAG_SUN_SHAFTS);
    CMD3(CCC_Token, "r2_sun_shafts", &ps_r_sun_shafts, qsun_shafts_token);
    CMD3(CCC_SSAO_Mode, "r2_ssao_mode", &ps_r_ssao_mode, qssao_mode_token);
    CMD3(CCC_Token, "r2_ssao", &ps_r_ssao, qssao_token);
    CMD3(CCC_Mask, "r2_ssao_blur", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_BLUR); // Need restart
    CMD3(CCC_Mask, "r2_ssao_opt_data", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_OPT_DATA); // Need restart
    CMD3(CCC_Mask, "r2_ssao_half_data", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HALF_DATA); // Need restart
    CMD3(CCC_Mask, "r2_ssao_hbao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HBAO); // Need restart
    CMD3(CCC_Mask, "r2_ssao_hdao", &ps_r2_ls_flags_ext, R2FLAGEXT_SSAO_HDAO); // Need restart
    CMD3(CCC_Mask, "r4_enable_tessellation", &ps_r2_ls_flags_ext, R2FLAGEXT_ENABLE_TESSELLATION); // Need restart
    CMD3(CCC_Mask, "r4_wireframe", &ps_r2_ls_flags_ext, R2FLAGEXT_WIREFRAME); // Need restart
    CMD3(CCC_Mask, "r2_steep_parallax", &ps_r2_ls_flags, R2FLAG_STEEP_PARALLAX);
    CMD3(CCC_Mask, "r2_detail_bump", &ps_r2_ls_flags, R2FLAG_DETAIL_BUMP);

    CMD3(CCC_Token, "r2_sun_quality", &ps_r_sun_quality, qsun_quality_token);

    //Igor: need restart
    CMD3(CCC_Mask, "r2_soft_water", &ps_r2_ls_flags, R2FLAG_SOFT_WATER);
    CMD3(CCC_Mask, "r2_soft_particles", &ps_r2_ls_flags, R2FLAG_SOFT_PARTICLES);

    CMD3(CCC_Token, "r3_water_refl", &ps_r_water_reflection, qwater_reflection_quality_token);
    CMD3(CCC_Mask, "r3_water_refl_half_depth", &ps_r2_ls_flags_ext, R3FLAGEXT_SSR_HALF_DEPTH);
    CMD3(CCC_Mask, "r3_water_refl_jitter", &ps_r2_ls_flags_ext, R3FLAGEXT_SSR_JITTER);

    //CMD3(CCC_Mask, "r3_msaa", &ps_r2_ls_flags, R3FLAG_MSAA);
    CMD3(CCC_MSAA, "r3_msaa", &ps_r3_msaa, qmsaa_token); // [DA_PORT] clears the upscaler list
    //CMD3(CCC_Mask, "r3_msaa_hybrid", &ps_r2_ls_flags, R3FLAG_MSAA_HYBRID);
    //CMD3(CCC_Mask, "r3_msaa_opt", &ps_r2_ls_flags, R3FLAG_MSAA_OPT);
    CMD3(CCC_Mask, "r3_gbuffer_opt", &ps_r2_ls_flags, R3FLAG_GBUFFER_OPT);
    CMD3(CCC_Mask, "r3_use_dx10_1", &ps_r2_ls_flags, (u32)R3FLAG_USE_DX10_1);
    //CMD3(CCC_Mask, "r3_msaa_alphatest", &ps_r2_ls_flags, (u32)R3FLAG_MSAA_ALPHATEST);
    CMD3(CCC_Token, "r3_msaa_alphatest", &ps_r3_msaa_atest, qmsaa__atest_token);
    CMD3(CCC_Token, "r3_minmax_sm", &ps_r3_minmax_sm, qminmax_sm_token);

//  Allow real-time fog config reload
#if (RENDER == R_R3) || (RENDER == R_R4)
#   ifndef MASTER_GOLD
    CMD1(CCC_Fog_Reload, "r3_fog_reload");
#   endif
#endif // (RENDER == R_R3) || (RENDER == R_R4)

    CMD3(CCC_Mask, "r3_dynamic_wet_surfaces", &ps_r2_ls_flags, R3FLAG_DYN_WET_SURF);
    CMD4(CCC_Float, "r3_dynamic_wet_surfaces_near", &ps_r3_dyn_wet_surf_near, 5, 70);
    CMD4(CCC_Float, "r3_dynamic_wet_surfaces_far", &ps_r3_dyn_wet_surf_far, 20, 100);
    CMD4(CCC_Integer, "r3_dynamic_wet_surfaces_sm_res", &ps_r3_dyn_wet_surf_sm_res, 64, 2048);

    CMD3(CCC_Mask, "r3_volumetric_smoke", &ps_r2_ls_flags, R3FLAG_VOLUMETRIC_SMOKE);
    CMD1(CCC_memory_stats, "render_memory_stats");

    //CMD3(CCC_Mask, "r2_sun_ignore_portals", &ps_r2_ls_flags, R2FLAG_SUN_IGNORE_PORTALS);

    CMD4(CCC_Integer, "r2_mt_calculate",    &ps_r2_mt_calculate, 0, 1);
#if RENDER == R_R4
    CMD4(CCC_Integer, "r2_mt_render",       &ps_r2_mt_render,    0, 1);
#endif

    // [DA_PORT] Dead Air compatibility stub commands
    CMD3(CCC_Token, "r2_shadow_map_size",   &ps_r2_shadow_map_size, qshadow_map_size_token);
    CMD4(CCC_Float, "r2_sun_shafts_value",  &ps_r2_sun_shafts_value, 0.f, 1.f);
    CMD4(CCC_Float, "r2_aberration_val",    &ps_r2_aberration_val, 0.f, 1.f);
    CMD4(CCC_Float, "r2_dof_diff_far",      &ps_r2_dof_diff_far, 0.f, 100.f);
    CMD4(CCC_Float, "r2_dof_diff_near",     &ps_r2_dof_diff_near, 0.f, 100.f);
    CMD4(CCC_Float, "r2_dof_pickable",      &ps_r2_dof_pickable, 0.f, 1.f);
    CMD4(CCC_Float, "r2_dof_time",          &ps_r2_dof_time, 0.f, 1.f);
    CMD4(CCC_Float, "r2_fxaa",              &ps_r2_fxaa, 0.f, 1.f);
    CMD4(CCC_Float, "r2_lensdirt",          &ps_r2_lensdirt, 0.f, 1.f);
    CMD4(CCC_Float, "r2_lensdirt_val",      &ps_r2_lensdirt_val, 0.f, 1.f);
    CMD4(CCC_Float, "r2_lenswater",         &ps_r2_lenswater, 0.f, 1.f);
    CMD4(CCC_Float, "r2_lenswater_val",     &ps_r2_lenswater_val, 0.f, 1.f);
    CMD4(CCC_Float, "r2_lumasharpen",       &ps_r2_lumasharpen, 0.f, 1.f);
    CMD4(CCC_Float, "r2_reflections",       &ps_r2_reflections, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_blend",         &ps_r2_sss_blend, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_enable",        &ps_r2_sss_enable, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_intensity",      &ps_r2_sss_intensity, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_phase1",        &ps_r2_sss_phase1, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_phase2",        &ps_r2_sss_phase2, 0.f, 1.f);
    CMD4(CCC_Float, "r2_sss_radius",         &ps_r2_sss_radius, 0.f, 1.f);
    CMD4(CCC_Float, "r2_technicolor",        &ps_r2_technicolor, 0.f, 1.f);
    CMD4(CCC_Float, "r2_tmp_w",              &ps_r2_tmp_w, 0.f, 1.f);
    CMD4(CCC_Float, "r2_tmp_x",              &ps_r2_tmp_x, 0.f, 1.f);
    CMD4(CCC_Float, "r2_tmp_y",              &ps_r2_tmp_y, 0.f, 1.f);
    CMD4(CCC_Float, "r2_tmp_z",              &ps_r2_tmp_z, 0.f, 1.f);
    CMD4(CCC_Float, "r2_vibrance_val",       &ps_r2_vibrance_val, -1.f, 1.f); // [DA_PORT] negative = desaturate
    CMD3(CCC_GradingPreset, "r__grading_preset", &ps_r_grading_preset, qgrading_preset_token); // [DA_PORT]
    CMD4(CCC_Float, "r2_vignette",           &ps_r2_vignette, 0.f, 1.f);
    CMD4(CCC_Float, "r__zoom_dof",           &ps_r2_zoom_dof, 0.f, 1.f);
    CMD4(CCC_Float, "r1_dynamic_lights",    &ps_r1_dynamic_lights, 0.f, 2.f);
    CMD4(CCC_Float, "r__actor_body",         &ps_r2_actor_body, 0.f, 1.f);
    CMD4(CCC_Float, "r__color_add_r",       &ps_r_color_add_r, -1.f, 1.f);
    CMD4(CCC_Float, "r__color_add_g",       &ps_r_color_add_g, -1.f, 1.f);
    CMD4(CCC_Float, "r__color_add_b",       &ps_r_color_add_b, -1.f, 1.f);
    CMD4(CCC_Float, "r__color_base_r",      &ps_r_color_base_r, 0.f, 2.f);
    CMD4(CCC_Float, "r__color_base_g",      &ps_r_color_base_g, 0.f, 2.f);
    CMD4(CCC_Float, "r__color_base_b",      &ps_r_color_base_b, 0.f, 2.f);
}
} // namespace xray::render::RENDER_NAMESPACE
