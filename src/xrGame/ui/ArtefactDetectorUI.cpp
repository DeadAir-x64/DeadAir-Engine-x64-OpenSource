#include "StdAfx.h"
#include "ArtefactDetectorUI.h"
#include "UIXmlInit.h"
#include "xrUICore/XML/xrUIXmlParser.h"
#include "xrUICore/Static/UIStatic.h"
#include "xrUICore/ui_base.h"
#include "Include/xrRender/UIRender.h"
#include "Include/xrRender/Kinematics.h"
#include "player_hud.h"
#include "CustomDetector.h"

void CUIDetectorWave::SetVelocity(float v) { m_curr_v = v; }

void CUIDetectorWave::Update()
{
    Fvector2 P = GetWndPos();

    float dp = m_curr_v * Device.fTimeDelta;

    P.x += dp;
    if (P.x > 0)
        P.x -= m_step;
    else if (P.x < -(2 * m_step))
        P.x += m_step;

    SetWndPos(P);
    inherited::Update();
}

void CUIDetectorWave::InitFromXML(CUIXml& xml, LPCSTR path)
{
    CUIXmlInit::InitFrameLine(xml, path, 0, this);
    m_step = xml.ReadAttribFlt(path, 0, "step");
}

// ===================================================================================
// [DA_PORT] CUIArtefactDetectorHudUI — generic hud_ui 3D artefact screen.
// Structurally mirrors CUIArtefactDetectorElite (EliteDetector.cpp), but sources its
// XML file, tag, attach bone and offset from the hud_ui_* config keys instead of the
// hardcoded "cover" bone + ui_p/ui_r. Lets low-tier detectors (simple/craft) that lack
// a native LCD screen still project detected artefacts, exactly as DA's binary intended.
// ===================================================================================
constexpr cpcstr HUDUI_AF_SIGN = "af_sign";

bool CUIArtefactDetectorHudUI::construct(CCustomDetector* p, const shared_str& hud_section, const shared_str& xml_tag)
{
    m_parent = p;

    // Missing keys/asset must degrade gracefully (fatal=false) — a modder may strip them.
    if (hud_section.size() == 0 || xml_tag.size() == 0)
        return false;

    CUIXml uiXml;
    if (!uiXml.Load(CONFIG_PATH, UI_PATH, UI_PATH_DEFAULT, "hud_ui_3d.xml", false))
        return false;

    string512 buff;
    xr_strcpy(buff, xml_tag.c_str());
    if (uiXml.NavigateToNode(buff, 0) == nullptr)
    {
        Msg("! [hud_ui] tag [%s] not found in hud_ui_3d.xml", xml_tag.c_str());
        return false;
    }

    CUIXmlInit::InitWindow(uiXml, buff, 0, this);

    m_wrk_area = xr_new<CUIWindow>("Work area");
    xr_sprintf(buff, "%s:wrk_area", xml_tag.c_str());
    CUIXmlInit::InitWindow(uiXml, buff, 0, m_wrk_area);
    m_wrk_area->SetAutoDelete(true);
    AttachChild(m_wrk_area);

    xr_strcpy(buff, xml_tag.c_str());
    XML_NODE pStoredRoot = uiXml.GetLocalRoot();
    uiXml.SetLocalRoot(uiXml.NavigateToNode(buff, 0));

    const int num = (int)uiXml.GetNodesNum(buff, 0, "palette");
    if (num > 0)
    {
        for (int idx = 0; idx < num; ++idx)
        {
            CUIStatic* S = xr_new<CUIStatic>("Palette");
            shared_str name = uiXml.ReadAttrib("palette", idx, "id");
            m_palette[name] = S;
            CUIXmlInit::InitStatic(uiXml, "palette", idx, S);
            S->SetAutoDelete(true);
            m_wrk_area->AttachChild(S);
            S->SetCustomDraw(true);
        }
    }
    else
    {
        CUIStatic* S = xr_new<CUIStatic>("Palette");
        m_palette[HUDUI_AF_SIGN] = S;
        CUIXmlInit::InitStatic(uiXml, HUDUI_AF_SIGN, 0, S);
        S->SetAutoDelete(true);
        m_wrk_area->AttachChild(S);
        S->SetCustomDraw(true);
    }
    uiXml.SetLocalRoot(pStoredRoot);

    m_attach_bone = pSettings->r_string(hud_section, "hud_ui_attach_bone");

    // Bad attach bone would crash LL_GetTransform later — validate up front.
    attachable_hud_item* hi = m_parent->HudItemData();
    if (!hi || !hi->m_model || hi->m_model->LL_BoneID(m_attach_bone) == BI_NONE)
    {
        Msg("! [hud_ui] attach bone [%s] not found on detector HUD model", m_attach_bone.c_str());
        return false;
    }

    Fvector _attach_p = pSettings->r_fvector3(hud_section, "hud_ui_pos");
    Fvector _attach_r = pSettings->r_fvector3(hud_section, "hud_ui_rot");

    _attach_r.mul(PI / 180.f);
    m_map_attach_offset.setHPB(_attach_r.x, _attach_r.y, _attach_r.z);
    m_map_attach_offset.translate_over(_attach_p);
    return true;
}

void CUIArtefactDetectorHudUI::update()
{
    inherited::update();
    CUIWindow::Update();
}

void CUIArtefactDetectorHudUI::GetUILocatorMatrix(Fmatrix& _m)
{
    attachable_hud_item* hi = m_parent->HudItemData();
    Fmatrix trans = hi->m_item_transform;
    // [DA_PORT] Имя кости из конфигурации детектора: нет её в модели → BI_NONE и чтение за массивом.
    const u16 bid = hi->m_model->LL_BoneID(m_attach_bone);
    Fmatrix attach_bone;
    if (bid == BI_NONE || bid >= hi->m_model->LL_BoneCount())
        attach_bone.identity();
    else
        attach_bone = hi->m_model->LL_GetTransform(bid);
    _m.mul(trans, attach_bone);
    _m.mulB_43(m_map_attach_offset);
}

void CUIArtefactDetectorHudUI::Draw()
{
    Fmatrix LM;
    GetUILocatorMatrix(LM);

    IUIRender::ePointType bk = UI().m_currentPointType;
    UI().m_currentPointType = IUIRender::pttLIT;

    GEnv.UIRender->CacheSetXformWorld(LM);
    GEnv.UIRender->CacheSetCullMode(IUIRender::cmNONE);

    CUIWindow::Draw();

    Fvector2 wrk_sz = m_wrk_area->GetWndSize();
    Fvector2 rp;
    m_wrk_area->GetAbsolutePos(rp);

    Fmatrix M, Mc;
    float h, p;
    Device.vCameraDirection.getHP(h, p);
    Mc.setHPB(h, 0, 0);
    Mc.c.set(Device.vCameraPosition);
    M.invert(Mc);

    UI().ScreenFrustumLIT().CreateFromRect(Frect().set(rp.x, rp.y, wrk_sz.x, wrk_sz.y));

    for (const auto& itm : m_items_to_draw)
    {
        Fvector pt3d;
        M.transform_tiny(pt3d, itm.pos);
        float kz = wrk_sz.y / m_parent->m_fAfDetectRadius;
        pt3d.x *= kz;
        pt3d.z *= kz;

        pt3d.x += wrk_sz.x / 2.0f;
        pt3d.z -= wrk_sz.y;

        Fvector2 pos;
        pos.set(pt3d.x, -pt3d.z);
        pos.sub(rp);
        itm.pStatic->SetWndPos(pos);
        itm.pStatic->Draw();
    }

    UI().m_currentPointType = bk;
}

void CUIArtefactDetectorHudUI::Clear() { m_items_to_draw.clear(); }

void CUIArtefactDetectorHudUI::RegisterItemToDraw(const Fvector& p, const shared_str& palette_idx)
{
    xr_map<shared_str, CUIStatic*>::iterator it = m_palette.find(palette_idx);
    if (it == m_palette.end())
    {
        // fall back to the generic artefact sign when a per-type icon isn't defined
        it = m_palette.find(HUDUI_AF_SIGN);
        if (it == m_palette.end())
            return;
    }
    m_items_to_draw.push_back(SDrawOneItem(it->second, p));
}
