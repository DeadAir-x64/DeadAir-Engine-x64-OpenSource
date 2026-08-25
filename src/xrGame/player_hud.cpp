#include "StdAfx.h"
#include "player_hud.h"
#include "HudItem.h"
#include "xrUICore/ui_base.h"
#include "Actor.h"
#include "physic_item.h"
#include "static_cast_checked.hpp"
#include "ActorEffector.h"
#include "WeaponMagazinedWGrenade.h" // XXX: move somewhere
#include "GamePersistent.h"

player_hud* g_player_hud = nullptr;
extern ENGINE_API shared_str current_player_hud_sect;

// --#SM+# Begin--
constexpr float PITCH_OFFSET_R    = 0.0f;   // Насколько сильно ствол смещается вбок (влево) при вертикальных поворотах камеры
constexpr float PITCH_OFFSET_N    = 0.0f;   // Насколько сильно ствол поднимается\опускается при вертикальных поворотах камеры
constexpr float PITCH_OFFSET_D    = 0.02f;  // Насколько сильно ствол приближается\отдаляется при вертикальных поворотах камеры
constexpr float PITCH_LOW_LIMIT   = -PI;    // Минимальное значение pitch при использовании совместно с PITCH_OFFSET_N
constexpr float ORIGIN_OFFSET     = -0.05f; // Фактор влияния инерции на положение ствола (чем меньше, тем масштабней инерция)
constexpr float ORIGIN_OFFSET_AIM = -0.03f; // (Для прицеливания)
constexpr float TENDTO_SPEED      = 5.f;    // Скорость нормализации положения ствола
constexpr float TENDTO_SPEED_AIM  = 8.f;    // (Для прицеливания)
// --#SM+# End--

float CalcMotionSpeed(const shared_str& anim_name, const float anim_speed)
{
    // Apply custom animation speeds / configuration only for singleplayer games.
    // Fast reloading / showing / hiding animation does not seem fair.
    if (IsGameTypeSingle())
        return anim_speed;
    else
        return (anim_name == "anm_show" || anim_name == "anm_hide") ? 2.0f : 1.0f;
}

const player_hud_motion* player_hud_motion_container::find_motion(const shared_str& name) const
{
    const auto it = m_anims.find(name);
    return it != m_anims.end() ? &it->second : nullptr;
}

void player_hud_motion_container::load(IKinematicsAnimated* model, const shared_str& sect)
{
    const CInifile::Sect& _sect = pSettings->r_section(sect);

    for (const auto& [name, anm] : _sect.Data)
    {
        if (0 == strncmp(name.c_str(), "anm_",  sizeof("anm_")  - 1) ||
            0 == strncmp(name.c_str(), "anim_", sizeof("anim_") - 1))
        {
            player_hud_motion pm;

            if (_GetItemCount(anm.c_str()) == 1)
            {
                pm.m_base_name = anm;
                pm.m_additional_name = anm;
                pm.m_anim_speed = 1.f;
            }
            else
            {
                R_ASSERT2(_GetItemCount(anm.c_str()) <= 3, anm.c_str());
                string512 str_item;
                _GetItem(anm.c_str(), 0, str_item);
                pm.m_base_name = str_item;

                _GetItem(anm.c_str(), 1, str_item);
                pm.m_additional_name = xr_strlen(str_item) > 0 ? str_item : pm.m_base_name;

                _GetItem(anm.c_str(), 2, str_item);
                pm.m_anim_speed = xr_strlen(str_item) > 0 ? atof(str_item) : 1.f;
            }

            // and load all motions for it
            for (u32 i = 0; i <= 8; ++i)
            {
                string512 buff;
                if (i == 0)
                    xr_strcpy(buff, pm.m_base_name.c_str());
                else
                    xr_sprintf(buff, "%s%d", pm.m_base_name.c_str(), i);

                MotionID motion_ID = model->ID_Cycle_Safe(buff);
                if (motion_ID.valid())
                {
                    pm.m_animations.emplace_back(motion_descr{ std::move(motion_ID), buff });
#ifdef DEBUG
//					Msg(" alias=[%s] base=[%s] name=[%s]",pm.m_alias_name.c_str(), pm.m_base_name.c_str(), buff);
#endif // #ifdef DEBUG
                }
            }
            // [DA_PORT] Some Dead Air weapon HUD configs reference decorative motions (e.g.
            // wpn_pm_hud's anim_fakeshot -> "fakeshot_pistol") that don't exist in the actual
            // model's animation set - a data gap, not an engine bug. This used to be a hard
            // R_ASSERT2 FATAL that reopened on every level load. Skip the entry and warn instead:
            // the core animations (idle/shoot/reload/etc.) all load fine independently, so a
            // missing cosmetic motion shouldn't block the whole game.
            if (pm.m_animations.empty())
            {
                Msg("! [DA_PORT] player_hud_motion_container::load: motion not found [%s] in section [%s], skipping",
                    pm.m_base_name.c_str(), sect.c_str());
            }
            else
            {
                m_anims.emplace(name, std::move(pm));
            }
        }
    }
}

// [DA_PORT] Поправка игрока к положению оружия в руках, метры. Разбор -- у места применения
// в player_hud::update. Обычные настройки, а не отладочные: должны сохраняться между запусками.
// [DA_PORT] Доворот нарисованного ствола к перекрестию. Команда da_aim_align, 0..1.
//
// Зачем. Пуля игрока НЕ летит вдоль ствола: CActor::g_fireParams (Actor_Weapon.cpp) подменяет и
// точку вылета, и направление на КАМЕРУ, то есть выстрел всегда идёт ровно в перекрестие, а
// посчитанные setup_firedeps ствольные vLastFP/vLastFD для игрока выбрасываются. Модель же ставится
// своими hands_position/hands_orientation из конфига и с камерой никак не связана. Отсюда и
// расхождение мушки с прицелом: на попадания оно не влияет совсем, но глазами видно.
//
// Что делает. Доворачивает модель так, чтобы её ось Z смотрела вдоль камеры — туда, куда и летит
// пуля. Правка общая: ни один из 93 конфигов оружия трогать не нужно, и от обзора, разрешения и
// прочих настроек она не зависит.
//
// 1 — ствол ровно в перекрестие, 0 — как в оригинале.
//
// ⛔ ПО УМОЛЧАНИЮ ВЫКЛЮЧЕНО, и это не осторожность, а вывод. Во всей линейке X-Ray выравнивание
// мушки — РУЧНАЯ работа по конфигу каждого ствола (aim_hud_offset_pos/rot), а не расчёт в движке;
// у OGSR даже есть отдельный режим «hard crosshair», который заставляет пулю вылетать из
// fire_point вместо центра экрана — он существует именно потому, что по умолчанию стреляет центр.
// Доворот модели этой работы не заменяет: он борется с моделью вместо того, чтобы поправить данные,
// и на проверке увёл оружие ещё дальше вправо.
//
// Ручку оставляем — она даёт быстро увидеть, насколько ствол разошёлся с камерой, — но включать её
// должен тот, кто этого хочет.
float ps_da_aim_align = 0.f;

// [DA_PORT] Подгонка положения оружия СТРЕЛКАМИ, прямо во время прицеливания. Команда da_tune_keys.
//
// Зачем отдельно от окна. Прицельное смещение (aim_hud_offset_*) подбирается только когда игрок
// ЦЕЛИТСЯ, а окно подгонки при этом не открыть — прицел отпустится. Набирать команды в консоли,
// удерживая правую кнопку, тоже нельзя. Без этого печать готовой строки для .ltx (da_aim_dump)
// для прицеливания попросту неприменима.
//
// Стрелки влево/вправо — X, вверх/вниз — Y, PgUp/PgDn — Z. ESC намеренно НЕ перехватываем: выход в
// меню должен работать всегда.
int ps_da_tune_keys = 0;
// [DA_PORT] Прибор выстрела — заполняется в CShootingObject::FireBullet, показывается панелью.
float g_da_shot_last_yaw = 0.f, g_da_shot_last_pitch = 0.f;
float g_da_shot_sum_yaw = 0.f, g_da_shot_sum_pitch = 0.f;
u32 g_da_shot_count = 0;
float g_da_shot_disp_deg = 0.f, g_da_shot_origin_right = 0.f, g_da_shot_origin_up = 0.f;

float g_da_muzzle_yaw = 0.f;
float g_da_muzzle_pitch = 0.f;

// [DA_PORT] 1 — применять поправку и в прицеле тоже (прежнее поведение), 0 — гасить её при
// вскидке. По умолчанию гасим: прицельная посадка у каждого ствола выверена отдельно.
int ps_da_hud_pos_in_aim = 0;

float ps_da_hud_pos_x = 0.f;
float ps_da_hud_pos_y = 0.f;
float ps_da_hud_pos_z = 0.f;

bool da_tune_keys_handle(int scancode)
{
    if (!ps_da_tune_keys)
        return false;

    constexpr float step = 0.002f;

    // Целимся — правим ПРИЦЕЛЬНОЕ смещение, стоим от бедра — положение рук. Это разные величины в
    // разных системах координат, и путать их нельзя: одно применяется к рукам, другое к самому
    // оружию. Признак берём у прибора прицеливания (Weapon.cpp).
    extern u8 g_da_zoom_idx;
    extern Fvector g_da_aim_offset_delta;
    const bool aiming = g_da_zoom_idx > 0;

    float* v = nullptr;
    float dir = 0.f;

    switch (scancode)
    {
    case SDL_SCANCODE_LEFT:     v = aiming ? &g_da_aim_offset_delta.x : &ps_da_hud_pos_x; dir = -1.f; break;
    case SDL_SCANCODE_RIGHT:    v = aiming ? &g_da_aim_offset_delta.x : &ps_da_hud_pos_x; dir = +1.f; break;
    case SDL_SCANCODE_DOWN:     v = aiming ? &g_da_aim_offset_delta.y : &ps_da_hud_pos_y; dir = -1.f; break;
    case SDL_SCANCODE_UP:       v = aiming ? &g_da_aim_offset_delta.y : &ps_da_hud_pos_y; dir = +1.f; break;
    case SDL_SCANCODE_PAGEDOWN: v = aiming ? &g_da_aim_offset_delta.z : &ps_da_hud_pos_z; dir = -1.f; break;
    case SDL_SCANCODE_PAGEUP:   v = aiming ? &g_da_aim_offset_delta.z : &ps_da_hud_pos_z; dir = +1.f; break;
    default: return false;
    }

    *v += dir * step;
    clamp(*v, -0.3f, 0.3f);
    return true;
}


// [DA_PORT] Признак «идёт подгонка положения оружия из скрипта». Держится модулем hud_adjust
// (level_script.cpp) и нужен, чтобы игра не мешала настройке. Один источник правды: отдельного
// состояния в player_hud НЕ заводим -- у Anomaly оно своё, и это второе место, где правда о том
// же самом.
int ps_da_hud_adjust = 0;

Fvector& attachable_hud_item::hands_attach_pos() { return m_measures.m_hands_attach[0]; }
Fvector& attachable_hud_item::hands_attach_rot() { return m_measures.m_hands_attach[1]; }

Fvector& attachable_hud_item::hands_offset_pos()
{
    const u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[0][idx];
}

Fvector& attachable_hud_item::hands_offset_rot()
{
    u8 idx = m_parent_hud_item->GetCurrentHudOffsetIdx();
    return m_measures.m_hands_offset[1][idx];
}

void attachable_hud_item::set_bone_visible(const shared_str& bone_name, BOOL bVisibility, BOOL bSilent)
{
    const u16 bone_id = m_model->LL_BoneID(bone_name);
    if (bone_id == BI_NONE)
    {
        if (bSilent)
            return;
        R_ASSERT2(false, make_string("model [%s] has no bone [%s]", m_visual_name.c_str(), bone_name.c_str()).c_str());
    }
    const BOOL bVisibleNow = m_model->LL_GetBoneVisible(bone_id);
    if (bVisibleNow != bVisibility)
        m_model->LL_SetBoneVisible(bone_id, bVisibility, TRUE);
}

void attachable_hud_item::update(bool bForce)
{
    if (!bForce && m_upd_firedeps_frame == Device.dwFrame)
        return;

    const bool is_16x9 = UICore::is_widescreen();

    if (m_measures.m_prop_flags.test(hud_item_measures::e_16x9_mode_now) != is_16x9)
    {
        reload_measures();
    }

    if (GamePersistent().GetHudTuner().is_active())
        m_measures.update(m_attach_offset);

    m_parent->calc_transform(m_attach_place_idx, m_attach_offset, m_item_transform);

    // [DA_PORT] Свести нарисованный ствол с перекрестием — разбор у объявления ps_da_aim_align.
    // Только основная рука: во второй ни ствола, ни мушки.
    if (ps_da_aim_align > EPS && m_attach_place_idx == 0 && m_parent_hud_item &&
        smart_cast<const CActor*>(m_parent_hud_item->object().H_Parent()))
    {
        Fvector cur = m_item_transform.k;
        cur.normalize_safe();

        float dot = cur.dotproduct(Device.vCameraDirection);
        clamp(dot, -1.f, 1.f);
        const float angle = acosf(dot) * ps_da_aim_align;

        Fvector axis;
        axis.crossproduct(cur, Device.vCameraDirection);
        if (angle > EPS_S && axis.square_magnitude() > EPS_S)
        {
            axis.normalize();
            Fmatrix rot;
            rot.rotation(axis, angle);

            // ⚠️ Направление поворота ПРОВЕРЯЕМ, а не предполагаем. Знак зависит от того, как в
            // движке заданы направление вращения и порядок сомножителей в crossproduct; ошибка
            // здесь не падает и не логируется — она просто уводит ствол в другую сторону, и
            // ровно на это я уже наступил: было доложено «стало ровнее», а оружие ушло правее.
            //
            // Проверка дешёвая: поворачиваем пробный вектор и смотрим, стало ли ближе к камере.
            // Если нет — берём тот же угол с обратным знаком.
            Fvector probe = cur;
            rot.transform_dir(probe);
            if (probe.dotproduct(Device.vCameraDirection) < dot)
                rot.rotation(axis, -angle);

            // Крутим сам базис, а не перемножаем матрицы: порядок умножения тут легко перепутать
            // местами, а поворот трёх осей при неподвижном начале читается однозначно.
            rot.transform_dir(m_item_transform.i);
            rot.transform_dir(m_item_transform.j);
            rot.transform_dir(m_item_transform.k);
        }
    }

    m_upd_firedeps_frame = Device.dwFrame;

    // [DA_PORT] Живой угол дула от центра экрана — обратная связь для подгонки стрелками.
    // Считаем здесь: матрица предмета уже окончательная, прицельное смещение в неё вошло.
    if (m_attach_place_idx == 0 && m_measures.m_prop_flags.test(hud_item_measures::e_fire_point) && m_model)
    {
        extern float g_da_muzzle_yaw, g_da_muzzle_pitch;
        Fvector fp;
        Fmatrix fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
        fire_mat.transform_tiny(fp, m_measures.m_fire_point_offset);
        m_item_transform.transform_tiny(fp);

        Fvector d;
        d.sub(fp, Device.vCameraPosition);
        const float fz = d.dotproduct(Device.vCameraDirection);
        if (fz > EPS)
        {
            g_da_muzzle_yaw = rad2deg(atan2f(d.dotproduct(Device.vCameraRight), fz));
            g_da_muzzle_pitch = rad2deg(atan2f(d.dotproduct(Device.vCameraTop), fz));
        }
    }

    if (IKinematicsAnimated* ka = m_model->dcast_PKinematicsAnimated())
    {
        ka->UpdateTracks();
        ka->dcast_PKinematics()->CalculateBones_Invalidate();
        ka->dcast_PKinematics()->CalculateBones(TRUE);
    }
}

void attachable_hud_item::update_hud_additional(Fmatrix& trans) const
{
    if (m_parent_hud_item)
    {
        m_parent_hud_item->UpdateHudAdditonal(trans);
    }
}

void attachable_hud_item::setup_firedeps(firedeps& fd)
{
    update(false);
    // fire point&direction
    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone);
        fire_mat.transform_tiny(fd.vLastFP, m_measures.m_fire_point_offset);
        m_item_transform.transform_tiny(fd.vLastFP);

        fd.vLastFD.set(0.f, 0.f, 1.f);
        m_item_transform.transform_dir(fd.vLastFD);
        VERIFY(_valid(fd.vLastFD));
        VERIFY(_valid(fd.vLastFD));

        fd.m_FireParticlesXForm.identity();
        fd.m_FireParticlesXForm.k.set(fd.vLastFD);
        Fvector::generate_orthonormal_basis_normalized(
            fd.m_FireParticlesXForm.k, fd.m_FireParticlesXForm.j, fd.m_FireParticlesXForm.i);
        VERIFY(_valid(fd.m_FireParticlesXForm));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_fire_point2))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_fire_bone2);
        fire_mat.transform_tiny(fd.vLastFP2, m_measures.m_fire_point2_offset);
        m_item_transform.transform_tiny(fd.vLastFP2);
        VERIFY(_valid(fd.vLastFP2));
        VERIFY(_valid(fd.vLastFP2));
    }

    if (m_measures.m_prop_flags.test(hud_item_measures::e_shell_point))
    {
        Fmatrix& fire_mat = m_model->LL_GetTransform(m_measures.m_shell_bone);
        fire_mat.transform_tiny(fd.vLastSP, m_measures.m_shell_point_offset);
        m_item_transform.transform_tiny(fd.vLastSP);
        VERIFY(_valid(fd.vLastSP));
        VERIFY(_valid(fd.vLastSP));
    }
}

bool attachable_hud_item::need_renderable() const { return m_parent_hud_item->need_renderable(); }

void attachable_hud_item::render(u32 context_id, IRenderable* root)
{
    GEnv.Render->add_Visual(context_id, root, m_model->dcast_RenderVisual(), m_item_transform);
    m_parent_hud_item->render_hud_mode();
}

bool attachable_hud_item::render_item_ui_query() const { return m_parent_hud_item->render_item_3d_ui_query(); }
void attachable_hud_item::render_item_ui() const { m_parent_hud_item->render_item_3d_ui(); }

Fmatrix hud_item_measures::load(const shared_str& sect_name, IKinematics* K)
{
    const bool is_16x9 = UICore::is_widescreen();
    string64 _prefix;
    xr_sprintf(_prefix, "%s", is_16x9 ? "_16x9" : "");
    string128 val_name;

    strconcat(val_name, "hands_position", _prefix);
    m_hands_attach[0] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(val_name, "hands_orientation", _prefix);
    m_hands_attach[1] = pSettings->r_fvector3(sect_name, val_name);

    m_item_attach[0] = pSettings->r_fvector3(sect_name, "item_position");
    m_item_attach[1] = pSettings->r_fvector3(sect_name, "item_orientation");

    Fmatrix attach_offset;
    update(attach_offset);

    shared_str bone_name;
    m_prop_flags.set(e_fire_point, pSettings->line_exist(sect_name, "fire_bone"));
    if (m_prop_flags.test(e_fire_point))
    {
        bone_name = pSettings->r_string(sect_name, "fire_bone");
        m_fire_bone = K->LL_BoneID(bone_name);
        m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
    }
    else
        m_fire_point_offset = {};

    m_prop_flags.set(e_fire_point2, pSettings->line_exist(sect_name, "fire_bone2"));
    if (m_prop_flags.test(e_fire_point2))
    {
        bone_name = pSettings->r_string(sect_name, "fire_bone2");
        m_fire_bone2 = K->LL_BoneID(bone_name);
        m_fire_point2_offset = pSettings->r_fvector3(sect_name, "fire_point2");
    }
    else
        m_fire_point2_offset = {};

    m_prop_flags.set(e_shell_point, pSettings->line_exist(sect_name, "shell_bone"));
    if (m_prop_flags.test(e_shell_point))
    {
        bone_name = pSettings->r_string(sect_name, "shell_bone");
        m_shell_bone = K->LL_BoneID(bone_name);
        m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
    }
    else
        m_shell_point_offset = {};

    m_hands_offset[0][0] = {};
    m_hands_offset[1][0] = {};

    strconcat(val_name, "aim_hud_offset_pos", _prefix);
    m_hands_offset[0][1] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(val_name, "aim_hud_offset_rot", _prefix);
    m_hands_offset[1][1] = pSettings->r_fvector3(sect_name, val_name);

    strconcat(val_name, "gl_hud_offset_pos", _prefix);
    m_hands_offset[0][2] = pSettings->r_fvector3(sect_name, val_name);
    strconcat(val_name, "gl_hud_offset_rot", _prefix);
    m_hands_offset[1][2] = pSettings->r_fvector3(sect_name, val_name);

    R_ASSERT2(pSettings->line_exist(sect_name, "fire_point") == pSettings->line_exist(sect_name, "fire_bone"),
        sect_name.c_str());
    R_ASSERT2(pSettings->line_exist(sect_name, "fire_point2") == pSettings->line_exist(sect_name, "fire_bone2"),
        sect_name.c_str());
    R_ASSERT2(pSettings->line_exist(sect_name, "shell_point") == pSettings->line_exist(sect_name, "shell_bone"),
        sect_name.c_str());

    load_inertion_params(sect_name);
    m_prop_flags.set(e_16x9_mode_now, is_16x9);

    return attach_offset;
}

Fmatrix hud_item_measures::load_monolithic(const shared_str& sect_name, IKinematics* K, CHudItem* owner)
{
    m_item_attach[0] = pSettings->r_fvector3(sect_name, "position");
    m_item_attach[1] = pSettings->r_fvector3(sect_name, "orientation");

    Fmatrix attach_offset;
    update(attach_offset);

    // fire bone
    if (auto* wpn = smart_cast<CWeapon*>(owner))
    {
        cpcstr fire_bone = pSettings->r_string(sect_name, "fire_bone");
        m_fire_bone = K->LL_BoneID(fire_bone);
        if (m_fire_bone >= K->LL_BoneCount())
            xrDebug::Fatal(DEBUG_INFO, "There is no '%s' bone for weapon '%s'.", fire_bone, sect_name.c_str());
        m_fire_bone2 = m_fire_bone;
        m_shell_bone = m_fire_bone;

        m_fire_point_offset = pSettings->r_fvector3(sect_name, "fire_point");
        m_fire_point2_offset = pSettings->read_if_exists<Fvector3>(sect_name, "fire_point2", m_fire_point_offset);

        if (pSettings->line_exist(owner->object().cNameSect(), "shell_particles"))
            m_shell_point_offset = pSettings->r_fvector3(sect_name, "shell_point");
        else
            m_shell_point_offset.set(0, 0, 0);

        m_hands_offset[0][0] = {};
        m_hands_offset[1][0] = {};

        if (wpn->IsZoomEnabled())
        {
            const auto load_zoom_offsets = [&](pcstr prefix, Fvector3& position, Fvector3& rotation)
            {
                string256 full_name;
                position = pSettings->r_fvector3(sect_name, strconcat(full_name, prefix, "zoom_offset"));
                rotation.x = pSettings->r_float(sect_name, strconcat(full_name, prefix, "zoom_rotate_x"));
                rotation.y = pSettings->r_float(sect_name, strconcat(full_name, prefix, "zoom_rotate_y"));
                rotation.z = pSettings->read_if_exists<float>(sect_name, strconcat(full_name, prefix, "zoom_rotate_z"), 0.f);
            };
            load_zoom_offsets("", m_hands_offset[0][1], m_hands_offset[1][1]);
            if (smart_cast<CWeaponMagazinedWGrenade*>(wpn))
            {
                load_zoom_offsets("grenade_", m_hands_offset[0][2], m_hands_offset[1][2]);
                if (wpn->GrenadeLauncherAttachable())
                    load_zoom_offsets("grenade_normal_", m_hands_offset[0][1], m_hands_offset[1][1]);
            }
        }
    }
    else
    {
        m_fire_bone  = BI_NONE;
        m_fire_bone2 = BI_NONE;
        m_shell_bone = BI_NONE;

        m_fire_point_offset  = {};
        m_fire_point2_offset = {};
        m_shell_point_offset = {};
    }

    load_inertion_params(sect_name);
    m_prop_flags.set(e_16x9_mode_now, UICore::is_widescreen());

    return attach_offset;
}

void hud_item_measures::load_inertion_params(const shared_str& sect_name)
{
    //Загрузка параметров инерции --#SM+# Begin--
    m_inertion_params.m_pitch_offset_r = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_right", PITCH_OFFSET_R);
    m_inertion_params.m_pitch_offset_n = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_up", PITCH_OFFSET_N);
    m_inertion_params.m_pitch_offset_d = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_forward", PITCH_OFFSET_D);
    m_inertion_params.m_pitch_low_limit = READ_IF_EXISTS(pSettings, r_float, sect_name, "pitch_offset_up_low_limit", PITCH_LOW_LIMIT);

    m_inertion_params.m_origin_offset = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_origin_offset", ORIGIN_OFFSET);
    m_inertion_params.m_origin_offset_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_origin_aim_offset", ORIGIN_OFFSET_AIM);
    m_inertion_params.m_tendto_speed = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_speed", TENDTO_SPEED);
    m_inertion_params.m_tendto_speed_aim = READ_IF_EXISTS(pSettings, r_float, sect_name, "inertion_tendto_aim_speed", TENDTO_SPEED_AIM);
    //--#SM+# End--
}

void hud_item_measures::update(Fmatrix& attach_offset)
{
    Fvector ypr = m_item_attach[1];
    ypr.mul(PI / 180.f);
    attach_offset.setHPB(ypr.x, ypr.y, ypr.z);
    attach_offset.translate_over(m_item_attach[0]);
}

// [DA_PORT] ВРЕМЕННЫЙ след анимаций рук: da_anim_trace 1.
//
// Повод: у части стволов одноручная сцена подбора не видна — левая рука не двигается, оружие просто
// уезжает вниз. Первый прибор (он писал только во время сцены) показал, что цикл сцены ложится
// правильно и никто его во время сцены не перебивает. Значит различие где-то ещё, и сравнивать надо
// ДВА прогона целиком — на сломанном стволе и на рабочем, — а для этого в записи должно стоять имя
// ствола. Отдельная ручка, потому что пишется каждая смена цикла рук, а их за секунду много.
int ps_da_anim_trace = 0;

// [DA_PORT] Скорость переезда посадки занятой руки, доля в секунду: вход и выход.
//
// У первоисточника это 2.5 и 5. Вход медленный намеренно — рука не должна «клевать» на старте. А на
// ВЫХОДЕ ждать нечего: цикл доигран, руке остаётся только вернуться на ствол, и полсекунды здесь
// читаются как задержка. Поэтому выход быстрее.
// [DA_PORT] Подбор посадки предмета сцены прямо в игре: da_scene_item_pos / _rot / _scale.
//
// Зачем. Модель сцены садится на кость хвата, и у каждой она стоит по-своему: наши рюкзаки рисовались
// висеть на спине, поэтому в руках упираются в самую камеру. Числа для конфига подбираются глазом, и
// делать это пересборкой - десятки заходов по двадцать секунд каждый. Поправки складываются с тем,
// что записано в секции, и печатаются командой da_scene_item_dump - готовой строкой для конфига.
//
// ⚠️ Это ОСНАСТКА, а не настройка игры: подобранное надо перенести в секцию, иначе оно живёт до
// перезапуска и только у тебя.
Fvector g_da_scene_item_pos_adj{};
Fvector g_da_scene_item_rot_adj{};
float g_da_scene_item_scale_adj = 1.f;

float ps_da_scene_seat_in = 2.5f;
float ps_da_scene_seat_out = 9.f;

attachable_hud_item::~attachable_hud_item()
{
    IRenderVisual* v = m_model->dcast_RenderVisual();
    GEnv.Render->model_Delete(v);
}

attachable_hud_item::attachable_hud_item(player_hud* parent, const shared_str& sect_name, IKinematicsAnimated* hands_model)
    : m_parent(parent), m_sect_name(sect_name)
{
    // Visual
    if (pSettings->line_exist(m_sect_name, "item_visual"))
    {
        m_monolithic = false;
        m_visual_name = pSettings->r_string(m_sect_name, "item_visual");
    }
    else if (pSettings->line_exist(m_sect_name, "visual"))
    {
        m_monolithic = true;
        m_visual_name = pSettings->r_string(m_sect_name, "visual");
    }
    R_ASSERT3(!m_visual_name.empty(), "Missing 'item_visual' from weapon hud section.", m_sect_name.c_str());

    m_model = smart_cast<IKinematics*>(GEnv.Render->model_Create(m_visual_name.c_str()));

    m_attach_place_idx = pSettings->read_if_exists<u16>(m_sect_name, "attach_place_idx", 0);

    IKinematicsAnimated* animatedHudItem;
    if (!m_monolithic && hands_model)
        animatedHudItem = hands_model;
    else
        animatedHudItem = smart_cast<IKinematicsAnimated*>(m_model);

    m_hand_motions.load(animatedHudItem, m_sect_name);
    reload_measures();
}

void attachable_hud_item::reload_measures()
{
    if (m_monolithic)
        m_attach_offset = m_measures.load_monolithic(m_sect_name, m_model, m_parent_hud_item);
    else
        m_attach_offset = m_measures.load(m_sect_name, m_model);
}

u32 attachable_hud_item::anim_play(const shared_str& anm_name_b, BOOL bMixIn, const CMotionDef*& md, u8& rnd_idx)
{
    string256 anim_name_r;
    const bool is_16x9 = UICore::is_widescreen();
    xr_sprintf(anim_name_r, "%s%s", anm_name_b.c_str(), m_attach_place_idx == 1 && is_16x9 ? "_16x9" : "");

    const player_hud_motion* anm = m_hand_motions.find_motion(anim_name_r);
    // [DA_PORT] Some Dead Air weapon configs reference motion aliases with no matching model
    // animation (data gap, see player_hud_motion_container::load above) - skip playing rather
    // than FATAL-crashing the whole game over a missing cosmetic HUD animation.
    if (!anm || anm->m_animations.empty())
    {
        Msg("! [DA_PORT] attachable_hud_item::anim_play: no motion for alias [%s] in model [%s], skipping",
            anim_name_r, m_sect_name.c_str());
        return 0;
    }

    const float speed = CalcMotionSpeed(anm->m_base_name, anm->m_anim_speed);

    rnd_idx = (u8)Random.randI(anm->m_animations.size());
    const motion_descr& M = anm->m_animations[rnd_idx];

    IKinematicsAnimated* ka = smart_cast<IKinematicsAnimated*>(m_model);

    // [DA_PORT] ВРЕМЕННЫЙ прибор: кто играет на руках, пока идёт скриптовая сцена.
    //
    // Повод: у части стволов одноручная сцена подбора не видна — рука не двигается, оружие просто
    // уезжает вниз. Ошибок в логе при этом НЕТ, цикл сцены находится и запускается. Значит его
    // затирают, и затирать может только сам ствол: его цикл покоя ложится в том числе на нулевую
    // часть модели, а она общая для обеих рук. Кто и когда переигрывает — видно только отсюда.
    if (ps_da_anim_trace || (m_parent && m_parent->da_script_anim_active()))
        Msg("~ [DA_ANIM] ствол [%s] играет [%s] (место %u, подмешивание %s, сцена идёт %s)",
            m_sect_name.c_str(), anim_name_r, u32(m_attach_place_idx), bMixIn ? "да" : "нет",
            (m_parent && m_parent->da_script_anim_active()) ? "ДА" : "нет");

    const u32 ret = m_parent->anim_play(m_attach_place_idx, M.mid, bMixIn, md, speed, m_monolithic ? ka : nullptr);

    if (ka)
    {
        shared_str item_anm_name;
        if (anm->m_base_name != anm->m_additional_name)
            item_anm_name = anm->m_additional_name;
        else
            item_anm_name = M.name;

        MotionID M2 = ka->ID_Cycle_Safe(item_anm_name);
        if (!M2.valid())
            M2 = ka->ID_Cycle_Safe("idle");
        else if (bDebug)
            Msg("playing item animation [%s]", item_anm_name.c_str());

        R_ASSERT3(M2.valid(), "model has no motion [idle] ", m_visual_name.c_str());

        if (!m_monolithic)
        {
            const u16 root_id = m_model->LL_GetBoneRoot();
            CBoneInstance& root_binst = m_model->LL_GetBoneInstance(root_id);
            root_binst.set_callback_overwrite(TRUE);
            root_binst.mTransform.identity();
        }

        const u16 pc = ka->partitions().count();
        for (u16 pid = 0; pid < pc; ++pid)
        {
            CBlend* B = ka->PlayCycle(pid, M2, bMixIn);
            R_ASSERT(B);
            B->speed *= speed;
        }

        m_model->CalculateBones_Invalidate();
    }

    R_ASSERT2(m_parent_hud_item, "parent hud item is NULL");
    CPhysicItem& parent_object = m_parent_hud_item->object();
    // R_ASSERT2		(parent_object, "object has no parent actor");
    // IGameObject*		parent_object = static_cast_checked<IGameObject*>(&m_parent_hud_item->object());

    if (IsGameTypeSingle() && parent_object.H_Parent() == Level().CurrentControlEntity())
    {
        CActor* current_actor = static_cast_checked<CActor*>(Level().CurrentControlEntity());
        VERIFY(current_actor);

        string_path ce_path;
        string_path anm_name;
        strconcat(anm_name, "camera_effects" DELIMITER "weapon" DELIMITER, M.name.c_str(), ".anm");
        if (FS.exist(ce_path, "$game_anims$", anm_name))
        {
            CEffectorCam* ec = current_actor->Cameras().GetCamEffector(eCEWeaponAction);
            if (ec)
                current_actor->Cameras().RemoveCamEffector(eCEWeaponAction);

            CAnimatorCamEffector* e = xr_new<CAnimatorCamEffector>();
            e->SetType(eCEWeaponAction);
            e->SetHudAffect(false);
            e->SetCyclic(false);
            e->Start(anm_name);
            current_actor->Cameras().AddCamEffector(e);
        }
    }
    return ret;
}

player_hud::~player_hud()
{
    if (m_model)
    {
        IRenderVisual* v = m_model->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    if (m_model_2)
    {
        IRenderVisual* v = m_model_2->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    for (auto& [name, item] : m_pool)
    {
        xr_delete(item);
    }
    m_pool.clear();
}

void player_hud::load(const shared_str& player_hud_sect)
{
    if (player_hud_sect == m_sect_name)
        return;

    m_sect_name = player_hud_sect;

    const bool b_reload = m_model != nullptr;
    if (m_model)
    {
        IRenderVisual* v = m_model->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
    }

    if (m_model_2)
    {
        IRenderVisual* v = m_model_2->dcast_RenderVisual();
        GEnv.Render->model_Delete(v);
        m_model_2 = nullptr;
    }

    if (!pSettings->section_exist(m_sect_name))
    {
        if (b_reload)
        {
            if (m_attached_items[1])
                m_attached_items[1]->m_parent_hud_item->on_a_hud_attach();

            if (m_attached_items[0])
                m_attached_items[0]->m_parent_hud_item->on_a_hud_attach();
        }

        return;
    }

    const shared_str& model_name = pSettings->r_string(m_sect_name, "visual");
    m_model = smart_cast<IKinematicsAnimated*>(GEnv.Render->model_Create(model_name.c_str()));

    // [DA_PORT] Вторая копия ТОЙ ЖЕ модели: у первой прячем левую руку, у второй правую.
    //
    // Идентификаторы костей и наборов движений у копий совпадают — модель одна, — поэтому цикл,
    // найденный по первой, законно играется на второй, а список привязок общий.
    //
    // ⚠️ Имя кости плеча у разных сборок разное: у первоисточника l_clavicle, у моделей от CoC
    // встречается bip01_l_clavicle. Перебираем оба и говорим вслух, если не нашли ни одного —
    // без сокрытия руки задвоятся, и это надо видеть в логе, а не разгадывать по экрану.
    m_model_2 = smart_cast<IKinematicsAnimated*>(GEnv.Render->model_Create(model_name.c_str()));
    if (m_model && m_model_2)
    {
        const auto da_hide_arm = [&](IKinematicsAnimated* model, pcstr n1, pcstr n2, pcstr side)
        {
            IKinematics* k = model->dcast_PKinematics();
            u16 id = k->LL_BoneID(n1);
            if (id == BI_NONE)
                id = k->LL_BoneID(n2);
            if (id == BI_NONE)
            {
                Msg("! [DA_PORT] руки: не нашёл кость %s плеча (%s / %s) в [%s] — половинки рук "
                    "задвоятся", side, n1, n2, model_name.c_str());
                return;
            }
            k->LL_SetBoneVisible(id, FALSE, TRUE);
        };

        da_hide_arm(m_model, "l_clavicle", "bip01_l_clavicle", "левого");
        da_hide_arm(m_model_2, "r_clavicle", "bip01_r_clavicle", "правого");
    }

    load_ancors();
    // Msg("hands visual changed to [%s] [%s] [%s]", model_name.c_str(), b_reload ? "R" : "", m_attached_items[0] ? "Y" : "");

    if (!b_reload)
    {
        m_model->PlayCycle("hand_idle_doun");
        if (m_model_2)
            m_model_2->PlayCycle("hand_idle_doun");
    }
    else
    {
        if (m_attached_items[1])
            m_attached_items[1]->m_parent_hud_item->on_a_hud_attach();

        if (m_attached_items[0])
            m_attached_items[0]->m_parent_hud_item->on_a_hud_attach();
    }
    m_model->dcast_PKinematics()->CalculateBones_Invalidate();
    m_model->dcast_PKinematics()->CalculateBones(TRUE);
    if (m_model_2)
    {
        m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
        m_model_2->dcast_PKinematics()->CalculateBones(TRUE);
    }
}

// [DA_PORT] Посадка половины рук: 0 — правая, 1 — левая.
//
// Если своего предмета у половины нет, берём посадку чужого: одиночный ствол держат ДВЕ руки, и
// левая обязана стоять там же, где правая. Пустые руки садятся по секции сцены — иначе они встают
// в начало координат интерфейса, то есть заметно не там, где задумано.
Fvector player_hud::da_attach_pos(u8 part) const
{
    if (m_attached_items[part])
        return m_attached_items[part]->hands_attach_pos();
    if (m_attached_items[part ? 0 : 1])
        return m_attached_items[part ? 0 : 1]->hands_attach_pos();
    return m_da_script_hands_pos;
}

Fvector player_hud::da_attach_rot(u8 part) const
{
    if (m_attached_items[part])
        return m_attached_items[part]->hands_attach_rot();
    if (m_attached_items[part ? 0 : 1])
        return m_attached_items[part ? 0 : 1]->hands_attach_rot();
    return m_da_script_hands_rot;
}

void player_hud::load_ancors()
{
    const CInifile::Sect& _sect = pSettings->r_section(m_sect_name);
    for (const auto& [name, bone] : _sect.Data)
    {
        if (0 == strncmp(name.c_str(), "ancor_", sizeof("ancor_") - 1))
        {
            m_ancors.emplace_back(m_model->dcast_PKinematics()->LL_BoneID(bone));
        }
    }
}

bool player_hud::render_item_ui_query() const
{
    bool res = false;
    if (m_attached_items[0])
        res |= m_attached_items[0]->render_item_ui_query();

    if (m_attached_items[1])
        res |= m_attached_items[1]->render_item_ui_query();

    return res;
}

void player_hud::render_item_ui() const
{
    if (m_attached_items[0])
        m_attached_items[0]->render_item_ui();

    if (m_attached_items[1])
        m_attached_items[1]->render_item_ui();
}

void player_hud::render_hud(u32 context_id, IRenderable* root)
{
    attachable_hud_item* item0 = m_attached_items[0];
    attachable_hud_item* item1 = m_attached_items[1];

    // [DA_PORT] Скриптовая анимация рисует руки САМА ПО СЕБЕ, без предмета в руках.
    //
    // Обычно руки — лишь подложка под оружие, и без предмета рисовать нечего: оба выхода ниже
    // именно об этом. Но скриптовая сцена (паркур) сначала убирает оружие в рюкзак, а потом просит
    // анимацию — и по прежнему условию руки не рисовались вовсе. Симптом ровно такой и был:
    // анимация игралась, ошибок в логе не было, а рук на экране нет.
    const bool script_anim = da_script_anim_active();

    if (!item0 && !item1 && !script_anim)
        return;

    const bool b_r0 = item0 && item0->need_renderable();
    const bool b_r1 = item1 && item1->need_renderable();

    if (!b_r0 && !b_r1 && !script_anim)
        return;

    if (m_model)
        GEnv.Render->add_Visual(context_id, root, m_model->dcast_RenderVisual(), m_transform);

    // [DA_PORT] Вторая половина рук — своя матрица, своя посадка. Разбор у объявления m_model_2.
    if (m_model_2)
        GEnv.Render->add_Visual(context_id, root, m_model_2->dcast_RenderVisual(), m_transform_2);

    // ⛔ Во время скриптовой сцены чужие предметы в руках НЕ рисуем.
    //
    // Сцена владеет руками целиком: она играет на них свой цикл и вешает свой предмет. Оружие к
    // этому моменту уже убрано логически (слот 0), но его attachable_hud_item остаётся
    // прикреплённым — и без этой проверки нож продолжал рисоваться поверх сцены, ломая и вид, и позу.
    //
    // 🪤 Проверять need_renderable бесполезно: она про ПРИЦЕЛ (CWeapon::need_renderable возвращает
    // ложь только при зуме со снайперской текстурой), а к убранному в кобуру отношения не имеет.
    // Первая попытка чинить именно ею ничего не изменила.
    // [DA_PORT] У ОДНОРУКОЙ сцены оружие остаётся в руках, значит и рисовать его надо.
    if (!script_anim || m_da_script_one_hand)
    {
        if (item0)
            item0->render(context_id, root);

        if (item1)
            item1->render(context_id, root);
    }

    // [DA_PORT] Предмет скриптовой сцены. Преобразование посчитано в update — см. разбор там.
    if (m_da_script_item_visual && script_anim)
        GEnv.Render->add_Visual(context_id, root, m_da_script_item_visual,
            m_da_script_item_transform);
}

#include "xrCore/Animation/Motion.hpp"

u32 player_hud::motion_length(const shared_str& anim_name, const shared_str& hud_name, const CMotionDef*& md)
{
    const float speed = CalcMotionSpeed(anim_name, 1.0f);
    attachable_hud_item* pi = create_hud_item(hud_name);
    const player_hud_motion* pm = pi->m_hand_motions.find_motion(anim_name);

    if (!pm)
        return 100; // ms TEMPORARY
    R_ASSERT2(pm,
        make_string("hudItem model [%s] has no motion with alias [%s]", hud_name.c_str(), anim_name.c_str()).c_str());
    IKinematicsAnimated* model = pi->m_monolithic ? smart_cast<IKinematicsAnimated*>(pi->m_model) : nullptr;
    return motion_length(pm->m_animations[0].mid, md, speed, model);
}

u32 player_hud::motion_length(const MotionID& M, const CMotionDef*& md, float speed, IKinematicsAnimated* itemModel) const
{
    IKinematicsAnimated* model = itemModel ? itemModel : m_model;
    md = model->LL_GetMotionDef(M);
    VERIFY(md);
    if (md->flags & esmStopAtEnd)
    {
        CMotion* motion = model->LL_GetRootMotion(M);
        return iFloor(0.5f + 1000.f * motion->GetLength() / (md->Dequantize(md->speed) * speed));
    }
    return 0;
}

void player_hud::update(const Fmatrix& cam_trans)
{
    Fmatrix trans = cam_trans;
    if (psHUD_Flags.test(HUD_LEFT_HANDED))
    {
        // faster than multiplication by flip matrix
        trans.m[0][0] = -trans.m[0][0];
        trans.m[0][1] = -trans.m[0][1];
        trans.m[0][2] = -trans.m[0][2];
        trans.m[0][3] = -trans.m[0][3];
    }

    update_inertion(trans);

    attachable_hud_item* item0 = m_attached_items[0];
    attachable_hud_item* item1 = m_attached_items[1];

    // [DA_PORT] Надстройки предметов идут КАЖДАЯ В СВОЮ половину: оружие правит правую матрицу,
    // предмет левой руки — левую. Прежде обе ложились на одну, и качка от оружия ехала по всему
    // костяку. trans_b — состояние ДО надстроек: к нему возвращается рука, отданная сцене.
    const Fmatrix trans_b = trans;
    Fmatrix trans_2 = trans;

    if (item0)
        item0->update_hud_additional(trans);
    if (item1)
        item1->update_hud_additional(trans_2);

    if (item0 && !item1)
        trans_2 = trans;
    else if (item1 && !item0)
        trans = trans_2;

    const bool monolithic = item0 && item0->m_monolithic || item1 && item1->m_monolithic;
    if (!m_model || monolithic)
    {
        m_transform = trans;
        m_transform_2 = trans_2;
    }
    else
    {
        // [DA_PORT] Посадка рук — ПО ПОЛОВИНАМ. Разбор устройства у объявления m_model_2.
        //
        // Две копии модели дают то, чего не было с одной: правой руке можно оставить посадку
        // ствола, а левой отдать посадку сцены. Раньше выбор был один на обе, и одноручная сцена
        // обязана была врать: цикл нарисован от своей посадки, а жил на оружейной.
        Fvector m1pos = da_attach_pos(0), m2pos = da_attach_pos(1);
        Fvector m1rot = da_attach_rot(0), m2rot = da_attach_rot(1);

        const bool da_scene = da_script_anim_active();

        if (da_scene && (m_da_script_hand == 2 || (!item0 && !item1)))
        {
            // Сцена владеет обеими руками (или в руках вовсе ничего) — посадка целиком её.
            m1pos = m2pos = m_da_script_hands_pos;
            m1rot = m2rot = m_da_script_hands_rot;
            trans = trans_b;
            trans_2 = trans_b;
        }
        else if (m_da_script_seat_k > 0.f)
        {
            // Одноручная сцена: посадка ЗАНЯТОЙ руки плавно переезжает к посадке сцены, вторая
            // остаётся на посадке своего предмета. Плавно — иначе рука прыгает на входе и выходе;
            // сам множитель ведётся в конце update.
            const bool right = (m_da_script_hand_seat == 0);
            Fvector& hp = right ? m1pos : m2pos;
            Fvector& hr = right ? m1rot : m2rot;
            hp.lerp(hp, m_da_script_hands_pos, m_da_script_seat_k);
            hr.lerp(hr, m_da_script_hands_rot, m_da_script_seat_k);

            Fmatrix tb = trans_b;
            if (right)
            {
                tb.inertion(trans, m_da_script_seat_k);
                trans = tb;
            }
            else
            {
                tb.inertion(trans_2, m_da_script_seat_k);
                trans_2 = tb;
            }
        }

        Fvector tmp = m1pos;

        // [DA_PORT] Поправка игрока к положению оружия в руках, МЕТРЫ.
        //
        // Зачем. Положение задаётся `hands_position` в конфиге КАЖДОГО ствола (в моде их 93, плюс
        // отдельный вариант `_16x9`), подобрано вручную и вкус у него общий на всех. Кому-то ствол
        // стоит слишком близко к лицу, кому-то низко; менять 93 файла ради этого нельзя, а трогать
        // поле зрения — значит менять и сцену.
        //
        // Поэтому здесь общая добавка поверх подобранного: относительные различия между стволами
        // сохраняются, сдвигается весь набор разом.
        //
        // Оси HUD: X вправо, Y вверх, Z ВПЕРЁД от камеры. Отрицательный Z придвигает оружие ближе к
        // лицу, положительный отодвигает — это и есть «расположение камеры относительно оружия».
        //
        // ⚠️ Направление ВЫСТРЕЛА этим не меняется: оно берётся из оси Z матрицы предмета
        // (`setup_firedeps`), а сдвиг на неё не влияет. Поправка чисто про удобство вида.
        {
            extern float ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z;
            extern int ps_da_hud_pos_in_aim;

            // [DA_PORT] Поправка ГАСНЕТ при прицеливании — иначе она сбивает выверенную посадку.
            //
            // Зачем. Эти ручки двигают РУКИ, то есть действуют в обоих состояниях сразу. От бедра
            // это ровно то, что нужно: игрок ставит оружие как ему удобно. А в прицеле положение
            // задано отдельно (aim_hud_offset_pos у каждого ствола) и выверено так, чтобы мушка
            // села на центр — общий сдвиг рук её оттуда уводит.
            //
            // Формула: множим на (1 - доля прицеливания). От бедра доля 0, поправка целиком; в
            // прицеле доля 1, поправка ноль, и оружие возвращается туда, где его выверили. Между
            // ними плавно — той же долей, которой движок ведёт саму вскидку, так что рассинхрона
            // с анимацией быть не может.
            //
            // ⭐ Пересчитывать системы координат не нужно: мы не досчитываем компенсацию в чужом
            // базисе (там я уже ошибся однажды), а просто не применяем сдвиг там, где он мешает.
            //
            // idx > 0 — признак того, что вскидка идёт или держится; при опущенном оружии он ноль,
            // и доля обнуляется вместе с ним, даже если в глобальной переменной осталось старое.
            float da_aim = 0.f;
            if (item0 && item0->m_parent_hud_item && item0->m_parent_hud_item->GetCurrentHudOffsetIdx() > 0)
            {
                extern float g_da_zoom_factor;
                da_aim = g_da_zoom_factor;
                clamp(da_aim, 0.f, 1.f);
            }

            const float da_k = ps_da_hud_pos_in_aim ? 1.f : (1.f - da_aim);
            tmp.x += ps_da_hud_pos_x * da_k;
            tmp.y += ps_da_hud_pos_y * da_k;
            tmp.z += ps_da_hud_pos_z * da_k;
        }

        // Поправка игрока сдвигает ВЕСЬ узел рук, поэтому ложится на обе половины одинаково.
        const Fvector da_shift = { tmp.x - m1pos.x, tmp.y - m1pos.y, tmp.z - m1pos.z };
        m1pos = tmp;
        m2pos.add(da_shift);

        m1rot.mul(PI / 180.f);
        m_attach_offset.setHPB(m1rot.x, m1rot.y, m1rot.z);
        m_attach_offset.translate_over(m1pos);

        m2rot.mul(PI / 180.f);
        m_attach_offset_2.setHPB(m2rot.x, m2rot.y, m2rot.z);
        m_attach_offset_2.translate_over(m2pos);

        m_transform.mul(trans, m_attach_offset);
        m_transform_2.mul(trans_2, m_attach_offset_2);

        m_model->UpdateTracks();
        m_model->dcast_PKinematics()->CalculateBones_Invalidate();
        m_model->dcast_PKinematics()->CalculateBones(TRUE);

        if (m_model_2)
        {
            m_model_2->UpdateTracks();
            m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
            m_model_2->dcast_PKinematics()->CalculateBones(TRUE);
        }
    }

    // [DA_PORT] Кости предмета скриптовой сцены двигаем ТУТ ЖЕ, раз в кадр.
    //
    // Прежде я звал только CalculateBones_Invalidate при отрисовке — это лишь помечает кости
    // грязными, но не проигрывает анимацию. Модель стояла в первом кадре, и движение выглядело
    // сломанным. Порядок обязателен и тот же, что у рук выше: сдвинуть дорожки, пометить, посчитать.
    //
    // Здесь, а не в render_hud: отрисовка зовётся по разу на каждый контекст, и анимация тогда
    // убегала бы вперёд кратно их числу.
    if (m_da_script_item_visual)
    {
        // [DA_PORT] Матрицу посадки собираем ЗДЕСЬ, каждый кадр: так поверх записанного в секции
        // ложатся консольные поправки подбора (da_scene_item_pos и родня). Разбор - у их объявления.
        {
            Fvector ypr = m_da_script_item_rot;
            ypr.add(g_da_scene_item_rot_adj);
            ypr.mul(PI / 180.f);
            m_da_script_item_offset.setHPB(ypr.x, ypr.y, ypr.z);

            Fvector pos = m_da_script_item_pos;
            pos.add(g_da_scene_item_pos_adj);
            m_da_script_item_offset.translate_over(pos);

            const float sc = m_da_script_item_scale * g_da_scene_item_scale_adj;
            if (!fsimilar(sc, 1.f))
            {
                Fmatrix S;
                S.scale(sc, sc, sc);
                // Порядок важен: сперва уменьшаем модель в её собственных осях, потом ставим на место.
                m_da_script_item_offset.mulB_43(S);
            }
        }

        // Положение считаем ЗДЕСЬ, а не при отрисовке: кости рук только что посчитаны, и точка
        // хвата уже верная. При отрисовке она была бы от прошлого кадра, а сама отрисовка идёт по
        // разу на каждый контекст — считать в ней одно и то же несколько раз незачем.
        if (m_da_script_item_attached)
        {
            // [DA_PORT] Предмет сцены висит на ТОЙ ЖЕ половине, что и её рука: иначе он считался бы
            // от правой матрицы, пока рука с ним живёт на левой, и уезжал бы от ладони.
            // lh_lead_gun — просьба секции взять точку хвата оружия, то есть правую, независимо от
            // играющей руки; она и решает выбор половины.
            const u16 idx = m_da_script_item_lead_gun ? u16(0) : m_da_script_item_attach;
            const bool left = !m_da_script_item_lead_gun && (m_da_script_hand == 1) && m_model_2;
            IKinematicsAnimated* model = left ? m_model_2 : m_model;
            const Fmatrix& base = left ? m_transform_2 : m_transform;

            IKinematics* k = model ? smart_cast<IKinematics*>(model) : nullptr;
            if (k && idx < m_ancors.size())
            {
                const Fmatrix ancor = k->LL_GetTransform(m_ancors[idx]);
                m_da_script_item_transform.mul(base, ancor);
                m_da_script_item_transform.mulB_43(m_da_script_item_offset);
            }
            else
                m_da_script_item_transform.mul(base, m_da_script_item_offset);
        }
        else
            m_da_script_item_transform.mul(m_transform, m_da_script_item_offset);

        // Кости двигаем только у анимированной модели: у неподвижной их либо нет вовсе, либо
        // считать нечего — положение ей задаёт одна матрица выше.
        if (m_da_script_item_model)
        {
            m_da_script_item_model->UpdateTracks();
            m_da_script_item_model->dcast_PKinematics()->CalculateBones_Invalidate();
            m_da_script_item_model->dcast_PKinematics()->CalculateBones(TRUE);
        }

    }

    if (item0)
        item0->update(true);

    if (item1)
        item1->update(true);

    // [DA_PORT] Рука снимается с задачи, как только СВОЙ цикл доигран — не дожидаясь, пока скрипт
    // позовёт остановку.
    //
    // Зачем. Скрипт закрывает сцену по своему расписанию (у обыска это отдельное событие времени), и
    // между «цикл кончился» и «скрипт спохватился» рука висела в последней позе. Отпустить замок
    // раньше безопасно: сцену как таковую мы не разбираем — ни предмет, ни отрисовку не трогаем, —
    // только возвращаем руке право играть покой ствола. Полную уборку по-прежнему делает остановка.
    if (m_da_script_hand != u8(-1) && m_da_script_one_hand && m_da_script_anim_end &&
        Device.dwTimeGlobal >= m_da_script_anim_end)
    {
        m_da_script_hand = u8(-1);
        if (attachable_hud_item* hi = m_attached_items[0])
            if (hi->m_parent_hud_item)
                hi->m_parent_hud_item->PlayAnimIdle();
    }

    // Переезд посадки: вход медленный, выход быстрый — разбор у объявления ps_da_scene_seat_in.
    if (m_da_script_hand != u8(-1))
        m_da_script_seat_k += Device.fTimeDelta * ps_da_scene_seat_in;
    else
        m_da_script_seat_k -= Device.fTimeDelta * ps_da_scene_seat_out;

    clamp(m_da_script_seat_k, 0.f, 1.f);
}

// [DA_PORT] Раскладка цикла по копиям модели рук — устройство перенесено из первоисточника.
//
// Половин две: правая живёт на m_model, левая на m_model_2 (у каждой копии спрятана чужая рука).
// Отсюда и замок: пока скриптовая сцена владеет рукой, обычная анимация предмета на неё не идёт —
// раньше её перебивал цикл покоя ствола, потому что модель была одна и нулевая часть костяка
// доставалась тому, кто сыграл последним.
//
// Правая копия играет части 0 и 2, левая — 0, 1 и 2. Разница не опечатка: у правой части 1 отвечает
// за спрятанную левую руку, и трогать её незачем.
void player_hud::da_play_blend(u16 pid, const MotionID& M, BOOL bMixIn, float speed, bool script_anim)
{
    switch (pid)
    {
    case 0: // обе руки
    {
        if (!script_anim && m_da_script_hand == 2)
            return;
        // ⚠️ Вниз уходим БЕЗ признака сцены: каждая половина проверяет свой замок сама. Иначе
        // двурукая сцена сняла бы замок и с той руки, которой владеет другая сцена.
        da_play_blend(1, M, bMixIn, speed, false);
        da_play_blend(2, M, bMixIn, speed, false);
        break;
    }
    case 1: // левая
    {
        if (!script_anim && m_da_script_hand == 1)
            return;
        if (!m_model_2)
            return;
        const u16 pc = m_model_2->partitions().count();
        for (u16 i = 0; i < pc; ++i)
        {
            if (CBlend* B = m_model_2->PlayCycle(i, M, bMixIn))
                B->speed *= speed;
        }
        m_model_2->dcast_PKinematics()->CalculateBones_Invalidate();
        break;
    }
    case 2: // правая
    {
        if (!script_anim && m_da_script_hand == 0)
            return;
        if (!m_model)
            return;
        const u16 pc = m_model->partitions().count();
        for (u16 i = 0; i < pc; ++i)
        {
            if (i == 1)
                continue;
            if (CBlend* B = m_model->PlayCycle(i, M, bMixIn))
                B->speed *= speed;
        }
        m_model->dcast_PKinematics()->CalculateBones_Invalidate();
        break;
    }
    default:
        break;
    }
}

// part: 0 — предмет правой руки (оружие), 1 — предмет левой (детектор).
u32 player_hud::anim_play(u16 part, const MotionID& M, BOOL bMixIn, const CMotionDef*& md, float speed, IKinematicsAnimated* itemModel)
{
    if (!itemModel && m_model)
    {
        // Один предмет ведёт ОБЕ руки: он в них и держится двумя. Два предмета — каждый свою.
        const u16 pid = (attached_item(0) && attached_item(1)) ? ((part == 0) ? u16(2) : u16(1)) : u16(0);
        da_play_blend(pid, M, bMixIn, speed, false);
    }

    return motion_length(M, md, speed, itemModel);
}

u32 player_hud::da_script_motion_length(pcstr section, pcstr anim, float speed)
{
    // [DA_PORT] Длина цикла рук, миллисекунды. Ноль означает "не нашли".
    //
    // Зачем понадобилось: скриптам нужно знать, сколько идёт чужая анимация, чтобы подгадать под
    // неё своё расписание. Без этого длительности подбираются на глаз и разъезжаются, стоит
    // мододелу поменять .omf. У Anomaly для этого есть game.get_motion_length, у нас не было.
    //
    // Набор движений берём из того же кэша, что и сама сцена, поэтому лишней загрузки нет.
    if (!m_model || !section || !anim)
        return 0;

    const shared_str sect(section);
    if (!pSettings->section_exist(sect))
        return 0;

    auto it = m_da_script_motions.find(sect);
    if (it == m_da_script_motions.end())
    {
        player_hud_motion_container container;
        container.load(m_model, sect);
        it = m_da_script_motions.emplace(sect, std::move(container)).first;
    }

    const player_hud_motion* motion = it->second.find_motion(anim);
    if (!motion || motion->m_animations.empty())
        return 0;

    const CMotionDef* md = nullptr;
    return motion_length(motion->m_animations[0].mid, md, speed > 0.f ? speed : 1.f, nullptr);
}

// [DA_PORT] Анимация рук по требованию скрипта. Разбор — у объявления в player_hud.h.
u32 player_hud::da_script_anim_play(u8 hand, pcstr section, pcstr anim, bool mix_in, float speed, u32 target_ms)
{
    if (!m_model || !section || !anim)
        return 0;

    const shared_str sect = section;
    if (!pSettings->section_exist(sect))
    {
        Msg("! [DA_PORT] анимация рук: секции [%s] нет", section);
        return 0;
    }

    // Набор движений грузим ОДИН раз на секцию: загрузка перебирает строки конфига и ищет циклы в
    // модели рук, а зовут её на каждое подтягивание.
    auto it = m_da_script_motions.find(sect);
    if (it == m_da_script_motions.end())
    {
        player_hud_motion_container container;
        container.load(m_model, sect);
        it = m_da_script_motions.emplace(sect, std::move(container)).first;
    }

    const player_hud_motion* motion = it->second.find_motion(anim);
    if (!motion || motion->m_animations.empty())
    {
        // ⚠️ Частый случай при переносе чужого мода: цикла нет в НАШЕЙ модели рук. Анимация лежит
        // в своём .omf под скелет другой сборки, и наш скелет о ней не знает. Пишем прямо, иначе
        // руки просто не двинутся и это будет выглядеть дефектом сцены.
        Msg("! [DA_PORT] анимация рук: цикл [%s] не найден в секции [%s] — нет в модели рук",
            anim, section);
        return 0;
    }

    // Посадка рук для этой сцены. Широкоэкранный вариант, если он задан: у секции те же имена
    // ключей, что и у обычного предмета в руках.
    {
        const Fvector zero{ 0.f, 0.f, 0.f };
        pcstr key = UI().is_widescreen() ? "hands_position_16x9" : "hands_position";
        m_da_script_hands_pos = pSettings->read_if_exists<Fvector>(sect, key, zero);
        pcstr rkey = UI().is_widescreen() ? "hands_orientation_16x9" : "hands_orientation";
        m_da_script_hands_rot = pSettings->read_if_exists<Fvector>(sect, rkey, zero);
    }

    const motion_descr& M = motion->m_animations[Random.randI(motion->m_animations.size())];

    // [DA_PORT] Растягиваем цикл под длительность СЦЕНЫ, если она задана.
    //
    // Зачем. Длину сцены задаёт скрипт своей величиной tm в конфиге (у хлеба 7000 мс), а длина
    // анимации записана в самом цикле (у того же хлеба 5970 мс) — совпадать они не обязаны. Цикл
    // помечен "остановиться в конце", поэтому он доигрывает и ЗАМИРАЕТ, а сцена идёт ещё секунду:
    // руки с предметом стоят неподвижно, и выглядит это оборванной анимацией.
    //
    // Подгонять скоростью честнее, чем растворять руки в конце: движение остаётся цельным, просто
    // идёт ровно столько, сколько отведено. Ноль означает "не трогать", и тогда всё как прежде.
    float eff_speed = speed;
    if (target_ms > 0)
    {
        const CMotionDef* base_md = nullptr;
        const u32 base = motion_length(M.mid, base_md, 1.f, nullptr);
        if (base > 0)
            eff_speed = float(base) / float(target_ms);
    }

    // [DA_PORT] Предмет в руках: бутылка, аптечка, пачка сигарет.
    //
    // Без него сцена выглядит так, будто игрок пьёт из воздуха — руки двигаются правильно, а
    // держат пустоту. Модель своя, отдельная от предметов инвентаря: она нужна только на время
    // сцены и живёт ровно столько же.
    da_script_item_release();
    if (pSettings->line_exist(sect, "item_visual"))
    {
        // Отметки hud_loading, как в первоисточнике, у нашего рендера нет — модель создаётся так
        // же, как модель рук и оружия строчками выше.
        pcstr visual = pSettings->r_string(sect, "item_visual");
        m_da_script_item_visual = GEnv.Render->model_Create(visual);
        m_da_script_item_model = smart_cast<IKinematicsAnimated*>(m_da_script_item_visual);

        // ⚠️ Неанимированная модель — это НОРМА, а не ошибка: так устроены наши рюкзаки и вообще
        // любая вещь, нарисованная не под сцену. Она просто держится в руках неподвижно. Говорим
        // об этом вслух, потому что снаружи «сумка не раскрывается» и «сумки нет вовсе» выглядят
        // одинаково, а причины разные.
        if (m_da_script_item_visual && !m_da_script_item_model)
            Msg("~ [DA_PORT] анимация рук: у модели [%s] нет анимации — держим в руках неподвижно",
                visual);
    }

    if (m_da_script_item_visual)
    {
        const Fvector zero{ 0.f, 0.f, 0.f };
        m_da_script_item_pos = pSettings->read_if_exists<Fvector>(sect, "item_position", zero);
        m_da_script_item_rot = pSettings->read_if_exists<Fvector>(sect, "item_orientation", zero);
        // [DA_PORT] Размер предмета сцены. Нужен потому, что модель может быть нарисована не под
        // руки: наш тяжёлый рюкзак крупнее сумки из набора в 1.8 раза по описанному объёму.
        m_da_script_item_scale = pSettings->read_if_exists<float>(sect, "item_scale", 1.f);
        m_da_script_item_attach = pSettings->read_if_exists<u16>(sect, "attach_place_idx", 0);

        // [DA_PORT] Две развилки крепления, перенесённые из первоисточника целиком.
        //
        // item_attached (по умолчанию ДА) — цеплять предмет к кости руки. Если нет, он ставится
        // собственной матрицей: так делают вещи, которые по замыслу не follow руку, а висят перед
        // камерой сами по себе.
        //
        // lh_lead_gun — брать точку хвата ОРУЖИЯ (ancor_0) независимо от того, какая рука занята.
        // Нужно предметам, которые держат правой, а анимация играет левой.
        m_da_script_item_attached = pSettings->read_if_exists<bool>(sect, "item_attached", true);

        // [DA_PORT] Подавлять ли корневую кость предмета — теперь решает секция.
        //
        // По умолчанию ДА, как было: у бутылок и еды в корне записан проезд по мировым
        // координатам, и без подавления предмет улетает за сотню метров.
        //
        // Но бывает наоборот. У мешка после разделки в корне записана вся расстановка сцены: он
        // поднимается и разворачивается сам, в пространстве рук. Обнуляя корень, мы стирали именно
        // её — мешок оставался где придётся. Замерено по костям: meat_* и mshk_* уходят от корня на
        // полметра и меняются за кадр целиком.
        m_da_script_item_root_lock = pSettings->read_if_exists<bool>(sect, "item_root_lock", true);
        m_da_script_item_lead_gun = pSettings->read_if_exists<bool>(sect, "lh_lead_gun", false);

        // ⚠️ У предмета СВОЁ имя цикла, второе в строке `anm_xxx = руки, предмет`. Если второго
        // нет, строка описывает только руки, и у предмета берём то же имя. Промах здесь означал бы
        // неподвижную бутылку в двигающейся руке.
        const shared_str item_anim =
            (motion->m_base_name != motion->m_additional_name) ? motion->m_additional_name : M.name;

        MotionID mid;
        if (m_da_script_item_model)
            mid = m_da_script_item_model->ID_Cycle_Safe(item_anim);
        if (m_da_script_item_model && !mid.valid())
            mid = m_da_script_item_model->ID_Cycle_Safe("idle");

        if (mid.valid())
        {
            // ⛔ Корневую кость ЗАБИВАЕМ единичной матрицей, иначе предмет улетает.
            //
            // Анимация предмета записана вместе с движением корня: в исходной сцене он ехал по
            // мировым координатам. Если корень не подавить, бутылка уезжает за сотню метров от
            // рук — формально она рисуется, а на экране её нет. Ровно это и было: отчёт показывал
            // и созданную модель, и вызов отрисовки, и координаты рядом с камерой, но видно ничего
            // не было.
            //
            // Тот же приём стоит в attachable_hud_item для обычного оружия — я его просто не
            // перенёс в скриптовый путь.
            IKinematics* k_item = m_da_script_item_model->dcast_PKinematics();
            if (k_item && m_da_script_item_root_lock)
            {
                const u16 root_id = k_item->LL_GetBoneRoot();
                CBoneInstance& root = k_item->LL_GetBoneInstance(root_id);
                root.set_callback_overwrite(TRUE);
                root.mTransform.identity();
            }

            const u16 pc = m_da_script_item_model->partitions().count();
            for (u16 pid = 0; pid < pc; ++pid)
                // Скорость у PlayCycle доводом не передаётся — правим её у полученной подмешки,
                // ровно как это делает anim_play для рук.
                if (CBlend* B = m_da_script_item_model->PlayCycle(pid, mid, mix_in ? TRUE : FALSE))
                    B->speed *= eff_speed;
            m_da_script_item_model->dcast_PKinematics()->CalculateBones_Invalidate();

        }
        else if (m_da_script_item_model)
            Msg("! [DA_PORT] анимация рук: у предмета [%s] нет цикла [%s] и нет idle",
                pSettings->r_string(sect, "item_visual"), item_anim.c_str());
    }

    // hand: 0 — правая, 1 — левая, 2 — обе. Наш anim_play различает руки ТОЛЬКО когда заняты обе
    // руки предметами; в скриптовой сцене их нет, и цикл ложится на все части сразу — что для
    // подтягивания и нужно.
    // hand: 0 — правая, 1 — левая, 2 — обе.
    //
    // [DA_PORT] Однорукая сцена: цикл ложится только на свою руку, вторая продолжает держать
    // оружие и рисоваться. Признак нужен трём местам сразу — выбору части в anim_play, посадке рук
    // и отрисовке прикреплённого, поэтому он поле, а не довод.
    m_da_script_one_hand = (hand != 2);
    // Замок владения: пока сцена держит руку, цикл предмета на неё не ложится (см. da_play_blend).
    m_da_script_hand = hand;
    m_da_script_hand_seat = (hand == 0) ? u8(0) : u8(1);
    // Половина модели: обе — 0, правая — 2, левая — 1. Соответствие взято из первоисточника.
    const u16 part = (hand == 2) ? u16(0) : (hand == 0 ? u16(2) : u16(1));

    Msg("~ [DA_ANIM] сцена: секция [%s], цикл [%s] -> движение [%s], рука %u, длина %u мс, "
        "прикреплено [%s] + [%s]",
        section, anim, M.name.c_str(), u32(hand), target_ms,
        m_attached_items[0] ? m_attached_items[0]->m_sect_name.c_str() : "-",
        m_attached_items[1] ? m_attached_items[1]->m_sect_name.c_str() : "-");

    const CMotionDef* md = nullptr;
    da_play_blend(part, M.mid, mix_in ? TRUE : FALSE, eff_speed, true);
    const u32 length = motion_length(M.mid, md, eff_speed, nullptr);

    m_da_script_anim_end = length ? (Device.dwTimeGlobal + length) : 0;
    m_da_script_anim_on = true;

    return length;
}

// Снятие модели предмета. Отдельной функцией: зовётся и при остановке, и перед новой сценой —
// иначе прежняя модель осталась бы висеть в руках поверх новой.
void player_hud::da_script_item_release()
{
    if (!m_da_script_item_visual)
        return;

    IRenderVisual* v = m_da_script_item_visual;
    m_da_script_item_visual = nullptr;
    m_da_script_item_model = nullptr;
    GEnv.Render->model_Delete(v);
}

void player_hud::da_script_anim_stop()
{
    // [DA_PORT] Здесь стоял `if (m_da_script_anim_on)` БЕЗ тела: телом молча становилась следующая
    // строка, а всё остальное шло безусловно. Проверка ничего не охраняла. Снятие модели предмета и
    // так безвредно при пустой сцене, поэтому поведение оставлено прежним, а обманчивая строка
    // убрана: следующий читающий не должен думать, что тут есть заслон.
    da_script_item_release();

    // [DA_PORT] После ОДНОРУКОЙ сцены просим оружие переиграть покой.
    //
    // Мы клали свой цикл на часть модели, где живёт вторая рука. Сам он не снимается, и рука
    // оставалась в последней позе сцены — на экране она просто пропадала со ствола. Оружейный
    // цикл покоя ставит её обратно.
    //
    // Для двурукой сцены этого не нужно: там оружие всё равно убрано, и его анимация встанет сама
    // при ближайшем обновлении.
    if (m_da_script_one_hand)
    {
        if (attachable_hud_item* hi = m_attached_items[0])
            if (hi->m_parent_hud_item)
                hi->m_parent_hud_item->PlayAnimIdle();
    }
    m_da_script_one_hand = false;
    // Снимаем замок владения — с этого мига предмет снова волен играть на своей руке.
    m_da_script_hand = u8(-1);

    m_da_script_anim_on = false;
    // Гасим только СВОЮ отметку. Насильно обрывать цикл незачем: если в руках есть предмет, его
    // собственная анимация встанет на место при ближайшем обновлении, а если рук ничем не занято —
    // обрывать нечего.
    m_da_script_anim_end = 0;
}

bool player_hud::da_script_anim_active() const
{
    if (!m_da_script_anim_on)
        return false;

    // ⛔ Сцена ОБЯЗАНА кончаться сама по времени, а не только по команде скрипта.
    //
    // Мод не зовёт game.stop_hud_motion при обычном завершении — он полагается, что движок закроет
    // сцену сам, когда доиграет цикл. Я же сделал признак чисто флаговым, и он не снимался НИКОГДА:
    // предмет сцены оставался в руках навсегда, а настоящее оружие не рисовалось (его отрисовку
    // сцена подавляет). Со стороны выглядело так, будто игрок дерётся батоном.
    //
    // Теперь время — заслон: даже если остановку не позвали, сцена закроется по концу цикла. А
    // растяжение под длительность сцены (см. da_script_anim_play) сводит этот момент с настоящим
    // концом сцены, так что ничего не обрывается раньше времени.
    if (m_da_script_anim_end && Device.dwTimeGlobal >= m_da_script_anim_end)
        return false;

    return true;
}
void player_hud::update_additional(Fmatrix& trans) const
{
    if (m_attached_items[0])
        m_attached_items[0]->update_hud_additional(trans);

    if (m_attached_items[1])
        m_attached_items[1]->update_hud_additional(trans);
}

// [DA_PORT] ПРИБОР УВОДА ПРИЦЕЛА. Команда da_aim_debug 1.
//
// Что меряем и почему именно это. Смещение оружия здесь берётся из РАЗНИЦЫ между направлением
// взгляда и «отстающим» вектором st_last_dir: origin.mad(diff_dir, _origin_offset). Догоняет
// отстающий вектор взгляд только внутри `if (inertion_allowed())`. Когда условие ложно, функция не
// делает НИЧЕГО — вектор замирает, а камера крутится дальше, и разница копится.
//
// Отсюда два числа, ради которых всё и затевалось: сколько кадров подряд догон не работал и на
// сколько градусов за это время разошлись взгляд с отстающим вектором. Если увод виден на экране, а
// угол при этом мал — версия неверна, и искать надо в другом месте.
//
// ⚠️ Замер стоит ДО раннего выхода. Прибор, который молчит ровно в разбираемом случае, бесполезен:
// именно «инерция запрещена» нас и интересует.
//
// Углы ПИКОВЫЕ (max_angle не сбрасывается): выброс длиной в пару кадров иначе не поймать.
int g_da_aim_debug = 0;
bool g_da_aim_allowed = false;
u32 g_da_aim_frozen_frames = 0;
float g_da_aim_angle_deg = 0.f;
float g_da_aim_angle_deg_max = 0.f;
float g_da_aim_shift = 0.f;
float g_da_aim_shift_max = 0.f;
float g_da_aim_tendto = 0.f;
float g_da_aim_power = 0.f;
u8 g_da_aim_offset_idx = 0;

// Вынесен из тела функции: был локальной статикой, прибору её не видно. Смысл и время жизни не
// изменились — та же одна переменная на весь процесс. Это, к слову, и есть подозреваемое место:
// вектор общий на процесс, не сбрасывается ни при загрузке сейва, ни при смене уровня и оружия.
static Fvector st_last_dir = { 0, 0, 0 };

void player_hud::update_inertion(Fmatrix& trans) const
{
    const bool allowed = inertion_allowed();

    if (g_da_aim_debug)
    {
        g_da_aim_allowed = allowed;

        Fvector last;
        last.normalize_safe(st_last_dir);
        if (last.square_magnitude() > EPS)
        {
            float dot = last.dotproduct(trans.k);
            clamp(dot, -1.f, 1.f);
            g_da_aim_angle_deg = rad2deg(acosf(dot));
            g_da_aim_angle_deg_max = _max(g_da_aim_angle_deg_max, g_da_aim_angle_deg);
        }

        if (allowed)
            g_da_aim_frozen_frames = 0;
        else
            ++g_da_aim_frozen_frames;
    }

    const Fvector origin_before = trans.c;

    if (allowed)
    {
        attachable_hud_item* pMainHud = m_attached_items[0];

        Fmatrix xform;
        Fvector& origin = trans.c;
        xform = trans;

        // load params
        hud_item_measures::inertion_params inertion_data;
        if (pMainHud != NULL)
        { // Загружаем параметры инерции из основного худа
            inertion_data.m_pitch_offset_r = pMainHud->m_measures.m_inertion_params.m_pitch_offset_r;
            inertion_data.m_pitch_offset_n = pMainHud->m_measures.m_inertion_params.m_pitch_offset_n;
            inertion_data.m_pitch_offset_d = pMainHud->m_measures.m_inertion_params.m_pitch_offset_d;
            inertion_data.m_pitch_low_limit = pMainHud->m_measures.m_inertion_params.m_pitch_low_limit;
            inertion_data.m_origin_offset = pMainHud->m_measures.m_inertion_params.m_origin_offset;
            inertion_data.m_origin_offset_aim = pMainHud->m_measures.m_inertion_params.m_origin_offset_aim;
            inertion_data.m_tendto_speed = pMainHud->m_measures.m_inertion_params.m_tendto_speed;
            inertion_data.m_tendto_speed_aim = pMainHud->m_measures.m_inertion_params.m_tendto_speed_aim;
        }
        else
        { // Загружаем дефолтные параметры инерции
            inertion_data.m_pitch_offset_r = PITCH_OFFSET_R;
            inertion_data.m_pitch_offset_n = PITCH_OFFSET_N;
            inertion_data.m_pitch_offset_d = PITCH_OFFSET_D;
            inertion_data.m_pitch_low_limit = PITCH_LOW_LIMIT;
            inertion_data.m_origin_offset = ORIGIN_OFFSET;
            inertion_data.m_origin_offset_aim = ORIGIN_OFFSET_AIM;
            inertion_data.m_tendto_speed = TENDTO_SPEED;
            inertion_data.m_tendto_speed_aim = TENDTO_SPEED_AIM;
        }

        // calc difference
        Fvector diff_dir;
        diff_dir.sub(xform.k, st_last_dir);

        // clamp by PI_DIV_2
        Fvector last;
        last.normalize_safe(st_last_dir);
        float dot = last.dotproduct(xform.k);
        if (dot < EPS)
        {
            Fvector v0;
            v0.crossproduct(st_last_dir, xform.k);
            st_last_dir.crossproduct(xform.k, v0);
            diff_dir.sub(xform.k, st_last_dir);
        }

        // tend to forward
        float _tendto_speed, _origin_offset;
        if (pMainHud != NULL && pMainHud->m_parent_hud_item->GetCurrentHudOffsetIdx() > 0)
        { // Худ в режиме "Прицеливание"
            float factor = pMainHud->m_parent_hud_item->GetInertionFactor();
            _tendto_speed = inertion_data.m_tendto_speed_aim - (inertion_data.m_tendto_speed_aim - inertion_data.m_tendto_speed) * factor;
            _origin_offset =
                inertion_data.m_origin_offset_aim - (inertion_data.m_origin_offset_aim - inertion_data.m_origin_offset) * factor;
        }
        else
        { // Худ в режиме "От бедра"
            _tendto_speed = inertion_data.m_tendto_speed;
            _origin_offset = inertion_data.m_origin_offset;
        }

        // Фактор силы инерции
        if (pMainHud != NULL)
        {
            float power_factor = pMainHud->m_parent_hud_item->GetInertionPowerFactor();
            _tendto_speed *= power_factor;
            _origin_offset *= power_factor;
        }

        st_last_dir.mad(diff_dir, _tendto_speed * Device.fTimeDelta);
        origin.mad(diff_dir, _origin_offset);

        // pitch compensation
        float pitch = angle_normalize_signed(xform.k.getP());

        if (pMainHud != NULL)
            pitch *= pMainHud->m_parent_hud_item->GetInertionFactor();

        // Отдаление\приближение
        origin.mad(xform.k, -pitch * inertion_data.m_pitch_offset_d);

        // Сдвиг в противоположную часть экрана
        origin.mad(xform.i, -pitch * inertion_data.m_pitch_offset_r);

        // Подьём\опускание
        clamp(pitch, inertion_data.m_pitch_low_limit, PI);
        origin.mad(xform.j, -pitch * inertion_data.m_pitch_offset_n);

        if (g_da_aim_debug)
        {
            g_da_aim_tendto = _tendto_speed;
            g_da_aim_power = (pMainHud ? pMainHud->m_parent_hud_item->GetInertionPowerFactor() : 1.f);
            g_da_aim_offset_idx = (pMainHud ? pMainHud->m_parent_hud_item->GetCurrentHudOffsetIdx() : 0);
        }
    }

    if (g_da_aim_debug)
    {
        g_da_aim_shift = origin_before.distance_to(trans.c);
        g_da_aim_shift_max = _max(g_da_aim_shift_max, g_da_aim_shift);
    }
}

attachable_hud_item* player_hud::create_hud_item(const shared_str& sect)
{
    current_player_hud_sect = sect;
    auto& item = m_pool[sect];

    if (!item)
        item = xr_new<attachable_hud_item>(this, sect, m_model);

    return item;
}

bool player_hud::allow_activation(CHudItem* item) const
{
    if (m_attached_items[1])
        return m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);
    else
        return true;
}

void player_hud::attach_item(CHudItem* item)
{
    attachable_hud_item* pi = create_hud_item(item->HudSection());
    const int item_idx = pi->m_attach_place_idx;

    if (m_attached_items[item_idx] != pi || pi->m_parent_hud_item != item)
    {
        if (m_attached_items[item_idx])
            m_attached_items[item_idx]->m_parent_hud_item->on_b_hud_detach();

        m_attached_items[item_idx] = pi;
        pi->m_parent_hud_item = item;
        pi->reload_measures();

        if (item_idx == 0 && m_attached_items[1])
            m_attached_items[1]->m_parent_hud_item->CheckCompatibility(item);

        item->on_a_hud_attach();
    }
    pi->m_parent_hud_item = item;
}

void player_hud::detach_item_idx(u16 idx)
{
    if (nullptr == attached_item(idx))
        return;

    m_attached_items[idx]->m_parent_hud_item->on_b_hud_detach();

    m_attached_items[idx]->m_parent_hud_item = nullptr;
    m_attached_items[idx] = nullptr;

    if (idx == 1 && attached_item(0))
    {
        u16 part_idR = m_model->partitions().part_id("right_hand");
        u32 bc = m_model->LL_PartBlendsCount(part_idR);
        for (u32 bidx = 0; bidx < bc; ++bidx)
        {
            CBlend* BR = m_model->LL_PartBlend(part_idR, bidx);
            if (!BR)
                continue;

            MotionID M = BR->motionID;

            u16 pc = m_model->partitions().count();
            for (u16 pid = 0; pid < pc; ++pid)
            {
                if (pid != part_idR)
                {
                    CBlend* B = m_model->PlayCycle(pid, M, TRUE); // this can destroy BR calling UpdateTracks !
                    if (BR->blend_state() != CBlend::eFREE_SLOT)
                    {
                        u16 bop = B->bone_or_part;
                        *B = *BR;
                        B->bone_or_part = bop;
                    }
                }
            }
        }
    }
    else if (idx == 0 && attached_item(1))
    {
        OnMovementChanged(mcAnyMove);
    }
}

void player_hud::detach_item(CHudItem* item)
{
    if (nullptr == item->HudItemData())
        return;

    const u16 item_idx = item->HudItemData()->m_attach_place_idx;

    if (m_attached_items[item_idx] == item->HudItemData())
    {
        detach_item_idx(item_idx);
    }
}

void player_hud::calc_transform(u16 attach_slot_idx, const Fmatrix& offset, Fmatrix& result) const
{
    // [DA_PORT] Предмет считается от СВОЕЙ половины рук: нулевая привязка живёт на правой копии,
    // первая на левой. Раньше обе брались от одной модели, и предмет левой руки ехал за правой.
    const bool left = (attach_slot_idx != 0) && (m_model_2 != nullptr);
    IKinematicsAnimated* model = left ? m_model_2 : m_model;
    const Fmatrix& base = left ? m_transform_2 : m_transform;

    const attachable_hud_item* item = m_attached_items[attach_slot_idx];
    if (item && !item->m_monolithic && model && attach_slot_idx < m_ancors.size())
    {
        IKinematics* k = smart_cast<IKinematics*>(model);
        const Fmatrix ancor_m = k->LL_GetTransform(m_ancors[attach_slot_idx]);
        result.mul(base, ancor_m);
        result.mulB_43(offset);
    }
    else
    {
        result.mul(base, offset);
        VERIFY(!fis_zero(DET(result)));
    }
}

bool player_hud::inertion_allowed() const
{
    if (const attachable_hud_item* hi = m_attached_items[0])
    {
        return hi->m_parent_hud_item->HudInertionEnabled() && hi->m_parent_hud_item->HudInertionAllowed();
    }
    return true;
}

void player_hud::OnMovementChanged(ACTOR_DEFS::EMoveCommand cmd) const
{
    CHudItem* hudItem0 = m_attached_items[0] ? m_attached_items[0]->m_parent_hud_item : nullptr;
    CHudItem* hudItem1 = m_attached_items[1] ? m_attached_items[1]->m_parent_hud_item : nullptr;

    if (cmd == 0)
    {
        if (hudItem0 && hudItem0->GetState() == CHUDState::eIdle)
            hudItem0->PlayAnimIdle();

        if (hudItem1 && hudItem1->GetState() == CHUDState::eIdle)
            hudItem1->PlayAnimIdle();
    }
    else
    {
        if (hudItem0)
            hudItem0->OnMovementChanged(cmd);

        if (hudItem1)
            hudItem1->OnMovementChanged(cmd);
    }
}
