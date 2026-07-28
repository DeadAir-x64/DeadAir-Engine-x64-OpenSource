#pragma once
#include "xrUICore/Windows/UIWindow.h"
#include "xrUICore/ProgressBar/UIDoubleProgressBar.h"

class CUIXml;
class CInventoryItem;

struct SLuaWpnParams;

class CUIWpnParams final : public CUIWindow
{
public:
    CUIWpnParams();

    bool InitFromXml(CUIXml& xml_doc);
    void SetInfo(CInventoryItem* slot_wpn, CInventoryItem& cur_wpn);
    bool Check(const shared_str& wpn_section);

    pcstr GetDebugType() override { return "CUIWpnParams"; }

protected:
    CUIDoubleProgressBar m_progressAccuracy; // red or green
    CUIDoubleProgressBar m_progressHandling;
    CUIDoubleProgressBar m_progressDamage;
    CUIDoubleProgressBar m_progressRPM;

    CUIStatic* m_icon_acc;
    CUIStatic* m_icon_dam;
    CUIStatic* m_icon_han;
    CUIStatic* m_icon_rpm;

    CUIStatic* m_stAmmo;
    CUIStatic m_textAccuracy{ "Accuracy" };
    CUIStatic m_textHandling{ "Handling" };
    CUIStatic m_textDamage{ "Damage" };
    CUIStatic m_textRPM{ "RPM" };
    CUIStatic* m_textAmmoTypes;
    CUIStatic* m_textAmmoUsedType;
    CUIStatic* m_textAmmoCount;
    CUIStatic* m_textAmmoCount2;
    CUIStatic* m_stAmmoType1;
    CUIStatic* m_stAmmoType2;
    CUIStatic* m_Prop_line;

    // [DA_PORT] see CUIConditionParams below - same pair, same author, in the weapon parameter block.
    CUIStatic m_textConditionW{ "Condition caption" };
    CUIStatic m_textConditionW2{ "Condition value" };
};

// -------------------------------------------------------------------------------------------------

class CUIConditionParams final : public CUIWindow
{
public:
    CUIConditionParams();

    bool InitFromXml(CUIXml& xml_doc);
    void SetInfo(CInventoryItem const* slot_wpn, CInventoryItem const& cur_wpn);

    pcstr GetDebugType() override { return "CUIConditionParams"; }

protected:
    CUIDoubleProgressBar m_progress; // red or green
    CUIStatic m_text;

    // [DA_PORT] The numeric condition, which the port was missing.
    //
    // SetInfo already computed the percentage and then threw it away into the progress bar alone. Dead
    // Air shows the number as well, coloured by how bad it is, and the markup has always had the slot
    // for it: cap_condition is the label, cap_condition2 the value. Both nodes exist in the mod's XML
    // and simply had nobody to fill them.
    CUIStatic m_textCondition{ "Condition caption" };
    CUIStatic m_textCondition2{ "Condition value" };
};
