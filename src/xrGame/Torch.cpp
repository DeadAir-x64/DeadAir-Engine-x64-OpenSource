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
      m_delta_h(0), m_switched_on(false), m_switched_on2(false), m_da_hand_on_time(0), m_da_hand_pending(false),
      light_render(GEnv.Render->light_create()),
      light_omni(GEnv.Render->light_create()),
      glow_render(GEnv.Render->glow_create()),
      // [DA_PORT] Порядок совпадает с порядком ОБЪЯВЛЕНИЯ в Torch.h — иначе -Wreorder. Само по себе
      // расхождение здесь было безвредным (все инициализаторы независимы), но предупреждение в файле,
      // который активно правят, маскирует будущее настоящее: когда одно поле инициализируют значением
      // другого, фактический порядок решает всё.
      m_da_use_spot(true), m_da_dynamic_applied(true),
      m_bNightVisionEnabled(false), m_bNightVisionOn(false), m_night_vision(nullptr)
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
// [DA_PORT] Задержка включения света в руке, секунды. Ручка `da_torch_hand_delay`.
//
// Была жёстко вписана как 1.5 — столько идёт анимация доставания, и задержка прятала свет, пока
// предмета в руке ещё не видно. На деле это читается как «палочка загорается не сразу», то есть
// лечение оказалось заметнее болезни. По умолчанию теперь НОЛЬ: свет появляется вместе с сигналом
// скрипта. Прежнее поведение возвращается значением 1.5.
float ps_da_torch_hand_delay = 0.0f;

// [DA_PORT] Показывать ли СВОЙ огонёк источника света от первого лица. 0 -- не показывать
// (по умолчанию), 1 -- как было. Разбор -- у места применения в CTorch::UpdateCL.
int ps_da_torch_glow_fp = 0;

// [DA_PORT] Отбрасывает ли тень фонарь СТАЛКЕРА. 0 — нет (по умолчанию), 1 — как было.
//
// СИМПТОМ: вечером и ночью по земле едет большой ровный чёрный клин, будто тень от NPC. Он и есть
// тень от NPC — от его собственного фонаря.
//
// ПОЧЕМУ. У актёра лампа отодвинута вперёд (TORCH_OFFSET), а сталкеру она ставится РОВНО в кость:
// `light_render->set_position(M.c)` в ветке else у CTorch::UpdateCL. Кость направляющая, на уровне
// груди, то есть источник оказывается ВНУТРИ тела. Ближняя плоскость теневой карты — 10 см
// (SMAP_near_plane в r2_types.h), а грудь, голова, руки и рюкзак дальше. Тело попадает в теневую
// карту собственного фонаря и с расстояния в сантиметры перекрывает почти весь конус: на земле это
// чёрный сектор в человеческий рост шириной, уезжающий за пределы видимости.
//
// Признаки сходятся все: клин появляется вечером (сталкеры зажигают фонари), едет вместе с NPC,
// пропадает при -noshadows (лампа перестаёт быть теневой) и НЕ ловится изоляцией одного источника —
// каждый фонарь даёт свой клин, а поодиночке остаётся неотличимо тёмное пятно в темноте.
//
// ⚠️ Тень фонаря сталкеру не нужна и по существу: свет от собственного тела он не загораживает
// никогда, а сцену от чужих стен ограничивает трафаретный объём, общий для всех ламп. Заодно это
// снимает по полному проходу сцены на каждого сталкера с фонарём — в лагере их десяток.
//
// Тень фонаря ИГРОКА не трогаем: его лампа отодвинута вперёд и работает верно.
int ps_da_npc_torch_shadow = 0;

void CTorch::DaUpdateLightState()
{
    // [DA_PORT] Раньше здесь стоял ранний выход, и это делало консольную ручку
    // `ai_use_torch_dynamic_lights` односторонней: выключить её на ходу было нельзя — уже горящие
    // лампы никто не гасил, потому что до строк с set_active управление просто не доходило.
    //
    // Ручка нужна рабочей: теперь свет у сталкеров есть, и на слабой машине ночью в лагере это
    // десятки источников. Пусть игрок сможет их убрать, не перезапуская игру.
    //
    // Свечение (glow) намеренно оставлено ВНЕ этого условия — так у автора: `if
    // (can_use_dynamic_lights()) { ...лампы... } glow_render->set_active(light_on);`. Огонёк фонаря
    // виден и без динамического света.
    const bool dynamic = can_use_dynamic_lights();
    m_da_dynamic_applied = dynamic;

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
    // [DA_PORT] У СТАЛКЕРА своя, простая модель: одна команда Switch (torch1) зажигает обе лампы.
    // Ровно так у автора: `light_render->set_active(light_on); if (!pA) light_omni->set_active(light_on);`
    //
    // Разведение torch1/torch2 и признак m_da_use_spot придуманы под ИГРОКА: у него два независимых
    // источника света (предмет в руках и налобный фонарь), и оба выключателя шлёт скрипт. У сталкера
    // нет ни того, ни другого: sr_light.script выдаёт ему device_torch и включает его через
    // enable_attachable_item, а это доходит до Switch (torch1) — CObjectHandler::attach → switch_torch.
    // Switch2 сталкеру не зовёт НИКТО: сервер шлёт в пакете только бит eTorchActive
    // (CSE_ALifeItemTorch::UPDATE_Write), про torch2 он не знает вовсе.
    //
    // Пока формула была общей, у сталкера выходило beam = false && true = false и omni = true &&
    // !true = false — фонарь не светил ни лучом, ни рассеянно. Симптома «ошибка» при этом нет
    // никакого: объект создан, включён, кость видима, а света нет.
    //
    // Фонарь БЕЗ владельца не светит ничем. Это не перестраховка: в момент net_Spawn родителя ещё
    // нет — его ставит событие GE_OWNERSHIP_TAKE, уже после, — а `m_active` в серверном объекте к
    // этому времени может быть включён. Без условия лампа успевала зажечься в начале координат
    // уровня. И тот же случай у осиротевших фонарей, что валяются по локации: гореть им незачем.
    // Состояние не теряется — при появлении владельца его пересчитывает OnH_A_Chield.
    //
    // 🪤 Откуда там включённый `m_active` — НЕ из сохранения: `CSE_ALifeItemTorch::STATE_Write` его
    // не пишет вовсе (проверено), в сейв идёт только унаследованная часть. Он живёт исключительно в
    // UPDATE-пакетах, то есть внутри сессии: сталкер побывал в онлайне с горящим фонарём, ушёл в
    // офлайн, вернулся — клиентская часть создаётся заново, а серверный признак остался включённым.
    // Практическое следствие: после загрузки сейва фонари у всех выключены и загораются заново
    // схемой света, массового «все горят при входе» не будет.
    const bool actor_owned = !!smart_cast<CActor*>(H_Parent());
    const bool npc_owned = !actor_owned && !!H_Parent();

    // [DA_PORT] Тень снимается только у фонаря сталкера — разбор у ps_da_npc_torch_shadow.
    // Здесь, а не в конструкторе: там владельца ещё нет, он приходит с GE_OWNERSHIP_TAKE.
    light_render->set_shadow(!npc_owned || 0 != ps_da_npc_torch_shadow);

    const bool beam = actor_owned ? (m_switched_on2 && m_da_use_spot) : (npc_owned && m_switched_on);
    bool omni = actor_owned ? (m_switched_on && !m_da_use_spot) : (npc_owned && m_switched_on);

    // [DA_PORT] Предмет со светом в руке зажигается не сразу, а через da_hand_torch_delay.
    //
    // Скрипт включает его по факту выбора предмета, а достать его игрок к этому моменту ещё не
    // успел: анимация идёт своим чередом. Свет вспыхивал в пустой руке.
    //
    // Отсчёт ведём здесь, а не в переключателе: сюда сходятся все пути смены состояния, и здесь
    // же видно, чей это фонарь. Сталкеров это не касается — у них анимации доставания нет.
    m_da_hand_pending = false;
    if (actor_owned && ps_da_torch_hand_delay > 0.f)
    {
        if (omni)
        {
            if (0 == m_da_hand_on_time)
                m_da_hand_on_time = Device.dwTimeGlobal;
            if (Device.dwTimeGlobal - m_da_hand_on_time < u32(ps_da_torch_hand_delay * 1000.f))
            {
                omni = false; // ещё достаём
                m_da_hand_pending = true;
            }
        }
        else
            m_da_hand_on_time = 0;
    }
    else
        m_da_hand_on_time = 0;

    light_render->set_active(beam && dynamic);
    light_omni->set_active(omni && dynamic);
    glow_render->set_active(beam);

    // [DA_PORT] Фонарь В РУКАХ ИГРОКА потолок теневых ламп не вытесняет и места в бюджете не
    // занимает. Иначе при значении по умолчанию (одна теневая карта за кадр) единственный слот
    // достаётся ближайшей лампе на стене, а то, чем игрок светит, остаётся без тени - и свет из
    // рук начинает проходить сквозь стены. Ставится здесь: сюда сходятся все пути смены состояния,
    // и здесь же уже известно, что владелец - актёр. У NPC приоритета нет: их фонарей может быть
    // много, и они бы съели бюджет целиком.
    const bool mine = actor_owned;
    light_render->set_never_demote(mine);
    light_omni->set_never_demote(mine);

    // [DA_PORT] Отчёт о КОНЕЧНОМ состоянии лампы, по факту его смены. Ставится здесь, а не в
    // переключателях: сюда сходятся все пути, и видно не «что попросили», а что получилось.
    if (mine)
    {
        const bool has_item = (m_da_item_range > 0.001f);

        // Печатаем ФАКТИЧЕСКОЕ состояние ламп, а не запрошенное: при выключенном динамическом свете
        // они гаснут, и отчёт «луч 1» тогда сбивал бы с толку ровно там, где нужен больше всего.
        const bool lit_beam = beam && dynamic;
        const bool lit_omni = omni && dynamic;

        static int last = -1;
        const int state = (lit_beam ? 1 : 0) | (lit_omni ? 2 : 0) | (m_switched_on ? 4 : 0) |
            (m_switched_on2 ? 8 : 0) | (has_item ? 16 : 0) | (dynamic ? 32 : 0);
        if (state != last)
        {
            last = state;
            const Fcolor& c = has_item ? m_da_color : m_da2_color;
            Msg("* [DA_PORT] лампа: луч %d, рассеянная %d (torch1 %d, torch2 %d, предмет %d), "
                "дальность %.1f, цвет %.2f/%.2f/%.2f, аниматор %d",
                lit_beam ? 1 : 0, lit_omni ? 1 : 0, m_switched_on ? 1 : 0, m_switched_on2 ? 1 : 0,
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

        // [DA_PORT] Проверка вместо VERIFY: он исчезает в релизе, а следующая строка — ВИРТУАЛЬНЫЙ
        // вызов. По нулевому указателю это переход по адресу 0, то есть падение без машинного
        // стека: «исполнение по адресу 0000000000000000», разматывать нечего.
        //
        // Визуала может не быть законно. Switch2 зовут и при выгрузке уровня, когда визуал уже снят,
        // и у предмета, который сейчас разбирают. В логе это выглядело так: строка отладки фонаря
        // печаталась (значит до неё дошло), а следом — прыжок в ноль.
        //
        // 🔑 Место стало горячим после того, как sr_light.script перестал УДАЛЯТЬ фонарь на свету и
        // начал его выключать: выключение идёт как раз сюда, и мина, лежавшая тихо, стала попадаться
        // на каждой выгрузке.
        if (!pVisual)
            return;

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
    else
    {
        // [DA_PORT] Владелец появился — пересчитать лампы. У сталкера это единственный момент, когда
        // включённый фонарь может зажечься обратно: `Switch` ему зовут при спавне (net_Spawn, по
        // сохранённому m_active) и при attach из sr_light.script, а между ними лежит установка
        // родителя. Без пересчёта фонарь сталкера, вошедшего в онлайн уже включённым, остался бы
        // тёмным до следующего цикла схемы света — а тот включает его лишь через
        // enable_attachable_item и только когда предмет ещё не прикреплён, то есть мог не прийти
        // вовсе.
        DaUpdateLightState();
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

    // [DA_PORT] Пока идёт задержка доставания, состояние надо пересчитать один раз, когда время
    // выйдет: само по себе оно не изменится, а сигнала об истечении никто не пришлёт. Признак
    // снимается внутри пересчёта, поэтому кадром позже сюда уже не зайдём.
    if (m_da_hand_pending && ps_da_torch_hand_delay > 0.f &&
        Device.dwTimeGlobal - m_da_hand_on_time >= u32(ps_da_torch_hand_delay * 1000.f))
        DaUpdateLightState();

    // [DA_PORT] Either light (torch/omni or torch2/spot) being on needs position/rotation updates.
    if (!m_switched_on && !m_switched_on2)
        return;

    // [DA_PORT] Владелец мёртв — фонарь гасим. Страховка в движке, потому что штатно это делает
    // СКРИПТ: `sr_light.check_light` зовётся из death_callback (xr_motivator.script) и выключает
    // фонарь веткой «сталкер не жив». Путь целиком скриптовый и оборвать его есть чем — в том числе
    // нашей же проверкой `db.storage[id]` в начале check_light: нет записи, и до выключения дело не
    // дойдёт вовсе.
    //
    // Раньше это ничего не стоило заметить: фонари сталкеров не светили в принципе. Теперь светят, и
    // цена промаха — горящий фонарь на трупе до самой выгрузки уровня. Проверка только для НЕ-актёра:
    // у игрока смерть обрабатывается своим путём, и трогать её незачем.
    if (H_Parent() && !smart_cast<CActor*>(H_Parent()))
    {
        const CEntityAlive* owner_alive = smart_cast<const CEntityAlive*>(H_Parent());
        if (owner_alive && !owner_alive->g_Alive())
        {
            Switch(false);
            return;
        }
    }

    // [DA_PORT] Признак динамического света мог смениться на ходу — консольной ручкой
    // `ai_use_torch_dynamic_lights`. Пересчитываем здесь, потому что штатно состояние ламп считается
    // ПО СОБЫТИЯМ, а у сталкера с горящим фонарём события может не быть часами: схема света зовёт
    // включение один раз, при входе в темноту, и больше не возвращается.
    //
    // Сверка в обе стороны, поэтому по запомненному значению, а не по факту «лампа активна»: ручку
    // могут и включить обратно, и свет тогда обязан вернуться.
    //
    // Стоит ПОСЛЕ проверки мёртвого владельца намеренно: у трупа лампы всё равно гасятся строкой
    // выше, и пересчитывать их перед этим — впустую.
    if (m_da_dynamic_applied != can_use_dynamic_lights())
        DaUpdateLightState();

    // [DA_PORT] Тот же случай, что в Switch2: без визуала это виртуальный вызов по нулю, то есть
    // прыжок по адресу 0 без стека. Ранний выход выше отсекает выключённый фонарь, но включённый
    // может дожить до выгрузки уровня, когда визуал уже снят.
    IKinematics* kinematics = smart_cast<IKinematics*>(Visual());
    if (!kinematics)
        return;

    // [DA_PORT] Кость направляющей берётся из user_data модели по ИМЕНИ, и её там может не быть:
    // LL_BoneID вернёт BI_NONE. В net_Spawn это ловит VERIFY, но он исчезает в релизе, а
    // LL_GetBoneInstance внутри защищён тоже только VERIFY — то есть в релизе мы читаем
    // bone_instances[65535], далеко за концом массива. Падения может и не случиться: чаще оттуда
    // приходит мусорная матрица, и лампа уезжает в бесконечность или в NaN.
    if (guid_bone == BI_NONE || guid_bone >= kinematics->LL_BoneCount())
        return;

    CBoneInstance& BI = kinematics->LL_GetBoneInstance(guid_bone);
    Fmatrix M;

    if (H_Parent())
    {
        CActor* actor = smart_cast<CActor*>(H_Parent());

        // [DA_PORT] Визуал РОДИТЕЛЯ тоже может быть снят — тот же прыжок в ноль, что и выше.
        // Берём один раз и проверяем: ниже он используется ещё раз, в ветке ближней камеры.
        IKinematics* parent_kinematics = H_Parent()->Visual() ? smart_cast<IKinematics*>(H_Parent()->Visual()) : nullptr;

        if (actor && parent_kinematics)
            parent_kinematics->CalculateBones_Invalidate();

        if (H_Parent()->XFORM().c.distance_to_sqr(Device.vCameraPosition) < _sqr(OPTIMIZATION_DISTANCE) ||
            GameID() != eGameIDSingle)
        {
            // near camera
            if (parent_kinematics)
                parent_kinematics->CalculateBones();
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

            // [DA_PORT] Огонёк СВОЕГО источника света в виде от первого лица не рисуем.
            //
            // Симптом: «при анимациях взаимодействия (еда и прочее) виден огонь зажигалки».
            //
            // Почему так выходит. Огонёк (glow) — это МИРОВОЙ спрайт на направляющей кости
            // предмета, а не эффект модели в руках: ставится он в M.c, где M = XFORM() *
            // BI.mTransform. У актёра эта кость на уровне груди, то есть спрайт висит прямо перед
            // камерой. В обычной игре его закрывает модель предмета в руках — а на анимации еды она
            // убирается (SetWeaponHideState), и огонёк оголяется.
            //
            // Поэтому чинить надо не анимацию, а само рисование: свой огонёк от первого лица не
            // виден НИКОГДА по замыслу, он для чужого взгляда и для вида от третьего лица. Свет при
            // этом остаётся — гасится ровно спрайт.
            //
            // Ставится здесь, а не в DaUpdateLightState: та зовётся по смене состояния, а вид
            // камеры меняется мимо неё.
            //
            // ⚠️ Выключатель на случай, если у модели в руках своего пламени нет и этот спрайт и был
            // видимым огнём: da_torch_glow_fp 1 возвращает прежнее поведение.
            {
                extern int ps_da_torch_glow_fp;
                const bool da_first_person = actor->HUDview();
                glow_render->set_active(m_switched_on2 || m_switched_on
                    ? (ps_da_torch_glow_fp || !da_first_person)
                    : false);
            }

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
