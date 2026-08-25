#include "StdAfx.h"
// [DA_PORT] нужен для перехвата надевания скриптом
#include "xrScriptEngine/script_engine.hpp"
#include "UIActorMenu.h"
#include "UITradeBar.h"
#include "Inventory.h"
#include "InventoryOwner.h"
#include "UIInventoryUtilities.h"
#include "UIItemInfo.h"
#include "Level.h"
#include "UICellItemFactory.h"
#include "UIDragDropListEx.h"
#include "UIDragDropReferenceList.h"
#include "UICellCustomItems.h"
#include "UIItemInfo.h"
#include "UIOutfitInfo.h"
#include "xrUICore/Windows/UIFrameLineWnd.h"
#include "xrUICore/PropertiesBox/UIPropertiesBox.h"
#include "xrUICore/ListBox/UIListBoxItem.h"
#include "UIMainIngameWnd.h"
#include "UIGameCustom.h"
#include "eatable_item_object.h"
#include "Silencer.h"
#include "Scope.h"
#include "GrenadeLauncher.h"
#include "Artefact.h"
#include "eatable_item.h"
#include "BottleItem.h"
#include "WeaponMagazined.h"
#include "medkit.h"
#include "antirad.h"
#include "CustomOutfit.h"
#include "ActorHelmet.h"
#include "xrUICore/Cursor/UICursor.h"
#include "MPPlayersBag.h"
#include "player_hud.h"
#include "CustomDetector.h"
#include "PDA.h"
#include "actor_defs.h"
#include "script_game_object_impl.h"

void move_item_from_to(u16 from_id, u16 to_id, u16 what_id);

void CUIActorMenu::InitInventoryMode()
{
    ShowIfExist(m_pInventoryWnd, true);
    m_pLists[eInventoryBagList]->Show(true);
    m_pLists[eInventoryBeltList]->Show(true);
    m_pLists[eInventoryOutfitList]->Show(true);
    ShowIfExist(m_pLists[eInventoryHelmetList], true);
    ShowIfExist(m_pLists[eInventoryDetectorList], true);
    ShowIfExist(m_pLists[eInventoryBackpackList], true);
    ShowIfExist(m_pLists[eInventoryKnifeList], true);
    m_pLists[eInventoryPistolList]->Show(true);
    m_pLists[eInventoryAutomaticList]->Show(true);
    ShowIfExist(m_pLists[eInventorySidearmList], true); // [DA_PORT] slot 5 sidearm (pistol/binocular) cell
    ShowIfExist(m_pQuickSlot, true);
    ShowIfExist(m_pLists[eTrashList], true);
    ShowIfExist(m_clock_value, true);

    InitInventoryContents(m_pLists[eInventoryBagList]);

    VERIFY(CurrentGameUI());
    CurrentGameUI()->UIMainIngameWnd->ShowZoneMap(true);
}

void CUIActorMenu::DeInitInventoryMode()
{
    ShowIfExist(m_pInventoryWnd, false);
    ShowIfExist(m_pLists[eTrashList], false);
    ShowIfExist(m_clock_value, false);
    clear_highlight_lists();
}

void CUIActorMenu::SendEvent_ActivateSlot(u16 slot, u16 recipient)
{
    NET_Packet P;
    CGameObject::u_EventGen(P, GEG_PLAYER_ACTIVATE_SLOT, recipient);
    P.w_u16(slot);
    CGameObject::u_EventSend(P);
    clear_highlight_lists();
}

void CUIActorMenu::SendEvent_Item2Slot(PIItem pItem, u16 recipient, u16 slot_id)
{
    if (pItem->parent_id() != recipient)
        move_item_from_to(pItem->parent_id(), recipient, pItem->object_id());

    NET_Packet P;
    CGameObject::u_EventGen(P, GEG_PLAYER_ITEM2SLOT, pItem->object().H_Parent()->ID());
    P.w_u16(pItem->object().ID());
    P.w_u16(slot_id);
    CGameObject::u_EventSend(P);

    PlaySnd(eItemToSlot);
    clear_highlight_lists();
};

void CUIActorMenu::SendEvent_Item2Belt(PIItem pItem, u16 recipient)
{
    if (pItem->parent_id() != recipient)
        move_item_from_to(pItem->parent_id(), recipient, pItem->object_id());

    NET_Packet P;
    CGameObject::u_EventGen(P, GEG_PLAYER_ITEM2BELT, pItem->object().H_Parent()->ID());
    P.w_u16(pItem->object().ID());
    CGameObject::u_EventSend(P);

    PlaySnd(eItemToBelt);
    clear_highlight_lists();
};

void CUIActorMenu::SendEvent_Item2Ruck(PIItem pItem, u16 recipient)
{
    if (pItem->parent_id() != recipient)
        move_item_from_to(pItem->parent_id(), recipient, pItem->object_id());

    NET_Packet P;
    CGameObject::u_EventGen(P, GEG_PLAYER_ITEM2RUCK, pItem->object().H_Parent()->ID());
    P.w_u16(pItem->object().ID());
    CGameObject::u_EventSend(P);

    PlaySnd(eItemToRuck);
    clear_highlight_lists();
};

void CUIActorMenu::SendEvent_Item_Eat(PIItem pItem, u16 recipient)
{
    if (pItem->parent_id() != recipient)
        move_item_from_to(pItem->parent_id(), recipient, pItem->object_id());

    NET_Packet P;
    CGameObject::u_EventGen(P, GEG_PLAYER_ITEM_EAT, recipient);
    P.w_u16(pItem->object().ID());
    CGameObject::u_EventSend(P);
    clear_highlight_lists();
};

void CUIActorMenu::SendEvent_Item_Drop(PIItem pItem, u16 recipient)
{
    R_ASSERT(pItem->parent_id() == recipient);
    if (!IsGameTypeSingle())
        pItem->DenyTrade();
    // pItem->SetDropManual			(TRUE);
    NET_Packet P;
    pItem->object().u_EventGen(P, GE_OWNERSHIP_REJECT, pItem->parent_id());
    P.w_u16(pItem->object().ID());
    pItem->object().u_EventSend(P);
    PlaySnd(eDropItem);
    clear_highlight_lists();
}

void CUIActorMenu::DropAllCurrentItem()
{
    if (CurrentIItem() && !CurrentIItem()->IsQuestItem())
    {
        u32 const cnt = CurrentItem()->ChildsCount();
        for (u32 i = 0; i < cnt; ++i)
        {
            CUICellItem* itm = CurrentItem()->PopChild(NULL);
            PIItem iitm = (PIItem)itm->m_pData;
            SendEvent_Item_Drop(iitm, m_pActorInvOwner->object_id());
        }

        SendEvent_Item_Drop(CurrentIItem(), m_pActorInvOwner->object_id());
    }
    SetCurrentItem(NULL);
}

bool CUIActorMenu::DropAllItemsFromRuck(bool quest_force)
{
    if (!IsShown() || !m_pLists[eInventoryBagList] || m_currMenuMode != mmInventory)
    {
        return false;
    }

    u32 const ci_count = m_pLists[eInventoryBagList]->ItemsCount();
    for (u32 i = 0; i < ci_count; ++i)
    {
        CUICellItem* ci = m_pLists[eInventoryBagList]->GetItemIdx(i);
        VERIFY(ci);
        PIItem item = (PIItem)ci->m_pData;
        VERIFY(item);

        if (!quest_force && item->IsQuestItem())
        {
            continue;
        }

        u32 const cnt = ci->ChildsCount();
        for (u32 j = 0; j < cnt; ++j)
        {
            CUICellItem* child_ci = ci->PopChild(NULL);
            PIItem child_item = (PIItem)child_ci->m_pData;
            SendEvent_Item_Drop(child_item, m_pActorInvOwner->object_id());
        }
        SendEvent_Item_Drop(item, m_pActorInvOwner->object_id());
    }

    SetCurrentItem(NULL);
    return true;
}

bool FindItemInList(CUIDragDropListEx* lst, PIItem pItem, CUICellItem*& ci_res)
{
    u32 count = lst->ItemsCount();
    for (u32 i = 0; i < count; ++i)
    {
        CUICellItem* ci = lst->GetItemIdx(i);
        for (u32 j = 0; j < ci->ChildsCount(); ++j)
        {
            CUIInventoryCellItem* ici = smart_cast<CUIInventoryCellItem*>(ci->Child(j));
            if (ici->object() == pItem)
            {
                ci_res = ici;
                // lst->RemoveItem(ci,false);
                return true;
            }
        }

        CUIInventoryCellItem* ici = smart_cast<CUIInventoryCellItem*>(ci);
        if (ici->object() == pItem)
        {
            ci_res = ci;
            // lst->RemoveItem(ci,false);
            return true;
        }
    }
    return false;
}

bool RemoveItemFromList(CUIDragDropListEx* lst, PIItem pItem)
{ // fixme
    CUICellItem* ci = NULL;
    if (FindItemInList(lst, pItem, ci))
    {
        R_ASSERT(ci);

        CUICellItem* dying_cell = lst->RemoveItem(ci, false);
        xr_delete(dying_cell);

        return true;
    }
    else
        return false;
}

void CUIActorMenu::OnInventoryAction(PIItem pItem, u16 action_type)
{
    CUIDragDropListEx* all_lists[] =
    {
        m_pLists[eInventoryBeltList], m_pLists[eInventoryKnifeList], m_pLists[eInventoryPistolList], m_pLists[eInventoryAutomaticList],
        m_pLists[eInventoryBackpackList], m_pLists[eInventoryOutfitList], m_pLists[eInventoryHelmetList], m_pLists[eInventoryDetectorList],
        m_pLists[eInventorySidearmList], // [DA_PORT] slot 5 sidearm cell
        m_pLists[eInventoryBagList], m_pLists[eTradeActorBagList], m_pLists[eTradeActorList]
    };

    switch (action_type)
    {
    case GE_TRADE_BUY:
    case GE_OWNERSHIP_TAKE:
    {
        bool b_already = false;

        CUIDragDropListEx* lst_to_add = nullptr;
        SInvItemPlace pl = pItem->m_ItemCurrPlace;
        if (pItem->BaseSlot() == GRENADE_SLOT)
        {
            pl.type = eItemPlaceRuck;
            pl.slot_id = GRENADE_SLOT;
        }
        // [DA_PORT] Печать на каждую раскладку предмета: под MASTER_GOLD, которого у нас нет, и вдобавок
        // структура SInvItemPlace передавалась как %d — печаталось не место, а мусор. Оставлена отладочной.
#ifdef DEBUG
        Msg("item place [%d]", pl.type);
#endif

        if (pl.type == eItemPlaceSlot)
            lst_to_add = GetSlotList(pl.slot_id);
        else if (pl.type == eItemPlaceBelt)
            lst_to_add = GetListByType(iActorBelt);
        else /* if(pl.type==eItemPlaceRuck)*/
        {
            if (pItem->parent_id() == m_pActorInvOwner->object_id())
                lst_to_add = GetListByType(iActorBag);
            else
                lst_to_add = GetListByType(iDeadBodyBag);
        }

        for (auto& curr : all_lists)
        {
            if (!curr) // m_pLists[eInventoryHelmetList] can be nullptr
                continue;
            CUICellItem* ci = nullptr;

            if (FindItemInList(curr, pItem, ci))
            {
                if (lst_to_add != curr)
                {
                    RemoveItemFromList(curr, pItem);
                }
                else
                {
                    b_already = true;
                }
                // break;
            }
        }
        CUICellItem* ci = nullptr;
        if (GetMenuMode() == mmDeadBodySearch && FindItemInList(m_pLists[eSearchLootBagList], pItem, ci))
            break;

        if (!b_already)
        {
            if (lst_to_add)
            {
                CUICellItem* itm = create_cell_item(pItem);
                lst_to_add->SetItem(itm);
            }
        }
        if (m_pActorInvOwner && m_pQuickSlot)
            m_pQuickSlot->ReloadReferences(m_pActorInvOwner);
    }
    break;
    case GE_TRADE_SELL:
    case GE_OWNERSHIP_REJECT:
    {
        if (CUIDragDropListEx::m_drag_item)
        {
            CUIInventoryCellItem* ici = smart_cast<CUIInventoryCellItem*>(CUIDragDropListEx::m_drag_item->ParentItem());
            R_ASSERT(ici);
            if (ici->object() == pItem)
            {
                CUIDragDropListEx* _drag_owner = ici->OwnerList();
                _drag_owner->DestroyDragItem();
            }
        }

        for (auto& curr : all_lists)
        {
            if (!curr) // m_pLists[eInventoryHelmetList] can be nullptr
                continue;

            if (RemoveItemFromList(curr, pItem))
            {
#ifdef DEBUG
                Msg("all ok. item [%d] removed from list", pItem->object_id());
#endif
                break;
            }
        }

        if (m_pActorInvOwner && m_pQuickSlot)
            m_pQuickSlot->ReloadReferences(m_pActorInvOwner);
    }
    break;
    }
    UpdateItemsPlace();
}
void CUIActorMenu::AttachAddon(PIItem item_to_upgrade)
{
    PlaySnd(eAttachAddon);
    R_ASSERT(item_to_upgrade);
    if (OnClient())
    {
        NET_Packet P;
        CGameObject::u_EventGen(P, GE_ADDON_ATTACH, item_to_upgrade->object().ID());
        P.w_u16(CurrentIItem()->object().ID());
        CGameObject::u_EventSend(P);
    };

    item_to_upgrade->Attach(CurrentIItem(), true);

    SetCurrentItem(NULL);
}

void CUIActorMenu::DetachAddon(LPCSTR addon_name, PIItem itm)
{
    PlaySnd(eDetachAddon);
    if (OnClient())
    {
        NET_Packet P;
        if (itm == NULL)
            CGameObject::u_EventGen(P, GE_ADDON_DETACH, CurrentIItem()->object().ID());
        else
            CGameObject::u_EventGen(P, GE_ADDON_DETACH, itm->object().ID());

        P.w_stringZ(addon_name);
        CGameObject::u_EventSend(P);
        return;
    }
    // [DA_PORT] Прибор на САМО снятие обвеса.
    //
    // По подсказкам различить не удалось: «два разных ствола в инвентаре» и «мод подменил секцию»
    // выглядят в логе одинаково -- меняются и номер, и секция. Здесь событие названо прямо: какой
    // ствол, какой номер, какая маска поломок ДО и ПОСЛЕ снятия. Если номер прежний, а маска стала
    // нулём -- её стирает снятие. Если номер сменился -- оружие подменили, и чинить надо перенос.
    PIItem target = (itm == NULL) ? CurrentIItem() : itm;
    CWeapon* w_before = smart_cast<CWeapon*>(target);
    const u16 id_before = w_before ? w_before->ID() : u16(-1);
    const u32 mask_before = w_before ? w_before->m_weapon_condition_type : 0;
    shared_str sect_before = w_before ? w_before->cNameSect() : shared_str("(не оружие)");

    if (itm == NULL)
        CurrentIItem()->Detach(addon_name, true);
    else
        itm->Detach(addon_name, true);

    if (w_before)
    {
        Msg("~ [DA_WPN] снят обвес [%s] с %s id[%u]: маска %u -> %u", addon_name, sect_before.c_str(),
            u32(id_before), mask_before, w_before->m_weapon_condition_type);
    }
}

// [DA_PORT] Dead Air equips items to small "utility" slot cells whose authored capacity (rows_num x
// cols_num, e.g. 2x1) can be smaller than the item's inventory grid: the slot-14 binocular/grenade cell
// holds a binocular (a wpn, grid can exceed 2x1) and the slot-5 sidearm cell holds pistols. Placing an
// oversized item makes CUICellContainer::FindFreeCell R_ASSERT "no free room" -> hard crash. Grow the cell
// to fit BEFORE placing. Use SetStartCellsCapacity (grows m_orig_cell_capacity too) so a later Compact()/
// ClearAll() -> ResetCellsCapacity() keeps the grown size instead of snapping back to the tiny original and
// asserting on the retry (that reset was why plain SetCellsCapacity still crashed). Account for a vertical
// list swapping x/y in FindFreeCell. Returns true if the (empty) cell now holds room for the item.
static bool DA_GrowSlotCellToFit(CUIDragDropListEx* list, CUICellItem* ci)
{
    if (!list || !ci)
        return false;
    Ivector2 need = ci->GetGridSize();
    if (list->GetVerticalPlacement())
        std::swap(need.x, need.y);
    const Ivector2 cap = list->CellsCapacity();
    if (cap.x < need.x || cap.y < need.y)
    {
        const Ivector2 grown{ _max(cap.x, need.x), _max(cap.y, need.y) };
        list->SetStartCellsCapacity(grown);
    }
    return list->CanSetItem(ci);
}

void CUIActorMenu::InitCellForSlot(u16 slot_idx)
{
    //VERIFY(KNIFE_SLOT <= slot_idx && slot_idx <= LAST_SLOT);
    PIItem item = m_pActorInvOwner->inventory().ItemFromSlot(slot_idx);
    if (!item)
    {
        return;
    }

    CUIDragDropListEx* curr_list = GetSlotList(slot_idx);
    if (!curr_list)
        return;
    CUICellItem* cell_item = create_cell_item(item);
    // [DA_PORT] A Dead Air slot cell can be authored smaller than the item it must hold (the slot-14
    // binocular cell reused for hand grenades: a grenade's inv grid can exceed the binocular cell).
    // That made SetItem() assert "no free room" (UIDragDropListEx.cpp:566) -> hard crash on menu open.
    // Grow the cell's grid capacity to fit the item before placing it, so the item shows instead.
    if (DA_GrowSlotCellToFit(curr_list, cell_item))
        curr_list->SetItem(cell_item);
    else
    {
        // Still won't fit (cell somehow occupied / unexpected grid): don't SetItem -> it would R_ASSERT and
        // crash. Show the item in the bag instead so nothing is lost and the menu opens.
        m_pLists[eInventoryBagList]->SetItem(cell_item);
    }
    if (m_currMenuMode == mmTrade && m_pPartnerInvOwner)
        ColorizeItem(cell_item, !CanMoveToPartner(item));

    // CCustomOutfit* outfit = smart_cast<CCustomOutfit*>(item);
    // if(outfit)
    //	outfit->ReloadBonesProtection();

    // CHelmet* helmet = smart_cast<CHelmet*>(item);
    // if(helmet)
    //	helmet->ReloadBonesProtection();
}

void CUIActorMenu::InitInventoryContents(CUIDragDropListEx* pBagList, bool onlyBagList /*= false*/)
{
    ClearAllLists();
    m_pMouseCapturer = NULL;
    m_UIPropertiesBox->Hide();
    SetCurrentItem(NULL);

    CUIDragDropListEx* curr_list = pBagList;

    TIItemContainer ruck_list;
    if (onlyBagList)
        m_pActorInvOwner->inventory().AddAvailableItems(ruck_list, true);
    else
        ruck_list = m_pActorInvOwner->inventory().m_ruck;

    std::sort(ruck_list.begin(), ruck_list.end(), InventoryUtilities::GreaterRoomInRuck);

    for (PIItem item : ruck_list)
    {
        CMPPlayersBag* bag = smart_cast<CMPPlayersBag*>(&item->object());
        if (bag)
            continue;

        CUICellItem* itm = create_cell_item(item);
        curr_list->SetItem(itm);
        if (m_currMenuMode == mmTrade && m_pPartnerInvOwner)
            ColorizeItem(itm, !CanMoveToPartner(item));

        // CCustomOutfit* outfit = smart_cast<CCustomOutfit*>(item);
        // if(outfit)
        //	outfit->ReloadBonesProtection();

        // CHelmet* helmet = smart_cast<CHelmet*>(item);
        // if(helmet)
        //	helmet->ReloadBonesProtection();
    }

    if (onlyBagList)
        return;

    // Slots
    InitCellForSlot(INV_SLOT_2);
    InitCellForSlot(INV_SLOT_3);
    InitCellForSlot(OUTFIT_SLOT);
    InitCellForSlot(DETECTOR_SLOT);
    InitCellForSlot(GRENADE_SLOT);
    InitCellForSlot(HELMET_SLOT);
    InitCellForSlot(BACKPACK_SLOT);

    //Alundaio
    if (!m_pActorInvOwner->inventory().SlotIsPersistent(KNIFE_SLOT))
        InitCellForSlot(KNIFE_SLOT);
    if (!m_pActorInvOwner->inventory().SlotIsPersistent(BINOCULAR_SLOT))
        InitCellForSlot(BINOCULAR_SLOT);
    if (!m_pActorInvOwner->inventory().SlotIsPersistent(ARTEFACT_SLOT))
        InitCellForSlot(ARTEFACT_SLOT);
    if (!m_pActorInvOwner->inventory().SlotIsPersistent(PDA_SLOT))
        InitCellForSlot(PDA_SLOT);
    //if (!m_pActorInvOwner->inventory().SlotIsPersistent(TORCH_SLOT))
    //    InitCellForSlot(TORCH_SLOT); // Alundaio: TODO find out why this crash when you unequip

    //for custom slots that exist past LAST_SLOT
    for (u16 i = SLOTS_COUNT; i <= m_pActorInvOwner->inventory().LastSlot(); ++i)
    {
        if (!m_pActorInvOwner->inventory().SlotIsPersistent(i))
            InitCellForSlot(i);
    }
    //-Alundaio

    curr_list = m_pLists[eInventoryBeltList];
    TIItemContainer::iterator itb = m_pActorInvOwner->inventory().m_belt.begin();
    TIItemContainer::iterator ite = m_pActorInvOwner->inventory().m_belt.end();
    for (; itb != ite; ++itb)
    {
        CUICellItem* itm = create_cell_item(*itb);
        curr_list->SetItem(itm);
        if (m_currMenuMode == mmTrade && m_pPartnerInvOwner)
            ColorizeItem(itm, !CanMoveToPartner(*itb));
    }

    if (m_pQuickSlot)
        m_pQuickSlot->ReloadReferences(m_pActorInvOwner);
}

bool CUIActorMenu::TryActiveSlot(CUICellItem* itm)
{
    PIItem iitem = (PIItem)itm->m_pData;
    u16 slot = iitem->BaseSlot();

    if (slot == GRENADE_SLOT)
    {
        // [DA_PORT] Dead Air shares engine slot 14 between the hand grenade and the binocular. Double-
        // clicking either from the bag must cleanly REPLACE whatever occupies the slot: move the current
        // occupant back to the bag with the real ToBag(), then equip the new item with ToSlot(). Using the
        // proper move calls (NOT SendEvent_Item2Ruck/Item2Slot, which only fire network events and left the
        // items' CurrPlace out of sync) keeps state consistent, so neither item ends up phantom-"in slot".
        // That stale state was the root of the bugs: context menu offered "unequip" for a bagged item, and
        // the force ToSlot / drag swap bailed at its UI==engine occupant check. TryActiveSlot only gets bag
        // items (the dbl-click dispatch routes a slotted item straight to ToBag via case iActorSlot).
        CUIDragDropListEx* slot_list = GetSlotList(slot);
        if (slot_list && slot_list->ItemsCount())
        {
            CUICellItem* prev_cell = slot_list->GetItemIdx(0);
            if (prev_cell && prev_cell != itm)
                ToBag(prev_cell, false);
        }
        return ToSlot(itm, false, slot);
    }
    if (slot == DETECTOR_SLOT)
    {
    }
    return false;
}

bool CUIActorMenu::ToSlotScript(CScriptGameObject* GO, bool force_place, u16 slot_id)
{
    CInventoryItem* iitem = smart_cast<CInventoryItem*>(GO->object().dcast_GameObject());

    if (!iitem || !m_pActorInvOwner->inventory().InRuck(iitem))
        return false;

    CUIDragDropListEx* invlist = GetListByType(iActorBag);
    CUICellContainer* c = invlist->GetContainer();
    auto& child_list = c->GetChildWndList();

    for (CUIWindow* it : child_list)
    {
        CUICellItem* i = static_cast<CUICellItem*>(it);
        PIItem pitm = static_cast<PIItem>(i->m_pData);
        if (pitm == iitem)
        {
            ToSlot(i, force_place, slot_id);
            return true;
        }
    }
    return false;
}

bool CUIActorMenu::ToSlot(CUICellItem* itm, bool force_place, u16 slot_id)
{
    CUIDragDropListEx* old_owner = itm->OwnerList();
    PIItem iitem = (PIItem)itm->m_pData;

    // item's section, the target slot, its config-derived BaseSlot (config "slot" + 1), whether the
    // inventory accepts it there, and whether a UI drop-list exists for that slot. A null slot-list
    // makes ToSlot return true at the "Alundaio" branch below WITHOUT visually placing the item ->
    // looks like "won't equip". Also flags a BaseSlot != target mismatch (bad/absent config "slot").
    {
        PIItem occ = m_pActorInvOwner->inventory().ItemFromSlot(slot_id);
    }

    // [DA_PORT] Перехват НАДЕВАНИЯ брони, шлема и рюкзака.
    //
    // Кому нужно: анимации надевания. Они обязаны отыграть ДО того, как вещь встанет в слот, иначе
    // игрок сразу видит результат, а сцена догоняет его поверх.
    //
    // ⚠️ Точка выбрана именно здесь, а НЕ в CInventory::Slot. Slot() зовётся и движком: при
    // загрузке сохранения, при спавне, при восстановлении снаряжения. Перехват там срабатывал бы
    // на каждую загрузку и оставлял игрока без брони. ToSlot вызывается только действием игрока -
    // перетаскиванием или двойным щелчком в инвентаре.
    //
    // Скрипт возвращает ложь - вещь не надеваем. Дальше он сам доодевает её по концу сцены через
    // db.actor:move_to_slot, а тот путь идёт через CInventory::Slot и сюда не возвращается.
    if (slot_id == OUTFIT_SLOT || slot_id == HELMET_SLOT || slot_id == BACKPACK_SLOT)
    {
        luabind::functor<bool> before;
        if (GEnv.ScriptEngine->functor("_G.da_before_wear", before))
        {
            if (CGameObject* go = smart_cast<CGameObject*>(iitem))
            {
                if (!before(go->lua_game_object(), (u16)slot_id))
                    return false;
            }
        }
    }

    bool b_own_item = (iitem->parent_id() == m_pActorInvOwner->object_id());
    if (slot_id == HELMET_SLOT)
    {
        CCustomOutfit* pOutfit = m_pActorInvOwner->GetOutfit();
        if (pOutfit && !pOutfit->bIsHelmetAvaliable)
            return false;
    }
    // [DA_PORT] block equipping a backpack (incl. force/drag) when the worn outfit forbids it.
    if (slot_id == BACKPACK_SLOT)
    {
        CCustomOutfit* pOutfit = m_pActorInvOwner->GetOutfit();
        if (pOutfit && !pOutfit->bIsBackpackAvaliable)
            return false;
    }

    if (m_pActorInvOwner->inventory().CanPutInSlot(iitem, slot_id))
    {
        CUIDragDropListEx* new_owner = GetSlotList(slot_id);

        //Alundaio
        if (!new_owner)
            return true;

        /*if (slot_id == GRENADE_SLOT || !new_owner)
        {
            return true; // fake, sorry (((
        }
        else*/ if (slot_id == OUTFIT_SLOT)
        {
            CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(iitem);
            if (pOutfit && !pOutfit->bIsHelmetAvaliable)
            {
                CUIDragDropListEx* helmet_list = GetSlotList(HELMET_SLOT);
                if (helmet_list && helmet_list->ItemsCount() == 1)
                {
                    CUICellItem* helmet_cell = helmet_list->GetItemIdx(0);
                    ToBag(helmet_cell, false);
                }
            }
        }

        [[maybe_unused]] const bool result = !b_own_item || m_pActorInvOwner->inventory().Slot(slot_id, iitem);
        VERIFY(result);

        CUICellItem* i = old_owner->RemoveItem(itm, (old_owner == new_owner));

        while (i->ChildsCount())
        {
            CUICellItem* child = i->PopChild(nullptr);
            old_owner->SetItem(child);
        }

        // [DA_PORT] same slot-cell-too-small guard as InitCellForSlot: grow the target cell to fit the
        // equipped item (binocular/pistol -> small slot-14/slot-5 cell) so SetItem doesn't R_ASSERT. If it
        // still won't fit, put it in the bag instead of crashing.
        if (DA_GrowSlotCellToFit(new_owner, i))
            new_owner->SetItem(i);
        else
        {
            CUIDragDropListEx* bag = GetListByType(iActorBag);
            (bag ? bag : new_owner)->SetItem(i);
        }

        SendEvent_Item2Slot(iitem, m_pActorInvOwner->object_id(), slot_id);

        SendEvent_ActivateSlot(slot_id, m_pActorInvOwner->object_id());

        // ColorizeItem						( itm, false );
        if (slot_id == OUTFIT_SLOT)
        {
            MoveArtefactsToBag();
        }
        return true;
    }
    else
    { // in case slot is busy
        if (!force_place || slot_id == NO_ACTIVE_SLOT)
            return false;

        if (m_pActorInvOwner->inventory().SlotIsPersistent(slot_id) && slot_id != DETECTOR_SLOT)
            return false;

        if (slot_id == KNIFE_SLOT && m_pActorInvOwner->inventory().CanPutInSlot(iitem, KNIFE_SLOT))
            return ToSlot(itm, force_place, KNIFE_SLOT);

        if (slot_id == INV_SLOT_2 && m_pActorInvOwner->inventory().CanPutInSlot(iitem, INV_SLOT_3) && CallOfPripyatMode)
            return ToSlot(itm, force_place, INV_SLOT_3);

        if (slot_id == INV_SLOT_3 && m_pActorInvOwner->inventory().CanPutInSlot(iitem, INV_SLOT_2) && CallOfPripyatMode)
            return ToSlot(itm, force_place, INV_SLOT_2);

        CUIDragDropListEx* slot_list = GetSlotList(slot_id);
        if (!slot_list)
            return false;

        const PIItem _iitem = m_pActorInvOwner->inventory().ItemFromSlot(slot_id);

        CUIDragDropListEx* invlist = GetListByType(iActorBag);
        if (invlist != slot_list)
        {
            if (slot_list->ItemsCount() != 1)
                return false;

            CUICellItem* slot_cell = slot_list->GetItemIdx(0);
            if (!(slot_cell && static_cast<PIItem>(slot_cell->m_pData) == _iitem))
                return false;

            if (ToBag(slot_cell, false) == false)
                return false;
        }
        else
        {
            //Alundaio: Since the player's inventory is being used as a slot we need to search for cell with matching m_pData
            auto container = slot_list->GetContainer();
            auto child_list = container->GetChildWndList();
            for (auto& it : child_list)
            {
                CUICellItem* i = static_cast<CUICellItem*>(it);
                const PIItem pitm = static_cast<PIItem>(i->m_pData);
                if (pitm == _iitem)
                {
                    if (ToBag(i, false))
                        break;

                    return false;
                }
            }

            return ToSlot(itm, false, slot_id);
        }

        bool result = ToSlot(itm, false, slot_id);
        if (b_own_item && result && slot_id == DETECTOR_SLOT)
        {
            // [DA_PORT] Приведение проверяется. Слот детектора занимает не только CCustomDetector:
            // конфиг Dead Air кладёт в один слот предметы РАЗНЫХ классов (в слот шлема, например,
            // и E_HLMET, и D_FLARE), а сюда приходит всё, чей BaseSlot совпал. Для не-детектора
            // smart_cast даёт ноль, и вызов метода по нулю ронял игру прямо на перетаскивании.
            CCustomDetector* det = smart_cast<CCustomDetector*>(iitem);
            if (det)
                det->ToggleDetector(g_player_hud->attached_item(0) != NULL);
        }

        return result;
    }
}

bool CUIActorMenu::ToBag(CUICellItem* itm, bool b_use_cursor_pos)
{
    PIItem iitem = (PIItem)itm->m_pData;

    bool b_own_item = (iitem->parent_id() == m_pActorInvOwner->object_id());

    bool b_already = m_pActorInvOwner->inventory().InRuck(iitem);

    CUIDragDropListEx* old_owner = itm->OwnerList();
    CUIDragDropListEx* new_owner = NULL;
    if (b_use_cursor_pos)
    {
        // [DA_PORT] Перетаскиваемого предмета может не быть, и его список — тоже.
        //
        // `m_drag_item` статический и живёт только во время перетаскивания. Признак того, что это
        // настоящая величина, а не «всегда есть»: в самом интерфейсе его проверяют перед
        // использованием ШЕСТЬ раз, а `BackList()` на ноль сверяет сам автор движка
        // (`UIDragDropListEx.cpp:446`). Голыми остались ровно три места переноса — здесь и два в
        // ToBelt. Стоящий ниже VERIFY в релизе пуст, то есть ноль уходил в `SetItem`.
        //
        // Достижимо и снаружи: ToSlot/ToBelt выведены в Lua (`UIActorMenu_script.cpp:137-138`) и
        // берут b_use_cursor_pos от скрипта — вызов без перетаскивания даёт ровно этот ноль.
        // Скрипты Dead Air их сегодня не зовут, но аддону ничто не мешает.
        //
        // Откат честный: берём тот же список, что и ветка без курсора. Это ровно то поведение,
        // которое получил бы вызывающий, передав b_use_cursor_pos = false.
        new_owner = CUIDragDropListEx::m_drag_item ? CUIDragDropListEx::m_drag_item->BackList() : nullptr;
        if (!new_owner)
            new_owner = GetListByType(iActorBag);
        VERIFY(GetListType(new_owner) == iActorBag);
    }
    else
        new_owner = GetListByType(iActorBag);

    if (m_pActorInvOwner->inventory().CanPutInRuck(iitem) || (b_already && (new_owner != old_owner)))
    {
        [[maybe_unused]] bool result = b_already || !b_own_item || m_pActorInvOwner->inventory().Ruck(iitem);
        VERIFY(result);
        CUICellItem* i = old_owner->RemoveItem(itm, (old_owner == new_owner));
        if (!i)
            return false;

        if (b_use_cursor_pos)
            new_owner->SetItem(i, old_owner->GetDragItemPosition());
        else
            new_owner->SetItem(i);

        if (!b_already || !b_own_item)
            SendEvent_Item2Ruck(iitem, m_pActorInvOwner->object_id());

        if (m_currMenuMode == mmTrade && m_pPartnerInvOwner)
        {
            ColorizeItem(itm, !CanMoveToPartner(iitem));
        }
        return true;
    }
    return false;
}

bool CUIActorMenu::ToBeltScript(CScriptGameObject* GO, bool b_use_cursor_pos)
{
    CInventoryItem* iitem = smart_cast<CInventoryItem*>(GO->object().dcast_GameObject());

    if (!iitem || !m_pActorInvOwner->inventory().InRuck(iitem))
        return false;

    CUIDragDropListEx* invlist = GetListByType(iActorBag);
    CUICellContainer* c = invlist->GetContainer();
    auto child_list = c->GetChildWndList();

    for (CUIWindow* it : child_list)
    {
        CUICellItem* i = static_cast<CUICellItem*>(it);
        PIItem pitm = static_cast<PIItem>(i->m_pData);
        if (pitm == iitem)
        {
            ToBelt(i, b_use_cursor_pos);
            return true;
        }
    }
    return false;
}

bool CUIActorMenu::ToBelt(CUICellItem* itm, bool b_use_cursor_pos)
{
    PIItem iitem = (PIItem)itm->m_pData;
    bool b_own_item = (iitem->parent_id() == m_pActorInvOwner->object_id());

    // [DA_PORT] Dead Air backpacks are artefact-class (belt=true) but must never go on the artefact belt.
    // CanPutInBelt already rejects them, but a drag lands in the "belt busy" swap branch below which then
    // mishandles the empty/oversized cell and crashes. Bail out early for any BACKPACK_SLOT item.
    if (iitem->BaseSlot() == BACKPACK_SLOT)
        return false;

    if (m_pActorInvOwner->inventory().CanPutInBelt(iitem))
    {
        CUIDragDropListEx* old_owner = itm->OwnerList();
        CUIDragDropListEx* new_owner = NULL;
        if (b_use_cursor_pos)
        {
            // [DA_PORT] См. разбор у ToBag: без перетаскивания m_drag_item — ноль. Откат тот же,
            // что у ветки без курсора.
            new_owner = CUIDragDropListEx::m_drag_item ? CUIDragDropListEx::m_drag_item->BackList() : nullptr;
            if (!new_owner)
                new_owner = m_pLists[eInventoryBeltList];
            VERIFY(new_owner == m_pLists[eInventoryBeltList]);
        }
        else
            new_owner = m_pLists[eInventoryBeltList];

        [[maybe_unused]] bool result = !b_own_item || m_pActorInvOwner->inventory().Belt(iitem);
        VERIFY(result);
        CUICellItem* i = old_owner->RemoveItem(itm, (old_owner == new_owner));

        if (b_use_cursor_pos)
            new_owner->SetItem(i, old_owner->GetDragItemPosition());
        else
            new_owner->SetItem(i);

        if (!b_own_item)
            SendEvent_Item2Belt(iitem, m_pActorInvOwner->object_id());

        // ColorizeItem						(itm, false);
        return true;
    }
    else
    { // in case belt slot is busy
        if (!iitem->Belt() || m_pActorInvOwner->inventory().BeltWidth() == 0)
            return false;

        CUIDragDropListEx* belt_list = NULL;
        if (b_use_cursor_pos)
            belt_list = CUIDragDropListEx::m_drag_item ? CUIDragDropListEx::m_drag_item->BackList() : nullptr;
        else
            return false;

        // [DA_PORT] Здесь отката нет и быть не может: вся ветка обмена держится на том, КУДА
        // указывает курсор, а без перетаскивания этого «куда» не существует. Ветка без курсора
        // строкой выше по той же причине просто выходит — выходим и мы.
        if (!belt_list)
            return false;

        Ivector2 belt_cell_pos = belt_list->PickCell(GetUICursor().GetCursorPosition());
        if (belt_cell_pos.x == -1 && belt_cell_pos.y == -1)
            return false;

        //		PIItem	_iitem						= m_pActorInvOwner->inventory().ItemFromSlot(slot_id);

        CUICellItem* slot_cell = belt_list->GetCellAt(belt_cell_pos).m_item;

        // ⚠️ [DA_PORT] ЯЧЕЙКА МОЖЕТ БЫТЬ ПУСТА, и раньше это роняло игру.
        //
        // Сюда попадают, когда `CanPutInBelt` отказал — например, на поясе кончились места. Ветка
        // называется «слот занят» и рассчитана на обмен: вынуть чужой предмет в рюкзак, положить свой.
        // Но игрок целится курсором куда угодно, в том числе в ПУСТУЮ ячейку, и тогда `m_item` — ноль.
        // Дальше `ToBag(nullptr)` первой же строкой берёт `itm->m_pData` и падает: обращение по
        // адресу 0x170, ровно то, что видно в отчётах тестера (три штуки, стек байт в байт).
        //
        // Проверка тут БЫЛА — строкой ниже лежит закомментированный VERIFY авторов движка. Он и
        // работал бы только в отладочной сборке, а у игрока молча пропускал бы ноль дальше.
        //
        // Сценарий из отчёта: набивать патроны в пояс, когда мест уже нет.
        if (!slot_cell)
            return false;

        bool result = ToBag(slot_cell, false);
        if (!result)
            return false;

        result = ToBelt(itm, b_use_cursor_pos);
        return result;
    }
}
CUIDragDropListEx* CUIActorMenu::GetSlotList(u16 slot_idx)
{
    if (slot_idx == NO_ACTIVE_SLOT)
    {
        return NULL;
    }
    switch (slot_idx)
    {
    case KNIFE_SLOT: return m_pLists[eInventoryKnifeList]; break;

    case INV_SLOT_2: return m_pLists[eInventoryPistolList]; break;

    case INV_SLOT_3: return m_pLists[eInventoryAutomaticList]; break;

    case BACKPACK_SLOT: return m_pLists[eInventoryBackpackList]; break;

    case OUTFIT_SLOT: return m_pLists[eInventoryOutfitList]; break;

    case HELMET_SLOT: return m_pLists[eInventoryHelmetList]; break;

    case DETECTOR_SLOT: return m_pLists[eInventoryDetectorList]; break;

    // [DA_PORT] engine slot 14 = Dead Air's binocular/grenade utility slot (GRENADE_SLOT == 14). Route
    // it to the dragdrop_binocular cell so the equipped grenade/binocular is visible. InitCellForSlot
    // grows the cell to fit the item first (the cell is authored for a binocular; a grenade grid can be
    // bigger, which previously asserted "no free room"). Falls back to the bag if the cell is absent.
    case GRENADE_SLOT:
        if (m_pLists[eInventoryBinocularList])
            return m_pLists[eInventoryBinocularList];
        break;

    // [DA_PORT] engine slot 5 = Dead Air's sidearm slot: every pistol and the binocular item live here
    // (config slot 4 -> base 5). Route to the dragdrop_sidearm cell so the equipped pistol/binocular is
    // visible. InitCellForSlot grows the cell to fit the item first. Falls back to the bag if the cell is
    // absent from the loaded actor_menu xml (older/4:3 layouts).
    case BINOCULAR_SLOT:
        if (m_pLists[eInventorySidearmList])
            return m_pLists[eInventorySidearmList];
        if (m_currMenuMode == mmTrade)
            return m_pLists[eTradeActorBagList];
        return m_pLists[eInventoryBagList];

    case PDA_SLOT:
    case TORCH_SLOT:
    case ARTEFACT_SLOT:

    default:
        if (m_currMenuMode == mmTrade)
        {
            return m_pLists[eTradeActorBagList];
        }
        return m_pLists[eInventoryBagList];
        break;
    };
    return NULL;
}

bool CUIActorMenu::TryUseItem(CUICellItem* cell_itm)
{
    if (!cell_itm)
    {
        return false;
    }
    PIItem item = (PIItem)cell_itm->m_pData;

    CBottleItem* pBottleItem = smart_cast<CBottleItem*>(item);
    CMedkit* pMedkit = smart_cast<CMedkit*>(item);
    CAntirad* pAntirad = smart_cast<CAntirad*>(item);
    CEatableItem* pEatableItem = smart_cast<CEatableItem*>(item);

    if (!(pMedkit || pAntirad || pEatableItem || pBottleItem))
    {
        return false;
    }
    if (!item->Useful())
    {
        return false;
    }

    //cell_itm->UpdateConditionProgressBar(); //Alundaio

    u16 recipient = m_pActorInvOwner->object_id();
    if (item->parent_id() != recipient)
    {
        // move_item_from_to	(itm->parent_id(), recipient, itm->object_id());
        cell_itm->OwnerList()->RemoveItem(cell_itm, false);
    }

    SendEvent_Item_Eat(item, recipient);
    PlaySnd(eItemUse);
    //SetCurrentItem(nullptr);
    return true;
}

bool CUIActorMenu::ToQuickSlot(CUICellItem* itm)
{
    PIItem iitem = (PIItem)itm->m_pData;
    CEatableItemObject* eat_item = smart_cast<CEatableItemObject*>(iitem);
    if (!eat_item)
        return false;

    //Alundaio: Fix deep recursion if placing icon greater then col/row set in actor_menu.xml
    Ivector2 iWH = iitem->GetInvGridRect().rb;
    if (iWH.x > 1 || iWH.y > 1)
        return false;
    //Alundaio: END

    if (m_pQuickSlot)
    {
        u8 slot_idx = u8(m_pQuickSlot->PickCell(GetUICursor().GetCursorPosition()).x);
        if (slot_idx == 255)
            return false;

        m_pQuickSlot->SetItem(create_cell_item(iitem), GetUICursor().GetCursorPosition());
        xr_strcpy(ACTOR_DEFS::g_quick_use_slots[slot_idx], iitem->m_section_id.c_str());
    }
    return true;
}

bool CUIActorMenu::OnItemDropped(PIItem itm, CUIDragDropListEx* new_owner, CUIDragDropListEx* old_owner)
{
    CUICellItem* _citem = (new_owner->ItemsCount() == 1) ? new_owner->GetItemIdx(0) : NULL;
    PIItem _iitem = _citem ? (PIItem)_citem->m_pData : NULL;

    if (!_iitem)
        return false;
    if (!_iitem->CanAttach(itm))
        return false;

    if (old_owner != m_pLists[eInventoryBagList])
        return false;

    AttachAddon(_iitem);

    return true;
}

void CUIActorMenu::TryHidePropertiesBox()
{
    if (m_UIPropertiesBox->IsShown())
    {
        m_UIPropertiesBox->Hide();
    }
}

void CUIActorMenu::ActivatePropertiesBox()
{
    TryHidePropertiesBox();
    if (!(m_currMenuMode == mmInventory || m_currMenuMode == mmDeadBodySearch || m_currMenuMode == mmUpgrade || m_currMenuMode == mmTrade))
    {
        return;
    }

    PIItem item = CurrentIItem();
    if (!item)
    {
        return;
    }

    CUICellItem* cell_item = CurrentItem();
    m_UIPropertiesBox->RemoveAll();
    bool b_show = false;

    if (m_currMenuMode == mmInventory || m_currMenuMode == mmDeadBodySearch)
    {
        PropertiesBoxForSlots(item, b_show);
        PropertiesBoxForWeapon(cell_item, item, b_show);
        PropertiesBoxForAddon(item, b_show);
        PropertiesBoxForUsing(item, b_show);
        PropertiesBoxForPlaying(item, b_show);
        if (m_currMenuMode == mmInventory)
            PropertiesBoxForDrop(cell_item, item, b_show);
    }
    // else if ( m_currMenuMode == mmDeadBodySearch )
    //{
    //	PropertiesBoxForUsing( item, b_show );
    //}
    else if (m_currMenuMode == mmUpgrade)
    {
        PropertiesBoxForRepair(item, b_show);
    }
    //Alundaio: Ability to donate item to npc during trade
    else if (m_currMenuMode == mmTrade)
    {
        CUIDragDropListEx* invlist = GetListByType(iActorBag);
        if (invlist->IsOwner(cell_item))
            PropertiesBoxForDonate(item, b_show);
    }
    //-Alundaio
    if (b_show)
    {
        m_UIPropertiesBox->AutoUpdateSize();

        Fvector2 cursor_pos;
        Frect vis_rect;
        GetAbsoluteRect(vis_rect);
        cursor_pos = GetUICursor().GetCursorPosition();
        cursor_pos.sub(vis_rect.lt);
        m_UIPropertiesBox->Show(vis_rect, cursor_pos);
        PlaySnd(eProperties);
    }
}

void CUIActorMenu::PropertiesBoxForSlots(PIItem item, bool& b_show)
{
    CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(item);
    CHelmet* pHelmet = smart_cast<CHelmet*>(item);
    CInventory& inv = m_pActorInvOwner->inventory();

    // Флаг-признак для невлючения пункта контекстного меню: Dreess Outfit, если костюм уже надет
    bool bAlreadyDressed = false;
    u16 cur_slot = item->BaseSlot();

    // [DA_PORT] hide "move to slot" for a backpack the worn outfit forbids (scientific suit) - equipping is
    // blocked anyway, so the menu entry would be a no-op. Mirrors how the helmet dress option is gated.
    bool daBackpackBlocked = false;
    if (cur_slot == BACKPACK_SLOT)
    {
        CCustomOutfit* worn = m_pActorInvOwner->GetOutfit();
        daBackpackBlocked = worn && !worn->bIsBackpackAvaliable;
    }

    if (!pOutfit && !pHelmet && !daBackpackBlocked && cur_slot != NO_ACTIVE_SLOT && !inv.SlotIsPersistent(cur_slot) &&
        inv.ItemFromSlot(cur_slot) != item /*&& inv.CanPutInSlot(item, cur_slot)*/)
    {
        m_UIPropertiesBox->AddItem("st_move_to_slot", NULL, INVENTORY_TO_SLOT_ACTION);
        b_show = true;
    }
    if (item->Belt() && inv.CanPutInBelt(item))
    {
        m_UIPropertiesBox->AddItem("st_move_on_belt", NULL, INVENTORY_TO_BELT_ACTION);
        b_show = true;
    }

    // [DA_PORT] Only offer "unequip / move to bag" for an item that is actually equipped (slot or belt).
    // A bagged item sharing an occupied slot (binocular vs the grenade in slot 14) must never show it -
    // guards against any residual UI<->engine desync leaving a ruck item looking slotted.
    if (item->CurrPlace() != eItemPlaceRuck &&
        item->Ruck() && inv.CanPutInRuck(item) && (cur_slot == NO_ACTIVE_SLOT || !inv.SlotIsPersistent(cur_slot)))
    {
        if (!pOutfit)
        {
            if (!pHelmet)
            {
                const bool has_translation = StringTable().has_translation("st_unequip");
                if (m_currMenuMode == mmDeadBodySearch || !has_translation)
                    m_UIPropertiesBox->AddItem("st_move_to_bag", nullptr, INVENTORY_TO_BAG_ACTION);
                else
                    m_UIPropertiesBox->AddItem("st_unequip", nullptr, INVENTORY_TO_BAG_ACTION);
            }
            else
                m_UIPropertiesBox->AddItem("st_undress_helmet", NULL, INVENTORY_TO_BAG_ACTION);
        }
        else
            m_UIPropertiesBox->AddItem("st_undress_outfit", NULL, INVENTORY_TO_BAG_ACTION);

        bAlreadyDressed = true;
        b_show = true;
    }
    if (pOutfit && !bAlreadyDressed)
    {
        m_UIPropertiesBox->AddItem("st_dress_outfit", NULL, INVENTORY_TO_SLOT_ACTION);
        b_show = true;
    }

    CCustomOutfit* outfit_in_slot = m_pActorInvOwner->GetOutfit();
    if (pHelmet && !bAlreadyDressed && (!outfit_in_slot || outfit_in_slot->bIsHelmetAvaliable))
    {
        m_UIPropertiesBox->AddItem("st_dress_helmet", NULL, INVENTORY_TO_SLOT_ACTION);
        b_show = true;
    }
}

void CUIActorMenu::PropertiesBoxForWeapon(CUICellItem* cell_item, PIItem item, bool& b_show)
{
    //отсоединение аддонов от вещи
    CWeapon* pWeapon = smart_cast<CWeapon*>(item);
    if (!pWeapon)
    {
        return;
    }

    if (pWeapon->GrenadeLauncherAttachable())
    {
        if (pWeapon->IsGrenadeLauncherAttached())
        {
            m_UIPropertiesBox->AddItem("st_detach_gl", NULL, INVENTORY_DETACH_GRENADE_LAUNCHER_ADDON);
            b_show = true;
        }
        else
        {
        }
    }
    if (pWeapon->ScopeAttachable())
    {
        if (pWeapon->IsScopeAttached())
        {
            m_UIPropertiesBox->AddItem("st_detach_scope", NULL, INVENTORY_DETACH_SCOPE_ADDON);
            b_show = true;
        }
        else
        {
        }
    }
    if (pWeapon->SilencerAttachable())
    {
        if (pWeapon->IsSilencerAttached())
        {
            m_UIPropertiesBox->AddItem("st_detach_silencer", NULL, INVENTORY_DETACH_SILENCER_ADDON);
            b_show = true;
        }
        else
        {
        }
    }
    if (smart_cast<CWeaponMagazined*>(pWeapon) && IsGameTypeSingle())
    {
        bool b = (pWeapon->GetAmmoElapsed() != 0);
        if (!b)
        {
            for (u32 i = 0; i < cell_item->ChildsCount(); ++i)
            {
                CWeaponMagazined* weap_mag = smart_cast<CWeaponMagazined*>((CWeapon*)cell_item->Child(i)->m_pData);
                if (weap_mag && weap_mag->GetAmmoElapsed())
                {
                    b = true;
                    break; // for
                }
            }
        }
        if (b)
        {
            m_UIPropertiesBox->AddItem("st_unload_magazine", NULL, INVENTORY_UNLOAD_MAGAZINE);
            b_show = true;
        }
    }
}

void CUIActorMenu::PropertiesBoxForAddon(PIItem item, bool& b_show)
{
    //присоединение аддонов к активному слоту (2 или 3)

    CScope* pScope = smart_cast<CScope*>(item);
    CSilencer* pSilencer = smart_cast<CSilencer*>(item);
    CGrenadeLauncher* pGrenadeLauncher = smart_cast<CGrenadeLauncher*>(item);
    CInventory* inv = &m_pActorInvOwner->inventory();

    PIItem item_in_slot_2 = inv->ItemFromSlot(INV_SLOT_2);
    PIItem item_in_slot_3 = inv->ItemFromSlot(INV_SLOT_3);

    if (!item_in_slot_2 && !item_in_slot_3)
        return;

    if (pScope)
    {
        if (item_in_slot_2 && item_in_slot_2->CanAttach(pScope))
        {
            shared_str str = StringTable().translate("st_attach_scope_to_pistol");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_2->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_2, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_scope_to_pistol",  (void*)item_in_slot_2,
            // INVENTORY_ATTACH_ADDON );
            b_show = true;
        }
        if (item_in_slot_3 && item_in_slot_3->CanAttach(pScope))
        {
            shared_str str = StringTable().translate("st_attach_scope_to_pistol");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_3->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_3, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_scope_to_rifle",  (void*)item_in_slot_3,
            // INVENTORY_ATTACH_ADDON );
            b_show = true;
        }
        return;
    }

    if (pSilencer)
    {
        if (item_in_slot_2 && item_in_slot_2->CanAttach(pSilencer))
        {
            shared_str str = StringTable().translate("st_attach_silencer_to_pistol");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_2->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_2, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_silencer_to_pistol",  (void*)item_in_slot_2,
            // INVENTORY_ATTACH_ADDON );
            b_show = true;
        }
        if (item_in_slot_3 && item_in_slot_3->CanAttach(pSilencer))
        {
            shared_str str = StringTable().translate("st_attach_silencer_to_pistol");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_3->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_3, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_silencer_to_rifle",  (void*)item_in_slot_3,
            // INVENTORY_ATTACH_ADDON );
            b_show = true;
        }
        return;
    }

    if (pGrenadeLauncher)
    {
        if (item_in_slot_2 && item_in_slot_2->CanAttach(pGrenadeLauncher))
        {
            shared_str str = StringTable().translate("st_attach_gl_to_rifle");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_2->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_2, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_gl_to_pistol",  (void*)item_in_slot_2,
            //INVENTORY_ATTACH_ADDON
            //);
            b_show = true;
        }
        if (item_in_slot_3 && item_in_slot_3->CanAttach(pGrenadeLauncher))
        {
            shared_str str = StringTable().translate("st_attach_gl_to_rifle");
            xr_sprintf(str, "%s %s", str.c_str(), item_in_slot_3->m_name.c_str());
            m_UIPropertiesBox->AddItem(str.c_str(), (void*)item_in_slot_3, INVENTORY_ATTACH_ADDON);
            //			m_UIPropertiesBox->AddItem( "st_attach_gl_to_rifle",  (void*)item_in_slot_3,
            //INVENTORY_ATTACH_ADDON
            //);
            b_show = true;
        }
    }
}

void CUIActorMenu::PropertiesBoxForUsing(PIItem item, bool& b_show)
{
    pcstr act_str = nullptr;
    CGameObject* GO = smart_cast<CGameObject*>(item);
    // [DA_PORT] Соседние ветки этого же меню приведение проверяют, эта — не проверяла.
    if (!GO)
        return;
    shared_str section_name = GO->cNameSect();

    //ability to set eat string from settings
    act_str = READ_IF_EXISTS(pSettings, r_string, section_name, "default_use_text", nullptr);
    if (act_str)
    {
        m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT_ACTION);
        b_show = true;
    }
    else
    {
        CMedkit* pMedkit = smart_cast<CMedkit*>(item);
        CAntirad* pAntirad = smart_cast<CAntirad*>(item);
        CEatableItem * pEatableItem = smart_cast<CEatableItem*>(item);
        CBottleItem* pBottleItem = smart_cast<CBottleItem*>(item);

        if (pMedkit || pAntirad)
            act_str = "st_use";
        else if (pBottleItem)
            act_str = "st_drink";
        else if (pEatableItem)
        {
            // XXX: Xottab_DUTY: remove this..
            if (!xr_strcmp(section_name, "vodka") || !xr_strcmp(section_name, "energy_drink"))
                act_str = "st_drink";
            else if (!xr_strcmp(section_name, "bread") || !xr_strcmp(section_name, "kolbasa") || !xr_strcmp(
                section_name, "conserva"))
                act_str = "st_eat";
            else
                act_str = "st_use";
        }
        if (act_str)
        {
            m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT_ACTION);
            b_show = true;
        }
    }

    //1st Custom Use action
    pcstr functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use1_functor", nullptr);
    if (functor_name)
    {
        luabind::functor<pcstr> funct1;
        if (GEnv.ScriptEngine->functor(functor_name, funct1))
        {
            act_str = funct1(GO->lua_game_object());
            if (act_str)
            {
                m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT2_ACTION);
                b_show = true;
            }
        }
    }

    // 2nd Custom Use action
    functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use2_functor", nullptr);
    if (functor_name)
    {
        luabind::functor<pcstr> funct1;
        if (GEnv.ScriptEngine->functor(functor_name, funct1))
        {
            act_str = funct1(GO->lua_game_object());
            if (act_str)
            {
                m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT3_ACTION);
                b_show = true;
            }
        }
    }

    // 3rd Custom Use action
    functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use3_functor", nullptr);
    if (functor_name)
    {
        luabind::functor<pcstr> funct1;
        if (GEnv.ScriptEngine->functor(functor_name, funct1))
        {
            act_str = funct1(GO->lua_game_object());
            if (act_str)
            {
                m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT4_ACTION);
                b_show = true;
            }
        }
    }

    // 4th Custom Use action
    functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use4_functor", nullptr);
    if (functor_name)
    {
        luabind::functor<pcstr> funct1;
        if (GEnv.ScriptEngine->functor(functor_name, funct1))
        {
            act_str = funct1(GO->lua_game_object());
            if (act_str)
            {
                m_UIPropertiesBox->AddItem(act_str, nullptr, INVENTORY_EAT5_ACTION);
                b_show = true;
            }
        }
    }
}

void CUIActorMenu::PropertiesBoxForPlaying(PIItem item, bool& b_show)
{
    CPda* pPda = smart_cast<CPda*>(item);
    if (!pPda || !pPda->CanPlayScriptFunction())
        return;

    LPCSTR act_str = "st_play";
    m_UIPropertiesBox->AddItem(act_str, NULL, INVENTORY_PLAY_ACTION);
    b_show = true;
}

void CUIActorMenu::PropertiesBoxForDrop(CUICellItem* cell_item, PIItem item, bool& b_show)
{
    if (!item->IsQuestItem())
    {
        m_UIPropertiesBox->AddItem("st_drop", NULL, INVENTORY_DROP_ACTION);
        b_show = true;

        if (cell_item->ChildsCount())
        {
            m_UIPropertiesBox->AddItem("st_drop_all", (void*)33, INVENTORY_DROP_ACTION);
        }
    }
}

void CUIActorMenu::PropertiesBoxForRepair(PIItem item, bool& b_show)
{
    CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(item);
    CWeapon* pWeapon = smart_cast<CWeapon*>(item);
    CHelmet* pHelmet = smart_cast<CHelmet*>(item);

    if ((pOutfit || pWeapon || pHelmet) && item->GetCondition() < 0.99f)
    {
        m_UIPropertiesBox->AddItem("ui_inv_repair", NULL, INVENTORY_REPAIR);
        b_show = true;
    }
}

//Alundaio: Ability to donate item during trade
void CUIActorMenu::PropertiesBoxForDonate(PIItem item, bool& b_show)
{
    if (!item->IsQuestItem())
    {
        m_UIPropertiesBox->AddItem("st_donate", nullptr, INVENTORY_DONATE_ACTION);
        b_show = true;
    }
}
//-Alundaio

void CUIActorMenu::ProcessPropertiesBoxClicked(CUIWindow* w, void* d)
{
    PIItem item = CurrentIItem();
    CUICellItem* cell_item = CurrentItem();
    if (!m_UIPropertiesBox->GetClickedItem() || !item || !cell_item || !cell_item->OwnerList())
    {
        return;
    }
    CWeapon* weapon = smart_cast<CWeapon*>(item);

    switch (m_UIPropertiesBox->GetClickedItem()->GetTAG())
    {
    case INVENTORY_TO_SLOT_ACTION: ToSlot(cell_item, true, item->BaseSlot()); break;
    case INVENTORY_TO_BELT_ACTION: ToBelt(cell_item, false); break;
    case INVENTORY_TO_BAG_ACTION: ToBag(cell_item, false); break;
    case INVENTORY_DONATE_ACTION: DonateCurrentItem(cell_item); break;
    case INVENTORY_EAT_ACTION: TryUseItem(cell_item); break;
    case INVENTORY_EAT2_ACTION:
    {
        const CGameObject* GO = smart_cast<CGameObject*>(item);
        if (cpcstr functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use1_action_functor", nullptr))
        {
            luabind::functor<bool> funct1;
            if (GEnv.ScriptEngine->functor(functor_name, funct1))
            {
                if (funct1(GO->lua_game_object()))
                    TryUseItem(cell_item);
            }
        }
        break;
    }
    case INVENTORY_EAT3_ACTION:
    {
        const CGameObject* GO = smart_cast<CGameObject*>(item);
        if (cpcstr functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use2_action_functor", nullptr))
        {
            luabind::functor<bool> funct2;
            if (GEnv.ScriptEngine->functor(functor_name, funct2))
            {
                if (funct2(GO->lua_game_object()))
                    TryUseItem(cell_item);
            }
        }
        break;
    }
    case INVENTORY_EAT4_ACTION:
    {
        const CGameObject* GO = smart_cast<CGameObject*>(item);
        if (cpcstr functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use3_action_functor", nullptr))
        {
            luabind::functor<bool> funct3;
            if (GEnv.ScriptEngine->functor(functor_name, funct3))
            {
                if (funct3(GO->lua_game_object()))
                    TryUseItem(cell_item);
            }
        }
        break;
    }
    case INVENTORY_EAT5_ACTION:
    {
        const CGameObject* GO = smart_cast<CGameObject*>(item);
        if (cpcstr functor_name = READ_IF_EXISTS(pSettings, r_string, GO->cNameSect(), "use4_action_functor", nullptr))
        {
            luabind::functor<bool> funct4;
            if (GEnv.ScriptEngine->functor(functor_name, funct4))
            {
                if (funct4(GO->lua_game_object()))
                    TryUseItem(cell_item);
            }
        }
        break;
    }
    case INVENTORY_DROP_ACTION:
    {
        void* d = m_UIPropertiesBox->GetClickedItem()->GetData();
        if (d == (void*)33)
        {
            DropAllCurrentItem();
        }
        else
        {
            SendEvent_Item_Drop(item, m_pActorInvOwner->object_id());
        }
        break;
    }
    case INVENTORY_ATTACH_ADDON:
    {
        PIItem item = CurrentIItem(); // temporary storing because of AttachAddon is setting curiitem to NULL
        AttachAddon((PIItem)(m_UIPropertiesBox->GetClickedItem()->GetData()));
        if (m_currMenuMode == mmDeadBodySearch)
            RemoveItemFromList(m_pLists[eSearchLootBagList], item);

        break;
    }
    case INVENTORY_DETACH_SCOPE_ADDON:
        if (weapon)
        {
            DetachAddon(weapon->GetScopeName().c_str());
            for (u32 i = 0; i < cell_item->ChildsCount(); ++i)
            {
                CUICellItem* child_itm = cell_item->Child(i);
                PIItem child_iitm = (PIItem)(child_itm->m_pData);
                CWeapon* wpn = smart_cast<CWeapon*>(child_iitm);
                if (child_iitm && wpn)
                {
                    DetachAddon(wpn->GetScopeName().c_str(), child_iitm);
                }
            }
        }
        break;
    case INVENTORY_DETACH_SILENCER_ADDON:
        if (weapon)
        {
            DetachAddon(weapon->GetSilencerName().c_str());
            for (u32 i = 0; i < cell_item->ChildsCount(); ++i)
            {
                CUICellItem* child_itm = cell_item->Child(i);
                PIItem child_iitm = (PIItem)(child_itm->m_pData);
                CWeapon* wpn = smart_cast<CWeapon*>(child_iitm);
                if (child_iitm && wpn)
                {
                    DetachAddon(wpn->GetSilencerName().c_str(), child_iitm);
                }
            }
        }
        break;
    case INVENTORY_DETACH_GRENADE_LAUNCHER_ADDON:
        if (weapon)
        {
            DetachAddon(weapon->GetGrenadeLauncherName().c_str());
            for (u32 i = 0; i < cell_item->ChildsCount(); ++i)
            {
                CUICellItem* child_itm = cell_item->Child(i);
                PIItem child_iitm = (PIItem)(child_itm->m_pData);
                CWeapon* wpn = smart_cast<CWeapon*>(child_iitm);
                if (child_iitm && wpn)
                {
                    DetachAddon(wpn->GetGrenadeLauncherName().c_str(), child_iitm);
                }
            }
        }
        break;
    case INVENTORY_RELOAD_MAGAZINE:
        if (weapon)
        {
            weapon->Action(kWPN_RELOAD, CMD_START);
        }
        break;
    case INVENTORY_UNLOAD_MAGAZINE:
    {
        CWeaponMagazined* weap_mag = smart_cast<CWeaponMagazined*>((CWeapon*)cell_item->m_pData);
        if (!weap_mag)
        {
            break;
        }
        weap_mag->UnloadMagazine();
        for (u32 i = 0; i < cell_item->ChildsCount(); ++i)
        {
            CUICellItem* child_itm = cell_item->Child(i);
            CWeaponMagazined* child_weap_mag = smart_cast<CWeaponMagazined*>((CWeapon*)child_itm->m_pData);
            if (child_weap_mag)
            {
                child_weap_mag->UnloadMagazine();
            }
        }
        break;
    }
    case INVENTORY_REPAIR:
    {
        TryRepairItem(this, 0);
        return;
        break;
    }
    case INVENTORY_PLAY_ACTION:
    {
        CPda* pPda = smart_cast<CPda*>(item);
        if (!pPda)
            break;
        pPda->PlayScriptFunction();
        break;
    }
    } // switch

    //SetCurrentItem(nullptr);
    UpdateItemsPlace();
} // ProcessPropertiesBoxClicked

void CUIActorMenu::UpdateOutfit()
{
    const u32 maxCount = m_pActorInvOwner->inventory().BeltMaxWidth();
    const Ivector2 maxCap = m_pLists[eInventoryBeltList]->CalculateCapacity(maxCount);
    m_pLists[eInventoryBeltList]->SetMaxCellsCapacity(maxCap);

    CCustomOutfit* outfit = m_pActorInvOwner->GetOutfit();
    if (m_pLists[eInventoryHelmetList])
    {
        if (outfit && !outfit->bIsHelmetAvaliable)
            m_pLists[eInventoryHelmetList]->SetCellsCapacity({ 0, 0 });
        else
            m_pLists[eInventoryHelmetList]->SetCellsCapacity(m_pLists[eInventoryHelmetList]->MaxCellsCapacity());
    }
    // [DA_PORT] same blocker treatment for the backpack cell: shrink to 0 (draws backpack_over) when the
    // worn outfit forbids a backpack, restore to full otherwise. Backpack grid (2x2) == cell cap, so this
    // never fights DA_GrowSlotCellToFit.
    if (m_pLists[eInventoryBackpackList])
    {
        if (outfit && !outfit->bIsBackpackAvaliable)
            m_pLists[eInventoryBackpackList]->SetCellsCapacity({ 0, 0 });
        else
            m_pLists[eInventoryBackpackList]->SetCellsCapacity(m_pLists[eInventoryBackpackList]->MaxCellsCapacity());
    }

    if (m_OutfitInfo)
    {
        m_OutfitInfo->UpdateInfo(outfit, nullptr, true, true);
    }

    if (ShadowOfChernobylMode)
    {
        m_pLists[eInventoryBeltList]->ResetCellsCapacity();
        return;
    }
    if (!outfit)
    {
        MoveArtefactsToBag();
        m_pLists[eInventoryBeltList]->SetCellsCapacity({ 0, 0 });
        return;
    }

    const u32 af_count = m_pActorInvOwner->inventory().BeltWidth();
    const Ivector2 cap = m_pLists[eInventoryBeltList]->CalculateCapacity(af_count);
    m_pLists[eInventoryBeltList]->SetCellsCapacity(cap);
}

void CUIActorMenu::MoveArtefactsToBag()
{
    while (m_pLists[eInventoryBeltList]->ItemsCount())
    {
        CUICellItem* ci = m_pLists[eInventoryBeltList]->GetItemIdx(0);
        VERIFY(ci && ci->m_pData);
        ToBag(ci, false);
    } // for i
    m_pLists[eInventoryBeltList]->ClearAll(true);
}

void CUIActorMenu::RefreshCurrentItemCell()
{
    CUICellItem* ci = CurrentItem();
    if (!ci)
        return;

    if (ci->ChildsCount() > 0)
    {
        CUIDragDropListEx* invlist = GetListByType(iActorBag);

        if (invlist->IsOwner(ci))
        {
            CUICellItem* parent = invlist->RemoveItem(ci, true);

            while (parent->ChildsCount())
            {
                CUICellItem* child = parent->PopChild(nullptr);
                invlist->SetItem(child);
            }

            invlist->SetItem(parent, GetUICursor().GetCursorPosition());
        }
    }
}
