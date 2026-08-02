#include "StdAfx.h"
#include "Torch.h"
#include "Entity.h"
#include "Actor.h"
#include "xrEngine/LightAnimLibrary.h"
#include "xrPhysics/PhysicsShell.h"
#include "xrServer_Objects_ALife_Items.h"
#include "ai_sounds.h"

#include "Level.h"
#include "Include/xrRender/Kinematics.h"
#include "xrEngine/CameraBase.h"
#include "xrEngine/xr_collide_form.h"
#include "Inventory.h"
#include "game_base_space.h"

#include "UIGameCustom.h"
#include "ActorEffector.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"

// [DA_PORT] Яркость луча фонарей (xrEngine, xr_ioc_cmd.cpp) — множители к цвету ламп.
extern ENGINE_API float ps_r__torch_bright;
extern ENGINE_API float ps_r__torch_bright_item;

constexpr pcstr TORCH_DEFINITION = "torch_definition";
static const float TORCH_INERTION_CLAMP = PI_DIV_6;
static const float TORCH_INERTION_SPEED_MAX = 7.5f;
static const float TORCH_INERTION_SPEED_MIN = 0.5f;
static Fvector TORCH_OFFSET = {-0.2f, +0.1f, -0.3f};
static const Fvector OMNI_OFFSET = {-0.2f, +0.1f, -0.1f};
static const float OPTIMIZATION_DISTANCE = 100.f;

CTorch::CTorch()
    : fBrightness(1.f), lanim(nullptr), guid_bone(BI_NONE),
      m_delta_h(0), m_switched_on(false), m_switched_on2(false),
      light_render(GEnv.Render->light_create()),
      light_omni(GEnv.Render->light_create()),
      glow_render(GEnv.Render->glow_create()),
      m_bNightVisionEnabled(false), m_bNightVisionOn(false), m_night_vision(nullptr),
      m_da_use_spot(true)
{
    m_prev_hp.set(0, 0);
    m_da_color.set(1.f, 1.f, 1.f, 1.f); // [DA_PORT] default white; overridden per item by torch_set_color_*

    // [DA_PORT] Налобный фонарь. Значения - те же, что каждый тик шлёт xr_actor.script (UpdateTorch),
    // чтобы луч был правильным ещё до первого его вызова.
    m_da_item_range = 0.f;
    m_da_item_cone_deg = 0.f;
    m_da2_range = 12.f;
    m_da2_cone_deg = 95.f;
    m_da2_color.set(1.f, 1.f, 0.9f, 1.f);
    m_da2_offset.set(0.2f, 0.1f);

    light_render->set_type(IRender_Light::SPOT);
    light_render->set_shadow(true);
    light_omni->set_type(IRender_Light::POINT);
    light_omni->set_shadow(false);

    // Disabling shift by x and z axes for 1st render,
    // because we don't have dynamic lighting in it.
    if (GEnv.Render->GenerationIsR1())
    {
        TORCH_OFFSET.x = 0;
        TORCH_OFFSET.z = 0;
    }
}

CTorch::~CTorch()
{
    light_render.destroy();
    light_omni.destroy();
    glow_render.destroy();
    xr_delete(m_night_vision);
}

inline bool CTorch::can_use_dynamic_lights()
{
    if (!H_Parent())
        return (true);

    CInventoryOwner* owner = smart_cast<CInventoryOwner*>(H_Parent());
    if (!owner)
        return (true);

    return (owner->can_use_dynamic_lights());
}

void CTorch::Load(LPCSTR section)
{
    inherited::Load(section);
    light_trace_bone = pSettings->r_string(section, "light_trace_bone");

    m_bNightVisionEnabled = !!pSettings->r_bool(section, "night_vision");

    if (pSettings->line_exist(section, "snd_turn_on"))
        m_sounds.LoadSound(section, "snd_turn_on", "sndTurnOn", false, SOUND_TYPE_ITEM_USING);
    if (pSettings->line_exist(section, "snd_turn_off"))
        m_sounds.LoadSound(section, "snd_turn_off", "sndTurnOff", false, SOUND_TYPE_ITEM_USING);
}

void CTorch::SwitchNightVision()
{
    if (OnClient())
        return;
    SwitchNightVision(!m_bNightVisionOn);
}

void CTorch::SwitchNightVision(bool vision_on, bool use_sounds)
{
    if (!m_bNightVisionEnabled)
        return;

    m_bNightVisionOn = vision_on;

    CActor* pA = smart_cast<CActor*>(H_Parent());
    if (!pA)
    {
        return;
    }
    if (!m_night_vision)
        m_night_vision = xr_new<CNightVisionEffector>(cNameSect());

    LPCSTR disabled_names = pSettings->r_string(cNameSect(), "disabled_maps");
    pcstr curr_map = Level().name().c_str();
    u32 cnt = _GetItemCount(disabled_names);
    bool b_allow = true;
    string512 tmp;
    for (u32 i = 0; i < cnt; ++i)
    {
        _GetItem(disabled_names, i, tmp);
        if (0 == xr_stricmp(tmp, curr_map))
        {
            b_allow = false;
            break;
        }
    }

    CHelmet* pHelmet = smart_cast<CHelmet*>(pA->inventory().ItemFromSlot(HELMET_SLOT));
    CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(pA->inventory().ItemFromSlot(OUTFIT_SLOT));


    if (pHelmet && pHelmet->m_NightVisionSect.size() && !b_allow)
    {
        m_night_vision->OnDisabled(pA, use_sounds);
        return;
    }
    else if (pOutfit && pOutfit->m_NightVisionSect.size() && !b_allow)
    {
        m_night_vision->OnDisabled(pA, use_sounds);
        return;
    }

    bool bIsActiveNow = m_night_vision->IsActive();

    if (m_bNightVisionOn)
    {
        if (!bIsActiveNow)
        {
            if (pHelmet && pHelmet->m_NightVisionSect.size())
            {
                m_night_vision->Start(pHelmet->m_NightVisionSect, pA, use_sounds);
                return;
            }
            else if (pOutfit && pOutfit->m_NightVisionSect.size())
            {
                m_night_vision->Start(pOutfit->m_NightVisionSect, pA, use_sounds);
                return;
            }
            m_bNightVisionOn = false; // in case if there is no nightvision in helmet and outfit
        }
    }
    else
    {
        if (bIsActiveNow)
        {
            m_night_vision->Stop(100000.0f, use_sounds);
        }
    }
}

void CTorch::Switch()
{
    if (OnClient())
        return;
    bool bActive = !m_switched_on;
    Switch(bActive);
}

void CTorch::Switch(bool light_on)
{
    // [DA_PORT] Модель автора: у ИГРОКА светит одна лампа — light_render, а light_omni ему не светит
    // никогда. В альфе это прямо так и написано: `if(!pA) light_omni->set_active(light_on); else
    // light_omni->set_active(false)`. Второй источник — только для NPC.
    //
    // Наш порт раньше держал для актёра два независимых света и разводил их признаком «предмет
    // споттовый / не споттовый». Состояний получалось больше, чем поводов их менять, и они регулярно
    // расходились между собой: то фонарь заливал светом выключенным, то не светил вовсе.
    m_switched_on = light_on;
    DaUpdateLightState();
}

// [DA_PORT] Единственное место, где решается, горит ли лампа игрока.
//
// Скрипты мода дают два независимых сигнала, и оба нужны:
//   • enable_torch  (torch1)  — `itms_manager` включает его один раз и держит включённым; это признак
//     «источник света в руках работает». Им светят фонарик, палочка и зажигалка — они горят сами,
//     без клавиши.
//   • enable_torch2 (torch2)  — клавиша игрока. Ею управляется налобный фонарь.
// Кому из двоих подчиняется лампа, решает наличие предмета со светом: он есть — горит по torch1, нет —
// по клавише. Это то поведение, которое подтверждено в игре, выраженное на ОДНОЙ лампе вместо двух.
void CTorch::DaUpdateLightState()
{
    if (!can_use_dynamic_lights())
        return;

    // [DA_PORT] У ДВУХ ЛАМП РАЗНЫЕ ВЫКЛЮЧАТЕЛИ, и это не усложнение, а суть механики.
    //
    // Конус (фонарик, налобный) — по клавише: torch2. Шар (палочка, зажигалка) — сам по себе: torch1,
    // который `itms_manager` держит включённым, пока предмет со светом выбран.
    //
    // ⛔ Правка от 31.07 завела ОБЕ лампы на torch2 «ради простоты: выключатель ровно один». Ценой
    // этого палочка перестала гореть сама: зажечь её можно было только клавишей, а клавиша закрыта
    // проверкой has_alife_info("enable_device_torch") — то есть требует НАЛОБНОГО ФОНАРЯ. Палочка без
    // фонаря не светила вовсе, а с фонарём — только после нажатия.
    //
    // Симптом от причины отстоял далеко: жалоба звучала как «не работает палочка», причина сидела в
    // строке про клавишу, а связывал их признак, выдаваемый за подобранный совсем другой предмет.
    // Комментарий рядом при этом всё время описывал верное поведение — «горят сами, без клавиши», —
    // расходясь с кодом под собой. Поэтому правила и разведены обратно, явно.
    //
    // Лишнего света это не даёт: когда ничего не выбрано, скрипт ставит `torch_switch_spot(true)`
    // (xr_actor.script, ветка else), и шар гаснет по признаку предмета, а не по выключателю.
    const bool beam = m_switched_on2 && m_da_use_spot;
    const bool omni = m_switched_on && !m_da_use_spot;

    light_render->set_active(beam);
    glow_render->set_active(beam);
    light_omni->set_active(omni);

    // [DA_PORT] Фонарь В РУКАХ ИГРОКА потолок теневых ламп не вытесняет и места в бюджете не
    // занимает. Иначе при значении по умолчанию (одна теневая карта за кадр) единственный слот
    // достаётся ближайшей лампе на стене, а то, чем игрок светит, остаётся без тени - и свет из
    // рук начинает проходить сквозь стены. Ставится здесь: сюда сходятся все пути смены состояния,
    // и здесь же уже известно, что владелец - актёр. У NPC приоритета нет: их фонарей может быть
    // много, и они бы съели бюджет целиком.
    const bool mine = !!smart_cast<CActor*>(H_Parent());
    light_render->set_never_demote(mine);
    light_omni->set_never_demote(mine);

    // [DA_PORT] Отчёт о КОНЕЧНОМ состоянии лампы, по факту его смены. Ставится здесь, а не в
    // переключателях: сюда сходятся все пути, и видно не «что попросили», а что получилось.
    if (mine)
    {
        const bool has_item = (m_da_item_range > 0.001f);

        static int last = -1;
        const int state = (beam ? 1 : 0) | (omni ? 2 : 0) | (m_switched_on ? 4 : 0) |
            (m_switched_on2 ? 8 : 0) | (has_item ? 16 : 0);
        if (state != last)
        {
            last = state;
            const Fcolor& c = has_item ? m_da_color : m_da2_color;
            Msg("* [DA_PORT] лампа: луч %d, рассеянная %d (torch1 %d, torch2 %d, предмет %d), "
                "дальность %.1f, цвет %.2f/%.2f/%.2f, аниматор %d",
                beam ? 1 : 0, omni ? 1 : 0, m_switched_on ? 1 : 0, m_switched_on2 ? 1 : 0,
                has_item ? 1 : 0, has_item ? m_da_item_range : m_da2_range, c.r, c.g, c.b,
                lanim ? 1 : 0);
        }
    }
}

// [DA_PORT] Сообщить СКРИПТУ, что фонарь погашен.
//
// Корень «нажимать приходится дважды» — рассинхрон: выключатель хранится в переменной скрипта
// (`itms_manager.Torch2`), она переживает загрузку и переход, а объект фонаря создаётся заново и
// стартует погашенным. Скрипт присылает АБСОЛЮТНОЕ значение из своей переменной, поэтому при
// расхождении первое нажатие уходит на то, чтобы привести её в ноль, и на экране не меняется ничего.
//
// Прошлая попытка лечить это на стороне движка («первая ничего не меняющая команда = переключи»)
// оказалась негодной: тем же путём приходит служебный вызов `enable_torch2(false)` из
// `actor_on_first_update`, и фонарь зажигался на каждой загрузке. Отличить служебный вызов от нажатия
// в движке нечем — это знание есть только у скрипта.
//
// Поэтому синхронизируем в ту сторону, где расхождения быть не может: движок ставит переменную скрипта
// в ноль сам, ровно там же, где гасит лампу.
void CTorch::DaSyncScriptSwitch()
{
    if (!GEnv.ScriptEngine || !GEnv.ScriptEngine->lua())
        return;

    luabind::object mgr = luabind::globals(GEnv.ScriptEngine->lua())["itms_manager"];
    const bool ok = (mgr.is_valid() && luabind::type(mgr) == LUA_TTABLE);
    if (ok)
        mgr["Torch2"] = false;

    Msg("%s [DA_PORT] фонарь: состояние скрипта %s", ok ? "*" : "~",
        ok ? "сброшено в выключено" : "СБРОСИТЬ НЕ УДАЛОСЬ (itms_manager недоступен)");
}

// [DA_PORT] Сколько скрытых фонарей у владельца. См. Switch2.
int CTorch::da_torch_count(CInventoryOwner* owner)
{
    if (!owner)
        return 0;

    int n = 0;
    for (const auto& it : owner->inventory().m_all)
        if (smart_cast<CTorch*>(it))
            ++n;
    return n;
}

void CTorch::Switch2()
{
    if (OnClient())
        return;
    bool bActive = !m_switched_on2;
    Switch2(bActive);
}

void CTorch::Switch2(bool light_on)
{
    // [DA_PORT] "torch2" - the real flashlight beam (light_render, spot+shadow), toggled
    // deliberately by the player via the torch key. Owns the on/off sounds, glow sprite and
    // the visible bulb bone, since this is the light the player actually sees switch state.
    CActor* pActor = smart_cast<CActor*>(H_Parent());
    if (pActor)
    {
        if (light_on && !m_switched_on2)
        {
            if (m_sounds.FindSoundItem("SndTurnOn", false))
                m_sounds.PlaySound("SndTurnOn", pActor->Position(), NULL, !!pActor->HUDview());
        }
        else if (!light_on && m_switched_on2)
        {
            if (m_sounds.FindSoundItem("SndTurnOff", false))
                m_sounds.PlaySound("SndTurnOff", pActor->Position(), NULL, !!pActor->HUDview());
        }
    }

    // [DA_PORT] Здесь действует АБСОЛЮТНАЯ семантика: скрипт присылает состояние, а не «переключи».
    // Так и нужно — скрипты сами гасят фонарь по разряду батареи и мерцают им при слабом заряде.
    //
    // ⚠️ История, которую стоит помнить. Двойное нажатие («первый щелчок ничего не делает») объяснялось
    // отсюда: печать стека Lua показывала на одно нажатие ДВА вызова, оба будто бы из
    // `itms_manager.script`. Вывод был неверен. Второй вызов приходил НЕ из Lua, а из штатного
    // обработчика клавиши в `CActor::IR_OnKeyboardPress` (`SwitchTorch()` → `Switch2()` без аргумента,
    // то есть переключатель): скрипт ставил «включено», движок тут же переворачивал. Стек Lua,
    // напечатанный из C++, к тому моменту уже не принадлежал вызывающему — он остался от предыдущего,
    // скриптового, прохода и выглядел как второй такой же.
    //
    // Урок: стек Lua, снятый из C++, доказывает происхождение вызова только если вызов ПРИШЁЛ из Lua.
    // Обработчик клавиши в ActorInput.cpp отключён (так же, как в исходниках автора).

    // [DA_PORT] Налобный включается только если он ОДИН. Второй скрытый фонарь снимается с трупа
    // сталкера (их раздаёт sr_light.script), и при двух экземплярах скрипт настраивает один, а включает
    // другой — свет пропадает без единой ошибки. Подбор второго теперь запрещён, но в старых
    // сохранениях он уже лежит, и там лучше честно не включаться, чем включать вслепую.
    if (light_on && !(m_da_item_range > 0.001f) && pActor && da_torch_count(pActor) != 1)
    {
        Msg("~ [DA_PORT] налобный фонарь не включён: скрытых фонарей в инвентаре %d, должен быть один",
            da_torch_count(pActor));
        return;
    }

    m_switched_on2 = light_on;
    DaUpdateLightState();

    // [DA_PORT] Отчёт о состоянии луча в момент переключения. Ставится не "на всякий случай": когда
    // луч не светил, из игры было видно ровно одно - щелчок есть, света нет, - и одинаково правдоподобно
    // выглядели три разные причины (лампа не включилась / включилась пустой / включилась и погашена
    // следом). Эти пять чисел различают их сразу.
    if (pActor)
    {
        const bool head = (m_da_item_range <= 0.001f);
        const Fcolor& c = head ? m_da2_color : m_da_color;
        Msg("* [DA_PORT] фонарь: %s, луч %s, дальность %.1f, конус %.0f, цвет %.2f/%.2f/%.2f, дин.свет %d, текстура %s",
            light_on ? "ВКЛ" : "выкл", head ? "налобный" : "предмета",
            head ? m_da2_range : m_da_item_range, head ? m_da2_cone_deg : m_da_item_cone_deg,
            c.r, c.g, c.b, can_use_dynamic_lights() ? 1 : 0,
            m_da_beam_texture.size() ? m_da_beam_texture.c_str() : "(нет)");
    }

    if (light_trace_bone.c_str())
    {
        IKinematics* pVisual = smart_cast<IKinematics*>(Visual());
        VERIFY(pVisual);
        u16 bi = pVisual->LL_BoneID(light_trace_bone);

        pVisual->LL_SetBoneVisible(bi, light_on, TRUE);
        pVisual->CalculateBones(TRUE);
    }
}
bool CTorch::torch_active() const { return (m_switched_on); }
bool CTorch::torch2_active() const { return (m_switched_on2); }
bool CTorch::net_Spawn(CSE_Abstract* DC)
{
    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeItemTorch* torch = smart_cast<CSE_ALifeItemTorch*>(e);
    R_ASSERT(torch);
    cNameVisual_set(torch->get_visual());

    R_ASSERT(!GetCForm());
    R_ASSERT(smart_cast<IKinematics*>(Visual()));
    CForm = xr_new<CCF_Skeleton>(this);

    if (!inherited::net_Spawn(DC))
        return (FALSE);

    bool b_r2 = GEnv.Render->GenerationIsR2OrHigher();

    IKinematics* K = smart_cast<IKinematics*>(Visual());
    CInifile* pUserData = K->LL_UserData();
    R_ASSERT3(pUserData, "Empty Torch user data!", torch->get_visual());
    lanim = LALib.FindItem(pUserData->r_string(TORCH_DEFINITION, "color_animator"));
    guid_bone = K->LL_BoneID(pUserData->r_string(TORCH_DEFINITION, "guide_bone"));
    VERIFY(guid_bone != BI_NONE);

    Fcolor clr = pUserData->r_fcolor(TORCH_DEFINITION, (b_r2) ? "color_r2" : "color");
    fBrightness = clr.intensity();
    float range = pUserData->r_float(TORCH_DEFINITION, (b_r2) ? "range_r2" : "range");
    light_render->set_color(clr);
    light_render->set_range(range);

    if (b_r2)
    {
        bool useVolumetric = pUserData->read_if_exists<bool>(TORCH_DEFINITION, "volumetric_enabled", false);
        light_render->set_volumetric(useVolumetric);
        if (useVolumetric)
        {
            float volQuality = pUserData->read_if_exists<float>(TORCH_DEFINITION, "volumetric_quality", 1.f);
            clamp(volQuality, 0.f, 1.f);
            light_render->set_volumetric_quality(volQuality);

            float volIntensity = pUserData->read_if_exists<float>(TORCH_DEFINITION, "volumetric_intensity", 1.f);
            clamp(volIntensity, 0.f, 10.f);
            light_render->set_volumetric_intensity(volIntensity);

            float volDistance = pUserData->read_if_exists<float>(TORCH_DEFINITION, "volumetric_distance", 1.f);
            clamp(volDistance, 0.f, 1.f);
            light_render->set_volumetric_distance(volDistance);
        }
    }

    Fcolor clr_o = pUserData->r_fcolor(TORCH_DEFINITION, (b_r2) ? "omni_color_r2" : "omni_color");
    float range_o = pUserData->r_float(TORCH_DEFINITION, (b_r2) ? "omni_range_r2" : "omni_range");
    light_omni->set_color(clr_o);
    light_omni->set_range(range_o);

    light_render->set_cone(deg2rad(pUserData->r_float(TORCH_DEFINITION, "spot_angle")));
    light_render->set_texture(pUserData->r_string(TORCH_DEFINITION, "spot_texture"));
    m_da_beam_texture = pUserData->r_string(TORCH_DEFINITION, "spot_texture"); // см. DaApplyBeam

    glow_render->set_texture(pUserData->r_string(TORCH_DEFINITION, "glow_texture"));
    glow_render->set_color(clr);
    glow_render->set_radius(pUserData->r_float(TORCH_DEFINITION, "glow_radius"));

    // [DA_PORT] Always start the actor's torch in the OFF state.
    // Dead Air expects the player to explicitly toggle it with the torch key (L).
    const bool start_on = torch->m_active && !smart_cast<CActor*>(H_Parent());
    Switch(start_on);
    VERIFY(!start_on || (torch->ID_Parent != 0xffff));

    if (torch->ID_Parent == 0)
        SwitchNightVision(torch->m_nightvision_active, false);
    // else
    //	SwitchNightVision	(false, false);

    m_delta_h = PI_DIV_2 - atan((range * 0.5f) / _abs(TORCH_OFFSET.x));

    return (TRUE);
}

void CTorch::net_Destroy()
{
    Switch(false);
    Switch2(false);
    SwitchNightVision(false);

    inherited::net_Destroy();
}

void CTorch::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
    m_focus.set(Position());

    // [DA_PORT] Фонарь игрока обязан быть работоспособен СРАЗУ, как попал к нему в руки, а не после
    // того, как до него доберётся скрипт.
    //
    // Зачем: настройку луча шлёт `xr_actor.script`, и делает это только при СМЕНЕ типа источника
    // света. Своя отметка типа у скрипта переживает и смену уровня, и появление нового объекта фонаря,
    // поэтому после перехода он считает, что уже всё настроил, и молчит — а объект-то новый, с
    // заводскими значениями и проекционной текстурой из user_data, которая света не даёт вовсе.
    // Снаружи это выглядит как «после перехода не работает ни один фонарь».
    //
    // Та же беда возникала, когда в инвентаре оказывалось два скрытых фонаря (второй снимается с трупа
    // сталкера): скрипт настраивал один, а включал другой. Подбор второго теперь запрещён
    // (CInventory::CanTakeItem), но уже лежащий в старом сохранении никуда не делся — эта строка
    // делает и его исправным.
    //
    // Значения берутся налобные: у объекта, до которого скрипт не дошёл, «предмета со светом» нет по
    // определению, а как только скрипт пришлёт свои — они перекроют эти.
    if (smart_cast<CActor*>(H_Parent()))
    {
        // [DA_PORT] Фонарь игрока ВСЕГДА начинает погашенным — при новой игре, загрузке сохранения и
        // переходе между уровнями. Так решено намеренно: состояние скрипта переживает эти события, а
        // объект фонаря создаётся заново, и восстанавливать «как было» означало бы согласовывать две
        // стороны, которые и без того расходились. Одно известное состояние надёжнее угаданного.
        m_switched_on2 = false;
        m_switched_on = false;
        DaApplyBeam();
        DaUpdateLightState();
        DaSyncScriptSwitch();
    }
}

void CTorch::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);

    Switch(false);
    Switch2(false);
    SwitchNightVision(false);

    m_sounds.StopAllSounds();
}

// [DA_PORT] --- Dead Air per-item torch light tuning (driven by xr_actor.script apply_torch_type) ---
// DA reconfigures the single device_torch for each equipped light item. We apply to both the spot
// (light_render) and omni (light_omni) since only the active one is rendered.
void CTorch::TorchSetRange(float r)
{
    m_da_item_range = r;
    light_omni->set_range(r);

    // [DA_PORT] Ручной фонарь загорается сам при выборе — как палочка и зажигалка.
    //
    // Отличить его от НАЛОБНОГО в движке больше нечем: у обоих конус, и признак m_da_use_spot у них
    // одинаков. Но скрипт настраивает их по-разному, и разница однозначная:
    //
    //   ручной фонарь (TorchType 1) — switch_spot(true) + set_range(60)
    //   пусто        (TorchType 0) — switch_spot(true) + set_range(0)
    //   налобный     (TorchType 2) — НЕ настраивается вовсе, ветка мертва: TorchType=2 не
    //                                выставляет ни один скрипт, и налобный живёт на значениях по
    //                                умолчанию из user_data предмета.
    //
    // Значит «конус с ненулевой дальностью» = в руках ручной фонарь, и только он. Налобного это не
    // касается: до него настройка не доходит, и он остаётся целиком на клавише.
    //
    // Включаем именно torch2, а не заводим третий признак: тогда клавиша продолжает им управлять как
    // раньше — фонарь загорелся при выборе, но выключить его по-прежнему можно.
    //
    // ⚠️ И ОБЯЗАТЕЛЬНО ГАСИМ, когда предмет убрали. Первая версия только зажигала, и это дало дефект:
    // при убирании ручного фонаря скрипт ставит дальность 0, DaApplyBeam ниже считает ноль признаком
    // «предмета со светом нет» и подставляет параметры НАЛОБНОГО — а включённым луч оставался мой.
    // На экране это выглядело как самопроизвольно загоревшийся налобный фонарь, которого у игрока
    // может вовсе не быть.
    //
    // Правило простое: раз зажгли сами, сами и гасим. Автоматика без парного выключения всегда
    // оставляет состояние, которое никто не убирает.
    if (m_da_use_spot)
    {
        const bool want_on = (r > 0.f);
        if (m_switched_on2 != want_on)
        {
            m_switched_on2 = want_on;
            DaUpdateLightState();
        }
    }

    DaApplyBeam();
}

void CTorch::TorchSetRadius(float deg)
{
    m_da_item_cone_deg = deg;
    // ⛔ У автора здесь есть ещё и `glow_render->set_radius(value)`, и её я НЕ переношу намеренно:
    // value — это угол конуса (50–95), а в user_data модели `glow_radius = 0.0`. Спрайт свечения
    // радиусом в полсотни единиц дал бы на экране огромное пятно вместо огонька.
    DaApplyBeam();
}

void CTorch::TorchSetInertion(float /*i*/) {} // reserved: beam inertia is fixed by TORCH_INERTION_* for now

void CTorch::TorchApplyDAColor()
{
    light_omni->set_color(m_da_color);
    glow_render->set_color(m_da_color);
    DaApplyBeam();
}

// [DA_PORT] --- Налобный фонарь (torch2) ---------------------------------------------------------
//
// Прожектор в порту один, а хозяев у него два: свет предмета в руках (torch_set_*) и налобный луч
// (torch2_set_*). Раздельно хранить обязательно: xr_actor.script шлёт налобные значения КАЖДЫЙ тик,
// а значения предмета - только при его смене, так что запись в общую лампу означала бы, что налобные
// 12/95 затирают фонарик через кадр после того, как его достали.
//
// Кто из двоих сейчас в лампе, решает дальность предмета: ноль в ветке "предмета нет" DA ставит
// именно как признак "луч принадлежит налобному", а не как настоящую дальность.
void CTorch::DaApplyBeam()
{
    const bool head = (m_da_item_range <= 0.001f);

    const float range = head ? m_da2_range : m_da_item_range;
    const float cone = head ? m_da2_cone_deg : m_da_item_cone_deg;

    light_render->set_range(range);
    if (cone > 0.001f)
        light_render->set_cone(deg2rad(cone));

    // Яркость — множителем поверх скриптового цвета. Насыщенность (соотношение каналов) сохраняется,
    // меняется только сила: у налобного цвет ещё и падает с зарядом батареи, и эту связь ломать нельзя.
    Fcolor beam = head ? m_da2_color : m_da_color;

    // [DA_PORT] Чёрный цвет = лампа не светит, хотя формально включена. Так и получалось у фонарика:
    // ветка «предмета со светом нет» выставляет цвет 0/0/0, а ветка фонарика цвет НЕ задаёт — она
    // рассчитывает на цветовой аниматор `light_torch_01`. Нет аниматора (не нашёлся в lanims.xr) —
    // перекрывать нечем, и остаётся чёрный: дальность верная, конус верный, текстура верная, света нет.
    //
    // Симптом обманчив тем, что выглядит как «фонарь сломан целиком», хотя сломано ровно одно число.
    if (!lanim && (beam.r + beam.g + beam.b) < 0.01f)
        beam.set(1.f, 1.f, 0.9f, 1.f);

    beam.mul_rgb(head ? ps_r__torch_bright : ps_r__torch_bright_item);
    light_render->set_color(beam);

    // [DA_PORT] Своя текстура проекции налобному. Прожектор светит СКВОЗЬ неё, и без пригодной
    // текстуры включённая лампа с верной дальностью и цветом не даёт на экране ничего.
    //
    // Ставить её больше некому: текстуру задаёт `torch_set_texture`, а зовут его только ветки
    // предметов (фонарик, палочка, зажигалка). Ветка «предмета со светом нет» - та единственная, куда
    // налобный фонарь и попадает, - не зовёт. Оставалась та, что прописана в user_data модели
    // (`internal\internal_light_torch_r2`), и с ней луча не было. Отсюда и симптом «фонарь начинает
    // работать после того, как возьмёшь любой светящийся предмет»: предмет подменял текстуру на
    // живую, и она оставалась в лампе навсегда.
    //
    // Имя взято из ветки TorchType 2 - той, что автор написал ровно под налобный фонарь (она мёртвая,
    // itms_manager этот тип не выставляет, но намерение в ней записано).
    if (head)
    {
        static const shared_str s_head_tex = "internal\\torch1";
        if (m_da_beam_texture != s_head_tex)
        {
            light_render->set_texture(s_head_tex.c_str());
            m_da_beam_texture = s_head_tex;
        }
    }

    // Горизонтальный увод луча компенсирует смещение фонаря в руке. У налобного смещения нет, и его
    // нужно ОБНУЛЯТЬ: иначе после фонарика в руках, где увод посчитан, налобный луч светил бы вбок.
    m_delta_h = head ? 0.f : (PI_DIV_2 - atan((m_da_item_range * 0.5f) / _abs(TORCH_OFFSET.x)));

    // Смена хозяина лампы меняет и то, по какому сигналу она горит, — см. DaUpdateLightState.
    DaUpdateLightState();
}

void CTorch::Torch2SetRange(float r)
{
    m_da2_range = r;
    DaApplyBeam();
}

void CTorch::Torch2SetRadius(float deg)
{
    m_da2_cone_deg = deg;
    DaApplyBeam();
}

void CTorch::Torch2SetColorR(float v) { m_da2_color.r = v; DaApplyBeam(); }
void CTorch::Torch2SetColorG(float v) { m_da2_color.g = v; DaApplyBeam(); }
void CTorch::Torch2SetColorB(float v) { m_da2_color.b = v; DaApplyBeam(); }
void CTorch::Torch2SetOffsetX(float v) { m_da2_offset.x = v; }
void CTorch::Torch2SetOffsetY(float v) { m_da2_offset.y = v; }

void CTorch::TorchSetColorR(float v) { m_da_color.r = v; TorchApplyDAColor(); }
void CTorch::TorchSetColorG(float v) { m_da_color.g = v; TorchApplyDAColor(); }
void CTorch::TorchSetColorB(float v) { m_da_color.b = v; TorchApplyDAColor(); }
void CTorch::TorchSetColorA(float v) { m_da_color.a = v; TorchApplyDAColor(); }

void CTorch::TorchSetAnimation(LPCSTR name)
{
    // [DA_PORT] Смена аниматора меняет и то, кто задаёт цвет лампы: с ним — он, без него — наш цвет.
    // Поэтому после установки пересчитываем луч (см. защиту от чёрного в DaApplyBeam).
    // "empty" (or none) => no color animator, so the script's torch_set_color_* stays applied
    // (glowstick/lighter green/orange). A real animator (e.g. "light_torch_01") drives the color
    // itself each frame in UpdateCL (flashlight/headlamp white).
    if (!name || !name[0] || 0 == xr_stricmp(name, "empty"))
        lanim = nullptr;
    else
    {
        lanim = LALib.FindItem(name);
        if (!lanim)
            Msg("~ [DA_PORT] фонарь: цветовой аниматор '%s' не найден - цвет берётся из настроек предмета", name);
    }

    DaApplyBeam();
}

void CTorch::TorchSetTexture(LPCSTR name)
{
    if (name && name[0])
    {
        light_render->set_texture(name);
        m_da_beam_texture = name;
    }
}

// [DA_PORT] Как у автора: переключается ТИП одной лампы, а не выбор между двумя источниками.
//
// В альфе `SwitchSpot` состоит ровно из двух строк — `set_type(SPOT)` либо `set_type(POINT)`. Фонарик
// светит конусом, палочка и зажигалка — шаром, но лампа при этом одна и та же. Наша прежняя версия
// вместо типа дёргала активность двух разных источников, и каждый такой рычаг приходилось потом
// «пересчитывать» ещё в трёх местах.
void CTorch::TorchSwitchSpot(bool spot)
{
    // ⛔ Переключение ТИПА одной лампы (как в альфе) отсюда убрано: вместе с остальной перестройкой оно
    // дало полное отсутствие света на экране. Тип ламп задан в конструкторе: прожектор — SPOT,
    // рассеянная — POINT, и меняется не тип, а то, какая из двух горит.
    //
    // [DA_PORT] Смена источника света в руках ГАСИТ всё: взял фонарик — палочка погасла, и наоборот.
    // Иначе от предыдущего предмета оставалась гореть его лампа, и на экране светили две сразу.
    if (m_da_use_spot != spot)
        m_switched_on2 = false;

    m_da_use_spot = spot;
    DaUpdateLightState();
}

void CTorch::UpdateCL()
{
    inherited::UpdateCL();

    // [DA_PORT] Either light (torch/omni or torch2/spot) being on needs position/rotation updates.
    if (!m_switched_on && !m_switched_on2)
        return;

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(guid_bone);
    Fmatrix M;

    if (H_Parent())
    {
        CActor* actor = smart_cast<CActor*>(H_Parent());
        if (actor)
            smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones_Invalidate();

        if (H_Parent()->XFORM().c.distance_to_sqr(Device.vCameraPosition) < _sqr(OPTIMIZATION_DISTANCE) ||
            GameID() != eGameIDSingle)
        {
            // near camera
            smart_cast<IKinematics*>(H_Parent()->Visual())->CalculateBones();
            M.mul_43(XFORM(), BI.mTransform);
        }
        else
        {
            // approximately the same
            M = H_Parent()->XFORM();
            H_Parent()->Center(M.c);
            M.c.y += H_Parent()->Radius() * 2.f / 3.f;
        }

        if (actor)
        {
            m_prev_hp.x = angle_inertion_var(m_prev_hp.x, -actor->cam_FirstEye()->yaw, TORCH_INERTION_SPEED_MIN,
                TORCH_INERTION_SPEED_MAX, TORCH_INERTION_CLAMP, Device.fTimeDelta);
            m_prev_hp.y = angle_inertion_var(m_prev_hp.y, -actor->cam_FirstEye()->pitch, TORCH_INERTION_SPEED_MIN,
                TORCH_INERTION_SPEED_MAX, TORCH_INERTION_CLAMP, Device.fTimeDelta);

            Fvector dir, right, up;
            dir.setHP(m_prev_hp.x + m_delta_h, m_prev_hp.y);
            Fvector::generate_orthonormal_basis_normalized(dir, up, right);

            if (true)
            {
                Fvector offset = M.c;
                offset.mad(M.i, TORCH_OFFSET.x);
                offset.mad(M.j, TORCH_OFFSET.y);
                offset.mad(M.k, TORCH_OFFSET.z);
                light_render->set_position(offset);

                if (true /*false*/)
                {
                    offset = M.c;
                    offset.mad(M.i, OMNI_OFFSET.x);
                    offset.mad(M.j, OMNI_OFFSET.y);
                    offset.mad(M.k, OMNI_OFFSET.z);
                    light_omni->set_position(offset);
                }
            } // if (true)
            glow_render->set_position(M.c);

            if (true)
            {
                light_render->set_rotation(dir, right);

                if (true /*false*/)
                {
                    light_omni->set_rotation(dir, right);
                }
            } // if (true)
            glow_render->set_direction(dir);

        } // if(actor)
        else
        {
            if (can_use_dynamic_lights())
            {
                light_render->set_position(M.c);
                light_render->set_rotation(M.k, M.i);

                Fvector offset = M.c;
                offset.mad(M.i, OMNI_OFFSET.x);
                offset.mad(M.j, OMNI_OFFSET.y);
                offset.mad(M.k, OMNI_OFFSET.z);
                light_omni->set_position(M.c);
                light_omni->set_rotation(M.k, M.i);
            } // if (can_use_dynamic_lights())

            glow_render->set_position(M.c);
            glow_render->set_direction(M.k);
        }
    } // if(HParent())
    else
    {
        if (getVisible() && m_pPhysicsShell)
        {
            M.mul(XFORM(), BI.mTransform);

            m_switched_on = false;
            m_switched_on2 = false;
            light_render->set_active(false);
            light_omni->set_active(false);
            glow_render->set_active(false);
        } // if (getVisible() && m_pPhysicsShell)
    }

    if (!m_switched_on && !m_switched_on2)
        return;

    // calc color animator
    if (!lanim)
        return;

    int frame;
    // возвращает в формате BGR
    u32 clr = lanim->CalculateBGR(Device.fTimeGlobal, frame);

    Fcolor fclr;
    fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
    fclr.mul_rgb(fBrightness / 255.f);
    if (can_use_dynamic_lights())
    {
        // [DA_PORT] Здесь цвет луча задаёт АНИМАТОР, кадр за кадром, минуя DaApplyBeam — значит и
        // множитель яркости нужен здесь же, иначе ручной фонарик (единственный предмет с аниматором)
        // остался бы единственным, на кого r__torch_bright_item не действует.
        Fcolor beam = fclr;
        beam.mul_rgb((m_da_item_range <= 0.001f) ? ps_r__torch_bright : ps_r__torch_bright_item);
        light_render->set_color(beam);
        light_omni->set_color(fclr);
    }
    glow_render->set_color(fclr);
}

void CTorch::create_physic_shell() { CPhysicsShellHolder::create_physic_shell(); }
void CTorch::activate_physic_shell() { CPhysicsShellHolder::activate_physic_shell(); }
void CTorch::setup_physic_shell() { CPhysicsShellHolder::setup_physic_shell(); }
void CTorch::net_Export(NET_Packet& P)
{
    inherited::net_Export(P);
    //	P.w_u8						(m_switched_on ? 1 : 0);

    u8 F = 0;
    F |= (m_switched_on ? eTorchActive : 0);
    F |= (m_bNightVisionOn ? eNightVisionActive : 0);
    F |= (m_switched_on2 ? eTorch2Active : 0);
    const CActor* pA = smart_cast<const CActor*>(H_Parent());
    if (pA)
    {
        if (pA->attached(this))
            F |= eAttached;
    }
    P.w_u8(F);
    //	Msg("CTorch::net_export - NV[%d]", m_bNightVisionOn);
}

void CTorch::net_Import(NET_Packet& P)
{
    inherited::net_Import(P);

    u8 F = P.r_u8();
    bool new_m_switched_on = !!(F & eTorchActive);
    bool new_m_bNightVisionOn = !!(F & eNightVisionActive);
    bool new_m_switched_on2 = !!(F & eTorch2Active);

    // [DA_PORT] Actor's torch/torch2 must stay under local key-press control, not server state.
    // Server packets may carry m_active=true (e.g. spawned or saved that way),
    // so ignore the eTorchActive/eTorch2Active flags for the player actor.
    if (new_m_switched_on != m_switched_on && !smart_cast<CActor*>(H_Parent()))
        Switch(new_m_switched_on);
    if (new_m_switched_on2 != m_switched_on2 && !smart_cast<CActor*>(H_Parent()))
        Switch2(new_m_switched_on2);
    if (new_m_bNightVisionOn != m_bNightVisionOn)
    {
        //		Msg("CTorch::net_Import - NV[%d]", new_m_bNightVisionOn);

        const CActor* pA = smart_cast<const CActor*>(H_Parent());
        if (pA)
        {
            SwitchNightVision(new_m_bNightVisionOn);
        }
    }
}
bool CTorch::can_be_attached() const
{
    const CActor* pA = smart_cast<const CActor*>(H_Parent());
    if (pA)
        return pA->inventory().InSlot(this);
    else
        return true;
}

void CTorch::afterDetach()
{
    inherited::afterDetach();
    Switch(false);
    Switch2(false);
}

void CTorch::enable(bool value)
{
    inherited::enable(value);

    if (!enabled())
    {
        if (m_switched_on)
            Switch(false);
        if (m_switched_on2)
            Switch2(false);
    }
}

CNightVisionEffector::CNightVisionEffector(const shared_str& section) : m_pActor(NULL)
{
    m_sounds.LoadSound(section.c_str(), "snd_night_vision_on", "NightVisionOnSnd", false, SOUND_TYPE_ITEM_USING);
    m_sounds.LoadSound(section.c_str(), "snd_night_vision_off", "NightVisionOffSnd", false, SOUND_TYPE_ITEM_USING);
    m_sounds.LoadSound(section.c_str(), "snd_night_vision_idle", "NightVisionIdleSnd", true, SOUND_TYPE_ITEM_USING);
    m_sounds.LoadSound(
        section.c_str(), "snd_night_vision_broken", "NightVisionBrokenSnd", false, SOUND_TYPE_ITEM_USING);
}

void CNightVisionEffector::Start(const shared_str& sect, CActor* pA, bool play_sound)
{
    m_pActor = pA;
    AddEffector(m_pActor, effNightvision, sect);
    if (play_sound)
    {
        PlaySounds(eStartSound);
        PlaySounds(eIdleSound);
    }
}

void CNightVisionEffector::Stop(const float factor, bool play_sound)
{
    if (!m_pActor)
        return;
    CEffectorPP* pp = m_pActor->Cameras().GetPPEffector((EEffectorPPType)effNightvision);
    if (pp)
    {
        pp->Stop(factor);
        if (play_sound)
            PlaySounds(eStopSound);

        m_sounds.StopSound("NightVisionOnSnd");
        m_sounds.StopSound("NightVisionIdleSnd");
    }
}

bool CNightVisionEffector::IsActive()
{
    if (!m_pActor)
        return false;
    CEffectorPP* pp = m_pActor->Cameras().GetPPEffector((EEffectorPPType)effNightvision);
    return (pp != NULL);
}

void CNightVisionEffector::OnDisabled(CActor* pA, bool play_sound)
{
    m_pActor = pA;
    if (play_sound)
        PlaySounds(eBrokeSound);
}

void CNightVisionEffector::PlaySounds(EPlaySounds which)
{
    if (!m_pActor)
        return;

    bool bPlaySoundFirstPerson = !!m_pActor->HUDview();
    switch (which)
    {
    case eStartSound: { m_sounds.PlaySound("NightVisionOnSnd", m_pActor->Position(), NULL, bPlaySoundFirstPerson);
    }
    break;
    case eStopSound: { m_sounds.PlaySound("NightVisionOffSnd", m_pActor->Position(), NULL, bPlaySoundFirstPerson);
    }
    break;
    case eIdleSound:
    {
        m_sounds.PlaySound("NightVisionIdleSnd", m_pActor->Position(), NULL, bPlaySoundFirstPerson, true);
    }
    break;
    case eBrokeSound: { m_sounds.PlaySound("NightVisionBrokenSnd", m_pActor->Position(), NULL, bPlaySoundFirstPerson);
    }
    break;
    default: NODEFAULT;
    }
}
