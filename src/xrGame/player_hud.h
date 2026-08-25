#pragma once
#include "firedeps.h"

#include "Include/xrRender/Kinematics.h"
#include "Include/xrRender/KinematicsAnimated.h"
#include "actor_defs.h"

class player_hud;
class CHudItem;
class CMotionDef;

struct motion_descr
{
    MotionID mid;
    shared_str name;
};

struct player_hud_motion
{
    shared_str m_base_name;
    shared_str m_additional_name;
    xr_vector<motion_descr> m_animations;
    float m_anim_speed;
};

struct player_hud_motion_container
{
    xr_unordered_map<shared_str, player_hud_motion> m_anims;

    [[nodiscard]]
    const player_hud_motion* find_motion(const shared_str& name) const;

    void load(IKinematicsAnimated* model, const shared_str& sect);
};

struct hud_item_measures
{
    enum
    {
        e_fire_point = (1 << 0),
        e_fire_point2 = (1 << 1),
        e_shell_point = (1 << 2),
        e_16x9_mode_now = (1 << 3)
    };

    Fvector m_hands_offset[2][3]{}; // pos,rot/ normal,aim,GL
    Fvector m_hands_attach[2]{}; // pos,rot
    Fvector m_item_attach[2]{}; // pos,rot

    Fvector m_fire_point_offset{};
    Fvector m_fire_point2_offset{};
    Fvector m_shell_point_offset{};

    u16 m_fire_bone;
    u16 m_fire_bone2;
    u16 m_shell_bone;
    Flags8 m_prop_flags;

    Fmatrix load(const shared_str& sect_name, IKinematics* K);
    Fmatrix load_monolithic(const shared_str& sect_name, IKinematics* K, CHudItem* owner);
    void load_inertion_params(const shared_str& sect_name);
    void update(Fmatrix& attach_offset);

    struct inertion_params
    {
        float m_pitch_offset_r;
        float m_pitch_offset_n;
        float m_pitch_offset_d;
        float m_pitch_low_limit;
        float m_origin_offset;
        float m_origin_offset_aim;
        float m_tendto_speed;
        float m_tendto_speed_aim;
    };
    inertion_params m_inertion_params; //--#SM+#--
};

struct attachable_hud_item
{
    player_hud* m_parent{};
    CHudItem* m_parent_hud_item{};
    shared_str m_sect_name;
    shared_str m_visual_name;
    IKinematics* m_model{};
    u16 m_attach_place_idx{};
    bool m_monolithic{};
    hud_item_measures m_measures;

    // runtime positioning
    Fmatrix m_attach_offset{};
    Fmatrix m_item_transform{};

    player_hud_motion_container m_hand_motions;

    attachable_hud_item(player_hud* parent, const shared_str& sect_name, IKinematicsAnimated* model);
    ~attachable_hud_item();

    void reload_measures();

    void update(bool bForce);
    void update_hud_additional(Fmatrix& trans) const;

    void setup_firedeps(firedeps& fd);

    void render(u32 context_id, IRenderable* root);
    void render_item_ui() const;
    bool render_item_ui_query() const;
    bool need_renderable() const;
    void set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent = FALSE);

    // hands bind position
    Fvector& hands_attach_pos();
    Fvector& hands_attach_rot();

    // hands runtime offset
    Fvector& hands_offset_pos();
    Fvector& hands_offset_rot();

    // props
    u32 m_upd_firedeps_frame{ u32(-1) };
    void tune(Ivector values);
    u32 anim_play(const shared_str& anim_name, BOOL bMixIn, const CMotionDef*& md, u8& rnd);
};

class player_hud
{
public:
    player_hud() = default;
    ~player_hud();
    void load(const shared_str& model_name);
    void load_default() { load("actor_hud_05"); };
    void update(const Fmatrix& trans);
    void render_hud(u32 context_id, IRenderable* root);
    void render_item_ui() const;
    bool render_item_ui_query() const;
    u32 anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, IKinematicsAnimated* itemModel);
    const shared_str& section_name() const { return m_sect_name; }
    attachable_hud_item* create_hud_item(const shared_str& sect);

    void attach_item(CHudItem* item);
    bool allow_activation(CHudItem* item) const;
    attachable_hud_item* attached_item(u16 item_idx) { return m_attached_items[item_idx]; };
    void detach_item_idx(u16 idx);
    void detach_item(CHudItem* item);
    void detach_all_items()
    {
        m_attached_items[0] = NULL;
        m_attached_items[1] = NULL;
    };

    void calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result) const;

    // [DA_PORT] Раскладка цикла по КОПИЯМ модели рук. Разбор — у определения в player_hud.cpp.
    //   pid 0 — обе руки, 1 — левая (m_model_2), 2 — правая (m_model).
    // script_anim: цикл ставит скриптовая сцена, замок владения на неё не действует.
    void da_play_blend(u16 pid, const MotionID& M, BOOL bMixIn, float speed, bool script_anim);
    // Посадка рук по половинам: 0 — правая, 1 — левая. Если своего предмета нет, берётся чужой.
    Fvector da_attach_pos(u8 part) const;
    Fvector da_attach_rot(u8 part) const;
    void tune(Ivector values);
    u32 motion_length(const MotionID& M, const CMotionDef*& md, float speed, IKinematicsAnimated* itemModel) const;
    u32 motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md);

    // [DA_PORT] Длина цикла из ЛЮБОЙ секции с анимациями рук, включая скриптовые сцены.
    // Отличие от motion_length выше: та требует attachable_hud_item, то есть настоящий предмет в
    // руках, а у сцены его может не быть вовсе.
    u32 da_script_motion_length(pcstr section, pcstr anim, float speed);
    void OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd) const;

    // [DA_PORT] Анимация рук, запущенная СКРИПТОМ, без предмета в руках.
    //
    // Зачем отдельный путь. Обычная анимация рук привязана к предмету: attachable_hud_item требует
    // в секции item_visual и падает без него. А скриптовой сцене (паркур) предмет не нужен вовсе —
    // нужны только руки и цикл на них. Поэтому берём от секции ТОЛЬКО набор движений и играем его
    // на модели рук напрямую.
    //
    // Возвращает длительность в миллисекундах, ноль — если секции или движения нет.
    // target_ms — под какую длительность растянуть цикл; 0 — играть как записан.
    u32 da_script_anim_play(u8 hand, pcstr section, pcstr anim, bool mix_in, float speed, u32 target_ms = 0);
    void da_script_anim_stop();
    void da_script_item_release();
    bool da_script_anim_active() const;
    // Текущая посадка предмета сцены — для печати подобранных чисел (da_scene_item_dump).
    void da_script_item_tune(Fvector& pos, Fvector& rot, float& scale) const
    {
        pos = m_da_script_item_pos;
        rot = m_da_script_item_rot;
        scale = m_da_script_item_scale;
    }

private:
    // Наборы движений для скриптовых анимаций, по одному на секцию. Держим их, а не создаём заново:
    // загрузка перебирает строки конфига и ищет циклы в модели, а зовут её каждое подтягивание.
    xr_map<shared_str, player_hud_motion_container> m_da_script_motions;
    u32 m_da_script_anim_end{};
    bool m_da_script_anim_on{}; // сцена идёт от play до stop, см. da_script_anim_active
    Fvector m_da_script_hands_pos{}; // посадка рук на время скриптовой анимации
    Fvector m_da_script_hands_rot{}; // поворот рук на время скриптовой анимации, ГРАДУСЫ
    bool m_da_script_item_root_lock{true}; // подавлять ли корневую кость предмета сцены
    bool m_da_script_one_hand{false}; // сцена играет ОДНОЙ рукой, вторая держит оружие
    // Модель предмета в руках на время скриптовой анимации: бутылка, аптечка и прочее.
    // Владеем ею сами — создаём при запуске, удаляем при остановке.
    // [DA_PORT] Сама модель предмета сцены — ею мы владеем и её рисуем.
    //
    // Отдельно от m_da_script_item_model, потому что НЕ У ВСЯКОЙ модели есть анимация: у наших
    // рюкзаков её нет, они рисовались висеть на спине. Раньше здесь стоял только приведённый к
    // анимированному указатель, и на неанимированной модели приведение давало ноль — предмет
    // молча не рисовался вовсе, а созданный визуал ещё и утекал.
    IRenderVisual* m_da_script_item_visual{};
    IKinematicsAnimated* m_da_script_item_model{};
    Fmatrix m_da_script_item_offset{};
    // [DA_PORT] Посадка предмета сцены хранится ЧИСЛАМИ, а не одной готовой матрицей: матрица
    // собирается каждый кадр, чтобы поверх неё ложились консольные поправки подбора.
    Fvector m_da_script_item_pos{};
    Fvector m_da_script_item_rot{};   // градусы
    float m_da_script_item_scale{ 1.f };
    u16 m_da_script_item_attach{};
    // Крепить к кости руки (обычный случай) или ставить собственной матрицей.
    bool m_da_script_item_attached{ true };
    // Брать точку хвата оружия независимо от руки — ключ lh_lead_gun у первоисточника.
    bool m_da_script_item_lead_gun{};
    // Готовое преобразование, считается раз в кадр вместе с костями.
    Fmatrix m_da_script_item_transform{};
public:

private:
    void load_ancors();
    void update_inertion(Fmatrix& trans) const;
    void update_additional(Fmatrix& trans) const;
    bool inertion_allowed() const;

private:
    shared_str m_sect_name;

    Fmatrix m_attach_offset{};

    Fmatrix m_transform{ Fidentity };
    IKinematicsAnimated* m_model{};

    // [DA_PORT] ВТОРАЯ КОПИЯ модели рук — только левая рука. Разбор у player_hud::load.
    //
    // Посадка рук (hands_position/orientation) — одна матрица на костяк. Пока модель одна, левую и
    // правую руку нельзя посадить порознь, и одноручная сцена обязана быть кривой: цикл нарисован
    // от своей посадки, а живёт на посадке ствола. Две копии одной модели с разными спрятанными
    // руками дают каждой половине СВОЮ матрицу, свою посадку и свою анимацию.
    IKinematicsAnimated* m_model_2{};
    Fmatrix m_transform_2{ Fidentity };
    Fmatrix m_attach_offset_2{};

    // Какой рукой владеет скриптовая сцена: 0 — правая, 1 — левая, 2 — обе, u8(-1) — ничем.
    // Пока владеет, оружие на эту руку играть не может (замок в da_play_blend).
    u8 m_da_script_hand{ u8(-1) };
    // Плавный переезд посадки занятой руки к посадке сцены: 0 — посадка предмета, 1 — сцены.
    // Набор 2.5 в секунду, спад 5 — как в первоисточнике: иначе рука прыгает на входе и выходе.
    float m_da_script_seat_k{ 0.f };
    // Чья посадка переезжает: 0 — правая, 1 — левая. Помним ОТДЕЛЬНО от m_da_script_hand, потому
    // что после конца сцены владение снимается сразу, а посадка возвращается ещё полсекунды —
    // и возвращать её надо той же руке, а не первой попавшейся.
    u8 m_da_script_hand_seat{ 1 };

    // Кости у обеих копий одинаковые (модель одна), поэтому список привязок общий.
    xr_vector<u16> m_ancors;
    attachable_hud_item* m_attached_items[2]{};
    xr_unordered_map<shared_str, attachable_hud_item*> m_pool;
};

extern player_hud* g_player_hud;
