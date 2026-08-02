#pragma once
#include "inventory_item_object.h"
#include "xrEngine/Feel_Touch.h"
#include "HudSound.h"
#include "CustomZone.h"
#include "Artefact.h"
#include "ai_sounds.h"

class CCustomZone;
class CInventoryOwner;

struct ITEM_TYPE
{
    Fvector2 freq; // min,max
    HUD_SOUND_ITEM detect_snds;

    shared_str zone_map_location;
    shared_str nightvision_particle;
};

//описание зоны, обнаруженной детектором
struct ITEM_INFO
{
    ITEM_TYPE* curr_ref;
    float snd_time;
    //текущая частота работы датчика
    float cur_period;
    // particle for night-vision mode
    CParticlesObject* pParticle;

    ITEM_INFO();
    ~ITEM_INFO();
};

template <typename K>
class CDetectList : public Feel::Touch
{
protected:
    typedef xr_map<shared_str, ITEM_TYPE> TypesMap;
    typedef typename TypesMap::iterator TypesMapIt;
    TypesMap m_TypesMap;

public:
    typedef xr_map<K*, ITEM_INFO> ItemsMap;
    typedef typename ItemsMap::iterator ItemsMapIt;
    ItemsMap m_ItemInfos;

protected:
    virtual void feel_touch_new(IGameObject* O)
    {
        K* pK = smart_cast<K*>(O);
        R_ASSERT(pK);
        TypesMapIt it = m_TypesMap.find(O->cNameSect());
        R_ASSERT(it != m_TypesMap.end());
        m_ItemInfos[pK].snd_time = 0.0f;
        m_ItemInfos[pK].curr_ref = &(it->second);
    }
    virtual void feel_touch_delete(IGameObject* O)
    {
        K* pK = smart_cast<K*>(O);
        R_ASSERT(pK);
        m_ItemInfos.erase(pK);
    }

public:
    void destroy()
    {
        TypesMapIt it = m_TypesMap.begin();
        for (; it != m_TypesMap.end(); ++it)
            HUD_SOUND_ITEM::DestroySound(it->second.detect_snds);
    }
    void clear()
    {
        m_ItemInfos.clear();
        Feel::Touch::feel_touch.clear();
    }
    virtual void load(LPCSTR sect, LPCSTR prefix)
    {
        u32 i = 1;
        string256 temp;
        do
        {
            xr_sprintf(temp, "%s_class_%d", prefix, i);
            if (pSettings->line_exist(sect, temp))
            {
                shared_str item_sect = pSettings->r_string(sect, temp);

                m_TypesMap.insert(std::make_pair(item_sect, ITEM_TYPE()));
                ITEM_TYPE& item_type = m_TypesMap[item_sect];

                xr_sprintf(temp, "%s_freq_%d", prefix, i);
                item_type.freq = pSettings->r_fvector2(sect, temp);

                xr_sprintf(temp, "%s_sound_%d_", prefix, i);
                HUD_SOUND_ITEM::LoadSound(sect, temp, item_type.detect_snds, SOUND_TYPE_ITEM);

                ++i;
            }
            else
                break;

        } while (true);
    }
};

class CAfList : public CDetectList<CArtefact>
{
protected:
    virtual bool feel_touch_contact(IGameObject* O);

public:
    CAfList() : m_af_rank(0) {}
    int m_af_rank;
};

class CUIArtefactDetectorBase;
class CUIArtefactDetectorHudUI;
class CLAItem;

class CCustomDetector : public CHudItemObject
{
    typedef CHudItemObject inherited;

protected:
    CUIArtefactDetectorBase* m_ui;
    // [DA_PORT] optional generic hud_ui_* 3D artefact screen (simple/advanced/craft)
    CUIArtefactDetectorHudUI* m_hud_ui{ nullptr };
    bool m_hud_ui_checked{ false };
    bool m_bFastAnimMode;
    bool m_bNeedActivation;

public:
    CCustomDetector();
    virtual ~CCustomDetector();

    virtual bool net_Spawn(CSE_Abstract* DC);
    virtual void Load(LPCSTR section);

    virtual void OnH_A_Chield();
    virtual void OnH_B_Independent(bool just_before_destroy);

    virtual void shedule_Update(u32 dt);
    virtual void UpdateCL();

    bool IsWorking();

    virtual void OnMoveToSlot(const SInvItemPlace& prev);
    virtual void OnMoveToRuck(const SInvItemPlace& prev);

    virtual void OnActiveItem();
    virtual void OnHiddenItem();
    virtual void OnStateSwitch(u32 S, u32 oldState);
    virtual void OnAnimationEnd(u32 state);
    virtual void UpdateXForm();

    // [DA_PORT] hud_ui_* 3D screen hooks (no-op unless the HUD section defines hud_ui_*)
    virtual void on_a_hud_attach() override;
    virtual void on_b_hud_detach() override;
    virtual void render_item_3d_ui() override;
    virtual bool render_item_3d_ui_query() override;

    void ToggleDetector(bool bFastMode);
    void HideDetector(bool bFastMode);
    void ShowDetector(bool bFastMode);
    float m_fAfDetectRadius;
    virtual bool CheckCompatibility(CHudItem* itm);

    virtual u32 ef_detector_type() const { return 1; };
protected:
    bool CheckCompatibilityInt(CHudItem* itm, u16* slot_to_activate);
    void TurnDetectorInternal(bool b);
    void UpdateNightVisionMode(bool b_off);
    void UpdateVisibility();
    virtual void UpfateWork();
    virtual void UpdateAf(){};
    virtual void CreateUI(){};

    // [DA_PORT] lazily builds m_hud_ui from HUD-section hud_ui_* keys, then feeds it
    // the positions of all currently-detected artefacts each work tick.
    void TryCreateHudUI();
    void UpdateHudUI();

    bool m_bWorking;
    float m_fAfVisRadius;
    float m_fDecayRate; //Alundaio
    CAfList m_artefacts;

    // [DA_PORT] World light for dropped/placed light items (e.g. the device_kerosinka kerosene lamp,
    // which is a slot=-1 DET_SIMP with light_enabled=true). When such an item lies independent in the
    // world it emits a real scene light (+ color animator flicker) so the lamp "burns" on the ground.
    // Handheld light items (flashlight/glowstick/lighter) render their light via the hidden device_torch;
    // this covers the OTHER case - a light item sitting in the world with no owner.
    ref_light m_world_light;
    CLAItem* m_world_lanim{ nullptr };
    float m_world_brightness{ 1.f };
    bool m_world_light_enabled{ false };
    bool m_world_light_on{ false };
    // [DA_PORT] world flame particle (device_kerosinka particles_enabled=true -> "kerosine_glow"). Plays and
    // moves with the world light so a lamp dropped/placed on the ground visibly burns, not just glows.
    CParticlesObject* m_world_particles{ nullptr };
    bool m_world_particles_enabled{ false };
    shared_str m_world_particles_name;
    void ActivateWorldLight(bool active);
    void UpdateWorldLight();

    // [DA_PORT] HUD flame particle (device_lighter hud_particles_enabled=true -> "gas_light_glow" attached to
    // HUD-model bone "light_bone_2"). Rides on the first-person model while the lighter is held/drawn.
    CParticlesObject* m_hud_particles{ nullptr };
    // [DA_PORT] Гасит пламя и подсветку зажигалки по событию, а не по обновлению — убранный предмет
    // обновляться перестаёт, и партикл застывает в воздухе. Подробности у определения.
    void DaStopHudEffects();

    bool m_hud_particles_enabled{ false };
    shared_str m_hud_particles_name;
    shared_str m_hud_particles_bone;
    // [DA_PORT] the held lighter must actually illuminate. device_lighter has no light_* of its own and its
    // beam is meant to come from the hidden device_torch (fragile script chain that can end up dropped), so
    // drive a small warm point light here while the flame item is drawn. Gated on hud_particles_enabled,
    // which only the lighter has -> flashlight/glowstick (device_torch spot/omni) are untouched.
    ref_light m_held_light;
    void UpdateHudParticles();
};

class CZoneList : public CDetectList<CCustomZone>
{
protected:
    virtual bool feel_touch_contact(IGameObject* O);

public:
    CZoneList();
    virtual ~CZoneList();
}; // class CZoneList
