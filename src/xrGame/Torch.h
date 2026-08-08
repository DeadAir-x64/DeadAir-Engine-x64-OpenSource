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
    // [DA_PORT] Скрипты мода дают два независимых ВЫКЛЮЧАТЕЛЯ: "torch" (m_switched_on, его
    // itms_manager держит включённым каждый тик, пока выбран предмет со светом) и "torch2"
    // (m_switched_on2 — клавиша игрока, ею управляется налобный фонарь).
    //
    // ⚠️ Здесь стояло, что m_switched_on — это ВСЕГДА light_omni, а m_switched_on2 — light_render.
    // Прямой связи нет, и полагаться на неё нельзя:
    //   • у ИГРОКА какая лампа кому подчиняется, решает ещё и m_da_use_spot (предмет споттовый или
    //     нет) — см. DaUpdateLightState;
    //   • у СТАЛКЕРА обе лампы идут по m_switched_on, потому что torch2 ему не шлёт никто.
    // Единственное место, где это решается, — DaUpdateLightState; читать надо его, а не эти поля.
    bool m_switched_on2;
    ref_light light_render;
    ref_light light_omni;
    ref_glow glow_render;
    Fvector m_focus; // upstream: только записывается в OnH_A_Chield, не читается нигде

    // [DA_PORT] Runtime light tuning driven by Dead Air's xr_actor.script. DA carries ONE hidden
    // device_torch (this CTorch) and reconfigures its light per equipped light item (flashlight =
    // white spot, glowstick = green glow, lighter = orange glow) via the torch_set_* bindings below.
    // m_da_color is honoured while the color animator is off ("empty"); m_da_use_spot tracks whether
    // the current item is a spot-beam item (flashlight) or an omni-glow item (glowstick/lighter).
    Fcolor m_da_color;
    bool m_da_use_spot;

    // [DA_PORT] Каким был признак динамического света, когда лампы выставляли в последний раз.
    // Нужен, чтобы консольная ручка `ai_use_torch_dynamic_lights` действовала СРАЗУ: состояние ламп
    // пересчитывается по событиям, а у сталкера с горящим фонарём события может не быть часами —
    // схема света зовёт включение один раз, при входе в темноту. Сверяется в UpdateCL.
    bool m_da_dynamic_applied;

    // [DA_PORT] ВТОРОЕ семейство настроек - для налобного фонаря (torch2). DA настраивает луч двумя
    // независимыми наборами: torch_set_* описывает свет ПРЕДМЕТА в руках (фонарик, палочка,
    // зажигалка) и применяется один раз при смене предмета, а torch2_set_* описывает налобный луч и
    // применяется КАЖДЫЙ тик (xr_actor.script, UpdateTorch). У нас прожектор один на оба, поэтому
    // значения хранятся раздельно, а в лампу уходит тот набор, чей свет сейчас и должен гореть:
    // предмет есть (дальность > 0) - его, нет - налобный. Пока torch2-сеттеры были заглушками,
    // налобный фонарь получал от ветки "предмета нет" дальность 0 и чёрный цвет - и не светил вовсе
    // при живом звуке щелчка и включённой лампе.
    float m_da_item_range;      // torch_set_range: <=0 означает "предмета со светом нет"

    // [DA_PORT] Когда предмет со светом попросили включить. Ноль — не просили.
    //
    // Фонарик в руке загорался раньше, чем игрок успевал его достать: скрипт включает свет по факту
    // выбора предмета, а анимация доставания к этому моменту ещё идёт. Свет держится погашенным
    // da_hand_torch_delay после запроса — ровно чтобы дождаться руки.
    u32 m_da_hand_on_time;
    bool m_da_hand_pending;     // свет попросили, но задержка ещё идёт
    float m_da_item_cone_deg;   // torch_set_radius
    float m_da2_range;          // torch2_set_range
    float m_da2_cone_deg;       // torch2_set_radius
    Fcolor m_da2_color;         // torch2_set_color_* (яркость падает с зарядом батареи)
    Fvector2 m_da2_offset;      // torch2_set_offset_*: зарезервировано, положение луча см. UpdateCL
    shared_str m_da_beam_texture; // что сейчас в прожекторе (см. DaApplyBeam: у налобного своя)

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

    // [DA_PORT] Налобный фонарь (torch2_set_* из xr_actor.script). См. m_da2_* выше.
    void Torch2SetRange(float r);
    void Torch2SetRadius(float deg);
    void Torch2SetColorR(float v);
    void Torch2SetColorG(float v);
    void Torch2SetColorB(float v);
    void Torch2SetOffsetX(float v);
    void Torch2SetOffsetY(float v);
    void DaApplyBeam();
    // [DA_PORT] Единственное место, решающее, горит ли лампа игрока. См. Torch.cpp.
    void DaUpdateLightState();
    static int da_torch_count(CInventoryOwner* owner);
    // [DA_PORT] Сообщить скрипту, что фонарь погашен (itms_manager.Torch2). См. Torch.cpp.
    void DaSyncScriptSwitch();

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
