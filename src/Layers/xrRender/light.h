#pragma once

#include "xrCDB/ISpatial.h"

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4) || (RENDER==R_GL)
#include "Light_Package.h"
#include "light_smapvis.h"
#include "light_gi.h"
#endif //(RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4) || (RENDER==R_GL)

namespace xray::render::RENDER_NAMESPACE
{
class light : public IRender_Light, public SpatialBase
{
public:
    struct
    {
        u32 type : 4;
        u32 bStatic : 1;
        u32 bActive : 1;
        u32 bShadow : 1;
        u32 bVolumetric : 1;
        u32 bHudMode : 1;
        u32 bNeverDemote : 1; // [DA_PORT] см. IRender_Light::set_never_demote

    } flags;
    Fvector position;
    Fvector direction;
    Fvector right;
    float range;
    float virtual_size;
    float cone;
    Fcolor color;

    vis_data hom;
    u32 frame_render;

    float m_volumetric_quality;
    float m_volumetric_intensity;
    float m_volumetric_distance;

#if RENDER == R_R1
    Flight ldata;
#else
    float falloff; // precalc to make light equal to zero at light range
    float attenuation0; // Constant attenuation
    float attenuation1; // Linear attenuation
    float attenuation2; // Quadratic attenuation

    light* omnipart[6];
    xr_vector<light_indirect> indirect;
    u32 indirect_photons;

    smapvis svis[R__NUM_CONTEXTS]; // used for 6-cubemap faces

    ref_shader s_spot;
    ref_shader s_point;
    ref_shader s_volumetric;
    // [DA_PORT] Отдельный шейдер объёмного света для источника БЕЗ тени.
    //
    // Заводской accum_volumetric ВСЕГДА делает выборку из атласа теней по X.S.size/posX/posY.
    // У источника без тени этих координат никто не считал — там мусор от прошлого кадра, выборка
    // уходит в никуда, и свет гасится ровной границей по краю конуса. Ровно этот симптом описан в
    // нашем комментарии к потолку теневых ламп («мир поделили надвое»).
    //
    // Найдено не у себя: Dead Air Refined — там завели вариант шейдера, который в атлас не смотрит.
    ref_shader s_volumetric_unshadowed;

#if (RENDER == R_R3) || (RENDER == R_R4) || (RENDER == R_GL)
    ref_shader s_spot_msaa[8];
    ref_shader s_point_msaa[8];
    ref_shader s_volumetric_msaa[8];
#endif //	(RENDER==R_R3) || (RENDER==R_R4) || (RENDER==R_GL)

    u32 m_xform_frame;
    Fmatrix m_xform;

    struct _vis
    {
        u32 frame2test; // frame the test is sheduled to
        u32 query_id; // ID of occlusion query
        u32 query_order; // order of occlusion query
        // [DA_PORT] С чего именно спрашивали. Ответ теперь приходит не сразу, а через кадр-другой,
        // и к тому времени камера может смотреть в другую сторону — тогда «ноль пикселей» уже ничего
        // не значит и лампу гасить нельзя. Перенесено из Dead Air Refined.
        u32 query_frame;
        Fvector query_camera_position;
        Fvector query_camera_direction;
        bool visible; // visible/invisible
        bool pending; // test is still pending
        // [DA_PORT] Сколько проверок подряд сказали «не видно». Гасим лампу только после нескольких:
        // один отрицательный ответ по нескольким пикселям — это не «скрылась», а дрожание порога.
        // См. light_vis.cpp.
        u8 miss_streak;
        u16 smap_ID;
    } vis;

    // [DA_PORT] Состояние СТАТИЧЕСКОЙ половины теневой карты этой лампы.
    //
    // Кэшируется только статика: она лежит в отдельном атласе (rt_da_smap_static) и каждый кадр
    // копируется в рабочий, поверх которого дорисовывается динамика. Поэтому здесь описано ровно
    // то, от чего зависит СТАТИЧЕСКАЯ картинка: где лампа стояла, куда светила, какой у неё был
    // радиус и какое место в атласе.
    //
    // ⛔ Отпечатка движущихся объектов здесь БОЛЬШЕ НЕТ, и это главное отличие от первой версии.
    // Раньше кэшировалась карта целиком, поэтому приходилось угадывать «шевельнулось ли что-то в
    // объёме лампы» — и любая неточность догадки давала моргание. Теперь угадывать нечего:
    // динамика рисуется каждый кадр, статика меняется только по событию.
    struct da_smap_cache
    {
        Fvector pos{};      // положение лампы на момент отрисовки статики
        Fvector dir{};      // направление
        float range{};      // радиус
        u32 posX{}, posY{}; // место в атласе
        u32 size{};         // и размер ячейки
        u16 smap_ID{};      // номер атласа
        u32 time_ms{};      // когда нарисована статика
        u32 gen{};          // поколение атласа статики: пережил ли кэш пересоздание целей
        u32 serial{};       // порядковый номер записи в атлас: чужая запись после нашей — кэш негоден
        // [DA_PORT] ПОСТОЯННОЕ место лампы в атласе статики — своё, независимое от рабочего.
        //
        // Рабочий атлас перепаковывается каждый кадр и делится на пачки, поэтому его раскладку
        // повторить нельзя. Так же устроено в HDRP: у кэшированных теней свой атлас, источник
        // получает место при первой отрисовке и держит его, а не влезший просто не кэшируется.
        u32 st_posX{}, st_posY{}, st_size{};
        bool st_owned{};    // место в атласе статики за лампой закреплено
        bool valid{};       // статика в атласе есть и соответствует полям выше
        bool slot_kept{};   // ячейка досталась ТА ЖЕ, что в прошлом кадре
    } da_cache;

    union _xform
    {
        struct Directional
        {
            Fmatrix combine;
            s32 minX, maxX;
            s32 minY, maxY;
            BOOL transluent;
        } D[R__NUM_SUN_CASCADES];
        struct Point
        {
            Fmatrix world;
            Fmatrix view;
            Fmatrix project;
            Fmatrix combine;
        } P;
        struct Spot
        {
            Fmatrix view;
            Fmatrix project;
            Fmatrix combine;
            u32 size;
            u32 posX;
            u32 posY;
            BOOL transluent;
        } S;
    } X;
#endif //	(RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4) || (RENDER==R_GL)

public:
    void set_type(LT type) override
    {
        flags.type = type;
    }

    void set_active(bool b) override;

    [[nodiscard]]
    bool get_active() override { return flags.bActive; }

    void set_shadow(bool b) override { flags.bShadow = b; }

    void set_volumetric(bool b) override { flags.bVolumetric = b; }

    void set_volumetric_quality(float fValue) override { m_volumetric_quality = fValue; }

    void set_volumetric_intensity(float fValue) override { m_volumetric_intensity = fValue; }

    void set_volumetric_distance(float fValue) override { m_volumetric_distance = fValue; }

    void set_position(const Fvector& P) override;

    void set_rotation(const Fvector& D, const Fvector& R) override;

    void set_cone(float angle) override;

    void set_range(float R) override;

    void set_virtual_size(float R) override { virtual_size = R; }

    void set_color(const Fcolor& C) override
    {
        color.set(C);
    }

    void set_color(float r, float g, float b) override
    {
        color.set(r, g, b, 1);
    }

    void set_texture(LPCSTR name) override;

    void set_hud_mode(bool b) override { flags.bHudMode = b; }
    void set_never_demote(bool b) override { flags.bNeverDemote = b; }
    [[nodiscard]]
    bool get_hud_mode() override { return flags.bHudMode; }

    void spatial_move() override;
    Fvector spatial_sector_point() override;

    IRender_Light* dcast_Light() override { return this; }
    vis_data& get_homdata();

#if (RENDER == R_R2) || (RENDER == R_R3) || (RENDER == R_R4) || (RENDER == R_GL)
    void gi_generate();
    void xform_calc();
    void vis_prepare(CBackend& cmd_list);
    void vis_update();
    void Export(light_Package& dest);
    void set_attenuation_params(float a0, float a1, float a2, float fo);
#endif // (RENDER==R_R2) || (RENDER==R_R3) || (RENDER==R_R4) || (RENDER==R_GL)

    [[nodiscard]]
    float get_LOD() const;

    light();
    ~light() override;
};
} // namespace xray::render::RENDER_NAMESPACE
