#pragma once
#include "xrUICore/Windows/UIWindow.h"
#include "xrUICore/ProgressBar/UIDoubleProgressBar.h"
#include "xrServerEntities/alife_space.h"

class CCustomOutfit;
class CHelmet;
class CUIStatic;
class CUIDoubleProgressBar;
class CUIXml;

class CUIOutfitImmunity final : public CUIStatic
{
public:
    CUIOutfitImmunity(pcstr immunity_name);

    bool InitFromXml(CUIXml& xml_doc, pcstr base_str, pcstr immunity, pcstr immunity_text);
    void SetProgressValue(float cur, float comp, float add = 0.0f);

    pcstr GetDebugType() override { return "CUIOutfitImmunity"; }

protected:
    CUIDoubleProgressBar m_progress;
    CUIStatic m_value; // 100%
    float m_magnitude;
}; // class CUIOutfitImmunity

// -------------------------------------------------------------------------------------

class CUIOutfitInfo final : public CUIWindow
{
public:
    CUIOutfitInfo() : CUIWindow("CUIOutfitInfo") {}

    void InitFromXml(CUIXml& xml_doc);
    void UpdateInfo(CCustomOutfit* cur_outfit, CCustomOutfit* slot_outfit = nullptr,
        bool add_artefact_info = false, bool hide_if_zero_immunity = false);
    void UpdateInfo(CHelmet* cur_helmet, CHelmet* slot_helmet = nullptr);

    void AdjustElements();

    pcstr GetDebugType() override { return "CUIOutfitInfo"; }

protected:
    xr_unordered_map<ALife::EHitType, CUIOutfitImmunity*> m_items;
    Fvector2 m_start_pos{};

    // [DA_PORT] Armour condition as a number, the way Dead Air shows it.
    //
    // The weapon side of this lives in CUIWpnParams; outfits get it here because that is where the mod
    // puts the nodes - outfit_info:cap_condition (label) and cap_condition2 (value), both already in
    // the markup with nobody reading them. The icon beside them, static_condition, is there too.
    CUIStatic m_stCondition{ "Condition icon" };
    CUIStatic m_textCondition{ "Condition caption" };
    CUIStatic m_textCondition2{ "Condition value" };
}; // class CUIOutfitInfo
