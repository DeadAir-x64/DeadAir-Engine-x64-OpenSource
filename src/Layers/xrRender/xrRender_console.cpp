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

// [DA_PORT] Объявления на ГЛОБАЛЬНОМ уровне, а не внутри пространства имён рендера: иначе имя
// разворачивается в xray::render::render_r4::ps_da_perf_dump, компилятор молчит, а линковщик потом
// не находит символ. Ровно на это я и наступил.
extern ENGINE_API int ps_da_perf_dump;

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
// [DA_PORT] Порог снятия мелких объектов, ПИКСЕЛИ экранного размера. Было 3.5 и пометка RO --
// наружу не выведено. Отраслевая практика отсекает всё, что уже 4-8 пикселей; 3.5 стояло ниже
// этого диапазона. Значение выбрано замером на Юпитере, пороги сравнивались попарно:
//
//   3.5: 3090 элементов, 533 прохода, gbuffer 1.46 мс процессора
//   4.5: 2966 элементов, 505 проходов
//   6.0: 2832 элемента,  478 проходов, gbuffer 1.26 мс (-0.20), но мелочь заметна на глаз
//
// Взято 4.5 -- половина выигрыша при вдвое меньшей потере в картинке. Отсечение работает не только
// на основном проходе: списки для теней строятся тем же кодом, поэтому дешевеют и каскады солнца,
// и теневые проходы ламп.
//
// ⚠️ Влияет на СОСТАВ отрисовки. Ручка r__ssa_discard, меняется на лету.
float ps_r__ssaDISCARD = 4.5f;

// [DA_PORT] ОТДЕЛЬНЫЙ порог отбрасывания для ЩИТОВ растительности (FLOD). Равен общему = как было.
//
// Зачем отдельно. Дальний фон заполняют деревья и кусты, а общий порог r__ssa_discard тянет за собой
// ВСЁ: ящики, трубы, мусор, мелочь построек. Замером: опустить общий с 16 до 2 стоит кадра ВТРОЕ, и
// подавляющая часть этой цены — не растительность.
//
// А щит растительности — это четыре вершины. Отодвинуть до горизонта именно их стоит несравнимо
// дешевле, и заполняют фон как раз они.
//
// Меньше значение — дальше живут. Ставить ниже общего осмысленно, выше — нет.
float ps_r__vegDISCARD = 0.5f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] Где НАЧИНАЕТСЯ уменьшение травы с расстоянием, долей от её дальности. 0 = как было.
//
// Было жёстко: сжатие стартует с ОДНОГО МЕТРА и идёт до самого края (fade_start = 1.f в
// DetailManager.cpp). То есть трава не заканчивается на r__detail_radius, а непрерывно мельчает всю
// дорогу: на середине дистанции она в три четверти роста, на семидесяти процентах — вполовину, а
// дальше мельче пикселя и отбрасывается по площади. Отсюда «травы вдали нет» при радиусе 299.
//
// ⚠️ Эти травинки УЖЕ обрабатываются: они в кэше, в списке видимости, матрицы для них посчитаны.
// Сдвигая начало сжатия, мы не добавляем объектов — мы показываем те, за которые уже платим.
// Добавляется закраска, а в неё кадр у нас не упирается (замер: +69% пикселей = −5 FPS).
//
// 0.7 означает «полный рост до 70% дальности, дальше сжатие». Выше 0.95 не пускаем: при совпадении
// начала и конца диапазон обнулится и деление уйдёт в бесконечность.
float ps_r__grass_fade_start = 0.95f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] Какую долю затухания травы отдавать ВЫСОТЕ вместо равномерного сжатия. 0 = как было.
//
// Замысел. Дальняя трава всё равно должна мельчать, вопрос — как именно. Равномерно она мельчает
// целиком: и ниже, и у́же, уходит за размер пикселя, отбрасывается по площади, и земля под ней
// оголяется — отсюда «трава кончилась, дальше пусто».
//
// Если же тратить затухание на ОДНУ высоту, пятно на земле остаётся прежним. Куртинка ложится
// плоско, но продолжает закрывать почву, то есть глазом читается ковром до самого края. И стоит она
// при этом дешевле: высокий стебель дорог вертикальными пикселями, а плоский их почти не занимает.
//
// 1 — затухание целиком в высоту (пятно не уменьшается вовсе), 0.5 — пополам.
float ps_r__grass_fade_flat = 0.f;
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
int ps_r__upscale_mipbias = 100; // [DA_PORT] см. da_effective_mipbias

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
float ps_r2_ssaLOD_A = 48.f;  // [DA_PORT] подобрано в игре 27.08
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

// [DA_PORT] ⚠️ R2FLAGEXT_SUN_ZCULLING ДОБАВЛЕН в набор по умолчанию (в апстриме его тут нет).
//
// Флаг решает, как дальний каскад солнца накладывает свет, и при ВЫКЛЮЧЕННОМ отсечении картинка
// неверна в обе стороны - это проверено в игре 01.08:
//
//   выключено, объём свой         -> дальний каскад режет экран ровной диагональю с вершиной на
//                                    солнце (видно на открытом месте при низком солнце);
//   выключено, объём предыдущего  -> дальше среднего каскада солнце не доходит вовсе, мир тёмный;
//   ВКЛЮЧЕНО                      -> объём предыдущего каскада плюс тест глубины "ближе": дальний
//                                    каскад добирает ровно то, что не осветили ближние. Верно.
//
// Мы всё это время играли с `on` в user.ltx и потому не видели ни первого, ни второго; у тестера
// стояло `off` (значение по умолчанию), и он получил сначала полосу, а после отката - темноту.
// Значение по умолчанию должно давать правильную картинку, а не то, что осталось от апстрима.
Flags32 ps_r2_ls_flags_ext = {
    /*R2FLAGEXT_SSAO_OPT_DATA |*/ R2FLAGEXT_SSAO_HALF_DATA | R2FLAGEXT_ENABLE_TESSELLATION | R3FLAGEXT_SSR_HALF_DEPTH |
    R3FLAGEXT_SSR_JITTER | R2FLAGEXT_SUN_ZCULLING};

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
float ps_r2_sun_lumscale = 2.5f; // [DA_PORT] was 1.0f, подобрано в игре 27.08
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

// [DA_PORT] Скорость привыкания динамических объектов к освещению: 1.0 -> 4.0.
//
// При заводском значении переход занимает около трёх секунд, и это читается как «сталкер медленно
// обугливается», зайдя в тень: модель ещё несёт яркость улицы, когда сама уже под крышей. При 4.0
// переход укладывается в долю секунды и остаётся плавным.
//
// Величина осталась ручкой r2_dhemi_smooth; 1.0 возвращает заводское поведение точь-в-точь.
// Найдено не у себя: Dead Air Refined, коммит fbcc00fd от 15.08.2026.
float ps_r2_lt_smooth = 4.f; // заводское: 1.f
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
// ⛔ [DA_PORT] МЁРТВАЯ ручка: объявлена и зарегистрирована, читателя НЕТ нигде. Двигать её
// бесполезно. Не оживлять её на месте: в user.ltx у игроков уже лежат чужие значения (у автора
// 0.1), и оживление сделало бы лучи впятеро слабее разом у всех. Взамен заведена чистая ручка
// r__sun_shafts_boost с нейтральной единицей — см. ниже.
float ps_r2_sun_shafts_value = 0.5f;

// [DA_PORT] Множитель силы солнечных лучей поверх погодного. Единица — как задано погодой.
//
// Зачем понадобился: с линейным пространством ДОБАВОЧНЫЕ эффекты сжимаются. Итог считается как
// альбедо × свет, лучи лежат в свете, и финальный переход в sRGB превращает удвоение яркости в
// прибавку примерно на треть. Лучи настраивались под прежнюю кривую отклика, поэтому стали
// бледнее — особенно на тёмных поверхностях, то есть там, где их и смотрят.
float ps_r__sun_shafts_boost = 1.f;
// [DA_PORT] Нижний порог силы лучей: работает там, где погода задала ноль.
float ps_r__sun_shafts_min = 0.f;
// [DA_PORT] Лучи в помещениях. Проход марширует луч взгляда по теневой карте солнца и СУММИРУЕТ
// освещённые шаги, поэтому отклик пропорционален длине пути: на улице луч длинный и лучи видны,
// в здании путь до стены короткий, а столб через окно занимает его малую долю — сумма мизерна.
// norm=1 переводит шейдер на ДОЛЮ освещённых шагов (не зависит от длины пути), gain разгоняет
// тонкие столбы через проёмы. На длинных лучах при gain 1 результат совпадает со стоком, поэтому
// уличная картинка не меняется. Умолчания — сток: сначала настройка в игре, потом меню.
float ps_r__sun_shafts_gain = 2.f;
float ps_r__sun_shafts_norm = 0.f;
// [DA_PORT] Гейт нормировки по покрытию над камерой. Дефолт 0: сначала настройка в игре.
float ps_r__sun_shafts_indoor = 0.f;
// [DA_PORT] Дальность лучей. Лучи — эффект БЛИЖНЕГО поля: столб через окно в 5-10 м от камеры.
// Без фейда по дистанции суммарный отклик накапливается на всей глубине сцены и дальний план
// тонет в молочной вуали (та же болезнь у всех реализаций без decay, см. GPU Gems 3 ch.13).
// Дефолт 10 м: столбу нужна длина от просвета крыши до пола, иначе он не читается «с неба».
// От дальнего «молока» защищают гейт/маски/фазовая функция, а не радиус.
float ps_r__sun_shafts_range = 10.f;
// [DA_PORT] Мастер-свитч улучшенных лучей. Дефолт 1: фича включена, чекбокс в меню её гасит.
// ЦЕЛОЧИСЛЕННЫЙ намеренно: чекбокс меню читает значение через Console->GetBool, а тот знает
// только CCC_Mask и CCC_Integer — для CCC_Float он всегда отвечает false, и галка открывалась
// снятой, ничего не записывая (свитч не срабатывал).
int ps_r__sun_shafts_mod = 1;
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
// [DA_PORT] Значение пункта «свой»: за пределами таблицы профилей.
constexpr u32 da_grading_preset_custom = 5;

const xr_token qgrading_preset_token[] =
{
    // [DA_PORT] Список сокращён до профилей, которые ШЕЙДЕР УМЕЕТ применить.
    //
    // Шейдерная половина цветокоррекции сведена к усилению по каналам и насыщенности: сдвиг и
    // полутона ASC CDL, выбор оператора тонировки и линейное пространство из шейдеров убраны.
    // Профили «плёнка», «бирюза», «отбелка», «нуар», «ночь днём», «техниколор», «выжженное» и
    // «сырость» стояли ИМЕННО на этих величинах — от них применилось бы только усиление, то есть
    // не тот вид, что подписан в меню. Пункт, который не делает обещанного, хуже отсутствующего.
    //
    // Возвращать их можно ровно вместе с шейдерной половиной, не раньше.
    { "ui_mm_grade_original", 0 },
    { "ui_mm_grade_default",  1 },
    { "ui_mm_grade_autumn",   2 },
    { "ui_mm_grade_cold",     3 },
    { "ui_mm_grade_vivid",    4 },
    // [DA_PORT] «Свой» — не профиль, а признак «ползунки трогали руками». Значение за пределами
    // таблицы профилей, поэтому применять нечего, см. CCC_GradingPreset::Execute.
    { "ui_mm_grade_custom",   5 },
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

// [DA_PORT] Степень в ASC CDL, по каналам. Единица — как было, картинка не меняется.
//
// Три величины стандарта: наклон (ps_r_color_base_*), сдвиг (ps_r_color_add_*) и степень. Первые
// две уже были, третьей не хватало — без неё правится яркость и подъём чёрного, но не полутона,
// а именно они и решают, читается картинка «плёнкой» или «пластиком».
float ps_r_color_power_r = 1.f;
float ps_r_color_power_g = 1.f;
float ps_r_color_power_b = 1.f;

// [DA_PORT] Доля ACES в тонировке: 0 — прежний расширенный Рейнхард без изменений, 1 — чистый
// ACES (приближение Нарковича, 2016), между ними плавный переход.
//
// Вещественная, а не переключатель, по двум причинам. Во-первых, встроенный настройщик читает
// значения через get_float, а тот для целочисленных команд возвращает НОЛЬ — ползунок бы не
// работал. Во-вторых, плавный переход и сам по себе полезнее: ACES заметно контрастнее, и
// половина дозы часто выглядит уместнее полной.
float ps_r_tonemap_aces = 0.f;
float ps_r_tonemap_white = 1.7f;

// [DA_PORT] ТОНИРОВКА ПО СВЕТИМОСТИ вместо поканальной. 0 - как было.
//
// Задача: листва под солнцем ВЫБЕЛИВАЕТСЯ. В тени дерево зелёное, на свету почти белое, и граница
// между ними читается как склейка двух разных картинок.
//
// Причина не в текстуре и не в лодах, а в самом операторе тонировки (tonemap в common_functions.h).
// Он сжимает яркость к единице ПОКАНАЛЬНО. Насыщенный зелёный (0.2, 0.6, 0.2), умноженный солнцем
// до (2, 6, 2), выходит примерно как (0.75, 0.95, 0.75): каналы подтягиваются друг к другу, разница
// между ними схлопывается, цвет уезжает в белый. Чем ярче свет, тем сильнее — отсюда и «до/после».
//
// Отраслевое лечение: считать кривую ОДИН раз по светимости и пересчитать каналы по отношению
// новой светимости к старой. Пропорции каналов сохраняются, зелёное остаётся зелёным при любой
// яркости. Так устроен Reinhard в исходной статье; поканально его стали применять позже, ради
// «плёночного» ухода в белый.
//
// ⚠️ Полностью сохранять насыщенность нельзя: тогда солнечный диск и блики выходят цветными и
// ядовитыми — настоящая передержка в белый УХОДИТ. Поэтому рядом идёт ps_r_tonemap_desat.
float ps_r_tonemap_hue = 1.f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] Насколько ПОЗДНО обесцвечивается почти-белое. Работает только вместе с ps_r_tonemap_hue.
//
// Показатель степени у доли «сколько уже дошло до белого». Единица — обесцвечивание начинается сразу
// и съедает выигрыш; четвёрка — только у самых ярких точек, где белизна и должна быть. Тот же приём
// под именем desat есть в ACES и в тонировке ffmpeg.
float ps_r_tonemap_desat = 8.f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] Доля линейного пространства в расчёте света. 0 — как было, 1 — полностью.
//
// ⛔ НЕ выносить в игровые настройки. Вся погода и цвета Dead Air настраивались под нынешний,
// гамма-пространственный конвейер; при единице картинка станет иной и поначалу скорее хуже, пока
// погоду не перенастроят заново. Это решение проекта, а не ползунок игрока — ручка отладочная,
// чтобы увидеть масштаб расхождения и оценить объём работы.
float ps_r_linear_light = 0.f;

u32 ps_steep_parallax = 0;
int ps_r__detail_radius = 99;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] Ceiling on shadow-casting lights per frame. 0 restores the stock "no limit".
//
// Every shadowed light re-submits the whole scene into its own shadow map (r2_R_lights.cpp:
// phase_smap_spot -> render_graph), so the scene is drawn 1 + N times per frame. Measured in the Bar:
// 87 lights, 84 visible, 61 shadowed, only 4 clipped - the level's 345k polygons went through the
// pipeline nearly sixty times. Lights ate 3.7 ms of the 4.5 ms GPU frame there.
// Lights beyond the budget are demoted to the unshadowed path (they still light the scene, they just
// stop casting), keeping the ones that matter most on screen.
// [DA_PORT] Дефолт - «Минимум». Проверено в игре 01.08: разницы в картинке почти нет, а кадр по
// процессорной части дешевле на 78% (1.56 мс против 2.78 мс на базе, подача геометрии 0.27 против
// 0.99). Одна ТЕНЕВАЯ точечная лампа - это шесть проходов сцены, а не один: движок разбирает её
// на шесть 90-градусных секторов, см. light::Export.
u32 ps_r__light_shadow_budget = 1;

// [DA_PORT] Трава в теневых картах прожекторов. Замер на Юпитере: при 39 теневых источниках она
// уезжает в каждый проход, dt_rend 13.67 мс против 0.41 мс без этих проходов. Тень травы от лампы
// на картинке почти не читается. 1 возвращает прежнее поведение.
// [DA_PORT] Сколько ближних каскадов солнца получают траву в теневую карту. 0 - тени травы нет
// вовсе, 3 - как было раньше (во всех). Разбор и замер - в render_phase_sun.cpp у Details->Render.
int ps_r__grass_shadow_cascades = 2;  // [DA_PORT] подобрано в игре 27.08 (⚠️ дороже, см. разбор)

// [DA_PORT] Дальность травы в ТЕНЯХ, в метрах. Отдельно от r__detail_radius, который задаёт дальность
// отрисовки в кадре.
//
// Ближний каскад солнца — 20 метров, но в него уходила вся трава, видимая игроку (при радиусе 299 —
// на трёхстах метрах): менеджер травы считает видимость ОДИН раз по камере, и теневой проход
// получал тот же список. Лишнее отсекала уже видеокарта, после обработки вершин — то есть работа
// делалась и выбрасывалась.
//
// 24 — размер ближнего каскада плюс небольшой запас: трава чуть за его границей всё ещё может
// отбросить тень внутрь ящика.
int ps_r__grass_shadow_dist = 40;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] ПОЛОСА ЗАТУХАНИЯ ТЕНИ ТРАВЫ, метры перед отсечкой. 0 - резкий обрыв, как было.
//
// Задача. Тень травы отсекается по радиусу вокруг камеры (ps_r__grass_shadow_dist). Граница —
// окружность, но солнце светит под углом, и в перспективе она читается как конус, едущий с
// игроком. Отодвинуть отсечку можно, убрать кромку — нет: она резкая по построению.
//
// Отраслевое лечение — не выключать на границе разом, а гасить НА ПОДХОДЕ к ней. В Unreal это
// PerInstanceFadeAmount: экземпляры затухают на отрезке перед Cull Distance End. Там же прямо
// признают наш симптом — «abrupt and hard fade distances», когда видно, как тени включаются и
// выключаются при движении.
//
// Гасим не прозрачностью, а ВЫСОТОЙ: травинка прижимается к земле, её тень укорачивается и сходит
// в ничто. Прозрачность в теневой карте потребовала бы отсева по альфе с дрожанием, то есть работы
// на каждый пиксель; высота обходится тремя умножениями в вершинном шейдере. Тот же приём, что и
// у ps_r__grass_fade_flat, только здесь он применяется ТОЛЬКО в теневом проходе.
//
// ⚠️ Полоса вычитается ИЗ дальности, а не прибавляется к ней: тень по-прежнему кончается там, где
// стояла отсечка, просто последние метры сходят на нет. Цена не растёт.
int ps_r__grass_shadow_fade = 10;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] ВОЗДУШНАЯ ПЕРСПЕКТИВА: доля цвета НЕБА в тумане. 0 - как было.
//
// Обесцвечивание дали физически верно и делается намеренно: свет от дальнего объекта проходит больше
// воздуха, рассеивается и теряет насыщенность. Вопрос не в силе, а в ОТТЕНКЕ ухода. Уход в холодный
// цвет неба читается воздухом и глубиной; уход в плоский серый читается выцветанием.
//
// У нас в данных второе: `fog_color` во ВСЕХ 116 файлах погоды задан тремя одинаковыми каналами
// (например 0.0078/0.0078/0.0078) - это чистый серый без всякого оттенка.
//
// Править 116 файлов мода нельзя (правило «данные один в один» и конфликт с обновлениями), поэтому
// подмешиваем оттенок в движке. Берём не выдуманный синий, а ТЕКУЩИЙ цвет неба: физически в даль
// подмешивается именно рассеянный свет неба, и такой оттенок сам подстраивается под погоду и время
// суток - на закате он станет тёплым, в пасмурь серым, и нигде не разойдётся с горизонтом.
//
// ⚠️ Яркость тумана сохраняем. Меняем только оттенок: иначе на плотной погоде даль поехала бы по
// светлоте и разошлась со стыком у горизонта - ровно та беда, из-за которой в других движках туман
// «не сходится с небом».
float ps_da_fog_sky_tint = 0.f;

int ps_r__grass_spot_shadows = 0;

// [DA_PORT] Ширина ядра фильтра теней у ДАЛЬНЕГО каскада солнца, в разах. 1 = как было.
//
// СИМПТОМ: при апскейлере тени дальних деревьев мерцают; вблизи чисто, без апскейлера чисто.
//
// ПРИЧИНА. Ядро в shadow.h постоянно В ТЕКСЕЛЯХ теневой карты (KERNEL/SMAP_size, четыре выборки в
// пределах одного текселя) и не зависит от расстояния. Вдали один экранный пиксель накрывает
// десятки текселей, и фильтр берёт точечную пробу высокочастотного узора листвы. Джиттер
// апскейлера сдвигает пробу каждый кадр, она скачет «лист/просвет», накопитель показывает
// мерцание.
//
// Механизм доказан противоестественным предсказанием: версия требовала, чтобы МЕНЬШАЯ теневая
// карта мерцала МЕНЬШЕ. Проверено: r2_smap_size 1024 помогает, 4096 хуже.
//
// Отраслевое лечение (Matt Pettineo): размер ядра задавать НА КАСКАД. У нас каскады разведены по
// проходам (SE_SUN_NEAR / SE_SUN_FAR), поэтому множитель передаётся в шейдер прямо там —
// r4_rendertarget_accum_direct.cpp. Ближний каскад не трогается никогда: вблизи фильтр и так
// накрывает пиксель, а расширение только размыло бы чёткие тени.
//
// ⛔ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО (1 = прежнее поведение). Значение подбирается глазами: чем шире, тем
// меньше мерцание и тем мягче дальние тени. Цена — четыре выборки остаются четырьмя, просто
// расходятся шире; лишнего времени это не стоит.
int ps_r__shadow_kernel_far = 1;
// [DA_PORT] Поворот выборки PCF на пиксель и на кадр. Разбор - в shaders/r3/shadow.h.
int ps_r__shadow_rotate = 0;

// [DA_PORT] Диагностика: подсветка каскадов солнечной тени. 0 — выкл; 1 — near=красный,
// middle=зелёный, far=синий (умножается на солнечный свет каждого каскада). Нужна, чтобы увидеть, на
// границе КАКИХ каскадов лежит видимый «клин». Применение — r4_rendertarget_accum_direct.cpp.
int ps_r__dbg_sun_cascades = 0;

// [DA_PORT] Дистанция (метры от камеры), на которой дальняя солнечная тень плавно уходит в свет.
// Затухание по РАССТОЯНИЮ, а не по краю теневой карты, — оно не зависит от поворота камеры, поэтому
// не даёт «клина/полосы», едущей со взглядом (край карты подогнан под пирамиду и двигается, а
// расстояние — нет). Тень гаснет на [0.75*d .. d]. Подбирать вживую. Применение — accum_sun_far.ps.
float ps_r__sun_shadow_fade = 140.f;

// [DA_PORT] Прибор: чем занят главный поток, пока ждёт расчёт видимости. Полный разбор — у
// g_da_wait_executed в r2.h.
//
// Коротко: зона wait_cull показывает 1.63 мс, но эти миллисекунды означают либо настоящий пузырь
// (воровать было нечего), либо честную работу кадра, просто записанную в эту зону. Различить можно
// только счётом выполненных задач, и от ответа зависит, имеет ли смысл распараллеливать расчёт
// видимости вообще.
//
// ⛔ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО: под прибором ожидание идёт своим циклом вместо стокового, и в обычной
// игре менять его незачем. Числа печатаются в конце строки DA_GPU как steal <выполнено>/<холостых>,
// то есть включать надо вместе с da_frame.
int ps_da_cull_prof = 0;

// [DA_PORT] Порог точного расчёта позы скелета, метры. 0 — выключено (как было).
//
// Замер назвал главного потребителя ожидания: CalculateBones стоит 0.49 мс на 116 скелетов в кадре
// — 82% renderable_Render и почти половина всего простоя главного потока. Анимация пересчитывается
// внутри определения видимости, на задаче, которую все ждут.
//
// Механизм «обновлять не каждый кадр» в движке УЖЕ есть: ранний выход по UCalc_Interval, объявленный
// в Kinematics.h как 100 мс (10 Гц). Отменяет его аргумент bForceExact, и рендер передавал
// безусловное TRUE — включая скелеты в двух сотнях метров.
//
// Это ровно Update Rate Optimization из Unreal: дальние модели пропускают полный расчёт позы,
// рекомендация Epic — 15 Гц и ниже на подходящих дистанциях, наши 10 Гц в неё укладываются.
//
// Дистанция делится на отношение поля зрения, как и отсечка геометрии: в прицеле порог обязан ехать
// вместе с увеличением. Разумная отправная точка — 50…70.
int ps_da_anim_lod = 0;


// [DA_PORT] Закрепление места в атласе теневых карт за источником, пока состав не менялся.
// Сам по себе кадр почти не ускоряет — это фундамент под кэш статических теней: пока прямоугольник
// каждый кадр новый, переиспользовать содержимое прошлого кадра нечего. 0 — прежняя перепаковка
// каждый кадр.
int ps_r__smap_stable_slots = 1;

// [DA_PORT] Чистить теневой атлас по ячейкам вместо всей текстуры. Сам по себе выигрыша не даёт —
// это фундамент под кэш теней ламп: пока атлас стирается целиком, содержимое прошлого кадра
// переиспользовать нечем. По умолчанию 0: путь новый, ошибка в нём молчаливая (мусор в тени или
// пропавшие чужие тени), проверять надо глазами и слоем проверки DirectX.
int ps_r__smap_clear_rect = 0;

// [DA_PORT] Кэш теней ламп: раздельная статика и динамика. Требует vid_restart. 0 — выключено.
//
// ЗАЧЕМ. Каждый теневой источник — это прогон ВСЕЙ сцены в свою карту, и в Баре таких 61. При этом
// неподвижная лампа над неподвижной комнатой рисует одно и то же каждый кадр.
//
// КАК. Схема взята у промышленных движков (Unreal Virtual Shadow Maps, Flax, Unity HDRP): держат
// ДВЕ копии глубины. Статику рисуют редко и переиспользуют, динамику — каждый кадр поверх
// восстановленной статики. HDRP описывает это дословно как «блит из кэшированного атласа в
// рабочий».
//
// ⛔ ПОЧЕМУ НЕ «КЭШИРОВАТЬ КАРТУ ЦЕЛИКОМ». Так было в первой версии, и она моргала. Кэш всей карты
// вынуждает УГАДЫВАТЬ, шевельнулось ли что-то в объёме лампы; любая неточность догадки и даёт
// скачки между свежей и просроченной тенью. При раздельной схеме гадать не о чем: динамика всегда
// свежая, статика меняется только по событию.
//
// Требует vid_restart: атлас статики выделяется при создании целей отрисовки. По умолчанию
// ВКЛЮЧЁН, поэтому при обычном запуске перезапуск не нужен -- атлас заводится сразу.
//
// Замеры, по которым принято решение: в баре при 56 лампах фаза света 2.51 -> 1.51 мс на
// видеокарте (-26%), на лампу 0.045 -> 0.027; на Юпитере попадание 15 из 15 (100%), статика не
// перерисовывается вовсе, отрисовка теневых карт стоит 0.07 мс. Расход -- атлас статики со
// стороной вдвое больше рабочей, около 16 МБ видеопамяти при r2_smap_size 1024.
//
// ⚠️ Шейдер копии (shaders/r3/da_smap_blit.ps и .s) обязан быть в отгрузке: без него движок молча
// подставляет заглушку. Проверка release_shipping это стережёт -- она же его отсутствие и нашла.
int ps_r__smap_cache_lights = 1;

// [DA_PORT] Во сколько раз атлас статики больше рабочего по стороне. Требует vid_restart.
//
// ⛔ Одинакового размера НЕ ХВАТАЕТ, и это не мелочь настройки. Рабочий атлас вмещает все теневые
// лампы только ПО ПАЧКАМ: замер в баре — 17 ламп, 4–5 пачек при стороне 1024. В атласе статики
// пачек нет, там место закреплено за лампой навсегда, значит поместиться должны ВСЕ сразу.
// При равном размере из семнадцати ламп место получали четыре, остальные не кэшировались вовсе —
// и кэш давал ровно 0% попаданий.
//
// Площадь растёт квадратом: 2 — вчетверо, 3 — вдевятеро. При стороне 1024 и множителе 2 это 2048,
// то есть около 16 МБ видеопамяти.
int ps_r__smap_cache_atlas = 2;

// [DA_PORT] Предельный возраст СТАТИЧЕСКОЙ половины, миллисекунды. 0 — без ограничения.
//
// В промышленной схеме сброс идёт по событиям, и срок жизни не нужен: HDRP обновляет кэш при
// изменении свойств источника и разрешения тени, никакого таймера там нет. Ручка оставлена как
// страховка на случай события, которое мы не учли — урок кэша солнца, где забыли направление
// взгляда, и без предела дефект жил бы вечно.
int ps_r__smap_cache_lights_ms = 0;

// [DA_PORT] Размер ячейки теневого атласа — степенями двойки. Без этого атлас перекладывается
// каждый кадр (порог смены размера — один процент, то есть шесть текселей от 512), и всё, что
// стоит на постоянстве мест, не работает: замер дал 113 отказов кэша из 113 по этой причине.
// Цена — до двух раз разрешения у ламп, попавших чуть выше степени двойки.
int ps_r__smap_size_pow2 = 0;

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
float ps_r2_gloss_factor = 4.0f; // [DA_PORT] возврат к АВТОРСКОМУ значению, подобрано 27.08
// [DA_PORT] ГЛЯНЕЦ ЛИСТВЫ — МНОЖИТЕЛЬ. Единица ничего не меняет, ноль убирает блеск начисто.
//
// ⚠️ Сначала он был АБСОЛЮТНЫМ значением и правил deffer_base.ps — а тот листву на деревьях
// не рисует, её рисует deffer_base_aref_bump.ps. Ручка честно работала и честно ни на что
// не влияла. Теперь это множитель, и он доходит до обоих шейдеров.
//
// Блик листве включили мы сами, вместе с просветом: сухая трава действительно ловит свет, а штатный
// def_gloss у неё почти нулевой. Достаётся он ВСЕЙ геометрии, которая режется по прозрачности —
// и траве, и хвое, и кустам, разделения между ними в шейдере нет.
//
// Дальше в combine_1.ps блик идёт как C.www*L.rgb*5, то есть впятеро усиленным, и он АДДИТИВНО БЕЛЫЙ.
// Под прямым солнцем это кладёт на хвою белёсый налёт по краям крон. Тонировкой такое не лечится
// ни в каком виде: цвет становится белым ДО неё, это не сжатие яркости, а добавленный белый свет.
//
// ⚠️ Рядом стоит ps_r2_gloss_factor, у нас поднятый до 6 против авторских 4 — он умножает то же самое.
// Если белизна остаётся и при нулевом глянце листвы, крутить надо его.
float ps_r__foliage_gloss = 0.0f; // [DA_PORT] МНОЖИТЕЛЬ. Ноль подобран в игре 27.08:
// блеск листве убран начисто. Проверено уже ПОСЛЕ починки сентинела, то есть ноль
// здесь означает настоящий ноль, а не «константа не привязана».

// [DA_PORT] ПРОСВЕТ ЛИСТВЫ: наклон нормали к зрителю. Было вшито числом 0.35 там же.
//
// Нормаль листа почти всегда смотрит вбок, и в отложенном освещении растительность вырождается в
// плоский тёмный силуэт против солнца. Настоящее подповерхностное рассеяние требует своего канала в
// G-буфере; вместо него нормаль слегка загибается к зрителю, и лист ловит свет с обеих сторон.
//
// Ноль возвращает штатную нормаль. Больше — светлее и «прозрачнее», но и площе.
float ps_r__foliage_bend = 0.2f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] НАСЫЩЕННОСТЬ ЛИСТВЫ. Было вшито числом 1.3 в deffer_base_aref_bump.ps.
//
// Единица — исходная насыщенность текстуры, ниже единицы уводит в серое, выше поднимает приглушённые
// цвета и не трогает уже яркие (это вибранс, а не простое насыщение).
float ps_r__foliage_vibrance = 1.6f;  // [DA_PORT] подобрано в игре 27.08

// [DA_PORT] ОБЕСЦВЕЧЕННЫЕ ВЕТКИ: гасим светлое И бесцветное в альбедо листвы. 0 - как было.
//
// ⭐ Найдено ПРИБОРОМ (r__light_probe), а не рассуждением, и опровергло три подряд догадки.
// Режим 1 (только блик) — чёрный, режим 6 (глянец) — чёрный: зеркальная составляющая на хвое равна
// нулю, глянца у неё нет. А режим 5 — чистое альбедо, БЕЗ ВСЯКОГО СВЕТА — уже показывает белёсые
// ветки. Значит они не «побелели от солнца»: они такие в самой текстуре, а свет лишь доводит
// светло-серое до единицы, где оно и читается белым.
//
// Поэтому лечить надо альбедо, и лечить прицельно. Мера здесь — «светлое И бесцветное»: у белёсой
// ветки высокая яркость при почти нулевой насыщенности, у зелёной хвои насыщенность есть. Так
// ветки уходят в серый, а хвоя остаётся нетронутой — в отличие от общего затемнения, которое
// придавило бы всё дерево разом.
//
// ⚠️ Действует на ВСЮ геометрию с прозрачностью в этом шейдере: хвоя, кусты, трава-карточки.
float ps_r__foliage_debleach = 0.6f;  // [DA_PORT] подобрано в игре 27.08

float ps_r2_gloss_min = 0.0f; // [DA_PORT] перенос из monolith/OGSR, см. u_diffuse2s
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
// [DA_PORT] Доля поправки мипов на масштаб рендера. Применяется вживую, перезапуск не нужен:
// SetupStates выполняется только при создании устройства, поэтому пересчёт зовём отсюда сами.
class CCC_UpscaleMipBias : public CCC_Integer
{
public:
    CCC_UpscaleMipBias(LPCSTR N, int* v) : CCC_Integer(N, v, 0, 200) {}
    void Execute(LPCSTR args) override
    {
        CCC_Integer::Execute(args);
#if defined(USE_DX11)
        if (HW.pDevice)
            SSManager.SetMipLODBias(da_effective_mipbias());
#endif
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

        SSManager.SetMipLODBias(da_effective_mipbias()); // [DA_PORT] с поправкой, а не голое *value
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

// [DA_PORT] Сколько кадров подряд мерить ОБЩИЙ ЗАМОК РАСЧЁТА КОСТЕЙ. 0 = молчит.
//
// Расчёт костей всех моделей сериализован одним глобальным замком UCalc_Mutex
// (SkeletonCustom.h). У соседей это место расшито: IX-Ray сделал потокобезопасный расчёт,
// Monolith - многопоточный. Прежде чем повторять, надо узнать, есть ли что расшивать
// ИМЕННО У НАС: если кости и так считаются в один поток, снятие замка не даст ничего.
//
// Прибор отвечает на три вопроса: сколько кадра уходит в ОЖИДАНИЕ на замке, сколько - в
// работу под ним, и приходят ли вызовы с рабочих потоков вообще. Разбор в SkeletonRigid.cpp.
int ps_da_bones_dump = 0;

// [DA_PORT] da_sun_only N: накапливать солнечный свет только от каскада N (1..3), 0 = все.
// Замер под мерцание тени; см. render_sun::accumulate_cascade.
int ps_da_sun_only = 0;

// [DA_PORT] Диагностические крутилки регистрируются через CCC_DaDebugInteger / CCC_DaDebugFloat
// (xr_ioc_cmd.h): они не пишутся в user.ltx и живут ровно один запуск. Там же — почему.

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
    pcstr smap_cache; // [DA_PORT] срок жизни кэша теневых карт солнца, МИЛЛИСЕКУНДЫ (0 = выключен)
};

// low, medium, high, ultra
const Preset s_perf_presets[4] = {
    // [DA_PORT] Тени от ламп сдвинуты на ступень вниз - иначе применение набора качества в меню
    // сбивало бы настройку обратно, а «Минимум» проверен в игре и стоит вдвое дешевле по процессору.
    // [DA_PORT] Последний столбец — кэш теневых карт солнца, в МИЛЛИСЕКУНДАХ.
    //
    // Замерено: фаза теневых карт дешевеет вдвое (0.803 -> 0.42 мс при 60 мс кэша). Цена — тень
    // средних и дальних каскадов отстаёт от мира на этот срок, включая тени NPC на средней
    // дистанции. Ступени названы по КАЧЕСТВУ и совпадают с именем набора: низкая точность = 150 мс,
    // средняя = 100, высокая = 50, ультра = без кэша. См. q_smap_cache — там же, почему наоборот.
    //
    // На «Максимуме» выключено намеренно: там игрок платит за точность, а не за кадры, и проверки
    // артефактов глазами у нас пока нет. Как проверим — можно будет включить и там.
    { "st_opt_shadow_lights_off",    "51",  "1024", "st_optimize_high", "st_optimize_high",
      "0.8", "60",  "0.75", "0.7", "1.0", "st_opt_smap_cache_low" },
    { "st_opt_shadow_lights_off",    "51",  "1024", "st_optimize_med",  "st_optimize_med",
      "1.0", "100", "0.5",  "1.0", "1.3", "st_opt_smap_cache_medium" },
    { "st_opt_shadow_lights_low",    "80",  "2048", "st_optimize_low",  "st_optimize_low",
      "1.0", "150", "0.4",  "1.3", "1.5", "st_opt_smap_cache_high" },
    { "st_opt_shadow_lights_medium", "120", "2048", "st_optimize_off",  "st_optimize_off",
      "1.0", "200", "0.3",  "1.6", "1.8", "st_opt_smap_cache_ultra" },
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
            da_perf_value_matches("r__detail_height", p.detail_height) &&
            da_perf_value_matches("da_smap_cache", p.smap_cache))
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
        xr_sprintf(cmd, "da_smap_cache %s", p.smap_cache);               Console->Execute(cmd);
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

// [DA_PORT] Кэш теневых карт солнца. Значение токена — САМ СРОК ЖИЗНИ В МИЛЛИСЕКУНДАХ, лишней
// таблицы перевода нет: одно число, один смысл (урок [[jitter-path-unified]] — константа с двумя
// смыслами молча гасит половину логики).
//
// Почему миллисекунды, а не кадры: тот же лимит в кадрах на быстрой машине означает 60 мс, а на
// слабой — полсекунды замороженных теней. То есть картинка портилась бы тем сильнее, чем слабее
// компьютер, — ровно у той аудитории, ради которой оптимизация и делается. Во времени устаревание
// ограничено одинаково у всех, а доля пропусков сама подстраивается под частоту кадров.
//
// Обратная сторона выбора шага: срок жизни короче кадра не даёт НИЧЕГО. При 30 к/с кадр длится
// 33 мс, поэтому нижняя ступень взята с запасом (50 мс), иначе на слабой машине «низкое» означало
// бы «выключено» — и именно там, где оно нужнее всего.
//
// ⚠️ Ступени идут ОТ БОЛЬШЕГО СРОКА К МЕНЬШЕМУ, и это не опечатка. Строка называется «точность теней
// солнца», то есть меряет КАЧЕСТВО, как и все остальные строки меню: «низкая» = тени обновляются
// реже = кадров больше. Если назвать её по механизму («кэш»), получится, что на общем наборе
// «Высокие» стоит «высокое кэширование», то есть худшая картинка — набор и строка читались бы
// в противоположные стороны. Поэтому имя ступени совпадает с именем общего набора один в один.
xr_token q_smap_cache[] = {
    { "st_opt_smap_cache_low", 150 },
    { "st_opt_smap_cache_medium", 100 },
    { "st_opt_smap_cache_high", 50 },
    { "st_opt_smap_cache_ultra", 0 },
    { nullptr, 0 },
};

// [DA_PORT] Настройка из меню. Числа консоль здесь НЕ принимает, и это защита, а не строгость.
//
// Список сохраняет себя в user.ltx через GetStatus, а комбо-бокс — через get_token_name(id): у обоих
// для значения вне списка ответ ПУСТАЯ СТРОКА. То есть стоило бы принять `da_smap_cache 75`, зайти в
// настройки и нажать «Применить» — и в конфиг ушло бы `da_smap_cache` без аргумента, на следующем
// запуске разбор бы упал, а настройка молча вернулась к нулю. Игрок увидел бы «настройки не
// сохраняются», причём не сразу и без единого сообщения.
//
// Точные миллисекунды остались нужны — ими искали насыщение кэша и ими же придётся искать порог
// артефактов. Они живут отдельной отладочной ручкой da_smap_cache_ms, которая пишет ту же
// переменную, но НЕ сохраняется: одно значение, один хозяин в конфиге.
class CCC_SmapCache : public CCC_Token
{
public:
    CCC_SmapCache(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    // Если da_smap_cache_ms увёл значение в сторону, показываем БЛИЖАЙШУЮ ступень: пусть ответ будет
    // приблизительным, лишь бы он всегда оставался годным и для конфига, и для списка в меню.
    void GetStatus(TStatus& S) override
    {
        CCC_Token::GetStatus(S);
        if (S[0] != '?')
            return;
        pcstr best = tokens[0].name;
        int best_d = -1;
        for (const xr_token* t = tokens; t->name; ++t)
        {
            const int d = _abs(t->id - (int)*value);
            if (best_d < 0 || d < best_d)
            {
                best_d = d;
                best = t->name;
            }
        }
        xr_strcpy(S, best);
    }
};

// [DA_PORT] Отладочная форма той же настройки: точный срок жизни в миллисекундах.
class CCC_SmapCacheMs : public IConsole_Command
{
    u32* value;

public:
    // bEmptyArgsHandled НЕ трогаем: с ним пустой аргумент уходит в Execute, а atoi("") = 0, то есть
    // запрос значения сам бы его и обнулял.
    CCC_SmapCacheMs(pcstr N, u32* V) : IConsole_Command(N), value(V) {}

    void Execute(pcstr args) override
    {
        const int ms = atoi(args);
        if (ms < 0 || ms > 1000)
        {
            InvalidSyntax();
            return;
        }
        *value = (u32)ms;
    }
    void GetStatus(TStatus& S) override { xr_sprintf(S, "%u", *value); }
    void Info(TInfo& I) override { xr_strcpy(I, "срок жизни кэша теней солнца, миллисекунды 0..1000"); }
    void Save(IWriter*) override {} // хозяин настройки в конфиге - da_smap_cache
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
// [DA_PORT] Ползунок цветокоррекции: тронули руками — выбор профиля переходит в «свой».
//
// Без этого профиль при следующей загрузке вернул бы свои числа поверх настроенных. См. разбор
// в CCC_GradingPreset::Execute.
class CCC_GradeValue : public CCC_Float
{
public:
    CCC_GradeValue(pcstr N, float* V, float min, float max) : CCC_Float(N, V, min, max) {}

    void Execute(pcstr args) override
    {
        const float before = *value;
        CCC_Float::Execute(args);
        if (!fsimilar(before, *value))
            ps_r_grading_preset = da_grading_preset_custom;
    }
};

// [DA_PORT] Профиль задаёт ВЕСЬ цветовой конвейер, а не четыре числа, как раньше.
//
// Пока величин было четыре (усиление и насыщенность), профиль и был «оттенком». Теперь их
// тринадцать: к ним добавились сдвиг и полутона ASC CDL, доля ACES, точка белого и доля линейного
// пространства. Если профиль применяет часть, а остальное оставляет как есть, то «вернуть профиль»
// перестаёт что-либо гарантировать — получится смесь профиля с тем, что игрок накрутил до него.
struct da_grade_profile
{
    float r, g, b;    // усиление по каналам (ASC CDL: наклон)
    float v;          // насыщенность
    float ar, ag, ab; // сдвиг
    float pr, pg, pb; // полутона (степень)
    float aces;       // доля плёночной кривой
    float white;      // точка белого
    float lin;        // доля линейного пространства
};

// Профили 2..4 — это ОТТЕНКИ, и они выражаются одним усилением по каналам плюс насыщенностью,
// то есть ровно тем, что шейдер применяет сейчас. Остальные восемь стояли на сдвиге, полутонах
// и плёночной кривой; их шейдерной половины больше нет, и из списка они убраны — см. таблицу
// токенов выше.
static const da_grade_profile da_grade_profiles[] =
{
    // ⚠️ Столбцы «сдвиг», «полутона», «ACES», «белый» и «линейн.» держатся НЕЙТРАЛЬНЫМИ во всех
    // строках намеренно. Шейдер их больше не читает, но команды консоли живы как отладочные —
    // и профиль, выставляя нейтраль, заодно вычищает то, что игрок мог накрутить ими раньше и
    // сохранить в user.ltx. Ненейтральное значение здесь было бы обещанием, которого никто не
    // исполнит.
    //
    //  усиление r/g/b     насыщ.   сдвиг r/g/b            полутона r/g/b      ACES  белый линейн.
    // 0 «Оригинал» — это АВТОРСКИЕ значения Dead Air (дефолты консоли самого мода:
    // 1.04/1.00/0.96 + вибранс 0.18), а не тождественное преобразование. В оригинальном движке
    // их никто не читал, поэтому вживую профиль не звучал — но это и есть «профиль автора»,
    // и игрок ждёт под нулём именно его, а не пустышку.
    { 1.04f, 1.00f, 0.96f,  0.18f,  0.00f, 0.00f, 0.00f,  1.00f,1.00f,1.00f,  0.f,  1.7f, 0.f }, // 0 оригинал
    { 1.00f, 1.00f, 1.00f, -0.16f,  0.00f, 0.00f, 0.00f,  1.00f,1.00f,1.00f,  0.f,  1.7f, 0.f }, // 1 наш
    { 1.10f, 1.02f, 0.90f,  0.30f,  0.00f, 0.00f, 0.00f,  1.00f,1.00f,1.00f,  0.f,  1.7f, 0.f }, // 2 осень
    { 0.96f, 1.00f, 1.08f,  0.10f,  0.00f, 0.00f, 0.00f,  1.00f,1.00f,1.00f,  0.f,  1.7f, 0.f }, // 3 холод
    { 1.06f, 1.02f, 0.98f,  0.40f,  0.00f, 0.00f, 0.00f,  1.00f,1.00f,1.00f,  0.f,  1.7f, 0.f }, // 4 сочный
};

// Общее применение для ОБЕИХ команд выбора: словесной (для меню) и числовой (для настройщика).
// Числа лежат в одном месте — разойтись двум путям не с чем.
void da_apply_grading_preset(u32 idx)
{
    if (idx >= std::size(da_grade_profiles))
        return; // «свой» и всё, что за таблицей: применять нечего

    const da_grade_profile& p = da_grade_profiles[idx];
    ps_r_color_base_r = p.r;
    ps_r_color_base_g = p.g;
    ps_r_color_base_b = p.b;
    ps_r2_vibrance_val = p.v;
    ps_r_color_add_r = p.ar;
    ps_r_color_add_g = p.ag;
    ps_r_color_add_b = p.ab;
    ps_r_color_power_r = p.pr;
    ps_r_color_power_g = p.pg;
    ps_r_color_power_b = p.pb;
    ps_r_tonemap_aces = p.aces;
    ps_r_tonemap_white = p.white;
    ps_r_linear_light = p.lin;
}

class CCC_GradingPreset : public CCC_Token
{
public:
    CCC_GradingPreset(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);

        // [DA_PORT] ⚠️ Хранилище у цветокоррекции должно быть ОДНО, иначе они разъезжаются.
        //
        // Было два: сами ползунки (r__color_base_*, r2_vibrance_val) и профиль. Оба уходили в
        // user.ltx, а строки там отсортированы — профиль стоит ниже ползунков и выполняется
        // последним. То есть при каждой загрузке он затирал всё, что игрок настроил руками.
        // Ровно тот же класс, что был с сезоном.
        //
        // Теперь ползунки, тронутые руками, переводят выбор в «свой», а «свой» ничего не
        // применяет: значения переживают загрузку, потому что применять их поверх некому.
        da_apply_grading_preset(*value);
    }
};

// [DA_PORT] Тот же выбор, но ЧИСЛОМ — для встроенного настройщика.
//
// Зачем вторая команда. Настройщик строит ползунки и читает значения через c:get_float, а тот
// понимает только CCC_Float: словесную команду он бы прочёл как ноль. Хранилище при этом ОДНО
// (ps_r_grading_preset), так что разъехаться двум путям не с чем — расходятся не команды, а
// хранилища, и вот их дублировать нельзя.
class CCC_GradingPresetNum : public CCC_Float
{
    float m_shadow;

public:
    CCC_GradingPresetNum(pcstr N, float* V, float mn, float mx) : CCC_Float(N, V, mn, mx), m_shadow(0.f) {}

    void Execute(pcstr args) override
    {
        CCC_Float::Execute(args);
        const u32 idx = u32(*value + 0.5f); // ползунок отдаёт дробное, профиль — целый
        ps_r_grading_preset = idx;
        da_apply_grading_preset(idx);
    }
};

// Числовое зеркало выбора: сюда пишет настройщик, отсюда же читает при открытии.
float ps_r_grading_preset_num = 1.f;

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

// [DA_PORT] Один замер вместо трёх: da_frame <кадров>.
//
// Разбор кадра приходилось собирать руками из da_perf_dump, da_gpu_log и da_render_log, помнить,
// что второму нужен ещё и rs_stats, и потом сводить три набора строк за разные отрезки времени.
// Отрезки не совпадали, и сравнивать их было нельзя.
//
// Здесь все счётчики взводятся на ОДНО И ТО ЖЕ число кадров, то есть меряют один и тот же кусок
// игры. Это и было главной бедой прежнего порядка, а не количество команд.
class CCC_DaFrame : public IConsole_Command
{
public:
    CCC_DaFrame(pcstr N) : IConsole_Command(N) {}

    void Execute(pcstr args) override
    {
        const int frames = atoi(args);
        if (frames <= 0 || frames > 200000)
        {
            Msg("~ da_frame <кадров>: разбор кадра целиком (блоки логики, фазы видеокарты и процессора)");
            return;
        }

        // ps_da_gpu_log живёт ВНУТРИ пространства имён рендера (da_gpu_timer.cpp), поэтому
        // объявляется здесь же, а не рядом с движковым.
        extern int ps_da_gpu_log;
        // 🪤 Третий счётчик раньше НЕ взводился, хотя ради него всё и затевалось: команда обещала
        // «один замер вместо трёх», а давала два. Разбор просадки на Юпитере уткнулся ровно в это -
        // фазы показали, что кадр съедает свет, а СКОЛЬКО там источников, сказать было нечем, и
        // пришлось идти за третьей командой отдельным заходом.
        extern int ps_da_render_log;

        ::ps_da_perf_dump = frames;
        ps_da_gpu_log = frames;
        ps_da_render_log = frames;

        Msg("~ [DA_PORT] разбор кадра на %d кадров: блоки логики [DA_PERF] + фазы [DA_GPU] "
            "(процессор/видеокарта) + счётчики отрисовки и источников света [DA_RENDER]/[DA_LIGHTS]",
            frames);
    }

    void Info(TInfo& I) override { xr_strcpy(I, "разбор кадра целиком: <кадров>"); }
};

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
#endif // DEBUG

    // [DA_PORT] Была под DEBUG, а зовёт её наш da_mem_test — в релизе он получал «Unknown command»
    // и молча терял перепись пула моделей из отчёта о памяти. Команда только печатает содержимое
    // пула, держать её отладочной незачем.
    CMD1(CCC_ModelPoolStat, "stat_models");
    CMD4(CCC_Float, "r__wallmark_ttl", &ps_r__WallmarkTTL, 1.0f, 10.f * 60.f);

    CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 8);

    // [DA_PORT] Порог снятия мелких объектов, в ПИКСЕЛЯХ экранного размера. Было зашито 3.5 с
    // пометкой RO -- наружу не выведено вовсе. Отраслевая практика считает нормальным отсекать
    // всё, что уже 4-8 пикселей; 3.5 стоит ниже этого диапазона, и подвинуть его внутрь дешевле и
    // безопаснее, чем склеивать вызовы отрисовки (замер её отверг: 0.58 мкс на вызов -- ниже
    // нижней отраслевой границы, экономить там нечего).
    //
    // 🪤 Первая попытка поставила эту строку рядом с r__ssa_glod_start -- а тот блок закрыт
    // #ifdef DEBUG, и в отгружаемой сборке команды просто не существовало. Соседство по смыслу
    // не значит соседство по условию компиляции.
    //
    // ⚠️ Влияет на СОСТАВ отрисовки: мелочь начнёт пропадать на дистанции. Меняется на лету,
    // пересчёт идёт каждый кадр в r2_R_calculate.cpp.
    CMD4(CCC_Float, "r__ssa_discard", &ps_r__ssaDISCARD, 1.f, 16.f);
    CMD4(CCC_Float, "r__veg_discard", &ps_r__vegDISCARD, 0.2f, 16.f); // [DA_PORT]
    CMD4(CCC_Float, "r__grass_fade_start", &ps_r__grass_fade_start, 0.f, 0.95f); // [DA_PORT]
    CMD4(CCC_Float, "r__grass_fade_flat", &ps_r__grass_fade_flat, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__geometry_lod", &ps_r__LOD, 0.1f, 2.f);

    // [DA_PORT] Geometry cut-off, ported from the author's build. Defaults match his: both levels on
    // "low" (i.e. the gentlest culling) and the harsher shadow-map pass enabled.
    // [DA_PORT] Render breakdown into the log, N frames. Needs rs_stats 1 as well: the sub-counters are
    // only totalled up inside DumpStatistics, so with the overlay off there is nothing to print.
    {
        extern int ps_da_render_log;
        // [DA_PORT] Потолок поднят с 2000 до 200000: значение — ЧИСЛО КАДРОВ, а минута игры при
        // 450 к/с это 27 тысяч. Прежний предел молча обрезал команду, и замер заканчивался на
        // четвёртой секунде — в отчёте это выглядело как «данных нет», а не как упёртый лимит.
        CMD4(CCC_DaDebugInteger, "da_render_log", &ps_da_render_log, 0, 200000);
        extern int ps_da_grass_dump;
        CMD4(CCC_DaDebugInteger, "da_grass_dump", &ps_da_grass_dump, 0, 200000); // [DA_PORT] период, кадров
        CMD1(CCC_DaFrame, "da_frame");
        // [DA_PORT] Замер по каскадам солнца: da_sun_log N печатает N кадров подряд. Против
        // мерцания целой тени на улице — см. комментарий в render_phase_sun.cpp::calculate.
        // [DA_PORT] ⚠️ ЧЕРЕЗ CCC_DaDebugInteger, а не CCC_Integer: диагностика не должна оседать в
        // user.ltx (почему именно — в xr_ioc_cmd.h, у самого CCC_DaDebug).
        CMD4(CCC_DaDebugInteger, "da_sun_log", &ps_da_sun_log, 0, 200000);
        CMD4(CCC_DaDebugInteger, "da_bones_dump", &ps_da_bones_dump, 0, 200000); // [DA_PORT]
        CMD4(CCC_DaDebugInteger, "da_sun_only", &ps_da_sun_only, 0, 3);

        // [DA_PORT] Разбор источников света: печать чисел и поштучная изоляция. Разбор — у
        // da_light_step в r2_R_lights.cpp. Обе ручки отладочные, в user.ltx не оседают.
        {
            extern int ps_da_light_dump;
            extern int ps_da_light_only;
            extern int ps_da_light_max;
            CMD4(CCC_DaDebugInteger, "da_light_dump", &ps_da_light_dump, 0, 100);
            CMD4(CCC_DaDebugInteger, "da_light_only", &ps_da_light_only, 0, 512);
            CMD4(CCC_DaDebugInteger, "da_light_max", &ps_da_light_max, 0, 512);
            // Обычный CCC_Integer: это ПРАВКА, а не диагностика, и она должна сохраняться.
            extern int ps_da_light_nearfix;
            CMD4(CCC_Integer, "da_light_nearfix", &ps_da_light_nearfix, 0, 2);
        }

        // [DA_PORT] Кэш теневых карт солнца. Разбор — в render_phase_sun.cpp, у da_smap_should_render.
        // ⚠️ Обычные CCC_Integer, а не CCC_DaDebug: это настройка производительности, она ДОЛЖНА
        // сохраняться в user.ltx, в отличие от диагностики.
        {
            extern u32 ps_da_smap_cache;
            extern int ps_da_smap_cache_near;
            CMD3(CCC_SmapCache, "da_smap_cache", &ps_da_smap_cache, q_smap_cache);
            CMD2(CCC_SmapCacheMs, "da_smap_cache_ms", &ps_da_smap_cache);
            // [DA_PORT] ⚠️ ЧЕРЕЗ CCC_DaDebugInteger: это ручка для замеров, а не настройка.
            // Кэширование ближнего каскада замораживает тень САМОГО ИГРОКА — оставить единицу в
            // user.ltx значит унести дефект в следующий запуск и искать его уже без подсказки.
            CMD4(CCC_DaDebugInteger, "da_smap_cache_near", &ps_da_smap_cache_near, 0, 1);
            // [DA_PORT] Порог поворота камеры для годности кэша, в градусах. Обычная настройка, а
            // не отладочная: ноль оставляет дыру, из-за которой Refined выключили кэш целиком.
            extern int ps_da_smap_cache_dir;
            CMD4(CCC_Integer, "da_smap_cache_dir", &ps_da_smap_cache_dir, 0, 90);
        }
    }
    // [DA_PORT] Разовый разбор прямого прохода: что именно даёт его вызовы отрисовки.
    // Разбор — у da_dump_forward_pass в r2_R_render.cpp.
    {
        extern int ps_da_forward_dump;
        CMD4(CCC_DaDebugInteger, "da_forward_dump", &ps_da_forward_dump, 0, 100);

        // [DA_PORT] То же для основного прохода (G-буфер).
        extern int ps_da_geom_dump;
        CMD4(CCC_DaDebugInteger, "da_geom_dump", &ps_da_geom_dump, 0, 100);
    }

    // [DA_PORT] Разбор стоимости частиц по частям: блокировка буфера / сборка / отрисовка.
    // Разбор — у CParticleEffect::Render в ParticleEffect.cpp.
    {
        extern int ps_da_particle_prof;
        CMD4(CCC_DaDebugInteger, "da_particle_prof", &ps_da_particle_prof, 0, 1000);

        // [DA_PORT] Разбор фазы света: ожидание списков / теневые карты / накопление.
        extern int ps_da_light_prof;
        CMD4(CCC_DaDebugInteger, "da_light_prof", &ps_da_light_prof, 0, 1000);

        // [DA_PORT] Разбор самого накопления по зонам: цель / маска / матрицы / константы / отрисовка.
        extern int ps_da_accum_prof;
        CMD4(CCC_DaDebugInteger, "da_accum_prof", &ps_da_accum_prof, 0, 1000);

        // [DA_PORT] Не собирать статику при обходе для ламп. 0 — как было, 1 — только у ламп с
        // годным кэшем статики, 2 — у всех (⚠️ замер потолка, тени от статики пропадут).
        extern int ps_da_light_skip_static;
        CMD4(CCC_DaDebugInteger, "r__light_skip_static", &ps_da_light_skip_static, 0, 4);

        // [DA_PORT] Общая выборка динамики на всю фазу света вместо запроса на каждую лампу.
        extern int ps_da_light_dyn_shared;
        CMD4(CCC_Integer, "r__light_dyn_shared", &ps_da_light_dyn_shared, 0, 1);
    }

    // [DA_PORT] Порог отрисовки эффектов частиц, МЕТРЫ (0 = без порога). Обычная настройка, а не
    // отладочная: она должна сохраняться между запусками. Разбор — у ps_da_particle_dist.
    {
        extern int ps_da_particle_dist;
        CMD4(CCC_Integer, "r__particle_dist", &ps_da_particle_dist, 0, 1000);
    }

#if RENDER == R_R4
    // [DA_PORT] GPU time per render phase, N frames into the log. See da_gpu_timer.h.
    {
        extern int ps_da_gpu_log;
        CMD4(CCC_DaDebugInteger, "da_gpu_log", &ps_da_gpu_log, 0, 200000);
    }

    // [DA_PORT] Прогрев кэша шейдеров в несколько потоков. Разбор — у da_shader_warmup в
    // r4_shaders.cpp. Команда, а не автозапуск: сначала мерим, потом решаем, где её звать.
    {
        class CCC_ShaderWarmup : public IConsole_Command
        {
        public:
            CCC_ShaderWarmup(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr args) override { RImplementation.da_shader_warmup(args && args[0] == '1'); }
        };
        CMD1(CCC_ShaderWarmup, "da_shader_warmup");

        class CCC_ShaderManifest : public IConsole_Command
        {
        public:
            CCC_ShaderManifest(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr) override { RImplementation.da_shader_manifest_save(); }
        };
        CMD1(CCC_ShaderManifest, "da_shader_manifest");

        // [DA_PORT] Видеопамять по требованию. Сам учёт идёт всегда и пишет в лог только пересечение
        // порога (разбор -- у CHW::da_vram_poll); команда нужна, чтобы снять число в нужный момент,
        // например сразу после загрузки уровня или на подозрительной сцене.
        class CCC_DaVram : public IConsole_Command
        {
        public:
            CCC_DaVram(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr) override { HW.da_vram_report("по запросу"); }
        };
        CMD1(CCC_DaVram, "da_vram");
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
    CMD4(CCC_Integer, "r__grass_spot_shadows", &ps_r__grass_spot_shadows, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__grass_shadow_cascades", &ps_r__grass_shadow_cascades, 0, 3); // [DA_PORT]
    CMD4(CCC_Integer, "r__grass_shadow_dist", &ps_r__grass_shadow_dist, 8, 300); // [DA_PORT]
    CMD4(CCC_Integer, "r__grass_shadow_fade", &ps_r__grass_shadow_fade, 0, 100); // [DA_PORT]
    CMD4(CCC_Float, "r__fog_sky_tint", &ps_da_fog_sky_tint, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Integer, "r__shadow_kernel_far", &ps_r__shadow_kernel_far, 1, 16); // [DA_PORT]
    CMD4(CCC_Integer, "r__shadow_rotate", &ps_r__shadow_rotate, 0, 1); // [DA_PORT]
    // [DA_PORT] Отладочная — НЕ сохраняется в user.ltx: раскраска каскадов солнца не должна
    // пережить сеанс и уехать игроку.
    CMD4(CCC_DaDebugInteger, "r__dbg_sun_cascades", &ps_r__dbg_sun_cascades, 0, 1); // [DA_PORT] диагностика каскадов
    CMD4(CCC_Float, "r__sun_shadow_fade", &ps_r__sun_shadow_fade, 20.f, 500.f); // [DA_PORT] дистанция ухода дальней тени
    CMD4(CCC_Integer, "da_cull_prof", &ps_da_cull_prof, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "da_anim_lod", &ps_da_anim_lod, 0, 500); // [DA_PORT]
    CMD4(CCC_Integer, "r__smap_stable_slots", &ps_r__smap_stable_slots, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__smap_clear_rect", &ps_r__smap_clear_rect, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__smap_cache_lights", &ps_r__smap_cache_lights, 0, 1); // [DA_PORT] нужен vid_restart
    CMD4(CCC_Integer, "r__smap_cache_atlas", &ps_r__smap_cache_atlas, 1, 4); // [DA_PORT] нужен vid_restart
    CMD4(CCC_Integer, "r__smap_cache_lights_ms", &ps_r__smap_cache_lights_ms, 0, 5000); // [DA_PORT]
    CMD4(CCC_Integer, "r__smap_size_pow2", &ps_r__smap_size_pow2, 0, 1); // [DA_PORT]
    CMD4(CCC_Float, "r__detail_height", &ps_r__Detail_height, 1, 2);

#ifdef DEBUG
    CMD4(CCC_Float, "r__detail_l_ambient", &ps_r__Detail_l_ambient, .5f, .95f);
    CMD4(CCC_Float, "r__detail_l_aniso", &ps_r__Detail_l_aniso, .1f, .5f);
#endif // DEBUG

    CMD3(CCC_Mask, "r__actor_shadow", &ps_r__common_flags, RFLAG_ACTOR_SHADOW);

    CMD2(CCC_tf_Aniso, "r__tf_aniso", &ps_r__tf_Anisotropic); // {1..16}
    CMD2(CCC_tf_MipBias, "r1_tf_mipbias", &ps_r__tf_Mipbias); // {-3 +3}
    CMD2(CCC_tf_MipBias, "r2_tf_mipbias", &ps_r__tf_Mipbias); // {-3 +3}
    CMD2(CCC_UpscaleMipBias, "r__upscale_mipbias", &ps_r__upscale_mipbias); // [DA_PORT]

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
    CMD4(CCC_Float, "r2_gloss_min", &ps_r2_gloss_min, 0.0f, 1.f);
    CMD4(CCC_Float, "r__foliage_gloss", &ps_r__foliage_gloss, 0.f, 1.f);         // [DA_PORT]
    CMD4(CCC_Float, "r__foliage_bend", &ps_r__foliage_bend, 0.f, 1.f);           // [DA_PORT]
    CMD4(CCC_Float, "r__foliage_vibrance", &ps_r__foliage_vibrance, 0.f, 3.f);   // [DA_PORT]
    CMD4(CCC_Float, "r__foliage_debleach", &ps_r__foliage_debleach, 0.f, 1.f);   // [DA_PORT]
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
    {
        // [DA_PORT] выключатель программного отсечения, для разбора мигающей геометрии
        extern int ps_da_hom_enabled;
        CMD4(CCC_DaDebugInteger, "r__hom", &ps_da_hom_enabled, 0, 1);
    }
#ifdef USE_DX11
    {
        // [DA_PORT] выключатель пакетной отрисовки деревьев — она сортирует список отрисовки и
        // удаляет из него нарисованное, поэтому для разбора пропавшей геометрии нужна ручка.
        extern int ps_da_tree_batch;
        CMD4(CCC_DaDebugInteger, "r__tree_batch", &ps_da_tree_batch, 0, 1);
    }
    {
        // [DA_PORT] зонд: сколько объектов в кадре делят одну геометрию (dxRender_Visual) — оценка
        // потолка для инстансинга статики/динамики ДО того, как его писать. Ничего не рисует иначе.
        extern int ps_da_instance_probe;
        CMD4(CCC_DaDebugInteger, "r__instance_probe", &ps_da_instance_probe, 0, 1);
    }
    {
        // [DA_PORT] досортировка статики по странице геометрии внутри шейдер-группы — кластеризует
        // смену привязки буфера вместо хаотичной по SSA. Выключатель для A/B в da_frame без пересборки.
        extern int ps_da_static_sort_geom;
        CMD4(CCC_DaDebugInteger, "r__static_sort_geom", &ps_da_static_sort_geom, 0, 1);
    }
    {
        // [DA_PORT] снятие точных дублей диапазона геометрии в статике — не инстансинг (у статики
        // трансформ запечён в вершины, разных позиций для инстансинга нет), а отказ рисовать второй
        // раз то, что уже нарисовано тем же диапазоном страницы. Выключатель для чистого A/B.
        extern int ps_da_static_dedup;
        CMD4(CCC_DaDebugInteger, "r__static_dedup", &ps_da_static_dedup, 0, 1);
    }
    {
        // [DA_PORT] тот же дедуп, что у статики, для MATRIX (динамика) — ключ (объект, визуал).
        extern int ps_da_matrix_dedup;
        CMD4(CCC_DaDebugInteger, "r__matrix_dedup", &ps_da_matrix_dedup, 0, 1);
    }
    {
        // [DA_PORT] разбивка цикла отрисовки графа изнутри: где именно лежат миллисекунды prim.
        extern int ps_da_graph_prof;
        CMD4(CCC_DaDebugInteger, "r__graph_prof", &ps_da_graph_prof, 0, 1);

        // [DA_PORT] Пропуск неиспользуемых стадий при настройке констант. 0 — прежний обход всех.
        extern int ps_da_cb_stage_skip;
        CMD4(CCC_DaDebugInteger, "r__cb_stage_skip", &ps_da_cb_stage_skip, 0, 1);
    }
    {
        // [DA_PORT] группировка проходов по таблице констант — включает ранний выход set_Constants.
        extern int ps_da_pass_sort_ctable;
        CMD4(CCC_DaDebugInteger, "r__pass_sort_ctable", &ps_da_pass_sort_ctable, 0, 1);
    }
    {
        // [DA_PORT] 0 = считать видимость основной сцены на главном потоке, без очереди задач.
        extern int ps_da_main_cull_mt;
        CMD4(CCC_DaDebugInteger, "r__main_cull_mt", &ps_da_main_cull_mt, 0, 1);
    }
    {
        // [DA_PORT] сколько параллельных контекстов раздавать (0 = все собранные, минимум 4).
        extern int ps_da_max_parallel_ctx;
        CMD4(CCC_DaDebugInteger, "r__max_parallel_ctx", &ps_da_max_parallel_ctx, 0, 16);
    }
#endif
    {
        // [DA_PORT] зонд разбора статики: da_vis_dump <кадров> пишет, какая проверка сняла визуал.
        extern int ps_da_portal_frustum;
        CMD4(CCC_DaDebugInteger, "r__portal_frustum", &ps_da_portal_frustum, 0, 1);
        extern int ps_da_vis_dump;
        extern float ps_da_vis_radius;
        CMD4(CCC_DaDebugInteger, "da_vis_dump", &ps_da_vis_dump, 0, 10);
        CMD4(CCC_DaDebugFloat, "da_vis_radius", &ps_da_vis_radius, 5.f, 300.f);
    }
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
    CMD4(CCC_Float, "r__sun_shafts_boost", &ps_r__sun_shafts_boost, 0.f, 8.f); // [DA_PORT]
    CMD4(CCC_Float, "r__sun_shafts_min", &ps_r__sun_shafts_min, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__sun_shafts_gain", &ps_r__sun_shafts_gain, 1.f, 8.f); // [DA_PORT]
    CMD4(CCC_Float, "r__sun_shafts_norm", &ps_r__sun_shafts_norm, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__sun_shafts_indoor", &ps_r__sun_shafts_indoor, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__sun_shafts_range", &ps_r__sun_shafts_range, 0.f, 200.f); // [DA_PORT]
    CMD4(CCC_Integer, "r__sun_shafts_mod", &ps_r__sun_shafts_mod, 0, 1); // [DA_PORT] чекбокс меню (GetBool знает только int/mask)
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
    CMD4(CCC_GradeValue, "r2_vibrance_val",  &ps_r2_vibrance_val, -1.f, 1.f); // [DA_PORT] negative = desaturate
    CMD3(CCC_GradingPreset, "r__grading_preset", &ps_r_grading_preset, qgrading_preset_token); // [DA_PORT]
    // [DA_PORT] Числовое зеркало для настройщика: 0 — сток, 1 — наш, дальше оттенки.
    CMD4(CCC_GradingPresetNum, "r__grade_preset", &ps_r_grading_preset_num, 0.f, 4.f);
    CMD4(CCC_Float, "r2_vignette",           &ps_r2_vignette, 0.f, 1.f);
    CMD4(CCC_Float, "r__zoom_dof",           &ps_r2_zoom_dof, 0.f, 1.f);
    CMD4(CCC_Float, "r1_dynamic_lights",    &ps_r1_dynamic_lights, 0.f, 2.f);
    CMD4(CCC_Float, "r__actor_body",         &ps_r2_actor_body, 0.f, 1.f);
    CMD4(CCC_GradeValue, "r__color_add_r",       &ps_r_color_add_r, -1.f, 1.f);
    CMD4(CCC_GradeValue, "r__color_add_g",       &ps_r_color_add_g, -1.f, 1.f);
    CMD4(CCC_GradeValue, "r__color_add_b",       &ps_r_color_add_b, -1.f, 1.f);
    CMD4(CCC_GradeValue, "r__color_base_r",      &ps_r_color_base_r, 0.f, 2.f);
    CMD4(CCC_GradeValue, "r__color_base_g",      &ps_r_color_base_g, 0.f, 2.f);
    CMD4(CCC_GradeValue, "r__color_base_b",      &ps_r_color_base_b, 0.f, 2.f);
    CMD4(CCC_GradeValue, "r__color_power_r",     &ps_r_color_power_r, 0.1f, 4.f);
    CMD4(CCC_GradeValue, "r__color_power_g",     &ps_r_color_power_g, 0.1f, 4.f);
    CMD4(CCC_GradeValue, "r__color_power_b",     &ps_r_color_power_b, 0.1f, 4.f);
    CMD4(CCC_Float, "r__tonemap_aces", &ps_r_tonemap_aces, 0.f, 2.f);
    // [DA_PORT] ⛔ Потолок 8, и поднимать его БЕССМЫСЛЕННО — проверено замером 20.08.
    //
    // Точка белого входит в формулу в КВАДРАТЕ и в знаменателе поправки:
    //     итог = L · (1 + L/W²) / (1 + L)
    // При L = 1: W=1.7 даёт 0.673, W=8 — 0.508, W=32 — 0.500. То есть от 1.7 к 8 картинка
    // меняется на четверть, а от 8 к 32 — на полтора процента, чего не видит никто.
    //
    // Работает ручка примерно от 1 до 5; выше поправка съедена квадратом и формула вырождается в
    // обычный Рейнхард. Я поднимал потолок до 32 — автор разницы не увидел, и не мог.
    CMD4(CCC_Float, "r__tonemap_white", &ps_r_tonemap_white, 0.5f, 8.f);
    CMD4(CCC_Float, "r__tonemap_hue", &ps_r_tonemap_hue, 0.f, 1.f);              // [DA_PORT]
    CMD4(CCC_Float, "r__tonemap_desat", &ps_r_tonemap_desat, 1.f, 8.f);          // [DA_PORT]
    CMD4(CCC_Float, "r__linear_light", &ps_r_linear_light, 0.f, 1.f);
}
} // namespace xray::render::RENDER_NAMESPACE
