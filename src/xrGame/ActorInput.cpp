#include "StdAfx.h"
#include "Actor.h"
#include "Torch.h"
#include "trade.h"
#include "xrEngine/CameraBase.h"

#ifdef DEBUG
#include "PHDebug.h"
#endif

#include "Hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "UIGameSP.h"
// [DA_PORT] нужен для перехвата обыска тела скриптом
#include "xrScriptEngine/script_engine.hpp"
#include "Inventory.h"
#include "Level.h"
#include "game_cl_base.h"
#include "xrEngine/xr_level_controller.h"
#include "ActorCondition.h"
#include "actor_input_handler.h"
#include "xrUICore/Static/UIStatic.h"
#include "ui/UIActorMenu.h"
#include "ui/UIDragDropReferenceList.h"
#include "CharacterPhysicsSupport.h"
#include "InventoryBox.h"
#include "player_hud.h"
#include "xrEngine/xr_input.h"
#include "flare.h"
#include "CustomDetector.h"
#include "clsid_game.h"
#include "HUDManager.h"
#include "Weapon.h"
#include "GamePersistent.h"
#include "script_game_object.h" // прибор da_use_log читает заслон дверей ИИ
#include "doors.h" // doors_door.h только объявляет door_state, значения лежат здесь
#include "doors_door.h"

bool g_bAutoClearCrouch = true;

void CActor::IR_OnKeyboardPress(int cmd)
{
    if (GamePersistent().GetHudTuner().is_active())
        return;

    // [DA_PORT] item placement-preview mode: fire = confirm, use/reload = cancel; swallow the rest so
    // the player positions the ghost without firing/using anything else.
    if (m_item_placement_active)
    {
        if (cmd == kWPN_FIRE)
            ConfirmItemPlacement();
        else if (cmd == kUSE || cmd == kWPN_RELOAD)
            CancelItemPlacement();
        return;
    }

    if (Remote())
        return;

    if (IsTalking())
        return;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;

    if (load_screen_renderer.IsActive())
        return;

    bool quickSlot = false;
    switch (cmd)
    {
    case kWPN_FIRE:
    {
        if ((mstate_wishful & mcLookout) && !IsGameTypeSingle())
            return;

        u16 slot = inventory().GetActiveSlot();
        if (inventory().ActiveItem() && (slot == INV_SLOT_3 || slot == INV_SLOT_2))
            mstate_wishful &= ~mcSprint;
        //-----------------------------
        if (OnServer())
        {
            NET_Packet P;
            P.w_begin(M_PLAYER_FIRE);
            P.w_u16(ID());
            u_EventSend(P);
        }
    }
    break;
    }

    if (!g_Alive())
        return;

    if (m_holder && kUSE != cmd)
    {
        m_holder->OnKeyboardPress(cmd);
        if (m_holder->allowWeapon() && inventory().Action((u16)cmd, CMD_START))
            return;
        return;
    }
    else if (inventory().Action((u16)cmd, CMD_START))
        return;

#ifndef MASTER_GOLD
    if (psActorFlags.test(AF_NO_CLIP))
    {
        NoClipFly(cmd);
        return;
    }
#endif

    switch (cmd)
    {
    case kJUMP: { mstate_wishful |= mcJump;
    }
    break;
    case kSPRINT_TOGGLE: { mstate_wishful ^= mcSprint;
    }
    break;
    case kCROUCH_TOGGLE:
    case kCROUCH:
    {
        if (psActorFlags.test(AF_CROUCH_TOGGLE) || cmd == kCROUCH_TOGGLE)
            g_bAutoClearCrouch = !g_bAutoClearCrouch;
        if (!g_bAutoClearCrouch)
            mstate_wishful ^= mcCrouch;
    }
    break;

    // [DA_PORT] Переключение камеры с клавиш отключено — как у автора (`//case kCAM_1: ...` в
    // `_engine_diff/da_alpha/src_/xrGame/ActorInput.cpp:107-109`). Мод рисуется от первого лица: вид
    // от третьего показывает то, подо что не делались ни модель актёра, ни оверлеи интерфейса, а
    // свободная камера отвязывает её от игрока прямо посреди боя. Клавиши `cam_1..3` по умолчанию ни
    // к чему не привязаны, но `bind` их достаёт — и тогда это выглядит как поломка, а не как режим.
    // В отладочном полёте (NoClipFly) переключение осталось: там оно и нужно, у автора тоже.
    case kCAM_1:
    case kCAM_2:
    case kCAM_3: break;

    case kNIGHT_VISION:
    {
        // [DA_PORT] Night vision is owned by the Lua script: itms_manager.script on_key_press catches
        // kNIGHT_VISION and calls torch:enable_night_vision(not torch:night_vision_enabled()), which
        // also gates it on the wpn_upd battery charge. The stock CoP C++ handler below ALSO toggled
        // the torch's night vision on this same key, so BOTH fired per press and cancelled out — NV
        // switched on then immediately back off (confirmed by the paired on=1/on=0 SwitchNightVision
        // calls in one press), so it never stayed on. Defer entirely to the script (DA has no C++ NV
        // key handling). SwitchNightVision() itself is kept for the debug no-clip path / other callers.
        break;
    }
    case kTORCH:
    {
        // [DA_PORT] Фонарь принадлежит скрипту — ровно как ночное зрение выше, и по той же причине.
        //
        // `itms_manager.script:on_key_press` ловит kTORCH сам: проверяет информацию
        // `enable_device_torch` (без неё фонаря у игрока ещё нет), заряд `wpn_upd`, играет щелчок и
        // выставляет АБСОЛЮТНОЕ состояние через `enable_torch2`. Штатный обработчик CoP, стоявший
        // здесь, дергал `Switch2()` — ПЕРЕКЛЮЧАТЕЛЬ. На одно нажатие приходило два действия: скрипт
        // ставил «включено», движок тут же переворачивал в «выключено». На экране не менялось ничего,
        // и включать приходилось со второго раза.
        //
        // Видно это было и без фонаря в инвентаре: скрипт в таком состоянии не делает ничего, а
        // движок всё равно зажигал налобный — ему хватало скрытого `device_torch` в слоте.
        //
        // Автор мода отключил ровно эти две строки: `//SwitchTorch();` — его собственные исходники,
        // `_engine_diff/da_alpha/src_/xrGame/ActorInput.cpp:115-119`. В отладочном полёте (NoClipFly)
        // вызов у него остался — оставлен и у нас.
        break;
    }

    case kDETECTOR:
    {
        PIItem det_active = inventory().ItemFromSlot(DETECTOR_SLOT);
        if (det_active)
        {
            CCustomDetector* det = smart_cast<CCustomDetector*>(det_active);
            det->ToggleDetector(g_player_hud->attached_item(0) != NULL);
            return;
        }
    }
    break;
    /*
        case kFLARE:{
                PIItem fl_active = inventory().ItemFromSlot(FLARE_SLOT);
                if(fl_active)
                {
                    CFlare* fl			= smart_cast<CFlare*>(fl_active);
                    fl->DropFlare		();
                    return				;
                }

                PIItem fli = inventory().Get(CLSID_DEVICE_FLARE, true);
                if(!fli)			return;

                CFlare* fl			= smart_cast<CFlare*>(fli);

                if(inventory().Slot(fl))
                    fl->ActivateFlare	();
            }break;
    */
    case kUSE: ActorUse(); break;
    case kDROP:
        b_DropActivated = TRUE;
        f_DropPower = 0;
        break;
    case kNEXT_SLOT: { OnNextWeaponSlot();
    }
    break;
    case kPREV_SLOT: { OnPrevWeaponSlot();
    }
    break;

    case kQUICK_USE_1:
    case kQUICK_USE_2:
    case kQUICK_USE_3:
    case kQUICK_USE_4:
        quickSlot = true;
        [[fallthrough]];
    case kUSE_BANDAGE:
    case kUSE_MEDKIT:
    {
        PIItem itm = nullptr;
        if (quickSlot)
        {
            if (!CurrentGameUI()->GetActorMenu().m_pQuickSlot)
                break;
            const shared_str& item_name = g_quick_use_slots[cmd - kQUICK_USE_1];
            if (item_name.size())
                itm = inventory().GetAny(item_name.c_str());
        }
        else
            itm = inventory().item((cmd == kUSE_BANDAGE) ? CLSID_IITEM_BANDAGE : CLSID_IITEM_MEDKIT);

        if (itm)
        {
            if (IsGameTypeSingle())
                inventory().Eat(itm);
            else
                inventory().ClientEat(itm);

            const bool compat = ClearSkyMode || ShadowOfChernobylMode;
            StaticDrawableWrapper* _s = CurrentGameUI()->AddCustomStatic("item_used", true, compat ? 3.0f : -1.0f);

            string1024 str;
            strconcat(sizeof(str), str, StringTable().translate("st_item_used").c_str(), ": ", itm->NameItem());
            _s->wnd()->SetText(str);
            if (quickSlot)
                CurrentGameUI()->GetActorMenu().m_pQuickSlot->ReloadReferences(this);
        }
    }
    break;
    }
}

void CActor::IR_OnMouseWheel(float x, float y)
{
    if (GamePersistent().GetHudTuner().is_active())
        return;

    if (inventory().Action((y > 0) ? (u16)kWPN_ZOOM_INC : (u16)kWPN_ZOOM_DEC, CMD_START))
        return;

    if (y > 0)
        OnNextWeaponSlot();
    else
        OnPrevWeaponSlot();
}

void CActor::IR_OnKeyboardRelease(int cmd)
{
    if (GamePersistent().GetHudTuner().is_active())
        return;

    if (Remote())
        return;

    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;

    if (g_Alive())
    {
        if (cmd == kUSE && !psActorFlags.test(AF_MULTI_ITEM_PICKUP))
            m_bPickupMode = false;

        if (m_holder)
        {
            m_holder->OnKeyboardRelease(cmd);

            if (m_holder->allowWeapon() && inventory().Action((u16)cmd, CMD_STOP))
                return;
            return;
        }
        else if (inventory().Action((u16)cmd, CMD_STOP))
            return;

        switch (cmd)
        {
        case kJUMP: mstate_wishful &= ~mcJump; break;
        case kDROP:
            if (GAME_PHASE_INPROGRESS == Game().Phase())
                g_PerformDrop();
            break;
        case kCROUCH:
            if (!psActorFlags.test(AF_CROUCH_TOGGLE))
                g_bAutoClearCrouch = true;
            break;
        }
    }
}

void CActor::IR_OnKeyboardHold(int cmd)
{
    // [DA_PORT] Отладочный пульс отсюда снят: он показал, что команда доходит до движения нормально,
    // и тем самым увёл поиск на уровень выше — к стеку приёмников ввода, где и нашлась застрявшая
    // консоль (см. iCapture в xr_input.cpp). Печатался на каждое удержание клавиши у стоящего актёра,
    // поэтому в рабочей сборке ему не место.
    if (GamePersistent().GetHudTuner().is_active())
        return;

    if (Remote() || !g_Alive())
        return;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;
    if (IsTalking())
        return;

    if (m_holder)
    {
        m_holder->OnKeyboardHold(cmd);
        return;
    }

#ifndef MASTER_GOLD
    if (psActorFlags.test(AF_NO_CLIP) &&
        (cmd == kFWD || cmd == kBACK || cmd == kL_STRAFE || cmd == kR_STRAFE || cmd == kJUMP || cmd == kCROUCH))
    {
        NoClipFly(cmd);
        return;
    }
#endif

    float LookFactor = GetLookFactor();
    switch (cmd)
    {
    case kUP:
    case kDOWN: cam_Active()->Move((cmd == kUP) ? kDOWN : kUP, 0, LookFactor); break;
    case kCAM_ZOOM_IN:
    case kCAM_ZOOM_OUT: cam_Active()->Move(cmd); break;
    case kLEFT:
    case kRIGHT:
        if (eacFreeLook != cam_active)
            cam_Active()->Move(cmd, 0, LookFactor);
        break;

    case kACCEL: mstate_wishful |= mcAccel; break;
    case kL_STRAFE:
    {
        mstate_wishful &= ~mcSprint;
        mstate_wishful |= mcLStrafe;
        break;
    }
    case kR_STRAFE:
    {
        mstate_wishful &= ~mcSprint;
        mstate_wishful |= mcRStrafe;
        break;
    }
    case kL_LOOKOUT: mstate_wishful |= mcLLookout; break;
    case kR_LOOKOUT: mstate_wishful |= mcRLookout; break;
    case kFWD: mstate_wishful |= mcFwd; break;
    case kBACK: mstate_wishful |= mcBack; break;
    case kCROUCH:
    {
        if (!psActorFlags.test(AF_CROUCH_TOGGLE))
            mstate_wishful |= mcCrouch;
    }
    break;
    }
}

void CActor::OnAxisMove(float x, float y, float scaleX, float scaleY, bool invertX, bool invertY)
{
    if (!fis_zero(x))
    {
        const float d = (invertX ? -1.f : 1.f) * x * scaleX;
        cam_Active()->Move((d < 0) ? kLEFT : kRIGHT, _abs(d));
    }
    if (!fis_zero(y))
    {
        const float d = (invertY ? -1.f : 1.f) * y * scaleY * 3.f / 4.f;
        cam_Active()->Move((d > 0) ? kUP : kDOWN, _abs(d));
    }
}

void CActor::IR_OnMouseMove(int dx, int dy)
{
    if (GamePersistent().GetHudTuner().is_active())
        return;

    PIItem iitem = inventory().ActiveItem();
    if (iitem && iitem->cast_hud_item())
        iitem->cast_hud_item()->ResetSubStateTime();

    if (Remote())
        return;

    if (m_holder)
    {
        m_holder->OnMouseMove(dx, dy);
        return;
    }

    const float LookFactor = GetLookFactor();
    const float scale = (cam_Active()->f_fov / g_fov) * psMouseSens * psMouseSensScale / 50.f / LookFactor;
    OnAxisMove(float(dx), float(dy), scale, scale, false, psMouseInvert.test(1));
}

void CActor::IR_OnControllerPress(int cmd, const ControllerAxisState& state)
{
    switch (cmd)
    {
    case kLOOK_AROUND:
    {
        PIItem iitem = inventory().ActiveItem();
        if (iitem && iitem->cast_hud_item())
            iitem->cast_hud_item()->ResetSubStateTime();
        break;
    }

    case kWPN_FIRE:
    {
        // special case for multiplayer
        IR_OnKeyboardPress(kWPN_FIRE);
        return;
    }
    } // switch (cmd)

    if (Remote() || !g_Alive())
        return;

    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;

    if (m_holder && kUSE != cmd)
    {
        m_holder->OnControllerPress(cmd, state);

        if (m_holder->allowWeapon())
            inventory().Action((u16)cmd, CMD_START);
        return;
    }

    switch (cmd)
    {
    case kLOOK_AROUND:
    {
        const auto fov = cam_Active()->f_fov;
        const float lookFactor = GetLookFactor();

        const float scale  = Device.fTimeDeltaReal * psLookIntensityMin * (fov / g_fov) * psControllerStickSensScale / lookFactor;
        const float scaleX = scale * psControllerStickSensX;
        const float scaleY = scale * psControllerStickSensY;

        OnAxisMove(state.x, state.y, scaleX, scaleY, psControllerFlags.test(ControllerInvertX), psControllerFlags.test(ControllerInvertY));
        break;
    }

    case kMOVE_AROUND:
    {
        if (state.magnitude < 0.5f)
            mstate_wishful |= mcAccel;

        if (!fis_zero(state.x))
        {
            if (state.x > 0.3f)
                mstate_wishful |= mcRStrafe;
            else if (state.x < -0.3f)
                mstate_wishful |= mcLStrafe;
        }
        if (!fis_zero(state.y))
        {
            if (state.y > 0.3f)
                mstate_wishful |= mcBack;
            else if (state.y < -0.3f)
            {
                mstate_wishful |= mcFwd;

                if (state.y < -0.95f)
                    mstate_wishful |= mcSprint;
            }
        }
        break;
    }

    default:
        // bypass as keyboard
        IR_OnKeyboardPress(cmd);
        break;
    } // switch (GetBindedAction(axis))}
}

void CActor::IR_OnControllerRelease(int cmd, const ControllerAxisState& state)
{
    if (Remote() || !g_Alive())
        return;

    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;

    if (m_holder)
    {
        m_holder->OnControllerRelease(cmd, state);

        if (m_holder->allowWeapon())
            inventory().Action((u16)cmd, CMD_STOP);
        return;
    }

    switch (cmd)
    {
    case kLOOK_AROUND:
    case kMOVE_AROUND:
        break;

    default:
        // bypass as keyboard
        IR_OnKeyboardRelease(cmd);
        break;
    }
}

void CActor::IR_OnControllerHold(int cmd, const ControllerAxisState& state)
{
    if (cmd == kLOOK_AROUND)
    {
        PIItem iitem = inventory().ActiveItem();
        if (iitem && iitem->cast_hud_item())
            iitem->cast_hud_item()->ResetSubStateTime();
    }

    if (Remote() || !g_Alive())
        return;
    if (m_input_external_handler && !m_input_external_handler->authorized(cmd))
        return;
    if (IsTalking())
        return;

    if (m_holder)
    {
        m_holder->OnControllerHold(cmd, state);
        return;
    }

    switch (cmd)
    {
    case kLOOK_AROUND:
    {
        static float intensity = psLookIntensityMin;

        const auto fov = cam_Active()->f_fov;
        const float lookFactor = GetLookFactor();

        const float scale  = Device.fTimeDeltaReal * intensity * (fov / g_fov) * psControllerStickSensScale / lookFactor;
        const float scaleX = scale * psControllerStickSensX;
        const float scaleY = scale * psControllerStickSensY;

        if (state.magnitude > 0.99f)
            intensity += psLookIntensityStep;
        else if (state.magnitude > 0.1f)
            intensity -= psLookIntensityStep;
        else
            intensity -= 5 * psLookIntensityStep;
        clamp(intensity, psLookIntensityMin, psLookIntensityMax);

        OnAxisMove(state.x, state.y, scaleX, scaleY, psControllerFlags.test(ControllerInvertX), psControllerFlags.test(ControllerInvertY));
        break;
    }

    case kMOVE_AROUND:
    {
        if (state.magnitude < 0.5f)
            mstate_wishful |= mcAccel;

        if (!fis_zero(state.x))
        {
            if (state.x > 0.3f)
                mstate_wishful |= mcRStrafe;
            else if (state.x < -0.3f)
                mstate_wishful |= mcLStrafe;
        }
        if (!fis_zero(state.y))
        {
            if (state.y > 0.3f)
                mstate_wishful |= mcBack;
            else if (state.y < -0.3f)
            {
                mstate_wishful |= mcFwd;

                if (state.y < -0.95f)
                    mstate_wishful |= mcSprint;
            }
        }
        break;
    }

    default:
        // bypass as keyboard
        IR_OnKeyboardHold(cmd);
        break;
    } // switch (GetBindedAction(axis))
}

void CActor::IR_OnControllerAttitudeChange(Fvector change)
{
    PIItem iitem = inventory().ActiveItem();
    if (iitem && iitem->cast_hud_item())
        iitem->cast_hud_item()->ResetSubStateTime();

    if (Remote())
        return;

    if (!IsZoomAimingMode() && !psActorFlags.test(AF_ALWAYS_USE_ATTITUDE_SENSORS))
        return;

    if (m_holder)
    {
        m_holder->OnControllerAttitudeChange(change);
        return;
    }

    const float LookFactor = GetLookFactor();
    const float scale = (cam_Active()->f_fov / g_fov) * psControllerSensorSens / 50.f / LookFactor;
    OnAxisMove(change.x, change.y, scale, scale, psControllerFlags.test(ControllerInvertX), psControllerFlags.test(ControllerInvertY));
}

#include "HudItem.h"
bool CActor::use_Holder(CHolderCustom* holder)
{
    if (m_holder)
    {
        bool b = false;
        CGameObject* holderGO = smart_cast<CGameObject*>(m_holder);

        if (smart_cast<CCar*>(holderGO))
            b = use_Vehicle(0);
        else if (holderGO->CLS_ID == CLSID_OBJECT_W_STATMGUN || holderGO->CLS_ID==CLSID_OBJECT_HOLDER_ENT)
            b = use_HolderEx(0, false);

        if (inventory().ActiveItem())
        {
            CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
            if (hi)
                hi->OnAnimationEnd(hi->GetState());
        }

        return b;
    }
    else
    {
        bool b = false;
        CGameObject* holderGO = smart_cast<CGameObject*>(holder);
        if (smart_cast<CCar*>(holder))
            b = use_Vehicle(holder);

        if (holderGO->CLS_ID == CLSID_OBJECT_W_STATMGUN || holderGO->CLS_ID==CLSID_OBJECT_HOLDER_ENT)
            b = use_HolderEx(holder, false);

        if (b)
        { // used succesfully
            // switch off torch...
            CAttachableItem* I = CAttachmentOwner::attachedItem(CLSID_DEVICE_TORCH);
            if (I)
            {
                CTorch* torch = smart_cast<CTorch*>(I);
                if (torch)
                    torch->Switch(false);
            }
        }

        if (inventory().ActiveItem())
        {
            CHudItem* hi = smart_cast<CHudItem*>(inventory().ActiveItem());
            if (hi)
                hi->OnAnimationEnd(hi->GetState());
        }

        return b;
    }
}

// [DA_PORT] Прибор «почему не открылось». Включается командой da_use_log.
//
// Повод: «двери часто не открываются на F». Обвинить можно троих, и по игре они неотличимы:
//   1. луч не дотянулся — использование требует дальности МЕНЬШЕ 2 метров от камеры, а луч меряет
//      до точки попадания, и по краю двери она дальше, чем кажется;
//   2. луч попал не в ту дверь — обход берёт ПЕРВЫЙ отданный объект, а не ближайший;
//   3. объект выбран, но отказалась сама дверь — заперта схемой, или её use() ничего не делает.
//
// Прибор печатает всё это одной строкой на нажатие, и первый же случай называет виновника.
int ps_da_use_log = 0;

void CActor::ActorUse()
{
    if (ps_da_use_log)
    {
        collide::rq_result& da_rq = HUD().GetCurrentRayQuery();
        CGameObject* da_hit = da_rq.O ? smart_cast<CGameObject*>(da_rq.O) : nullptr;

        // Заслон дверей ИИ. CGameObject::use молча возвращает false и НЕ зовёт скрипт, если у двери
        // есть хоть один зачинщик из числа NPC: is_blocked сравнивает желаемое состояние двери с
        // запрошенным, и при непустом списке зачинщиков одно из двух сравнений истинно всегда.
        // По игре это неотличимо от «дверь не нажалась»: ни звука, ни сообщения.
        string128 da_gate = "";
        if (m_pUsableObject)
        {
            CScriptGameObject* const da_so = m_pUsableObject->lua_game_object();
            if (da_so && da_so->m_door)
            {
                const bool da_bo = da_so->m_door->is_blocked(doors::door_state_open);
                const bool da_bc = da_so->m_door->is_blocked(doors::door_state_closed);
                xr_sprintf(da_gate, " | дверь ИИ: заслон откр %d закр %d, замок откр %d закр %d%s", da_bo ? 1 : 0,
                    da_bc ? 1 : 0, da_so->m_door->is_locked(doors::door_state_open) ? 1 : 0,
                    da_so->m_door->is_locked(doors::door_state_closed) ? 1 : 0,
                    (da_bo || da_bc) ? " -- ОТКАЗ ДО СКРИПТА" : "");
            }
        }

        Msg("~ [DA_USE] луч: %s [%s], дальность %.2f (предел 2.00), видим %d | внешний обработчик %d "
            "| выбран для использования: %s%s",
            da_hit ? da_hit->cName().c_str() : "(объекта нет, только статика)",
            da_hit ? da_hit->cNameSect().c_str() : "-", da_rq.range,
            da_rq.O ? (da_rq.O->getVisible() ? 1 : 0) : 0, input_external_handler_installed() ? 1 : 0,
            m_pUsableObject ? m_pUsableObject->cName().c_str() : "НИКТО", da_gate);
    }

    if (m_holder)
    {
        CGameObject* GO = smart_cast<CGameObject*>(m_holder);
        NET_Packet P;
        CGameObject::u_EventGen(P, GEG_PLAYER_DETACH_HOLDER, ID());
        P.w_u16(GO->ID());
        CGameObject::u_EventSend(P);
        return;
    }

    if (!psActorFlags.test(AF_MULTI_ITEM_PICKUP))
        m_bPickupMode = true;

    if (character_physics_support()->movement()->PHCapture())
        character_physics_support()->movement()->PHReleaseObject();

    if (m_pUsableObject && NULL == m_pObjectWeLookingAt->cast_inventory_item())
    {
        m_pUsableObject->use(this);
    }

    if (m_pInvBoxWeLookingAt && m_pInvBoxWeLookingAt->nonscript_usable())
    {
        CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
        if (pGameSP) // single
        {
            if (!m_pInvBoxWeLookingAt->closed())
            {
                pGameSP->StartCarBody(this, m_pInvBoxWeLookingAt);
            }
        }
        return;
    }

    if (!m_pUsableObject || m_pUsableObject->nonscript_usable())
    {
        if (m_pPersonWeLookingAt)
        {
            CEntityAlive* pEntityAliveWeLookingAt = smart_cast<CEntityAlive*>(m_pPersonWeLookingAt);

            VERIFY(pEntityAliveWeLookingAt);

            if (IsGameTypeSingle())
            {
                if (pEntityAliveWeLookingAt->g_Alive())
                {
                    TryToTalk();
                }
                else
                {
                    //только если находимся в режиме single
                    CUIGameSP* pGameSP = smart_cast<CUIGameSP*>(CurrentGameUI());
                    if (pGameSP)
                    {
                        if (!m_pPersonWeLookingAt->deadbody_closed_status())
                        {
                            if (pEntityAliveWeLookingAt->AlreadyDie() &&
                                pEntityAliveWeLookingAt->GetLevelDeathTime() + 3000 < Device.dwTimeGlobal)
                            {
                                // 99.9% dead
                                //
                                // [DA_PORT] Перехват намерения обыскать тело.
                                //
                                // Кому это нужно: анимации обыска. Она обязана отыграть ДО того, как
                                // откроется окно, иначе игрок видит сразу содержимое, а сцена
                                // догоняет его поверх меню.
                                //
                                // Скрипт возвращает ложь — окно не открываем, дальше он открывает
                                // его сам по концу сцены, вызовом obj:use(db.actor). Тот путь идёт
                                // через CScriptGameObject::Use и сюда не возвращается, поэтому
                                // повторного перехвата не будет.
                                //
                                // ⚠️ Обыск ЯЩИКОВ намеренно не трогаем: у тела и у ящика разные
                                // точки входа, и анимация задумана про тело.
                                bool allow = true;
                                luabind::functor<bool> before;
                                if (GEnv.ScriptEngine->functor("_G.da_before_body_search", before))
                                {
                                    if (CGameObject* go = smart_cast<CGameObject*>(pEntityAliveWeLookingAt))
                                        allow = before(go->lua_game_object());
                                }

                                if (allow)
                                    pGameSP->StartCarBody(this, m_pPersonWeLookingAt);
                            }
                        }
                    }
                }
            }
        }

        collide::rq_result& RQ = HUD().GetCurrentRayQuery();
        CPhysicsShellHolder* object = smart_cast<CPhysicsShellHolder*>(RQ.O);
        u16 element = BI_NONE;
        if (object)
            element = (u16)RQ.element;

        if (object && Level().IR_GetKeyState(SDL_SCANCODE_LSHIFT))
        {
            bool b_allow = !!pSettings->line_exist("ph_capture_visuals", object->cNameVisual());
            if (b_allow && !character_physics_support()->movement()->PHCapture())
            {
                character_physics_support()->movement()->PHCaptureObject(object, element);
            }
        }
        else
        {
            if (object && smart_cast<CHolderCustom*>(object))
            {
                NET_Packet P;
                CGameObject::u_EventGen(P, GEG_PLAYER_ATTACH_HOLDER, ID());
                P.w_u16(object->ID());
                CGameObject::u_EventSend(P);
                return;
            }
        }
    }
}

BOOL CActor::HUDview() const
{
    return IsFocused() && (cam_active == eacFirstEye) &&
        ((!m_holder) || (m_holder && m_holder->allowWeapon() && m_holder->HUDView()));
}

static u16 SlotsToCheck[] = {
    KNIFE_SLOT, // 0
    INV_SLOT_2, // 1
    INV_SLOT_3, // 2
    GRENADE_SLOT, // 3
    ARTEFACT_SLOT, // 10
};

void CActor::OnNextWeaponSlot()
{
    u32 ActiveSlot = inventory().GetActiveSlot();
    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = inventory().GetPrevActiveSlot();

    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = KNIFE_SLOT;

    u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(SlotsToCheck[0]);

    u32 CurSlot = 0;
    for (; CurSlot < NumSlotsToCheck; CurSlot++)
    {
        if (SlotsToCheck[CurSlot] == ActiveSlot)
            break;
    };

    if (CurSlot >= NumSlotsToCheck)
        return;

    for (u32 i = CurSlot + 1; i < NumSlotsToCheck; i++)
    {
        if (inventory().ItemFromSlot(SlotsToCheck[i]))
        {
            if (SlotsToCheck[i] == ARTEFACT_SLOT)
            {
                IR_OnKeyboardPress(kARTEFACT);
            }
            else
                IR_OnKeyboardPress(kWPN_1 + i);
            return;
        }
    }
};

void CActor::OnPrevWeaponSlot()
{
    u32 ActiveSlot = inventory().GetActiveSlot();
    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = inventory().GetPrevActiveSlot();

    if (ActiveSlot == NO_ACTIVE_SLOT)
        ActiveSlot = KNIFE_SLOT;

    u32 NumSlotsToCheck = sizeof(SlotsToCheck) / sizeof(SlotsToCheck[0]);
    u32 CurSlot = 0;

    for (; CurSlot < NumSlotsToCheck; CurSlot++)
    {
        if (SlotsToCheck[CurSlot] == ActiveSlot)
            break;
    };

    if (CurSlot >= NumSlotsToCheck)
        CurSlot = NumSlotsToCheck - 1; // last in row

    for (s32 i = s32(CurSlot - 1); i >= 0; i--)
    {
        if (inventory().ItemFromSlot(SlotsToCheck[i]))
        {
            if (SlotsToCheck[i] == ARTEFACT_SLOT)
            {
                IR_OnKeyboardPress(kARTEFACT);
            }
            else
                IR_OnKeyboardPress(kWPN_1 + i);
            return;
        }
    }
};

float CActor::GetLookFactor()
{
    if (m_input_external_handler)
        return m_input_external_handler->mouse_scale_factor();

    float factor = 1.f;

    PIItem pItem = inventory().ActiveItem();

    if (pItem)
        factor *= pItem->GetControlInertionFactor();

    VERIFY(!fis_zero(factor));

    return factor;
}

void CActor::set_input_external_handler(CActorInputHandler* handler)
{
    // clear state
    if (handler)
        mstate_wishful = 0;

    // release fire button
    if (handler)
        IR_OnKeyboardRelease(kWPN_FIRE);

    // set handler
    m_input_external_handler = handler;
}

void CActor::SwitchNightVision()
{
    CWeapon* wpn1 = NULL;
    CWeapon* wpn2 = NULL;
    if (inventory().ItemFromSlot(INV_SLOT_2))
        wpn1 = smart_cast<CWeapon*>(inventory().ItemFromSlot(INV_SLOT_2));

    if (inventory().ItemFromSlot(INV_SLOT_3))
        wpn2 = smart_cast<CWeapon*>(inventory().ItemFromSlot(INV_SLOT_3));

    xr_vector<CAttachableItem*> const& all = CAttachmentOwner::attached_objects();
    xr_vector<CAttachableItem*>::const_iterator it = all.begin();
    xr_vector<CAttachableItem*>::const_iterator it_e = all.end();
    for (; it != it_e; ++it)
    {
        CTorch* torch = smart_cast<CTorch*>(*it);
        if (torch)
        {
            if (wpn1 && wpn1->IsZoomed())
                return;

            if (wpn2 && wpn2->IsZoomed())
                return;

            torch->SwitchNightVision();
            return;
        }
    }
}

void CActor::SwitchTorch()
{
    PIItem torch_item = inventory().ItemFromSlot(TORCH_SLOT);
    if (!torch_item)
        return;

    CTorch* torch = smart_cast<CTorch*>(torch_item);
    if (torch)
        torch->Switch2();
}

#ifndef MASTER_GOLD
void CActor::NoClipFly(int cmd)
{
    Fvector cur_pos; // = Position();
    cur_pos.set(0, 0, 0);
    float scale = 1.0f;
    if (pInput->iGetAsyncKeyState(SDL_SCANCODE_LSHIFT))
        scale = 0.25f;
    else if (pInput->iGetAsyncKeyState(SDL_SCANCODE_LALT))
        scale = 4.0f;

    switch (cmd)
    {
    case kJUMP: cur_pos.y += 0.1f; break;
    case kCROUCH: cur_pos.y -= 0.1f; break;
    case kFWD: cur_pos.z += 0.1f; break;
    case kBACK: cur_pos.z -= 0.1f; break;
    case kL_STRAFE: cur_pos.x -= 0.1f; break;
    case kR_STRAFE: cur_pos.x += 0.1f; break;
    case kCAM_1: cam_Set(eacFirstEye); break;
    case kCAM_2: cam_Set(eacLookAt); break;
    case kCAM_3: cam_Set(eacFreeLook); break;
    case kNIGHT_VISION: SwitchNightVision(); break;
    case kTORCH: SwitchTorch(); break;
    case kDETECTOR:
    {
        PIItem det_active = inventory().ItemFromSlot(DETECTOR_SLOT);
        if (det_active)
        {
            CCustomDetector* det = smart_cast<CCustomDetector*>(det_active);
            det->ToggleDetector(g_player_hud->attached_item(0) != NULL);
            return;
        }
    }
    break;
    case kUSE: ActorUse(); break;
    }
    cur_pos.mul(scale);
    Fmatrix mOrient;
    mOrient.rotateY(-(cam_Active()->GetWorldYaw()));
    mOrient.transform_dir(cur_pos);
    Position().add(cur_pos);
    character_physics_support()->movement()->SetPosition(Position());
}
#endif // !MASTER_GOLD
