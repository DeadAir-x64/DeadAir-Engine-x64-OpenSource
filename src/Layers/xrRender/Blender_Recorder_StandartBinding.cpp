#include "stdafx.h"
#pragma hdrstop

#include "ResourceManager.h"
#include "Blender_Recorder.h"
#include "Blender.h"

#include "xrEngine/IGame_Persistent.h"
#include "xrEngine/Environment.h"

// [DA_PORT] Ручки луж живут в глобальном пространстве имён (xr_ioc_cmd.cpp), объявлять их ВНУТРИ
// namespace нельзя: имя сманглится в render_r4:: и линковка упадёт на неразрешённых символах.
extern ENGINE_API int ps_r__puddles;
extern ENGINE_API float ps_r__puddles_buildup;
extern ENGINE_API float ps_r__puddles_dry;
extern ENGINE_API float ps_r__puddles_size;
extern ENGINE_API float ps_r__puddles_force;
extern ENGINE_API float ps_r__puddles_gloss;
extern ENGINE_API float ps_r__puddles_dark;
extern ENGINE_API float ps_r__puddles_damp;
extern ENGINE_API float ps_r__puddles_ripple;
extern ENGINE_API int ps_r__puddles_debug;
extern ENGINE_API u32 ps_r__puddles_dist;
extern ENGINE_API int ps_r__puddles_gbuf;
extern ENGINE_API float ps_r__puddles_edge;
extern ENGINE_API float ps_r__puddles_rim;
extern ENGINE_API float ps_r__puddles_rim_width;
extern ENGINE_API float g_da_rain_wetness; // [DA_PORT] сюда кладём накопленную влажность для игры

namespace xray::render::RENDER_NAMESPACE
{
// matrices
#define BIND_DECLARE(xf)\
    class cl_xform_##xf : public R_constant_setup\
    {\
        void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.xforms.set_c_##xf(C); }\
    };\
    static cl_xform_##xf binder_##xf
BIND_DECLARE(w);
BIND_DECLARE(invw);
BIND_DECLARE(v);
BIND_DECLARE(p);
BIND_DECLARE(wv);
BIND_DECLARE(vp);
BIND_DECLARE(wvp);
BIND_DECLARE(wvp_old); // [DA_PORT] motion vectors
BIND_DECLARE(wvp_nojit); // [DA_PORT] motion vectors

#define DECLARE_TREE_BIND(c)\
    class cl_tree_##c : public R_constant_setup\
    {\
        void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.tree.set_c_##c(C); }\
    };\
    static cl_tree_##c tree_binder_##c

DECLARE_TREE_BIND(m_xform_v);
DECLARE_TREE_BIND(m_xform);
DECLARE_TREE_BIND(consts);
DECLARE_TREE_BIND(wave);
DECLARE_TREE_BIND(wind);
DECLARE_TREE_BIND(wave_old); // [DA_PORT]
DECLARE_TREE_BIND(wind_old); // [DA_PORT]
DECLARE_TREE_BIND(c_scale);
DECLARE_TREE_BIND(c_bias);
DECLARE_TREE_BIND(c_sun);

class cl_hemi_cube_pos_faces : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.hemi.set_c_pos_faces(C); }
};

static cl_hemi_cube_pos_faces binder_hemi_cube_pos_faces;

class cl_hemi_cube_neg_faces : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.hemi.set_c_neg_faces(C); }
};

static cl_hemi_cube_neg_faces binder_hemi_cube_neg_faces;

class cl_material : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.hemi.set_c_material(C); }
};

static cl_material binder_material;

class cl_texgen : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        Fmatrix mTexgen;

#if defined(USE_DX11)
        Fmatrix mTexelAdjust =
        {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        };
#elif defined(USE_OGL)
        Fmatrix mTexelAdjust =
        {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        };
#else
#    error No graphics API selected or in use!
#endif

        mTexgen.mul(mTexelAdjust, cmd_list.xforms.m_wvp);
        cmd_list.set_c(C, mTexgen);
    }
};
static cl_texgen binder_texgen;

class cl_VPtexgen : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        Fmatrix mTexgen;

#if defined(USE_DX11)
        Fmatrix mTexelAdjust =
        {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        };
#elif defined(USE_OGL)
        Fmatrix mTexelAdjust =
        {
            0.5f, 0.0f, 0.0f, 0.0f,
            0.0f, 0.5f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.5f, 0.5f, 0.0f, 1.0f
        };
#else
#    error No graphics API selected or in use!
#endif

        mTexgen.mul(mTexelAdjust, cmd_list.xforms.m_vp);
        cmd_list.set_c(C, mTexgen);
    }
};
static cl_VPtexgen binder_VPtexgen;

// fog
#ifndef _EDITOR
class cl_fog_plane : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            // Plane
            Fvector4 plane;
            Fmatrix& M = Device.mFullTransform;
            plane.x = -(M._14 + M._13);
            plane.y = -(M._24 + M._23);
            plane.z = -(M._34 + M._33);
            plane.w = -(M._44 + M._43);
            float denom = -1.0f / _sqrt(_sqr(plane.x) + _sqr(plane.y) + _sqr(plane.z));
            plane.mul(denom);

            // Near/Far
            float A = g_pGamePersistent->Environment().CurrentEnv.fog_near;
            float B = 1 / (g_pGamePersistent->Environment().CurrentEnv.fog_far - A);
            result.set(-plane.x * B, -plane.y * B, -plane.z * B, 1 - (plane.w - A) * B); // view-plane
        }
        cmd_list.set_c(C, result);
    }
};
static cl_fog_plane binder_fog_plane;

// fog-params
class cl_fog_params : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            // Near/Far
            float n = g_pGamePersistent->Environment().CurrentEnv.fog_near;
            float f = g_pGamePersistent->Environment().CurrentEnv.fog_far;
            float r = 1 / (f - n);
            result.set(-n * r, r, r, r);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_fog_params binder_fog_params;

// fog-color
class cl_fog_color : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.fog_color.x, desc.fog_color.y, desc.fog_color.z, 0);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_fog_color binder_fog_color;
#endif

// times
class cl_times : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        float t = Device.fTimeGlobal;
        cmd_list.set_c(C, t, t * 10, t / 10, _sin(t));
    }
};
static cl_times binder_times;

// [DA_PORT] ---- Лужи: сила дождя и накопленная влажность --------------------------------------
//
// x — сколько льёт сейчас (rain_density текущей погоды, 0..1);
// y — насколько земля намокла (0..1), с задержкой на набор и высыхание;
// z — доля поверхности под лужами при полной влажности (r__puddles_size);
// w — свободно.
//
// Накопитель считается РАЗ НА КАДР, а не на каждую привязку: биндер зовут по разу на каждый проход
// и каждый объект, и без пометки кадра влага набиралась бы со скоростью, зависящей от того, сколько
// геометрии в кадре (см. [[silent-failure-debugging]] — биндер раз на проход вместо раза на объект
// уже стоил нам одного молчаливого дефекта, тут та же ловушка с другой стороны).
class cl_rain_params : public R_constant_setup
{
    u32 marker{};
    float wetness{};
    Fvector4 result{};

    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            marker = Device.dwFrame;

            const float rain = ps_r__puddles ? g_pGamePersistent->Environment().CurrentEnv.rain_density : 0.f;

            // Проверочный режим: сырость задана руками, накопитель не участвует — иначе после
            // r__puddles_force пришлось бы ещё полторы минуты ждать, пока «наберётся».
            const float dbg = float(ps_r__puddles_debug); // 1 — каналы, 2 — заливка

            // [DA_PORT] При включённой отладке — раз в секунду в лог. Нужно, чтобы отделить «значение
            // не посчиталось в движке» от «посчиталось, но не доехало до шейдера»: по одной картинке
            // эти два случая неотличимы, а перебирать их вслепую уже дважды выходило дороже.
            if (ps_r__puddles_debug)
            {
                static u32 last_log = 0;
                if (Device.dwTimeGlobal - last_log > 1000)
                {
                    last_log = Device.dwTimeGlobal;
                    Msg("* [DA_PORT] лужи: дождь %.3f, влажность %.3f, размер %.2f, force %.2f, вкл %d",
                        rain, wetness, ps_r__puddles_size, ps_r__puddles_force, ps_r__puddles);
                }
            }

            if (ps_r__puddles && ps_r__puddles_force > 0.f)
            {
                wetness = ps_r__puddles_force;
                g_da_rain_wetness = wetness;
                result.set(ps_r__puddles_force, wetness, ps_r__puddles_size, dbg);
                cmd_list.set_c(C, result);
                return;
            }

            // ⚠️ Сила дождя задаёт СКОРОСТЬ намокания, а не потолок.
            //
            // Раньше влажность шла экспонентой К ЗНАЧЕНИЮ rain_density, то есть слабый дождь навсегда
            // упирался в свою же долю: при 0.2 влажность стояла на 0.2, маска луж умножалась на неё
            // же — и луж не было вовсе. А в конфигах DA слабый дождь это как раз 0.1–0.3, единица
            // бывает только в грозу. Получалось, что лужи появлялись лишь в ливень.
            //
            // В жизни слабый дождь мочит землю не слабее, а ДОЛЬШЕ: воды в минуту меньше, но она
            // копится, и через полчаса моросящего дождя лужи не хуже грозовых. Поэтому дождь любой
            // силы ведёт влажность к единице, и только скорость зависит от силы.
            //
            // Нижняя граница 0.25 — чтобы морось (0.05) не набирала воду сутки: самый слабый дождь
            // наполняет за четыре срока r__puddles_buildup, ливень — за один.
            const float dt = Device.fTimeDelta;
            if (rain > 0.02f)
            {
                const float speed = (0.25f + 0.75f * rain) / _max(ps_r__puddles_buildup, EPS_S);
                wetness += dt * speed;
            }
            else
            {
                // Сохнет тем же ходом, только во столько раз медленнее, во сколько сказано ручкой.
                wetness -= dt / _max(ps_r__puddles_buildup * ps_r__puddles_dry, EPS_S);
            }
            clamp(wetness, 0.f, 1.f);

            // [DA_PORT] Отдаём наружу: по этому числу игровой код решает, плескать ли под ногами.
            g_da_rain_wetness = wetness;

            result.set(rain, wetness, ps_r__puddles_size, dbg);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_rain_params binder_rain_params;

// [DA_PORT] Вид воды: зеркальность, потемнение, глянец просто мокрой земли, сила ряби.
// Отдельной константой от rain_params — та про погоду, эта про внешний вид, и меняется руками.
// [DA_PORT] Вторая константа вида: дальность, за которой луж не рисуем. Отдельной сделана потому,
// что первые четыре слота заняты, а расширять по месту дешевле, чем перетасовывать смыслы.
class cl_puddle_look2 : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // [DA_PORT] w — выключатель G-буферной половины (r__puddles_gbuf), см. deffer_impl_flat.ps
        // [DA_PORT] y — жёсткость края, z — сила каймы, w — выключатель G-буферной половины.
        cmd_list.set_c(C, float(ps_r__puddles_dist), ps_r__puddles_edge, ps_r__puddles_rim,
            float(ps_r__puddles_gbuf));
    }
};
static cl_puddle_look2 binder_puddle_look2;

// [DA_PORT] Третья константа вида: ширина каймы. Заведена потому, что в look2 остался один слот, а
// кайме нужно два — сила и ширина. Три свободных слота оставлены на будущее.
class cl_puddle_look3 : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        cmd_list.set_c(C, ps_r__puddles_rim_width, 0.f, 0.f, 0.f);
    }
};
static cl_puddle_look3 binder_puddle_look3;

class cl_puddle_look : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // [DA_PORT] Отметка о том, что константу вообще спросили. Если этой строки в логе нет, а
        // строка про влажность есть — значит имя da_puddle_look не нашлось в таблице, шейдер читает
        // мусор из чужого регистра, и внутри лужи не работают ни потемнение, ни глянец. Отличить это
        // от «работает, но выглядит слабо» по картинке нельзя, а по логу — сразу.
        if (ps_r__puddles_debug)
        {
            static u32 last_log = 0;
            if (Device.dwTimeGlobal - last_log > 1000)
            {
                last_log = Device.dwTimeGlobal;
                Msg("* [DA_PORT] лужи, вид: глянец %.2f, темнее в %.2f, мокрота %.2f, рябь %.2f",
                    ps_r__puddles_gloss, ps_r__puddles_dark, ps_r__puddles_damp, ps_r__puddles_ripple);
            }
        }
        cmd_list.set_c(C, ps_r__puddles_gloss, ps_r__puddles_dark, ps_r__puddles_damp, ps_r__puddles_ripple);
    }
};
static cl_puddle_look binder_puddle_look;

// eye-params
class cl_eye_P : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        Fvector& V = Device.vCameraPosition;
        cmd_list.set_c(C, V.x, V.y, V.z, 1.f);
    }
};
static cl_eye_P binder_eye_P;

// eye-params
class cl_eye_D : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        Fvector& V = Device.vCameraDirection;
        cmd_list.set_c(C, V.x, V.y, V.z, 0.f);
    }
};
static cl_eye_D binder_eye_D;

// eye-params
class cl_eye_N : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        Fvector& V = Device.vCameraTop;
        cmd_list.set_c(C, V.x, V.y, V.z, 0.f);
    }
};
static cl_eye_N binder_eye_N;

#ifndef _EDITOR
// D-Light0
class cl_sun0_color : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.sun_color.x, desc.sun_color.y, desc.sun_color.z, 0);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_sun0_color binder_sun0_color;
class cl_sun0_dir_w : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.sun_dir.x, desc.sun_dir.y, desc.sun_dir.z, 0);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_sun0_dir_w binder_sun0_dir_w;
class cl_sun0_dir_e : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            Fvector D;
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            Device.mView.transform_dir(D, desc.sun_dir);
            D.normalize();
            result.set(D.x, D.y, D.z, 0);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_sun0_dir_e binder_sun0_dir_e;

//
class cl_amb_color : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.ambient.x, desc.ambient.y, desc.ambient.z, desc.weight);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_amb_color binder_amb_color;
class cl_hemi_color : public R_constant_setup
{
    u32 marker;
    Fvector4 result;
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        if (marker != Device.dwFrame)
        {
            const auto& desc = g_pGamePersistent->Environment().CurrentEnv;
            result.set(desc.hemi_color.x, desc.hemi_color.y, desc.hemi_color.z, desc.hemi_color.w);
        }
        cmd_list.set_c(C, result);
    }
};
static cl_hemi_color binder_hemi_color;
#endif

static class cl_screen_res : public R_constant_setup
{
    void setup(CBackend& cmd_list, R_constant* C) override
    {
        // [DA_PORT] Deliberately the plain window size, i.e. stock behaviour. Deriving it from the current
        // render target instead is tempting — the deferred passes and the 2D/UI shaders genuinely need
        // different answers once r__render_scale shrinks the scene targets — but it also changes what
        // passes that render into small fixed-size targets (bloom, luminance) see, and that is a
        // behavioural change at 100% scale, which must stay byte-identical to before.
        cmd_list.set_c(C, (float)Device.dwWidth, (float)Device.dwHeight, 1.0f / (float)Device.dwWidth,
            1.0f / (float)Device.dwHeight);
    }
} binder_screen_res;

// SM_TODO: cmd_list.hemi заменить на более "логичное" место
static class cl_hud_params : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, g_pGamePersistent->m_pGShaderConstants->hud_params); }
} binder_hud_params;

static class cl_script_params : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_script_params); }
} binder_script_params;

static class cl_blend_mode : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.set_c(C, g_pGamePersistent->m_pGShaderConstants->m_blender_mode); }
} binder_blend_mode;

class cl_camo_data : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override  { cmd_list.hemi.c_camo_data = C; }
};
static cl_camo_data binder_camo_data;

class cl_custom_data : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.hemi.c_custom_data = C; }
};
static cl_custom_data binder_custom_data;

class cl_entity_data : public R_constant_setup //--#SM+#--
{
    void setup(CBackend& cmd_list, R_constant* C) override { cmd_list.hemi.c_entity_data = C; }
};
static cl_entity_data binder_entity_data;

// Standart constant-binding
void CBlender_Compile::SetMapping()
{
    // misc
    r_Constant("m_hud_params", &binder_hud_params); //--#SM+#--
    r_Constant("m_script_params", &binder_script_params); //--#SM+#--
    r_Constant("m_blender_mode", &binder_blend_mode); //--#SM+#--

    // objects data
    r_Constant("m_obj_camo_data", &binder_camo_data); //--#SM+#--
    r_Constant("m_obj_custom_data", &binder_custom_data); //--#SM+#--
    r_Constant("m_obj_entity_data", &binder_entity_data); //--#SM+#--

    // matrices
    r_Constant("m_W", &binder_w);
    r_Constant("m_invW", &binder_invw);
    r_Constant("m_V", &binder_v);
    r_Constant("m_P", &binder_p);
    r_Constant("m_WV", &binder_wv);
    r_Constant("m_VP", &binder_vp);
    r_Constant("m_WVP", &binder_wvp);
    // [DA_PORT] Motion vectors. Registered here, with the transforms, rather than as ordinary
    // constant-setup binders: those run once per shader pass, while the scene graph draws many
    // objects per pass. Only the transform path is re-evaluated for every draw call.
    r_Constant("m_WVP_old", &binder_wvp_old);
    r_Constant("m_VP_nojit", &binder_wvp_nojit); // shader-facing name kept as it was

    r_Constant("m_xform_v", &tree_binder_m_xform_v);
    r_Constant("m_xform", &tree_binder_m_xform);
    r_Constant("consts", &tree_binder_consts);
    r_Constant("wave", &tree_binder_wave);
    r_Constant("wind", &tree_binder_wind);
    r_Constant("wave_old", &tree_binder_wave_old); // [DA_PORT] motion vectors
    r_Constant("wind_old", &tree_binder_wind_old); // [DA_PORT] motion vectors
    r_Constant("c_scale", &tree_binder_c_scale);
    r_Constant("c_bias", &tree_binder_c_bias);
    r_Constant("c_sun", &tree_binder_c_sun);

    // hemi cube
    r_Constant("L_material", &binder_material);
    r_Constant("hemi_cube_pos_faces", &binder_hemi_cube_pos_faces);
    r_Constant("hemi_cube_neg_faces", &binder_hemi_cube_neg_faces);

    // Igor temp solution for the texgen functionality in the shader
    r_Constant("m_texgen", &binder_texgen);
    r_Constant("mVPTexgen", &binder_VPtexgen);

#ifndef _EDITOR
    // fog-params
    r_Constant("fog_plane", &binder_fog_plane);
    r_Constant("fog_params", &binder_fog_params);
    r_Constant("fog_color", &binder_fog_color);
#endif
    // time
    r_Constant("timers", &binder_times);

    // [DA_PORT] лужи: дождь сейчас + накопленная влажность, и отдельно — их вид
    r_Constant("rain_params", &binder_rain_params);
    r_Constant("da_puddle_look", &binder_puddle_look);
    r_Constant("da_puddle_look2", &binder_puddle_look2);
    r_Constant("da_puddle_look3", &binder_puddle_look3);

    // eye-params
    r_Constant("eye_position", &binder_eye_P);
    r_Constant("eye_direction", &binder_eye_D);
    r_Constant("eye_normal", &binder_eye_N);

#ifndef _EDITOR
    // global-lighting (env params)
    r_Constant("L_sun_color", &binder_sun0_color);
    r_Constant("L_sun_dir_w", &binder_sun0_dir_w);
    r_Constant("L_sun_dir_e", &binder_sun0_dir_e);
    //r_Constant("L_lmap_color", &binder_lm_color);
    r_Constant("L_hemi_color", &binder_hemi_color);
    r_Constant("L_ambient", &binder_amb_color);
#endif
    r_Constant("screen_res", &binder_screen_res);

    // detail
    // if (bDetail  && detail_scaler)
    // Igor: bDetail can be overridden by no_detail_texture option.
    // But shader can be deatiled implicitly, so try to set this parameter
    // anyway.
    if (detail_scaler)
        r_Constant("dt_params", detail_scaler);

    // other common
    for (u32 it = 0; it < RImplementation.Resources->v_constant_setup.size(); it++)
    {
        std::pair<shared_str, R_constant_setup*> cs = RImplementation.Resources->v_constant_setup[it];
        r_Constant(cs.first.c_str(), cs.second);
    }
}
} // namespace xray::render::RENDER_NAMESPACE
