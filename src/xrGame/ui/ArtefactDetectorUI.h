#pragma once
#include "xrUICore/Windows/UIFrameLineWnd.h"

class CUIStatic;
class CUIFrameLineWnd;
class CUIDetectorWave;
class CSimpleDetector;
class CAdvancedDetector;
class CEliteDetector;
class CCustomDetector;
class CUIXml;
class CLAItem;
class CBoneInstance;

class XR_NOVTABLE CUIArtefactDetectorBase
{
public:
    virtual ~CUIArtefactDetectorBase();
    virtual void update() {}
};

inline CUIArtefactDetectorBase::~CUIArtefactDetectorBase() = default;

class CUIDetectorWave final : public CUIFrameLineWnd
{
    typedef CUIFrameLineWnd inherited;

protected:
    float m_curr_v{};
    float m_step{};

public:
    CUIDetectorWave() : CUIFrameLineWnd(CUIDetectorWave::GetDebugType()) {}

    void InitFromXML(CUIXml& xml, LPCSTR path);
    void SetVelocity(float v);
    void Update() override;

    pcstr GetDebugType() override { return "CUIDetectorWave"; }
};

class CUIArtefactDetectorSimple final : public CUIArtefactDetectorBase
{
    typedef CUIArtefactDetectorBase inherited;

    CSimpleDetector* m_parent;
    u16 m_flash_bone;
    u16 m_on_off_bone;
    u32 m_turn_off_flash_time;

    ref_light m_flash_light;
    ref_light m_on_off_light;
    CLAItem* m_pOnOfLAnim{};
    CLAItem* m_pFlashLAnim{};
    void setup_internals();

public:
    ~CUIArtefactDetectorSimple() override;
    void update() override;
    void Flash(bool bOn, float fRelPower);

    void construct(CSimpleDetector* p);
};

class CUIArtefactDetectorElite final : public CUIArtefactDetectorBase, public CUIWindow
{
    typedef CUIArtefactDetectorBase inherited;

    CUIWindow* m_wrk_area{};

    xr_map<shared_str, CUIStatic*> m_palette;

    struct SDrawOneItem
    {
        SDrawOneItem(CUIStatic* s, const Fvector& p) : pStatic(s), pos(p) {}
        CUIStatic* pStatic;
        Fvector pos;
    };
    xr_vector<SDrawOneItem> m_items_to_draw;
    CEliteDetector* m_parent{};
    Fmatrix m_map_attach_offset;

    void GetUILocatorMatrix(Fmatrix& _m);

public:
    CUIArtefactDetectorElite() : CUIWindow(CUIArtefactDetectorElite::GetDebugType()) {}

    void update() override;
    void Draw() override;

    void construct(CEliteDetector* p);
    void Clear();
    void RegisterItemToDraw(const Fvector& p, const shared_str& palette_idx);

    pcstr GetDebugType() override { return "CUIArtefactDetectorElite"; }
};

// [DA_PORT] Generic hud_ui 3D screen: renders artefact blips on a device screen
// attached to a configurable bone. Driven by hud_ui_* keys in the HUD section
// (hud_ui_xml_tag_name / hud_ui_attach_bone / hud_ui_pos / hud_ui_rot) + hud_ui_3d.xml.
// Mirrors the proven CUIArtefactDetectorElite dots pipeline, but parameterized so the
// simple/advanced/craft detectors (which lack ui_p/ui_r) can show a screen too.
class CUIArtefactDetectorHudUI final : public CUIArtefactDetectorBase, public CUIWindow
{
    typedef CUIArtefactDetectorBase inherited;

    CUIWindow* m_wrk_area{};

    xr_map<shared_str, CUIStatic*> m_palette;

    struct SDrawOneItem
    {
        SDrawOneItem(CUIStatic* s, const Fvector& p) : pStatic(s), pos(p) {}
        CUIStatic* pStatic;
        Fvector pos;
    };
    xr_vector<SDrawOneItem> m_items_to_draw;
    CCustomDetector* m_parent{};
    Fmatrix m_map_attach_offset;
    shared_str m_attach_bone;

    void GetUILocatorMatrix(Fmatrix& _m);

public:
    CUIArtefactDetectorHudUI() : CUIWindow(CUIArtefactDetectorHudUI::GetDebugType()) {}

    void update() override;
    void Draw() override;

    // returns false if the asset or tag is missing (caller then skips hud_ui rendering)
    bool construct(CCustomDetector* p, const shared_str& hud_section, const shared_str& xml_tag);
    void Clear();
    void RegisterItemToDraw(const Fvector& p, const shared_str& palette_idx);

    pcstr GetDebugType() override { return "CUIArtefactDetectorHudUI"; }
};

class CUIArtefactDetectorAdv final : public CUIArtefactDetectorBase
{
    typedef CUIArtefactDetectorBase inherited;

    CAdvancedDetector* m_parent{};
    Fvector m_target_dir;
    float m_cur_y_rot;
    float m_curr_ang_speed;
    u16 m_bid;

public:
    void update() override;
    void construct(CAdvancedDetector* p);
    void SetValue(const float v1, const Fvector& v2);
    float CurrentYRotation() const;
    static void BoneCallback(CBoneInstance* B);
    void ResetBoneCallbacks();
    void SetBoneCallbacks();
};
