#pragma once

#include "inventory_item_object.h"
#include "HudSound.h"

class CLAItem;
class CNightVisionEffector;

class CTorch : public CInventoryItemObject
{
private:
    typedef CInventoryItemObject inherited;

protected:
    float fBrightness;
    CLAItem* lanim;

    u16 guid_bone;
    shared_str light_trace_bone;

    float m_delta_h;
    Fvector2 m_prev_hp;
    bool m_switched_on;
    // [DA_PORT] Dead Air's scripts drive two independent lights: "torch" (itms_manager.script
    // forces this ON every actor_on_update tick - it's meant to be a harmless base light) and
    // "torch2" (the one the player actually toggles with the torch key). m_switched_on/light_omni
    // stay the "torch" (dim, no shadow); m_switched_on2/light_render are the "torch2" (bright
    // spot with shadow) - the real flashlight beam the player controls.
    bool m_switched_on2;
    ref_light light_render;
    ref_light light_omni;
    ref_glow glow_render;
    Fvector m_focus;

    // [DA_PORT] Runtime light tuning driven by Dead Air's xr_actor.script. DA carries ONE hidden
    // device_torch (this CTorch) and reconfigures its light per equipped light item (flashlight =
    // white spot, glowstick = green glow, lighter = orange glow) via the torch_set_* bindings below.
    // m_da_color is honoured while the color animator is off ("empty"); m_da_use_spot tracks whether
    // the current item is a spot-beam item (flashlight) or an omni-glow item (glowstick/lighter).
    Fcolor m_da_color;
    bool m_da_use_spot;

private:
    inline bool can_use_dynamic_lights();

public:
    CTorch();
    virtual ~CTorch();

    virtual void Load(LPCSTR section);
    virtual bool net_Spawn(CSE_Abstract* DC);
    virtual void net_Destroy();
    virtual void net_Export(NET_Packet& P); // export to server
    virtual void net_Import(NET_Packet& P); // import from server

    virtual void OnH_A_Chield();
    virtual void OnH_B_Independent(bool just_before_destroy);

    virtual void UpdateCL();

    void Switch();
    void Switch(bool light_on);
    bool torch_active() const;

    // [DA_PORT] "torch2" - the real player-controlled flashlight beam (see m_switched_on2 above).
    void Switch2();
    void Switch2(bool light_on);
    bool torch2_active() const;

    // [DA_PORT] Dead Air per-item light tuning (xr_actor.script -> torch_set_* bindings). Applied to
    // light_render (spot) / light_omni (point) / glow_render so flashlight/glowstick/lighter each emit
    // their own light from the single device_torch.
    void TorchSetRange(float r);
    void TorchSetRadius(float deg);
    void TorchSetInertion(float i);
    void TorchSetColorR(float v);
    void TorchSetColorG(float v);
    void TorchSetColorB(float v);
    void TorchSetColorA(float v);
    void TorchSetAnimation(LPCSTR name);
    void TorchSetTexture(LPCSTR name);
    void TorchSwitchSpot(bool spot);
    void TorchApplyDAColor();

    virtual bool can_be_attached() const;

    // CAttachableItem
    virtual void enable(bool value);

public:
    void SwitchNightVision();
    void SwitchNightVision(bool light_on, bool use_sounds = true);

    bool GetNightVisionStatus() { return m_bNightVisionOn; }
    CNightVisionEffector* GetNightVision() { return m_night_vision; }
protected:
    bool m_bNightVisionEnabled;
    bool m_bNightVisionOn;

    CNightVisionEffector* m_night_vision;
    HUD_SOUND_COLLECTION m_sounds;

    enum EStats
    {
        eTorchActive = (1 << 0),
        eNightVisionActive = (1 << 1),
        eAttached = (1 << 2),
        eTorch2Active = (1 << 3)
    };

public:
    virtual bool use_parent_ai_locations() const { return (!H_Parent()); }
    virtual void create_physic_shell();
    virtual void activate_physic_shell();
    virtual void setup_physic_shell();

    virtual void afterDetach();

private:
    DECLARE_SCRIPT_REGISTER_FUNCTION(CGameObject);
};

class CNightVisionEffector
{
    CActor* m_pActor;
    HUD_SOUND_COLLECTION m_sounds;

public:
    enum EPlaySounds
    {
        eStartSound = 0,
        eStopSound,
        eIdleSound,
        eBrokeSound
    };
    CNightVisionEffector(const shared_str& sect);
    void Start(const shared_str& sect, CActor* pA, bool play_sound = true);
    void Stop(const float factor, bool play_sound = true);
    bool IsActive();
    void OnDisabled(CActor* pA, bool play_sound = true);
    void PlaySounds(EPlaySounds which);
};
