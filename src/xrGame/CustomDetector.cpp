#include "StdAfx.h"
#include "CustomDetector.h"
#include "ui/ArtefactDetectorUI.h"
#include "Include/xrRender/UIRender.h"
#include "HUDManager.h"
#include "Inventory.h"
#include "Level.h"
#include "map_manager.h"
#include "ActorEffector.h"
#include "Actor.h"
#include "xrUICore/Windows/UIWindow.h"
#include "player_hud.h"
#include "Weapon.h"
#include "xrEngine/LightAnimLibrary.h" // [DA_PORT] world lamp light (device_kerosinka) color animator
#include "ParticlesObject.h" // [DA_PORT] world flame particle (device_kerosinka kerosine_glow)

ITEM_INFO::ITEM_INFO() : snd_time(0), cur_period(0)
{
    pParticle = nullptr;
    curr_ref = nullptr;
}

ITEM_INFO::~ITEM_INFO()
{
    if (pParticle)
        CParticlesObject::Destroy(pParticle);
}

bool CCustomDetector::CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate)
{
    if (itm == nullptr)
        return true;

    CInventoryItem& iitm = itm->item();
    u32 slot = iitm.BaseSlot();
    bool bres = (slot == INV_SLOT_2 || slot == KNIFE_SLOT || slot == BOLT_SLOT);
    if (!bres && slot_to_activate)
    {
        *slot_to_activate = NO_ACTIVE_SLOT;
        if (m_pInventory->ItemFromSlot(BOLT_SLOT))
            *slot_to_activate = BOLT_SLOT;

        if (m_pInventory->ItemFromSlot(KNIFE_SLOT))
            *slot_to_activate = KNIFE_SLOT;

        if (m_pInventory->ItemFromSlot(INV_SLOT_3) && m_pInventory->ItemFromSlot(INV_SLOT_3)->BaseSlot() != INV_SLOT_3)
            *slot_to_activate = INV_SLOT_3;

        if (m_pInventory->ItemFromSlot(INV_SLOT_2) && m_pInventory->ItemFromSlot(INV_SLOT_2)->BaseSlot() != INV_SLOT_3)
            *slot_to_activate = INV_SLOT_2;

        if (*slot_to_activate != NO_ACTIVE_SLOT)
            bres = true;
    }

    if (itm->GetState() != CHUDState::eShowing)
        bres = bres && !itm->IsPending();

    if (bres)
    {
        CWeapon* W = smart_cast<CWeapon*>(itm);
        if (W)
            bres = bres && (W->GetState() != CHUDState::eBore) && (W->GetState() != CWeapon::eReload) &&
                (W->GetState() != CWeapon::eSwitch) && !W->IsZoomed();
    }
    return bres;
}

bool CCustomDetector::CheckCompatibility(CHudItem* itm)
{
    if (!inherited::CheckCompatibility(itm))
        return false;

    if (!CheckCompatibilityInt(itm, NULL))
    {
        HideDetector(true);
        return false;
    }
    return true;
}

void CCustomDetector::HideDetector(bool bFastMode)
{
    if (GetState() == eIdle)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ShowDetector(bool bFastMode)
{
    if (GetState() == eHidden)
        ToggleDetector(bFastMode);
}

void CCustomDetector::ToggleDetector(bool bFastMode)
{
    m_bNeedActivation = false;
    m_bFastAnimMode = bFastMode;

    if (GetState() == eHidden)
    {
        PIItem iitem = m_pInventory->ActiveItem();
        CHudItem* itm = (iitem) ? iitem->cast_hud_item() : NULL;
        u16 slot_to_activate = NO_ACTIVE_SLOT;

        if (CheckCompatibilityInt(itm, &slot_to_activate))
        {
            if (slot_to_activate != NO_ACTIVE_SLOT)
            {
                m_pInventory->Activate(slot_to_activate);
                m_bNeedActivation = true;
            }
            else
            {
                SwitchState(eShowing);
                TurnDetectorInternal(true);
            }
        }
    }
    else if (GetState() == eIdle)
        SwitchState(eHiding);
}

void CCustomDetector::OnStateSwitch(u32 S, u32 oldState)
{
    inherited::OnStateSwitch(S, oldState);

    switch (S)
    {
    case eShowing:
    {
        g_player_hud->attach_item(this);
        m_sounds.PlaySound("sndShow", Fvector().set(0, 0, 0), this, true, false);
        PlayHUDMotion(m_bFastAnimMode ? "anm_show_fast" : "anm_show", "anim_show", FALSE /*TRUE*/, this, GetState());
        SetPending(TRUE);
    }
    break;
    case eHiding:
    {
        if (oldState != eHiding)
        {
            m_sounds.PlaySound("sndHide", Fvector().set(0, 0, 0), this, true, false);
            PlayHUDMotion(m_bFastAnimMode ? "anm_hide_fast" : "anm_hide", "anim_show", FALSE/*TRUE*/, this, GetState());
            SetPending(TRUE);
        }
    }
    break;
    case eIdle:
    {
        PlayAnimIdle();
        SetPending(FALSE);
    }
    break;
    }
}

void CCustomDetector::OnAnimationEnd(u32 state)
{
    inherited::OnAnimationEnd(state);
    switch (state)
    {
    case eShowing:
    {
        SwitchState(eIdle);
        if (IsUsingCondition() && m_fDecayRate > 0.f)
            this->SetCondition(-m_fDecayRate);
    }
    break;
    case eHiding:
    {
        SwitchState(eHidden);
        TurnDetectorInternal(false);
        g_player_hud->detach_item(this);
    }
    break;
    }
}

void CCustomDetector::UpdateXForm() { CInventoryItem::UpdateXForm(); }
void CCustomDetector::OnActiveItem() { return; }
void CCustomDetector::OnHiddenItem() { DaStopHudEffects(); } // [DA_PORT] см. DaStopHudEffects
CCustomDetector::CCustomDetector()
{
    m_ui = NULL;
    m_bFastAnimMode = false;
    m_bNeedActivation = false;
}

CCustomDetector::~CCustomDetector()
{
    m_artefacts.destroy();
    TurnDetectorInternal(false);
    xr_delete(m_ui);
    xr_delete(m_hud_ui);
    if (m_world_light)
        m_world_light.destroy(); // [DA_PORT] release the world lamp light
    if (m_world_particles) // [DA_PORT] release the world flame particle
        CParticlesObject::Destroy(m_world_particles);
    if (m_hud_particles) // [DA_PORT] release the HUD flame particle
        CParticlesObject::Destroy(m_hud_particles);
    if (m_held_light) // [DA_PORT] release the held lighter glow
        m_held_light.destroy();
}

// [DA_PORT] --- World light for a light-emitting DET_SIMP lying in the world (device_kerosinka) ---
void CCustomDetector::ActivateWorldLight(bool active)
{
    if (!m_world_light_enabled && !m_world_particles_enabled)
        return;

    if (m_world_light_enabled && active && !m_world_light)
    {
        LPCSTR sect = cNameSect().c_str();
        m_world_light = GEnv.Render->light_create();
        m_world_light->set_type(IRender_Light::POINT);
        m_world_light->set_shadow(!!READ_IF_EXISTS(pSettings, r_bool, sect, "light_shadow", FALSE));
        m_world_light->set_range(READ_IF_EXISTS(pSettings, r_float, sect, "light_range", 4.f));

        Fcolor clr;
        clr.set(1.f, 1.f, 1.f, 1.f);
        if (pSettings->line_exist(sect, "light_color"))
            clr = pSettings->r_fcolor(sect, "light_color");
        m_world_brightness = READ_IF_EXISTS(pSettings, r_float, sect, "light_brightness", 1.f);
        clr.mul_rgb(m_world_brightness);
        m_world_light->set_color(clr);

        if (pSettings->line_exist(sect, "light_color_animmator"))
        {
            LPCSTR anim = pSettings->r_string(sect, "light_color_animmator");
            if (anim && anim[0] && 0 != xr_stricmp(anim, "empty"))
                m_world_lanim = LALib.FindItem(anim);
        }
    }

    if (m_world_light)
    {
        if (active)
        {
            Fvector pos = Position();
            pos.y += 0.2f;
            m_world_light->set_position(pos);
        }
        m_world_light->set_active(active);
    }

    // [DA_PORT] world flame particle (kerosine_glow) rides the same on/off as the light.
    if (m_world_particles_enabled)
    {
        if (active)
        {
            if (!m_world_particles)
                m_world_particles = CParticlesObject::Create(m_world_particles_name.c_str(), FALSE, false);
            if (m_world_particles && !m_world_particles->IsPlaying())
            {
                Fvector pos = Position();
                pos.y += 0.2f;
                m_world_particles->play_at_pos(pos);
            }
        }
        else if (m_world_particles && m_world_particles->IsPlaying())
            m_world_particles->Stop();
    }

    m_world_light_on = active;
}

void CCustomDetector::UpdateWorldLight()
{
    Fvector pos = Position();
    pos.y += 0.2f;

    // [DA_PORT] keep the flame on the lamp as it settles; runs even if this item has no light.
    if (m_world_particles && m_world_particles->IsPlaying())
    {
        Fmatrix xf;
        xf.identity();
        xf.c = pos;
        m_world_particles->UpdateParent(xf, Fvector().set(0.f, 0.f, 0.f));
    }

    if (!m_world_light || !m_world_light->get_active())
        return;

    m_world_light->set_position(pos);

    if (m_world_lanim)
    {
        int frame;
        u32 clr = m_world_lanim->CalculateBGR(Device.fTimeGlobal, frame); // BGR
        Fcolor fclr;
        fclr.set((float)color_get_B(clr), (float)color_get_G(clr), (float)color_get_R(clr), 1.f);
        fclr.mul_rgb(m_world_brightness / 255.f);
        m_world_light->set_color(fclr);
    }
}

void CCustomDetector::UpdateHudParticles()
{
    if (!m_hud_particles_enabled)
        return;

    // Show the flame only while the first-person HUD model is attached (item drawn in hand). HudItemData()
    // is null when holstered / not the active item, so this self-stops on holster.
    attachable_hud_item* hi = HudItemData();
    bool show = false;
    Fmatrix xf;
    xf.identity();
    if (hi && hi->m_model)
    {
        // [DA_PORT] Положение берём через setup_firedeps, а НЕ перемножением матриц вручную.
        //
        // Раньше здесь было `xf.mul(m_item_transform, bone.mTransform)`, и это выглядело разумно, но
        // давало не то: в матрицу кости входит её собственный поворот и масштаб, а у кости огонька в
        // скелете зажигалки он произвольный. Частица уезжала и разворачивалась, и на экране огня
        // просто не было — без единого сообщения, потому что играть она при этом продолжала.
        //
        // setup_firedeps делает то, что нужно: сам обновляет позу на текущий кадр (наш вариант брал
        // прошлую), переносит ТОЧКУ огня с её смещением из конфига и строит ориентацию от направления
        // предмета в руке. Ровно так же считает огонь фальшфейер (flare.cpp) — то есть путь в нашем
        // дереве уже проверен, - и так же делал автор мода.
        //
        // Точка ПЕРВАЯ, хотя в секции предмета написано `particles_bone = light_bone_2`. Ключ
        // относится не сюда: у автора он идёт в мировой партикл на брошенном предмете
        // (`StartParticles(..., m_particles_bone, ...)`), а огонь в руке позиционируется первой точкой
        // огня — `fire_bone = light_bone_1` со смещением `fire_point = 0,-0.02,0.01`, то есть кем-то
        // подобранным под фитиль. По второй точке пламя горело В СТОРОНЕ от зажигалки.
        //
        // Так же считает фальшфейер (flare.cpp) — берёт vLastFP, не vLastFP2.
        firedeps fd;
        hi->setup_firedeps(fd);

        const auto& flags = hi->m_measures.m_prop_flags;
        const bool have1 = flags.test(hud_item_measures::e_fire_point);
        if (have1 || flags.test(hud_item_measures::e_fire_point2))
        {
            xf.set(fd.m_FireParticlesXForm);
            xf.c.set(have1 ? fd.vLastFP : fd.vLastFP2);
            show = true;
        }
    }

    if (show)
    {
        if (!m_hud_particles)
            m_hud_particles = CParticlesObject::Create(m_hud_particles_name.c_str(), FALSE);
        if (m_hud_particles && !m_hud_particles->IsPlaying())
            m_hud_particles->Play(true); // HUD-mode particle (rendered in the first-person pass)
        if (m_hud_particles)
            m_hud_particles->SetXFORM(xf);
    }
    else
    {
        // [DA_PORT] УНИЧТОЖАЕМ, а не просто гасим.
        //
        // Stop() лишь прекращает выброс: объект остаётся живым и в очереди отрисовки, а матрицу ему
        // больше никто не обновляет — он застывает в последнем HUD-положении, то есть привязанным к
        // камере. Со стороны это выглядит как огонёк, который ходит за игроком и после того, как
        // зажигалка убрана. Так же поступают фальшфейер (flare.cpp) и авторская реализация.
        CParticlesObject::Destroy(m_hud_particles);
    }

    // [DA_PORT] warm glow so the held lighter lights the environment (world-space, near the actor - the HUD
    // bone is in view space and would not illuminate the world). Self-contained; independent of device_torch.
    const bool held = (hi != nullptr);
    if (held)
    {
        if (!m_held_light)
        {
            m_held_light = GEnv.Render->light_create();
            m_held_light->set_type(IRender_Light::POINT);
            m_held_light->set_shadow(false);
            m_held_light->set_range(5.f);
            Fcolor c;
            c.set(1.0f, 0.55f, 0.22f, 1.0f); // warm flame
            m_held_light->set_color(c);
        }
        // gentle flame flicker on the range so it reads as fire, not a bulb
        const float flick = 0.9f + 0.1f * _sin(Device.fTimeGlobal * 11.f);
        m_held_light->set_range(5.f * flick);
        Fvector p = H_Parent() ? H_Parent()->Position() : Position();
        p.y += 1.4f; // hand/chest height in first person
        m_held_light->set_position(p);
        m_held_light->set_active(true);
    }
    else if (m_held_light)
        m_held_light->set_active(false);
}

bool CCustomDetector::net_Spawn(CSE_Abstract* DC)
{
    TurnDetectorInternal(false);
    const bool r = inherited::net_Spawn(DC);
    // [DA_PORT] a light item spawned straight into the world (e.g. a placed kerosene lamp) burns now.
    if (r && m_world_light_enabled && !H_Parent())
        ActivateWorldLight(true);
    return r;
}

void CCustomDetector::Load(LPCSTR section)
{
    m_animation_slot = 7;
    inherited::Load(section);

    m_fAfDetectRadius = pSettings->read_if_exists<float>(section, "af_radius", 30.0f);
    m_fAfVisRadius = pSettings->read_if_exists<float>(section, "af_vis_radius", 2.0f);
    m_fDecayRate = READ_IF_EXISTS(pSettings, r_float, section, "decay_rate", 0.f); //Alundaio
    m_artefacts.load(section, "af");

    m_sounds.LoadSound(section, "snd_draw", "sndShow");
    m_sounds.LoadSound(section, "snd_holster", "sndHide");

    // [DA_PORT] light_enabled=true DET_SIMP items (kerosene lamp) emit a world light when on the ground.
    m_world_light_enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "light_enabled", FALSE);
    // [DA_PORT] particles_enabled=true DET_SIMP items (kerosene lamp -> "kerosine_glow") show a world flame.
    m_world_particles_enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "particles_enabled", FALSE);
    if (m_world_particles_enabled)
    {
        m_world_particles_name = READ_IF_EXISTS(pSettings, r_string, section, "particles", "");
        if (!m_world_particles_name.size())
            m_world_particles_enabled = false;
    }
    // [DA_PORT] hud_particles_enabled=true (device_lighter) -> flame on the first-person HUD model bone.
    m_hud_particles_enabled = !!READ_IF_EXISTS(pSettings, r_bool, section, "hud_particles_enabled", FALSE);
    if (m_hud_particles_enabled)
    {
        m_hud_particles_name = READ_IF_EXISTS(pSettings, r_string, section, "particles", "");
        m_hud_particles_bone = READ_IF_EXISTS(pSettings, r_string, section, "particles_bone", "");
        if (!m_hud_particles_name.size() || !m_hud_particles_bone.size())
            m_hud_particles_enabled = false;
    }
}

void CCustomDetector::shedule_Update(u32 dt)
{
    inherited::shedule_Update(dt);

    if (!IsWorking())
        return;

    Position().set(H_Parent()->Position());

    Fvector P;
    P.set(H_Parent()->Position());

    if (IsUsingCondition() && GetCondition() <= 0.01f)
        return;

    m_artefacts.feel_touch_update(P, m_fAfDetectRadius);
}

bool CCustomDetector::IsWorking() { return m_bWorking && H_Parent() && H_Parent() == Level().CurrentViewEntity(); }
void CCustomDetector::UpfateWork()
{
    UpdateAf();
    m_ui->update();
    UpdateHudUI();
}

// [DA_PORT] hud_ui_* generic 3D artefact screen -------------------------------------
void CCustomDetector::TryCreateHudUI()
{
    if (m_hud_ui || m_hud_ui_checked)
        return;

    m_hud_ui_checked = true; // one probe per HUD attach; reset in on_b_hud_detach

    attachable_hud_item* hi = HudItemData();
    if (!hi)
    {
        m_hud_ui_checked = false; // HUD model not ready yet — retry next tick
        return;
    }

    const shared_str& hud_sect = hi->m_sect_name;
    if (!pSettings->line_exist(hud_sect, "hud_ui_xml_tag_name"))
        return; // this detector has no hud_ui screen (e.g. elite uses ui_p/ui_r instead)

    shared_str tag = pSettings->r_string(hud_sect, "hud_ui_xml_tag_name");

    CUIArtefactDetectorHudUI* ui = xr_new<CUIArtefactDetectorHudUI>();
    if (!ui->construct(this, hud_sect, tag))
    {
        xr_delete(ui);
        return;
    }
    m_hud_ui = ui;
}

void CCustomDetector::UpdateHudUI()
{
    if (!m_hud_ui)
    {
        TryCreateHudUI();
        if (!m_hud_ui)
            return;
    }

    m_hud_ui->Clear();

    Fvector detector_pos = Position();
    for (const auto& itemInfo : m_artefacts.m_ItemInfos)
    {
        CArtefact* pAf = itemInfo.first;
        if (pAf->H_Parent())
            continue;

        m_hud_ui->RegisterItemToDraw(pAf->Position(), pAf->cNameSect());

        if (pAf->CanBeInvisible())
        {
            float d = detector_pos.distance_to(pAf->Position());
            if (d < m_fAfVisRadius)
                pAf->SwitchVisibility(true);
        }
    }

    m_hud_ui->update();
}

void CCustomDetector::on_a_hud_attach()
{
    inherited::on_a_hud_attach();
    m_hud_ui_checked = false; // re-probe hud_ui config on (re)attach
}

void CCustomDetector::on_b_hud_detach()
{
    inherited::on_b_hud_detach();
    xr_delete(m_hud_ui);
    m_hud_ui_checked = false;
    DaStopHudEffects(); // огонёк зажигалки уходит вместе с моделью в руке
}

// [DA_PORT] Погасить пламя и подсветку зажигалки НЕМЕДЛЕННО, а не «когда-нибудь на обновлении».
//
// Само по себе `UpdateHudParticles` гасит их корректно — но только пока его зовут. Убранный из рук
// предмет перестаёт обновляться, и партикл остаётся играть с последней матрицей: в HUD-пространстве
// это выглядит как огонёк, застывший в воздухе посреди уровня. Поэтому гасим по событию — на
// отсоединении модели от рук и на уборке предмета.
void CCustomDetector::DaStopHudEffects()
{
    CParticlesObject::Destroy(m_hud_particles); // [DA_PORT] см. UpdateHudParticles: гасить мало

    if (m_held_light)
        m_held_light->set_active(false);
}

void CCustomDetector::render_item_3d_ui()
{
    inherited::render_item_3d_ui();
    if (!m_hud_ui)
        return;

    R_ASSERT(HudItemData());
    m_hud_ui->Draw();
    GEnv.UIRender->CacheSetCullMode(IUIRender::cmCCW); // restore cull mode
}

bool CCustomDetector::render_item_3d_ui_query() { return IsWorking() && (m_hud_ui != nullptr); }

void CCustomDetector::UpdateVisibility()
{
    // check visibility
    attachable_hud_item* i0 = g_player_hud->attached_item(0);
    if (i0 && HudItemData())
    {
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (bClimb)
        {
            HideDetector(true);
            m_bNeedActivation = true;
        }
        else
        {
            CWeapon* wpn = smart_cast<CWeapon*>(i0->m_parent_hud_item);
            if (wpn)
            {
                u32 state = wpn->GetState();
                if (wpn->IsZoomed() || state == CWeapon::eReload || state == CWeapon::eSwitch)
                {
                    HideDetector(true);
                    m_bNeedActivation = true;
                }
            }
        }
    }
    else if (m_bNeedActivation)
    {
        attachable_hud_item* i0 = g_player_hud->attached_item(0);
        bool bClimb = ((Actor()->MovingState() & mcClimb) != 0);
        if (!bClimb)
        {
            CHudItem* huditem = (i0) ? i0->m_parent_hud_item : NULL;
            bool bChecked = !huditem || CheckCompatibilityInt(huditem, 0);

            if (bChecked)
                ShowDetector(true);
        }
    }
}

void CCustomDetector::UpdateCL()
{
    inherited::UpdateCL();

    // [DA_PORT] keep the world lamp's light positioned + flickering; runs for the independent
    // (on-the-ground) object too, before the actor-only early return below.
    if (m_world_light_on)
        UpdateWorldLight();

    // [DA_PORT] keep the held lighter's HUD flame on its bone (self-gates on hud_particles_enabled + drawn).
    UpdateHudParticles();

    if (H_Parent() != Level().CurrentEntity())
        return;

    UpdateVisibility();
    if (!IsWorking())
        return;
    UpfateWork();
}

void CCustomDetector::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
    ActivateWorldLight(false); // [DA_PORT] picked up -> no world light (handheld uses device_torch)
}
void CCustomDetector::OnH_B_Independent(bool just_before_destroy)
{
    inherited::OnH_B_Independent(just_before_destroy);

    // [DA_PORT] dropped into the world -> a light item (kerosene lamp) starts burning on the ground.
    if (m_world_light_enabled && !just_before_destroy)
        ActivateWorldLight(true);

    m_artefacts.clear();

	if (GetState() != eHidden)
	{
		// Detaching hud item and animation stop in OnH_A_Independent
		TurnDetectorInternal(false);
		SwitchState(eHidden);
	}
}

void CCustomDetector::OnMoveToRuck(const SInvItemPlace& prev)
{
    inherited::OnMoveToRuck(prev);
    if (prev.type == eItemPlaceSlot)
    {
        SwitchState(eHidden);
        g_player_hud->detach_item(this);
    }
    TurnDetectorInternal(false);
    StopCurrentAnimWithoutCallback();
    DaStopHudEffects(); // [DA_PORT] убрали в рюкзак — огонёк туда не летит
}

void CCustomDetector::OnMoveToSlot(const SInvItemPlace& prev) { inherited::OnMoveToSlot(prev); }
void CCustomDetector::TurnDetectorInternal(bool b)
{
    m_bWorking = b;
    if (b && m_ui == NULL)
    {
        CreateUI();
    }
    else
    {
        //.		xr_delete			(m_ui);
    }

    UpdateNightVisionMode(b);
}

#include "game_base_space.h"
void CCustomDetector::UpdateNightVisionMode(bool b_on) {}
bool CAfList::feel_touch_contact(IGameObject* O)
{
    TypesMapIt it = m_TypesMap.find(O->cNameSect());

    bool res = (it != m_TypesMap.end());
    if (res)
    {
        CArtefact* pAf = smart_cast<CArtefact*>(O);

        if (pAf->GetAfRank() > m_af_rank)
            res = false;
    }
    return res;
}
