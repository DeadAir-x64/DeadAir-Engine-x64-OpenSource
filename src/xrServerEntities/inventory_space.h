#pragma once
#include "xrCommon/xr_vector.h"
#include "xrCore/xrstring.h"

#define CMD_START (1 << 0)
#define CMD_STOP (1 << 1)

enum
{
    NO_ACTIVE_SLOT = 0,
    KNIFE_SLOT = 1, // btn1			was (0)			!!!
    INV_SLOT_2, // btn2 PISTOL_SLOT	was (1)
    INV_SLOT_3, // btn3 RIFLE_SLOT	was (2)
    // [DA_PORT] Stock CoP/OpenXRay puts hand grenades on this slot (=4, config slot 3). Dead Air
    // RELOCATED hand grenades to engine slot 14 (grenade_f1 has config "slot = 13" -> base_slot 14,
    // confirmed by in-game trace). No DA item uses config slot 3, so slot 4 is unused in DA. The real
    // GRENADE_SLOT is redefined at 14 below so every grenade code path (throw activation, fake-for-
    // grenade auto-slot, NPC combat, actor-menu grenade branch) targets the slot DA actually uses.
    GRENADE_LEGACY_SLOT_4, // = 4, stock CoP grenade slot; UNUSED by DA data (kept for numbering/saves)
    BINOCULAR_SLOT, // btn5 BINOCULAR_SLOT
    BOLT_SLOT, // btn6 BOLT_SLOT
    OUTFIT_SLOT, // outfit
    PDA_SLOT, // pda
    DETECTOR_SLOT, // detector
    TORCH_SLOT, // torch
    ARTEFACT_SLOT, // artefact
    HELMET_SLOT,
    // [DA_PORT] Dead Air defines more inventory slots than stock CoC (system.ltx [inventory] has
    // slot_persistent_1..15). NOTE the +1 offset in inventory_item.cpp:104 (base_slot_id = config
    // "slot" + 1, SOC->CoP convention), so a config "slot = N" lands on engine slot N+1:
    //   engine 13 (config slot 12) = knife-hit "script animation" items (anm_base)
    //   engine 14 (config slot 13) = unused in DA data
    //   engine 15 (config slot 14) = backpack (kit_hunt / backpack_light/heavy / exobackpack)
    // Old CoC ended at BACKPACK_SLOT=13, so the backpack (engine 15) had no slot and couldn't be
    // equipped (log: "slots_count = 14, but real slots count is 15"). Slots 0..12 keep their old
    // indices, so saves stay compatible.
    SCRIPT_ANIM_SLOT, // = 13
    GRENADE_SLOT, // = 14, Dead Air's hand-grenade slot (config slot 13 -> base_slot 14); was RESERVED_SLOT_14
    BACKPACK_SLOT, // = 15, backpack
    SLOTS_COUNT
};

#define RUCK_HEIGHT 280
#define RUCK_WIDTH 7

class CInventoryItem;
class CInventory;

typedef CInventoryItem* PIItem;
typedef xr_vector<PIItem> TIItemContainer;

enum eItemPlace
{
    eItemPlaceUndefined = 0,
    eItemPlaceSlot,
    eItemPlaceBelt,
    eItemPlaceRuck
};

struct SInvItemPlace
{
    union
    {
        struct
        {
            u16 type : 4;
            u16 slot_id : 6;
            u16 base_slot_id : 6;
        };
        u16 value;
    };
};

extern u16 INV_STATE_LADDER;
extern u16 INV_STATE_CAR;
extern u16 INV_STATE_BLOCK_ALL;
extern u16 INV_STATE_INV_WND;
extern u16 INV_STATE_BUY_MENU;

struct II_BriefInfo
{
    shared_str name;
    shared_str icon;
    shared_str cur_ammo;
    shared_str fmj_ammo;
    shared_str ap_ammo;
    shared_str third_ammo; //Alundaio
    shared_str total_ammo;
    shared_str fire_mode;

    shared_str grenade;

    II_BriefInfo() { clear(); }
    IC void clear()
    {
        name = "";
        icon = "";
        cur_ammo = "";
        fmj_ammo = "";
        ap_ammo = "";
        third_ammo = ""; //Alundaio
        total_ammo = "";
        fire_mode = "";
        grenade = "";
    }
};
