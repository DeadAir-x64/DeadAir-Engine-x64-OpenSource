#include "pch_script.h"
#include "Actor_Flags.h"
#include "HUDManager.h"

#ifdef DEBUG
#include "PHDebug.h"
#endif // DEBUG

#include "alife_space.h"
#include "Hit.h"
#include "PHDestroyable.h"
#include "Car.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "CameraLook.h"
#include "CameraFirstEye.h"
#include "EffectorFall.h"
#include "EffectorBobbing.h"
#include "ActorEffector.h"
#include "EffectorZoomInertion.h"
#include "SleepEffector.h"
#include "character_info.h"
#include "CustomOutfit.h"
#include "ActorCondition.h"
#include "UIGameCustom.h"
#include "xrPhysics/matrix_utils.h"
#include "clsid_game.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "Grenade.h"
#include "Torch.h"

// breakpoints
#include "xrEngine/xr_input.h"
//
#include "Actor.h"
#include "ActorAnimation.h"
#include "actor_anim_defs.h"
#include "HudItem.h"
#include "ai_sounds.h"
#include "ai_space.h"
#include "trade.h"
#include "Inventory.h"

#include "Level.h"
#include "GamePersistent.h"
#include "game_cl_base.h"
#include "game_cl_single.h"
#include "xrMessages.h"
#include "xrCDB/Intersect.hpp"

#include "alife_registry_wrappers.h"
#include "Include/xrRender/Kinematics.h"
#include "Artefact.h"
#include "CharacterPhysicsSupport.h"
#include "material_manager.h"
#include "xrPhysics/IColisiondamageInfo.h"
#include "ui/UIMainIngameWnd.h"
#include "ui/UIArtefactPanel.h"
#include "map_manager.h"
#include "GametaskManager.h"
#include "actor_memory.h"
#include "script_game_object.h"
#include "game_object_space.h"
#include "xrScriptEngine/script_callback_ex.h"
#include "InventoryBox.h"
#include "location_manager.h"
#include "player_hud.h"
#include "ai/monsters/basemonster/base_monster.h"

#include "Include/xrRender/UIRender.h"

#include "xrAICore/Navigation/ai_object_location.h"
#include "ui/UIMotionIcon.h"
#include "ui/UIActorMenu.h"
#include "ActorHelmet.h"
#include "ui/UIDragDropReferenceList.h"
#include "xrCore/xr_token.h"

#include "xrEngine/Rain.h"

//Alundaio
#include "script_hit.h"
//-Alundaio

//const u32 patch_frames = 50;
//const float respawn_delay = 1.f;
//const float respawn_auto = 7.f;

constexpr float default_feedback_duration = 0.2f;

extern float cammera_into_collision_shift;
extern int g_first_person_death;

string32 ACTOR_DEFS::g_quick_use_slots[4] = {};
// skeleton

Flags32 psActorFlags =
{
    AF_GODMODE_RT |
    AF_AUTOPICKUP |
    AF_RUN_BACKWARD |
    AF_IMPORTANT_SAVE |
    AF_MULTI_ITEM_PICKUP |
    AF_USE_TRACERS
};

float psLookIntensityMin  = 15.f;
float psLookIntensityMax  = 60.f;
float psLookIntensityStep = 0.7f;

float psCursorIntensityMin  = 5.f;
float psCursorIntensityMax  = 15.f;
float psCursorIntensityStep = 0.5f;

int psActorSleepTime = 1;

CActor::CActor() : CEntityAlive(), current_ik_cam_shift(0)
{
    encyclopedia_registry = xr_new<CEncyclopediaRegistryWrapper>();
    game_news_registry = xr_new<CGameNewsRegistryWrapper>();

    // Cameras
    cameras[eacFirstEye] = xr_new<CCameraFirstEye>(this);
    cameras[eacFirstEye]->Load("actor_firsteye_cam");

    if (strstr(Core.Params, "-psp"))
        psActorFlags.set(AF_PSP, TRUE);
    else
        psActorFlags.set(AF_PSP, FALSE);

    if (psActorFlags.test(AF_PSP))
    {
        cameras[eacLookAt] = xr_new<CCameraLook2>(this);
        cameras[eacLookAt]->Load("actor_look_cam_psp");
    }
    else
    {
        cameras[eacLookAt] = xr_new<CCameraLook>(this);
        cameras[eacLookAt]->Load("actor_look_cam");
    }
    cameras[eacFreeLook] = xr_new<CCameraLook>(this);
    cameras[eacFreeLook]->Load("actor_free_cam");
    cameras[eacFixedLookAt] = xr_new<CCameraFixedLook>(this);
    cameras[eacFixedLookAt]->Load("actor_look_cam");

    cam_active = eacFirstEye;
    fPrevCamPos = 0.0f;
    vPrevCamDir.set(0.f, 0.f, 1.f);
    fCurAVelocity = 0.0f;
    // эффекторы
    pCamBobbing = 0;

    r_torso.yaw = 0;
    r_torso.pitch = 0;
    r_torso.roll = 0;
    r_torso_tgt_roll = 0;
    r_model_yaw = 0;
    r_model_yaw_delta = 0;
    r_model_yaw_dest = 0;

    b_DropActivated = 0;
    f_DropPower = 0.f;

    m_fRunFactor = 2.f;
    m_fCrouchFactor = 0.2f;
    m_fClimbFactor = 1.f;
    m_fCamHeightFactor = 0.87f;

    m_fFallTime = s_fFallTime;
    m_bAnimTorsoPlayed = false;

    m_pPhysicsShell = NULL;

    m_fFeelGrenadeRadius = 10.0f;
    m_fFeelGrenadeTime = 1.0f;

    m_holder = NULL;
    m_holderID = u16(-1);

#ifdef DEBUG
    Device.seqRender.Add(this, REG_PRIORITY_LOW);
#endif

    //разрешить использование пояса в inventory
    inventory().SetBeltUseful(true);

    m_pPersonWeLookingAt = NULL;
    m_pVehicleWeLookingAt = NULL;
    m_pObjectWeLookingAt = NULL;
    m_bPickupMode = false;

    pStatGraph = NULL;

    m_pActorEffector = NULL;

    SetZoomAimingMode(false);

    m_fSprintFactor = 4.f;

    // hFriendlyIndicator.create(FVF::F_LIT,RCache.Vertex.Buffer(),RCache.QuadIB);

    m_pUsableObject = NULL;

    m_anims = xr_new<SActorMotions>();
    //Alundaio: Needed for car
    m_vehicle_anims = xr_new<SActorVehicleAnims>();
    //-Alundaio
    m_entity_condition = NULL;
    m_iLastHitterID = u16(-1);
    m_iLastHittingWeaponID = u16(-1);
    m_statistic_manager = NULL;
    //-----------------------------------------------------------------------------------
    m_memory = GEnv.isDedicatedServer ? 0 : xr_new<CActorMemory>(this);
    m_bOutBorder = false;
    m_hit_probability = 1.f;
    m_feel_touch_characters = 0;
    //-----------------------------------------------------------------------------------
    m_dwILastUpdateTime = 0;

    m_location_manager = xr_new<CLocationManager>(this);
    m_block_sprint_counter = 0;

    m_disabled_hitmarks = false;
    m_inventory_disabled = false;

    // Alex ADD: for smooth crouch fix
    CurrentHeight = -1.f;
}

CActor::~CActor()
{
    DestroyShadowVisual(); // [DA_PORT] теневая модель живёт отдельно от визуала актёра
    DestroyLegsVisual();   // [DA_PORT] и перволичные ноги тоже
    xr_delete(m_location_manager);
    xr_delete(m_memory);

    xr_delete(encyclopedia_registry);
    xr_delete(game_news_registry);
#ifdef DEBUG
    Device.seqRender.Remove(this);
#endif
    // xr_delete(Weapons);
    for (auto& camera : cameras)
        xr_delete(camera);

    m_HeavyBreathSnd.destroy();
    m_BloodSnd.destroy();
    m_DangerSnd.destroy();

    xr_delete(m_pActorEffector);

    xr_delete(m_pPhysics_support);

    xr_delete(m_anims);
    //Alundaio: For car
    xr_delete(m_vehicle_anims);
    //-Alundaio
}

void CActor::reinit()
{
    character_physics_support()->movement()->CreateCharacter();
    character_physics_support()->movement()->SetPhysicsRefObject(this);
    CEntityAlive::reinit();
    CInventoryOwner::reinit();

    character_physics_support()->in_Init();
    material().reinit();

    m_pUsableObject = NULL;
    if (!GEnv.isDedicatedServer)
        memory().reinit();

    set_input_external_handler(0);
    m_time_lock_accel = 0;
}

void CActor::reload(LPCSTR section)
{
    CEntityAlive::reload(section);
    CInventoryOwner::reload(section);
    material().reload(section);
    CStepManager::reload(section);
    if (!GEnv.isDedicatedServer)
        memory().reload(section);
    m_location_manager->reload(section);
}
void set_box(LPCSTR section, CPHMovementControl& mc, u32 box_num)
{
    Fbox bb;
    Fvector vBOX_center, vBOX_size;
    // m_PhysicMovementControl: BOX
    string64 buff, buff1;
    strconcat(sizeof(buff), buff, "ph_box", xr_itoa(box_num, buff1, 10), "_center");
    vBOX_center = pSettings->r_fvector3(section, buff);
    strconcat(sizeof(buff), buff, "ph_box", xr_itoa(box_num, buff1, 10), "_size");
    vBOX_size = pSettings->r_fvector3(section, buff);
    vBOX_size.y += cammera_into_collision_shift / 2.f;
    bb.set(vBOX_center, vBOX_center);
    bb.grow(vBOX_size);
    mc.SetBox(box_num, bb);
}
void CActor::Load(LPCSTR section)
{
    // Msg						("Loading actor: %s",section);
    inherited::Load(section);
    material().Load(section);
    CInventoryOwner::Load(section);
    m_location_manager->Load(section);

    if (GameID() == eGameIDSingle)
        OnDifficultyChanged();
    //////////////////////////////////////////////////////////////////////////
    ISpatial* self = smart_cast<ISpatial*>(this);
    if (self)
    {
        self->GetSpatialData().type |= STYPE_VISIBLEFORAI;
        self->GetSpatialData().type &= ~STYPE_REACTTOSOUND;
    }
    //////////////////////////////////////////////////////////////////////////

    // m_PhysicMovementControl: General
    // m_PhysicMovementControl->SetParent		(this);

    /*
    Fbox	bb;Fvector	vBOX_center,vBOX_size;
    // m_PhysicMovementControl: BOX
    vBOX_center= pSettings->r_fvector3	(section,"ph_box2_center"	);
    vBOX_size	= pSettings->r_fvector3	(section,"ph_box2_size"		);
    bb.set	(vBOX_center,vBOX_center); bb.grow(vBOX_size);
    character_physics_support()->movement()->SetBox		(2,bb);

    // m_PhysicMovementControl: BOX
    vBOX_center= pSettings->r_fvector3	(section,"ph_box1_center"	);
    vBOX_size	= pSettings->r_fvector3	(section,"ph_box1_size"		);
    bb.set	(vBOX_center,vBOX_center); bb.grow(vBOX_size);
    character_physics_support()->movement()->SetBox		(1,bb);

    // m_PhysicMovementControl: BOX
    vBOX_center= pSettings->r_fvector3	(section,"ph_box0_center"	);
    vBOX_size	= pSettings->r_fvector3	(section,"ph_box0_size"		);
    bb.set	(vBOX_center,vBOX_center); bb.grow(vBOX_size);
    character_physics_support()->movement()->SetBox		(0,bb);
    */

    //// m_PhysicMovementControl: Foots
    // Fvector	vFOOT_center= pSettings->r_fvector3	(section,"ph_foot_center"	);
    // Fvector	vFOOT_size	= pSettings->r_fvector3	(section,"ph_foot_size"		);
    // bb.set	(vFOOT_center,vFOOT_center); bb.grow(vFOOT_size);
    ////m_PhysicMovementControl->SetFoots	(vFOOT_center,vFOOT_size);

    // m_PhysicMovementControl: Crash speed and mass
    float cs_min = pSettings->r_float(section, "ph_crash_speed_min");
    float cs_max = pSettings->r_float(section, "ph_crash_speed_max");
    float mass = pSettings->r_float(section, "ph_mass");
    character_physics_support()->movement()->SetCrashSpeeds(cs_min, cs_max);
    character_physics_support()->movement()->SetMass(mass);
    if (pSettings->line_exist(section, "stalker_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtStalker, pSettings->r_float(section, "stalker_restrictor_radius"));
    if (pSettings->line_exist(section, "stalker_small_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtStalkerSmall, pSettings->r_float(section, "stalker_small_restrictor_radius"));
    if (pSettings->line_exist(section, "medium_monster_restrictor_radius"))
        character_physics_support()->movement()->SetActorRestrictorRadius(
            rtMonsterMedium, pSettings->r_float(section, "medium_monster_restrictor_radius"));
    character_physics_support()->movement()->Load(section);

    set_box(section, *character_physics_support()->movement(), 2);
    set_box(section, *character_physics_support()->movement(), 1);
    set_box(section, *character_physics_support()->movement(), 0);

    m_fWalkAccel = pSettings->r_float(section, "walk_accel");
    m_fJumpSpeed = pSettings->r_float(section, "jump_speed");
    m_fRunFactor = pSettings->r_float(section, "run_coef");
    m_fRunBackFactor = pSettings->r_float(section, "run_back_coef");
    m_fWalkBackFactor = pSettings->r_float(section, "walk_back_coef");
    m_fCrouchFactor = pSettings->r_float(section, "crouch_coef");
    m_fClimbFactor = pSettings->r_float(section, "climb_coef");
    m_fSprintFactor = pSettings->r_float(section, "sprint_koef");
    // [DA_PORT] Weight-based sprint penalty (Actor_Movement.cpp). The alpha reads these as required
    // keys, but they are absent from the release configs, so they are optional here with a 0 default =
    // no penalty, i.e. movement is untouched until the configs opt in. Beware when tuning: the penalty
    // is subtracted from sprint_koef (1.9) in kilograms, and caps at 1.5 — a value of 1.0 would drop
    // sprint to 0.4 for anyone holding a weapon, i.e. slower than walking.
    m_fSprintWeaponFactor = READ_IF_EXISTS(pSettings, r_float, section, "sprint_weapon_koef", 0.0f);
    m_fSprintOutfitFactor = READ_IF_EXISTS(pSettings, r_float, section, "sprint_outfit_koef", 0.0f);
    // [DA_PORT] hold-breath sway multiplier (see UpdateCL). DA: [actor] breath_koef = 0.02
    m_fBreathKoef = READ_IF_EXISTS(pSettings, r_float, section, "breath_koef", 0.02f);

    m_fWalk_StrafeFactor = READ_IF_EXISTS(pSettings, r_float, section, "walk_strafe_coef", 1.0f);
    m_fRun_StrafeFactor = READ_IF_EXISTS(pSettings, r_float, section, "run_strafe_coef", 1.0f);

    m_fCamHeightFactor = pSettings->r_float(section, "camera_height_factor");
    character_physics_support()->movement()->SetJumpUpVelocity(m_fJumpSpeed);
    float AirControlParam = pSettings->r_float(section, "air_control_param");
    character_physics_support()->movement()->SetAirControlParam(AirControlParam);

    m_fPickupInfoRadius = pSettings->r_float(section, "pickup_info_radius");

    m_fFeelGrenadeRadius = pSettings->read_if_exists<float>(section, "feel_grenade_radius", 10.0f);
    m_fFeelGrenadeTime = pSettings->read_if_exists<float>(section, "feel_grenade_time", 1.0f);
    m_fFeelGrenadeTime *= 1000.0f;

    character_physics_support()->in_Load(section);

    if (!GEnv.isDedicatedServer)
    {
        string256 buf;

        cpcstr hit_snd_sect = pSettings->r_string(section, "hit_sounds");
        for (int hit_type = 0; hit_type < (int)ALife::eHitTypeMax; ++hit_type)
        {

            cpcstr hit_name = ALife::g_cafHitType2String((ALife::EHitType)hit_type);
            cpcstr hit_snds = READ_IF_EXISTS(pSettings, r_string, hit_snd_sect, hit_name, "");
            const int cnt = _GetItemCount(hit_snds);
#ifndef MASTER_GOLD
            if (cnt == 0)
            {
                Msg("~ [%s] is missing sounds for type [%s]", hit_snd_sect, hit_name);
            }
#endif
            sndHit[hit_type].reserve(cnt);
            for (int i = 0; i < cnt; ++i)
            {
                sndHit[hit_type].emplace_back().create(_GetItem(hit_snds, i, buf), st_Effect, sg_SourceType);
            }
        }

        sndDie[0].create(strconcat(buf, cName().c_str(), "\\die0"), st_Effect, SOUND_TYPE_MONSTER_DYING);
        sndDie[1].create(strconcat(buf, cName().c_str(), "\\die1"), st_Effect, SOUND_TYPE_MONSTER_DYING);
        sndDie[2].create(strconcat(buf, cName().c_str(), "\\die2"), st_Effect, SOUND_TYPE_MONSTER_DYING);
        sndDie[3].create(strconcat(buf, cName().c_str(), "\\die3"), st_Effect, SOUND_TYPE_MONSTER_DYING);

        m_HeavyBreathSnd.create(
            pSettings->r_string(section, "heavy_breath_snd"), st_Effect, SOUND_TYPE_MONSTER_INJURING);
        m_BloodSnd.create(pSettings->r_string(section, "heavy_blood_snd"), st_Effect, SOUND_TYPE_MONSTER_INJURING);
        if (!pSettings->line_exist(section, "heavy_danger_snd"))
            m_DangerSnd = m_BloodSnd;
        else
        {
            m_DangerSnd.create(pSettings->r_string(section, "heavy_danger_snd"),
                st_Effect, SOUND_TYPE_MONSTER_INJURING);
        }
    }
    if (psActorFlags.test(AF_PSP))
        cam_Set(eacLookAt);
    else
        cam_Set(eacFirstEye);

    // sheduler
    shedule.t_min = shedule.t_max = 1;

    // настройки дисперсии стрельбы
    m_fDispBase = pSettings->r_float(section, "disp_base");
    m_fDispBase = deg2rad(m_fDispBase);

    m_fDispAim = pSettings->r_float(section, "disp_aim");
    m_fDispAim = deg2rad(m_fDispAim);

    m_fDispVelFactor = pSettings->r_float(section, "disp_vel_factor");
    m_fDispAccelFactor = pSettings->r_float(section, "disp_accel_factor");
    m_fDispCrouchFactor = pSettings->r_float(section, "disp_crouch_factor");
    m_fDispCrouchNoAccelFactor = pSettings->r_float(section, "disp_crouch_no_acc_factor");

    LPCSTR default_outfit = READ_IF_EXISTS(pSettings, r_string, section, "default_outfit", 0);
    SetDefaultVisualOutfit(default_outfit);

    invincibility_fire_shield_1st = READ_IF_EXISTS(pSettings, r_string, section, "Invincibility_Shield_1st", 0);
    invincibility_fire_shield_3rd = READ_IF_EXISTS(pSettings, r_string, section, "Invincibility_Shield_3rd", 0);
    //-----------------------------------------
    m_AutoPickUp_AABB =
        READ_IF_EXISTS(pSettings, r_fvector3, section, "AutoPickUp_AABB", Fvector().set(0.02f, 0.02f, 0.02f));
    m_AutoPickUp_AABB_Offset =
        READ_IF_EXISTS(pSettings, r_fvector3, section, "AutoPickUp_AABB_offs", Fvector().set(0, 0, 0));

    m_sCharacterUseAction = "character_use";
    m_sDeadCharacterUseAction = "dead_character_use";
    m_sDeadCharacterUseOrDragAction = "dead_character_use_or_drag";
    m_sDeadCharacterDontUseAction = "dead_character_dont_use";
    m_sCarCharacterUseAction = "car_character_use";
    m_sInventoryItemUseAction = "inventory_item_use";
    m_sInventoryBoxUseAction = "inventory_box_use";
    //---------------------------------------------------------------------
    m_sHeadShotParticle = READ_IF_EXISTS(pSettings, r_string, section, "HeadShotParticle", 0);
}

void CActor::PHHit(SHit& H) { m_pPhysics_support->in_Hit(H, false); }
struct playing_pred
{
    IC bool operator()(ref_sound& s) { return (NULL != s._feedback()); }
};

void CActor::Hit(SHit* pHDS)
{
    bool b_initiated = pHDS->aim_bullet; // physics strike by poltergeist

    pHDS->aim_bullet = false;

    SHit& HDS = *pHDS;
    if (HDS.hit_type < ALife::eHitTypeBurn || HDS.hit_type >= ALife::eHitTypeMax)
    {
        string256 err;
        xr_sprintf(err, "Unknown/unregistered hit type [%d]", HDS.hit_type);
        R_ASSERT2(0, err);
    }
#ifdef DEBUG
    if (ph_dbg_draw_mask.test(phDbgCharacterControl))
    {
        DBG_OpenCashedDraw();
        Fvector to;
        to.add(Position(), Fvector().mul(HDS.dir, HDS.phys_impulse()));
        DBG_DrawLine(Position(), to, color_xrgb(124, 124, 0));
        DBG_ClosedCashedDraw(500);
    }
#endif // DEBUG

    float feedback_duration = default_feedback_duration;
    bool bPlaySound = true;
    if (!g_Alive())
        bPlaySound = false;

    if (!IsGameTypeSingle() && !GEnv.isDedicatedServer)
    {
        game_PlayerState* ps = Game().GetPlayerByGameID(ID());
        if (ps && ps->testFlag(GAME_PLAYER_FLAG_INVINCIBLE))
        {
            bPlaySound = false;
            if (Device.dwFrame != last_hit_frame && HDS.bone() != BI_NONE)
            {
                // вычислить позицию и направленность партикла
                Fmatrix pos;

                CParticlesPlayer::MakeXFORM(this, HDS.bone(), HDS.dir, HDS.p_in_bone_space, pos);

                // установить particles
                CParticlesObject* ps = NULL;

                if (eacFirstEye == cam_active && this == Level().CurrentEntity())
                    ps = CParticlesObject::Create(invincibility_fire_shield_1st, TRUE);
                else
                    ps = CParticlesObject::Create(invincibility_fire_shield_3rd, TRUE);

                ps->UpdateParent(pos, Fvector().set(0.f, 0.f, 0.f));
                GamePersistent().ps_needtoplay.push_back(ps);
            };
        };

        last_hit_frame = Device.dwFrame;
    };

    if (!GEnv.isDedicatedServer && !sndHit[HDS.hit_type].empty() && conditions().PlayHitSound(pHDS))
    {
        ref_sound& S = sndHit[HDS.hit_type][Random.randI(sndHit[HDS.hit_type].size())];
        bool b_snd_hit_playing = sndHit[HDS.hit_type].end() !=
            std::find_if(sndHit[HDS.hit_type].begin(), sndHit[HDS.hit_type].end(), playing_pred());

        if (ALife::eHitTypeExplosion == HDS.hit_type)
        {
            if (this == Level().CurrentControlEntity())
            {
                S.set_volume(10.0f);
                if (!m_sndShockEffector)
                {
                    m_sndShockEffector = xr_new<SndShockEffector>();
                    m_sndShockEffector->Start(this, float(S.get_length_sec() * 1000.0f), HDS.damage());
                }
            }
            else
                bPlaySound = false;
        }
        if (bPlaySound && !b_snd_hit_playing)
        {
            Fvector point = Position();
            point.y += CameraHeight();
            S.play_at_pos(this, point);
        }
        if (S.get_length_sec() > default_feedback_duration)
            feedback_duration = S.get_length_sec();
    }

    float high_freq_feedback = clampr(HDS.damage(), 0.f, 1.f);
    if (high_freq_feedback < 0.01f)
        high_freq_feedback *= 100.f;
    else
        high_freq_feedback *= 10.f;

    float low_freq_feedback;
    switch (HDS.hit_type)
    {
    case ALife::eHitTypeShock:
    case ALife::eHitTypeRadiation:
        low_freq_feedback = 0.f;
        break;

    case ALife::eHitTypeBurn:
    case ALife::eHitTypeLightBurn:
    case ALife::eHitTypeChemicalBurn:
    case ALife::eHitTypeTelepatic:
        if (HDS.damage() < 0.01f)
        {
            low_freq_feedback = 0.f;
            break;
        }
        [[fallthrough]];

    default:
    {
        low_freq_feedback = clampr(HDS.phys_impulse(), 0.f, 1.f);
        if (fis_zero(low_freq_feedback))
            low_freq_feedback = high_freq_feedback;
        else if (low_freq_feedback >= 10.f)
            low_freq_feedback /= 100.f;
        else if (low_freq_feedback >= 1.f)
            low_freq_feedback /= 10.f;
        else if (low_freq_feedback >= 0.1f)
            low_freq_feedback /= 5.f;
        break;
    }
    } // switch (HDS.hit_type)

    // Feedback with low freq for a little,
    // feedback with high freq rest of the time.
    if (!GEnv.isDedicatedServer && ALife::eHitTypeExplosion == HDS.hit_type)
    {
        m_controller_feedback =
        {
            /*.high_freq    =*/ high_freq_feedback,
            /*.duration     =*/ feedback_duration,
            /*.submit_time  =*/ Device.fTimeGlobal,
            /*.update_time  =*/ Device.fTimeGlobal + default_feedback_duration,
            /*.needs_update =*/ true
        };
    }

    if (!GEnv.isDedicatedServer && !m_sndShockEffector)
    {
        pInput->Feedback(CInput::FeedbackController, low_freq_feedback, high_freq_feedback, feedback_duration);
    }

    // slow actor, only when he gets hit
    m_hit_slowmo = conditions().HitSlowmo(pHDS);

    //---------------------------------------------------------------
    if ((Level().CurrentViewEntity() == this) && !GEnv.isDedicatedServer && (HDS.hit_type == ALife::eHitTypeFireWound))
    {
        IGameObject* pLastHitter = Level().Objects.net_Find(m_iLastHitterID);
        IGameObject* pLastHittingWeapon = Level().Objects.net_Find(m_iLastHittingWeaponID);
        HitSector(pLastHitter, pLastHittingWeapon);
    }

    if ((mstate_real & mcSprint) && Level().CurrentControlEntity() == this && conditions().DisableSprint(pHDS))
    {
        bool const is_special_burn_hit_2_self = (pHDS->who == this) && (pHDS->boneID == BI_NONE) &&
            ((pHDS->hit_type == ALife::eHitTypeBurn) || (pHDS->hit_type == ALife::eHitTypeLightBurn));
        if (!is_special_burn_hit_2_self)
        {
            mstate_wishful &= ~mcSprint;
        }
    }
    if (!GEnv.isDedicatedServer && !m_disabled_hitmarks)
    {
        bool b_fireWound = (pHDS->hit_type == ALife::eHitTypeFireWound || pHDS->hit_type == ALife::eHitTypeWound_2);
        b_initiated = b_initiated && (pHDS->hit_type == ALife::eHitTypeStrike);

        if (b_fireWound || b_initiated)
            HitMark(HDS.damage(), HDS.dir, HDS.who, HDS.bone(), HDS.p_in_bone_space, HDS.impulse, HDS.hit_type);
    }

    if (IsGameTypeSingle())
    {
        if (GodMode())
        {
            HDS.power = 0.0f;
            inherited::Hit(&HDS);
            return;
        }

        HDS.power = HitArtefactsOnBelt(HDS.damage(), HDS.hit_type);
        HDS.add_wound = true;
        if (g_Alive())
        {
            CScriptHit tLuaHit;

            tLuaHit.m_fPower = HDS.power;
            tLuaHit.m_fImpulse = HDS.impulse;
            tLuaHit.m_tDirection = HDS.direction();
            tLuaHit.m_tHitType = HDS.hit_type;
            tLuaHit.m_tpDraftsman = smart_cast<const CGameObject*>(HDS.who)->lua_game_object();

            luabind::functor<bool> funct;
            if (GEnv.ScriptEngine->functor("_G.CActor__BeforeHitCallback", funct))
            {
                if (!funct(smart_cast<CGameObject*>(this->lua_game_object()), &tLuaHit, HDS.boneID))
                    return;
            }

            HDS.power = tLuaHit.m_fPower;
            HDS.impulse = tLuaHit.m_fImpulse;
            HDS.dir = tLuaHit.m_tDirection;
            HDS.hit_type = (ALife::EHitType)(tLuaHit.m_tHitType);
            //HDS.who = smart_cast<CObject*>(tLuaHit.m_tpDraftsman->object());
            //HDS.whoID = tLuaHit.m_tpDraftsman->ID();

            /* AVO: send script callback*/
            callback(GameObject::eHit)(
                this->lua_game_object(),
                HDS.damage(),
                HDS.direction(),
                smart_cast<const CGameObject*>(HDS.who)->lua_game_object(),
                HDS.boneID
            );
        }
        inherited::Hit(&HDS);
    }
    else
    {
        m_bWasBackStabbed = false;
        if (HDS.hit_type == ALife::eHitTypeWound_2 && Check_for_BackStab_Bone(HDS.bone()))
        {
            // convert impulse into local coordinate system
            Fmatrix mInvXForm;
            mInvXForm.invert(XFORM());
            Fvector vLocalDir;
            mInvXForm.transform_dir(vLocalDir, HDS.dir);
            vLocalDir.invert();

            Fvector a = {0, 0, 1};
            float res = a.dotproduct(vLocalDir);
            if (res < -0.707)
            {
                game_PlayerState* ps = Game().GetPlayerByGameID(ID());

                if (!ps || !ps->testFlag(GAME_PLAYER_FLAG_INVINCIBLE))
                    m_bWasBackStabbed = true;
            }
        };

        float hit_power = 0.0f;

        if (m_bWasBackStabbed)
            hit_power = (HDS.damage() == 0) ? 0 : 100000.0f;
        else
            hit_power = HitArtefactsOnBelt(HDS.damage(), HDS.hit_type);

        HDS.power = hit_power;
        HDS.add_wound = true;
        inherited::Hit(&HDS);

        if (OnServer() && !g_Alive() && HDS.hit_type == ALife::eHitTypeExplosion)
        {
            game_PlayerState* ps = Game().GetPlayerByGameID(ID());
            Game().m_WeaponUsageStatistic->OnExplosionKill(ps, HDS);
        }
    }
}

void CActor::HitMark(float P, Fvector dir, IGameObject* who_object, s16 element, Fvector position_in_bone_space,
    float impulse, ALife::EHitType hit_type_)
{
    // hit marker
    if (/*(hit_type==ALife::eHitTypeFireWound||hit_type==ALife::eHitTypeWound_2) && */
        g_Alive() && Local() && (Level().CurrentEntity() == this))
    {
        HUD().HitMarked(dir);

        CEffectorCam* ce = Cameras().GetCamEffector((ECamEffectorType)effFireHit);
        if (ce)
            return;

        int id = -1;
        Fvector cam_pos, cam_dir, cam_norm;
        cam_Active()->Get(cam_pos, cam_dir, cam_norm);
        cam_dir.normalize_safe();
        dir.normalize_safe();

        float ang_diff = angle_difference(cam_dir.getH(), dir.getH());
        Fvector cp;
        cp.crossproduct(cam_dir, dir);
        bool bUp = (cp.y > 0.0f);

        Fvector cross;
        cross.crossproduct(cam_dir, dir);
        VERIFY(ang_diff >= 0.0f && ang_diff <= PI);

        float _s1 = PI_DIV_8;
        float _s2 = _s1 + PI_DIV_4;
        float _s3 = _s2 + PI_DIV_4;
        float _s4 = _s3 + PI_DIV_4;

        if (ang_diff <= _s1)
        {
            id = 2;
        }
        else if (ang_diff > _s1 && ang_diff <= _s2)
        {
            id = (bUp) ? 5 : 7;
        }
        else if (ang_diff > _s2 && ang_diff <= _s3)
        {
            id = (bUp) ? 3 : 1;
        }
        else if (ang_diff > _s3 && ang_diff <= _s4)
        {
            id = (bUp) ? 4 : 6;
        }
        else if (ang_diff > _s4)
        {
            id = 0;
        }
        else
        {
            VERIFY(0);
        }

        string64 sect_name;
        xr_sprintf(sect_name, "effector_fire_hit_%d", id);
        AddEffector(this, effFireHit, sect_name, P * 0.001f);

    } // if hit_type
}

void CActor::HitSignal(float perc, Fvector& vLocalDir, IGameObject* who, s16 element)
{
    //AVO: get bone names from IDs
    //cpcstr bone_name = smart_cast<IKinematics*>(this->Visual())->LL_BoneName_dbg(element);
    //Msg("Bone [%d]->[%s]", element, bone_name);
    //-AVO

    if (g_Alive())
    {
        // check damage bone
        Fvector D;
        XFORM().transform_dir(D, vLocalDir);

        float yaw, pitch;
        D.getHP(yaw, pitch);
        IRenderVisual* pV = Visual();
        IKinematicsAnimated* tpKinematics = smart_cast<IKinematicsAnimated*>(pV);
        IKinematics* pK = smart_cast<IKinematics*>(pV);
        VERIFY(tpKinematics);
#pragma todo("Dima to Dima : forward-back bone impulse direction has been determined incorrectly!")
        MotionID motion_ID = m_anims->m_normal.m_damage[iFloor(pK->LL_GetBoneInstance(element).get_param(1) +
            (angle_difference(r_model_yaw + r_model_yaw_delta, yaw) <= PI_DIV_2 ? 0 : 1))];
        float power_factor = perc / 100.f;
        clamp(power_factor, 0.f, 1.f);
        VERIFY(motion_ID.valid());
        tpKinematics->PlayFX(motion_ID, power_factor);
    }
}
void start_tutorial(LPCSTR name);
void CActor::Die(IGameObject* who)
{
#ifdef DEBUG
    Msg("--- Actor [%s] dies !", this->Name());
#endif // #ifdef DEBUG
    inherited::Die(who);

    // [DA_PORT] Труп рисуется обычным путём и в третьем лице, поэтому перволичное тело
    // (`actors\legs\*.ogf` — без рук и головы) в качестве трупа выглядело бы поломанным. Меняем
    // визуал на ту же полную NPC-модель, которой до этого рисовалась тень, и теневую модель
    // отпускаем: живому она была нужна, мёртвому нет.
    //
    // Порядок важен: сначала гасим теневую (её кости привязаны к старому скелету), потом меняем
    // визуал. RebuildShadowVisual из OnChangeVisual увидит !g_Alive() и ничего не соберёт.
    if (IsGameTypeSingle())
    {
        if (CCustomOutfit* outfit = GetOutfit())
        {
            const shared_str full = da_actor_full_visual(outfit->cNameSect().c_str());
            if (full.size() && full != cNameVisual())
            {
                DestroyShadowVisual();
                ChangeVisual(full);
            }
        }
    }

    if (OnServer())
    {
        u16 I = inventory().FirstSlot();
        u16 E = inventory().LastSlot();

        for (; I <= E; ++I)
        {
            PIItem item_in_slot = inventory().ItemFromSlot(I);
            if (I == inventory().GetActiveSlot())
            {
                if (item_in_slot)
                {
                    if (IsGameTypeSingle())
                    {
                        CGrenade* grenade = smart_cast<CGrenade*>(item_in_slot);
                        if (grenade)
                            grenade->DropGrenade();
                        else
                            item_in_slot->SetDropManual(TRUE);
                    }
                    else
                    {
                        // This logic we do on a server site
                        /*
                        if ((*I).m_pIItem->object().CLS_ID != CLSID_OBJECT_W_KNIFE)
                        {
                            (*I).m_pIItem->SetDropManual(TRUE);
                        }*/
                    }
                };
                continue;
            }
            else
            {
                CCustomOutfit* pOutfit = smart_cast<CCustomOutfit*>(item_in_slot);
                if (pOutfit)
                    continue;
            };
            if (item_in_slot)
                inventory().Ruck(item_in_slot);
        };

        ///!!! чистка пояса
        TIItemContainer& l_blist = inventory().m_belt;
        while (!l_blist.empty())
            inventory().Ruck(l_blist.front());

        if (!IsGameTypeSingle())
        {
            // if we are on server and actor has PDA - destroy PDA
            for (auto& l_it : inventory().m_ruck)
            {
                if (GameID() == eGameIDArtefactHunt)
                {
                    auto pArtefact = smart_cast<CArtefact*>(l_it);
                    if (pArtefact)
                    {
                        l_it->SetDropManual(true);
                        continue;
                    }
                }

                if (l_it->object().CLS_ID == CLSID_OBJECT_PLAYERS_BAG)
                {
                    l_it->SetDropManual(true);
                    continue;
                }
            }
        }
    }

    if (!GEnv.isDedicatedServer)
    {
        sndDie[Random.randI(SND_DIE_COUNT)].play_at_pos(this, Position());

        m_HeavyBreathSnd.stop();
        m_BloodSnd.stop();
        m_DangerSnd.stop();
    }

    if (IsGameTypeSingle())
    {
        pcstr camera = READ_IF_EXISTS(pSettingsOpenXRay, r_string, "gameplay", "actor_death_camera", "freelook");

        if (xr_strcmp("firsteye", camera) == 0 || g_first_person_death)
            cam_Set(eacFirstEye);
        else if (xr_strcmp("freelook", camera) == 0)
            cam_Set(eacFreeLook);
        else if (xr_strcmp("fixedlook", camera) == 0)
            cam_Set(eacFixedLookAt);

        CurrentGameUI()->HideShownDialogs();
        start_tutorial("game_over");
    }
    else
    {
        cam_Set(eacFixedLookAt);
    }

    mstate_wishful &= ~mcAnyMove;
    mstate_real &= ~mcAnyMove;

    xr_delete(m_sndShockEffector);
}

void CActor::SwitchOutBorder(bool new_border_state)
{
    if (new_border_state)
    {
        callback(GameObject::eExitLevelBorder)(lua_game_object());
    }
    else
    {
        //.		Msg("enter level border");
        callback(GameObject::eEnterLevelBorder)(lua_game_object());
    }
    m_bOutBorder = new_border_state;
}

void CActor::g_Physics(Fvector& _accel, float jump, float dt)
{
    // Correct accel
    Fvector accel;
    accel.set(_accel);
    m_hit_slowmo -= dt;
    if (m_hit_slowmo < 0)
        m_hit_slowmo = 0.f;

    accel.mul(1.f - m_hit_slowmo);

    if (g_Alive())
    {
        if (mstate_real & mcClimb && !cameras[eacFirstEye]->bClampYaw)
            accel.set(0.f, 0.f, 0.f);
        character_physics_support()->movement()->Calculate(accel, cameras[cam_active]->vDirection, 0, jump, dt, false);
        bool new_border_state = character_physics_support()->movement()->isOutBorder();
        if (m_bOutBorder != new_border_state && Level().CurrentControlEntity() == this)
        {
            SwitchOutBorder(new_border_state);
        }
#ifndef MASTER_GOLD
        if (!psActorFlags.test(AF_NO_CLIP))
#endif
        {
            character_physics_support()->movement()->GetPosition(Position());
        }
        character_physics_support()->movement()->bSleep = false;
    }

    if (Local() && g_Alive())
    {
        if (character_physics_support()->movement()->gcontact_Was)
            Cameras().AddCamEffector(xr_new<CEffectorFall>(character_physics_support()->movement()->gcontact_Power));

        if (!fis_zero(character_physics_support()->movement()->gcontact_HealthLost))
        {
            VERIFY(character_physics_support());
            VERIFY(character_physics_support()->movement());
            ICollisionDamageInfo* di = character_physics_support()->movement()->CollisionDamageInfo();
            VERIFY(di);
            bool b_hit_initiated = di->GetAndResetInitiated();
            Fvector hdir;
            di->HitDir(hdir);
            SetHitInfo(this, NULL, 0, Fvector().set(0, 0, 0), hdir);
            //				Hit
            //(m_PhysicMovementControl->gcontact_HealthLost,hdir,di->DamageInitiator(),m_PhysicMovementControl->ContactBone(),di->HitPos(),0.f,ALife::eHitTypeStrike);//s16(6
            //+ 2*::Random.randI(0,2))
            if (Level().CurrentControlEntity() == this)
            {
                SHit HDS = SHit(character_physics_support()->movement()->gcontact_HealthLost,
                    //.								0.0f,
                    hdir, di->DamageInitiator(), character_physics_support()->movement()->ContactBone(), di->HitPos(),
                    0.f, di->HitType(), 0.0f, b_hit_initiated);
                //				Hit(&HDS);

                NET_Packet l_P;
                HDS.GenHeader(GE_HIT, ID());
                HDS.whoID = di->DamageInitiator()->ID();
                HDS.weaponID = di->DamageInitiator()->ID();
                HDS.Write_Packet(l_P);

                u_EventSend(l_P);
            }
        }
    }
}
extern ENGINE_API float g_fov;
// [DA_PORT] like CoC-Xray (Actor.cpp): fake fov a zoom texture (scope overlay) is calibrated
// for, so scope magnification is screen-fov independent. Console: "scope_fov".
float g_scope_fov = 75.0f;

float CActor::currentFOV()
{
    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2))
        return g_fov;

    CWeapon* pWeapon = smart_cast<CWeapon*>(inventory().ActiveItem());

    if (eacFirstEye == cam_active && pWeapon && pWeapon->IsZoomed())
    {
        // [DA_PORT] Dead Air (CoC lineage) treats zoom factors as optical MAGNIFICATION
        // multipliers (their data: 1.1x..4x), not a target fov in degrees. The old degrees
        // math returned factor*0.75 => a 1.35° camera for an 1.8x sight ("zoom to the
        // stratosphere"). True-optics formula ported from CoC-Xray (Alundaio).
        if (!pWeapon->ZoomTexture())
            return atan(tan(g_fov * (0.5f * PI / 180.f)) / pWeapon->GetZoomFactor()) / (0.5f * PI / 180.f);

        if (!pWeapon->IsRotatingToZoom())
            return atan(tan(g_scope_fov * (0.5f * PI / 180.f)) / pWeapon->GetZoomFactor()) / (0.5f * PI / 180.f);
    }
    return g_fov;
}

void CActor::UpdateCL()
{
    if (m_item_placement_active) // [DA_PORT] keep the placement ghost tracking the crosshair
        UpdateItemPlacement();

    if (g_Alive() && Level().CurrentViewEntity() == this)
    {
        if (CurrentGameUI() && !CurrentGameUI()->TopInputReceiver() && !m_holder)
        {
            const bool allowed = psActorFlags.test(AF_MULTI_ITEM_PICKUP);

            for (u8 i = 0; i < bindtypes_count && allowed; ++i)
            {
                const int dik = GetActionDik(kUSE, i);
                if (dik && pInput->iGetAsyncKeyState(dik))
                    m_bPickupMode = true;
            }
        }
        else
        {
            m_bPickupMode = false;
        }
    }

    UpdateInventoryOwner(Device.dwTimeDelta);

    if (m_feel_touch_characters > 0)
    {
        for (auto& it : feel_touch)
        {
            auto sh = smart_cast<CPhysicsShellHolder*>(it);
            if (sh)
            {
                auto shcps = sh->character_physics_support();
                if (shcps)
                    shcps->movement()->UpdateObjectBox(shcps->movement()->PHCharacter());
            }
        }
    }
    if (m_holder)
        m_holder->UpdateEx(currentFOV());

    m_snd_noise -= 0.3f * Device.fTimeDelta;

    inherited::UpdateCL();
    m_pPhysics_support->in_UpdateCL();

    if (g_Alive())
        PickupModeUpdate();

    PickupModeUpdate_COD();

    SetZoomAimingMode(false);
    CWeapon* pWeapon = smart_cast<CWeapon*>(inventory().ActiveItem());

    cam_Update(float(Device.dwTimeDelta) / 1000.0f, currentFOV());

    Device.OnCameraUpdated();

    if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID())
    {
        psHUD_Flags.set(HUD_CROSSHAIR_RT2, true);
        psHUD_Flags.set(HUD_DRAW_RT, true);
    }
    if (pWeapon)
    {
        if (pWeapon->IsZoomed())
        {
            float full_fire_disp = pWeapon->GetFireDispersion(true);

            // [DA_PORT] Dead Air aim-sway: extra sway from actor psy/power (scripts feed it each frame
            // via set_actor_zoom_inertion; 0 => no extra), then hold-breath — holding the accel/sprint
            // ACTION (mcAccel, rebind-safe) while aiming steadies the aim by breath_koef (DA=0.02).
            full_fire_disp *= (1.f + m_fZoomInertionScale);
            if (mstate_real & mcAccel)
                full_fire_disp *= m_fBreathKoef;

            CEffectorZoomInertion* S = smart_cast<CEffectorZoomInertion*>(Cameras().GetCamEffector(eCEZoom));
            if (S)
                S->SetParams(full_fire_disp);

            SetZoomAimingMode(true);
        }

        if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID())
        {
            float fire_disp_full = pWeapon->GetFireDispersion(true, true);
            m_fdisp_controller.SetDispertion(fire_disp_full);

            fire_disp_full = m_fdisp_controller.GetCurrentDispertion();

            HUD().SetCrosshairDisp(fire_disp_full, 0.02f);
            HUD().ShowCrosshair(pWeapon->use_crosshair());
#ifdef DEBUG
            HUD().SetFirstBulletCrosshairDisp(pWeapon->GetFirstBulletDisp());
#endif

            BOOL B = !((mstate_real & mcLookout) && !IsGameTypeSingle());

            psHUD_Flags.set(HUD_WEAPON_RT, B);

            B = B && pWeapon->show_crosshair();

            psHUD_Flags.set(HUD_CROSSHAIR_RT2, B);

            psHUD_Flags.set(HUD_DRAW_RT, pWeapon->show_indicators());
        }
    }
    else
    {
        if (Level().CurrentEntity() && this->ID() == Level().CurrentEntity()->ID())
        {
            HUD().SetCrosshairDisp(0.f);
            HUD().ShowCrosshair(false);
        }
    }

    UpdateDefferedMessages();

    if (g_Alive())
        CStepManager::update(this == Level().CurrentViewEntity());

    spatial.type |= STYPE_REACTTOSOUND;

    if (m_sndShockEffector)
    {
        if (this == Level().CurrentViewEntity())
        {
            m_sndShockEffector->Update();

            if (!m_sndShockEffector->InWork() || !g_Alive())
                xr_delete(m_sndShockEffector);
        }
        else
            xr_delete(m_sndShockEffector);
    }
    // Feedback with low freq for a little,
    // feedback with high freq rest of the time.
    if (m_controller_feedback.needs_update)
    {
        if (Device.fTimeGlobal >= m_controller_feedback.update_time)
        {
            const float remaining_duration = m_controller_feedback.duration - (Device.fTimeGlobal - m_controller_feedback.submit_time);

            if (remaining_duration <= EPS_L)
                m_controller_feedback = {};
            else
            {
                const float frequency_part = m_controller_feedback.high_freq / m_controller_feedback.duration;
                const float frequency = m_controller_feedback.high_freq - frequency_part;

                pInput->Feedback(CInput::FeedbackController, 0.f, frequency, remaining_duration);
                m_controller_feedback =
                {
                    /*.high_freq    =*/ frequency,
                    /*.duration     =*/ remaining_duration,
                    /*.submit_time  =*/ Device.fTimeGlobal,
                    /*.update_time  =*/ Device.fTimeGlobal + default_feedback_duration,
                    /*.needs_update =*/ true
                };
            }
        }

    }

    Fmatrix trans;
    if (cam_Active() == cam_FirstEye())
    {
        Cameras().hud_camera_Matrix(trans);
    }
    else
        Cameras().camera_Matrix(trans);

    if (IsFocused())
        g_player_hud->update(trans);

    if (psActorFlags.test(AF_MULTI_ITEM_PICKUP))
        m_bPickupMode = false;
}

float NET_Jump = 0;
void CActor::set_state_box(u32 mstate)
{
    if (mstate & mcCrouch)
    {
        if (isActorAccelerated(mstate_real, IsZoomAimingMode()))
            character_physics_support()->movement()->ActivateBox(1, true);
        else
            character_physics_support()->movement()->ActivateBox(2, true);
    }
    else
        character_physics_support()->movement()->ActivateBox(0, true);
}
void CActor::shedule_Update(u32 DT)
{
    setSVU(OnServer());
    //.	UpdateInventoryOwner			(DT);

    if (IsFocused())
    {
        BOOL bHudView = HUDview();
        if (bHudView)
        {
            CInventoryItem* pInvItem = inventory().ActiveItem();
            if (pInvItem)
            {
                CHudItem* pHudItem = smart_cast<CHudItem*>(pInvItem);
                if (pHudItem)
                {
                    if (pHudItem->IsHidden())
                    {
                        g_player_hud->detach_item(pHudItem);
                    }
                    else
                    {
                        g_player_hud->attach_item(pHudItem);
                    }
                }
            }
            else
            {
                g_player_hud->detach_item_idx(0);
                // Msg("---No active item in inventory(), item 0 detached.");
            }
        }
        else
        {
            g_player_hud->detach_all_items();
            // Msg("---No hud view found, all items detached.");
        }
    }

    if (m_holder || !getEnabled() || !Ready())
    {
        m_sDefaultObjAction = nullptr;
        inherited::shedule_Update(DT);
        return;
    }

    clamp(DT, 0u, 100u);
    float dt = float(DT) / 1000.f;

    // Check controls, create accel, prelimitary setup "mstate_real"

    //----------- for E3 -----------------------------
    //	if (Local() && (OnClient() || Level().CurrentEntity()==this))
    if (Level().CurrentControlEntity() == this && !Level().IsDemoPlay())
    //------------------------------------------------
    {
        g_cl_CheckControls(mstate_wishful, NET_SavedAccel, NET_Jump, dt);
        {
            /*
            if (mstate_real & mcJump)
            {
                NET_Packet	P;
                u_EventGen(P, GE_ACTOR_JUMPING, ID());
                P.w_sdir(NET_SavedAccel);
                P.w_float(NET_Jump);
                u_EventSend(P);
            }
            */
        }
        g_cl_Orientate(mstate_real, dt);
        g_Orientate(mstate_real, dt);

        g_Physics(NET_SavedAccel, NET_Jump, dt);

        g_cl_ValidateMState(dt, mstate_wishful);
        g_SetAnimation(mstate_real);

        // Check for game-contacts
        Fvector C;
        float R;
        // m_PhysicMovementControl->GetBoundingSphere	(C,R);

        Center(C);
        R = Radius();
        feel_touch_update(C, R);
        Feel_Grenade_Update(m_fFeelGrenadeRadius);

        // Dropping
        if (b_DropActivated)
        {
            f_DropPower += dt * 0.1f;
            clamp(f_DropPower, 0.f, 1.f);
        }
        else
        {
            f_DropPower = 0.f;
        }
        if (!Level().IsDemoPlay())
        {
            mstate_wishful &= ~mcAccel;
            mstate_wishful &= ~mcLStrafe;
            mstate_wishful &= ~mcRStrafe;
            mstate_wishful &= ~mcLLookout;
            mstate_wishful &= ~mcRLookout;
            mstate_wishful &= ~mcFwd;
            mstate_wishful &= ~mcBack;
            extern bool g_bAutoClearCrouch;
            if (g_bAutoClearCrouch)
                mstate_wishful &= ~mcCrouch;
        }
    }
    else
    {
        make_Interpolation();

        if (NET.size())
        {
            //			NET_SavedAccel = NET_Last.p_accel;
            //			mstate_real = mstate_wishful = NET_Last.mstate;

            g_sv_Orientate(mstate_real, dt);
            g_Orientate(mstate_real, dt);
            g_Physics(NET_SavedAccel, NET_Jump, dt);
            if (!m_bInInterpolation)
                g_cl_ValidateMState(dt, mstate_wishful);
            g_SetAnimation(mstate_real);

            set_state_box(NET_Last.mstate);
        }
        mstate_old = mstate_real;
    }
    if (this == Level().CurrentViewEntity())
    {
        UpdateMotionIcon(mstate_real);
    };
    NET_Jump = 0;

    inherited::shedule_Update(DT);

    //эффектор включаемый при ходьбе
    if (!pCamBobbing)
    {
        pCamBobbing = xr_new<CEffectorBobbing>();
        Cameras().AddCamEffector(pCamBobbing);
    }
    pCamBobbing->SetState(mstate_real, conditions().IsLimping(), IsZoomAimingMode());

    //звук тяжелого дыхания при уталости и хромании
    if (this == Level().CurrentControlEntity() && !GEnv.isDedicatedServer)
    {
        if (conditions().IsLimping() && g_Alive() && !psActorFlags.test(AF_GODMODE_RT))
        {
            if (!m_HeavyBreathSnd._feedback())
            {
                m_HeavyBreathSnd.play_at_pos(this, Fvector().set(0, ACTOR_HEIGHT, 0), sm_Looped | sm_2D);
            }
            else
            {
                m_HeavyBreathSnd.set_position(Fvector().set(0, ACTOR_HEIGHT, 0));
            }
        }
        else if (m_HeavyBreathSnd._feedback())
        {
            m_HeavyBreathSnd.stop();
        }

        // -------------------------------
        float bs = conditions().BleedingSpeed();
        if (bs > 0.6f)
        {
            Fvector snd_pos;
            snd_pos.set(0, ACTOR_HEIGHT, 0);
            if (!m_BloodSnd._feedback())
                m_BloodSnd.play_at_pos(this, snd_pos, sm_Looped | sm_2D);
            else
                m_BloodSnd.set_position(snd_pos);

            float v = bs + 0.25f;

            m_BloodSnd.set_volume(v);
        }
        else
        {
            if (m_BloodSnd._feedback())
                m_BloodSnd.stop();
        }

        if (!g_Alive() && m_BloodSnd._feedback())
            m_BloodSnd.stop();
        // -------------------------------
        bs = conditions().GetZoneDanger();
        if (bs > 0.1f)
        {
            Fvector snd_pos;
            snd_pos.set(0, ACTOR_HEIGHT, 0);
            if (!m_DangerSnd._feedback())
                m_DangerSnd.play_at_pos(this, snd_pos, sm_Looped | sm_2D);
            else
                m_DangerSnd.set_position(snd_pos);

            float v = bs + 0.25f;
            //			Msg( "bs            = %.2f", bs );

            m_DangerSnd.set_volume(v);
        }
        else
        {
            if (m_DangerSnd._feedback())
                m_DangerSnd.stop();
        }

        if (!g_Alive() && m_DangerSnd._feedback())
            m_DangerSnd.stop();
    }

    //если в режиме HUD, то сама модель актера не рисуется
    // [DA_PORT] ...except while placing an item. The placement ghost is drawn as part of the actor, so
    // an invisible actor means an invisible ghost — and since a hidden object still generates shadows,
    // all that reached the screen was the ghost's shadow on the ground. During placement the actor is
    // made visible and renderable_Render below skips the body, so only the ghost is drawn.
    if (!character_physics_support()->IsRemoved())
        setVisible(!HUDview() || m_item_placement_active);

    //что актер видит перед собой
    collide::rq_result& RQ = HUD().GetCurrentRayQuery();

    if (!input_external_handler_installed() && RQ.O && RQ.O->getVisible() && RQ.range < 2.0f)
    {
        m_pObjectWeLookingAt = smart_cast<CGameObject*>(RQ.O);

        CGameObject* game_object = smart_cast<CGameObject*>(RQ.O);
        m_pUsableObject = game_object;
        m_pInvBoxWeLookingAt = smart_cast<CInventoryBox*>(game_object);
        m_pPersonWeLookingAt = game_object->cast_inventory_owner();
        m_pVehicleWeLookingAt = smart_cast<CHolderCustom*>(game_object);
        CEntityAlive* pEntityAlive = smart_cast<CEntityAlive*>(game_object);

        if (GameID() == eGameIDSingle)
        {
            if (m_pUsableObject && m_pUsableObject->tip_text())
            {
                m_sDefaultObjAction = StringTable().translate(m_pUsableObject->tip_text());
            }
            else
            {
                if (m_pPersonWeLookingAt && pEntityAlive->g_Alive() && m_pPersonWeLookingAt->IsTalkEnabled())
                {
                    m_sDefaultObjAction = m_sCharacterUseAction;
                }
                else if (pEntityAlive && !pEntityAlive->g_Alive())
                {
                    if (m_pPersonWeLookingAt && m_pPersonWeLookingAt->deadbody_closed_status())
                    {
                        m_sDefaultObjAction = m_sDeadCharacterDontUseAction;
                    }
                    else
                    {
                        bool b_allow_drag = !!pSettings->line_exist("ph_capture_visuals", pEntityAlive->cNameVisual());
                        if (b_allow_drag)
                        {
                            m_sDefaultObjAction = m_sDeadCharacterUseOrDragAction;
                        }
                        else if (pEntityAlive->cast_inventory_owner())
                        {
                            m_sDefaultObjAction = m_sDeadCharacterUseAction;
                        }
                    } // m_pPersonWeLookingAt
                }
                else if (m_pVehicleWeLookingAt)
                {
                    m_sDefaultObjAction = m_pVehicleWeLookingAt->m_sUseAction ? m_pVehicleWeLookingAt->m_sUseAction : m_sCarCharacterUseAction;
                }
                else if (m_pObjectWeLookingAt && m_pObjectWeLookingAt->cast_inventory_item() &&
                    m_pObjectWeLookingAt->cast_inventory_item()->CanTake())
                {
                    m_sDefaultObjAction = m_sInventoryItemUseAction;
                }
                else
                {
                    m_sDefaultObjAction = nullptr;
                }
            }
        }
    }
    else
    {
        m_pPersonWeLookingAt = nullptr;
        m_sDefaultObjAction = nullptr;
        m_pUsableObject = nullptr;
        m_pObjectWeLookingAt = nullptr;
        m_pVehicleWeLookingAt = nullptr;
        m_pInvBoxWeLookingAt = nullptr;
    }

    //	UpdateSleep									();

    //для свойст артефактов, находящихся на поясе
    UpdateArtefactsOnBeltAndOutfit();
    m_pPhysics_support->in_shedule_Update(DT);
    Check_for_AutoPickUp();
};
#include "debug_renderer.h"
// [DA_PORT] defined in script_game_object_script3.cpp (a script TU): calls the Lua functor
// itms_manager.inv_item_place_confirmed to release the item and spawn the world object. Kept out of
// Actor.cpp because pulling luabind into this non-script TU breaks compilation.
extern void DA_ConfirmItemPlacement(u16 item_id, float x, float y, float z, float rot);

// [DA_PORT] --- "Установить" item placement preview (kerosene lamp etc.) ---
void CActor::StartItemPlacement(LPCSTR section, u16 item_id)
{
    CancelItemPlacement();
    if (!section || !pSettings->section_exist(section) || !pSettings->line_exist(section, "visual"))
        return;
    m_item_placement_section = section;
    m_item_placement_item_id = item_id;
    m_item_placement_visual = GEnv.Render->model_Create(pSettings->r_string(section, "visual"));
    if (!m_item_placement_visual)
        return;
    m_item_placement_xform.identity();

    // [DA_PORT] Ghost highlight: a soft cyan point light plus a glow sprite, so the preview reads as a
    // hologram rather than as an item already lying there. Cyan because every real light source in the
    // Zone is warm — nothing in the world glows this colour, which is what makes it read as "not real".
    m_item_placement_light = GEnv.Render->light_create();
    m_item_placement_light->set_type(IRender_Light::POINT);
    m_item_placement_light->set_shadow(false);
    // Bright and close to white: the ghost gets NO other light. It is drawn through the actor, and the
    // actor is invisible in first person, so its hemi/sun data is zero — without this lamp the preview
    // renders as a flat black silhouette. The blue tint is kept only as a hint, not as the main colour.
    m_item_placement_light->set_range(4.0f);
    m_item_placement_light->set_color(0.85f, 0.95f, 1.10f);
    m_item_placement_light->set_active(true);

    m_item_placement_glow = GEnv.Render->glow_create();
    m_item_placement_glow->set_texture("glow\glow_torch_r2");
    m_item_placement_glow->set_color(0.35f, 0.75f, 0.95f);
    m_item_placement_glow->set_radius(0.45f);
    m_item_placement_glow->set_active(true);

    m_item_placement_active = true;

    // [DA_PORT] Close the inventory: placement is aimed with the crosshair, so the menu that started it
    // has to get out of the way. Done here rather than in the script so every item using the placement
    // functor behaves the same without touching mod data.
    if (CurrentGameUI())
        CurrentGameUI()->GetActorMenu().HideDialog();

    UpdateItemPlacement();
}

void CActor::UpdateItemPlacement()
{
    if (!m_item_placement_active)
        return;
    Fvector cam_pos, cam_dir, cam_norm;
    cam_Active()->Get(cam_pos, cam_dir, cam_norm);
    cam_dir.normalize_safe();
    collide::rq_result& RQ = HUD().GetCurrentRayQuery();
    const float dist = (RQ.range > 0.1f) ? _min(RQ.range, 4.0f) : 2.5f;
    Fvector pos;
    pos.mad(cam_pos, cam_dir, dist);
    m_item_placement_xform.setHPB(cam_dir.getH(), 0.f, 0.f);
    m_item_placement_xform.c = pos;

    // Pulse: slow breathing so the highlight is obviously a preview and not a placed light source.
    // Shallow pulse only: the lamp is what makes the model visible at all, so it must never dim far.
    const float pulse = 0.85f + 0.15f * _abs(_sin(Device.fTimeGlobal * 2.2f));

    // Above and slightly towards the player, so the shape is lit from the front and reads as an object
    // rather than as a silhouette.
    Fvector light_pos = pos;
    light_pos.y += 0.8f;
    Fvector to_player;
    to_player.sub(Position(), pos).normalize_safe();
    light_pos.mad(to_player, 0.5f);

    if (m_item_placement_light)
    {
        m_item_placement_light->set_position(light_pos);
        m_item_placement_light->set_color(0.85f * pulse, 0.95f * pulse, 1.10f * pulse);
    }
    if (m_item_placement_glow)
    {
        Fvector glow_pos = pos;
        glow_pos.y += 0.25f;
        m_item_placement_glow->set_position(glow_pos);
        m_item_placement_glow->set_color(0.35f * pulse, 0.75f * pulse, 0.95f * pulse);
    }
}

void CActor::ConfirmItemPlacement()
{
    if (!m_item_placement_active)
        return;
    const Fvector pos = m_item_placement_xform.c;
    const float h = m_item_placement_xform.k.getH();
    const u16 id = m_item_placement_item_id;
    // Hand the confirmed spot to itms_manager (it releases the inventory item and alife-creates the
    // world object), reusing the existing DA placement/spawn logic.
    DA_ConfirmItemPlacement(id, pos.x, pos.y, pos.z, h);
    CancelItemPlacement();
}

void CActor::CancelItemPlacement()
{
    if (m_item_placement_visual)
        GEnv.Render->model_Delete(m_item_placement_visual);
    m_item_placement_visual = nullptr;

    if (m_item_placement_light)
    {
        m_item_placement_light->set_active(false);
        m_item_placement_light = nullptr;
    }
    if (m_item_placement_glow)
    {
        m_item_placement_glow->set_active(false);
        m_item_placement_glow = nullptr;
    }
    m_item_placement_active = false;
    m_item_placement_item_id = 0xffff;
    m_item_placement_section = nullptr;
}

void CActor::renderable_RenderBody(u32 context_id, IRenderable* root)
{
    VERIFY(_valid(XFORM()));
    inherited::renderable_Render(context_id, root);
    CInventoryOwner::renderable_Render(context_id, root);
}

// [DA_PORT] Кости теневой модели получают позу настоящего скелета. Колбэк только присваивает
// заранее снятое положение: он вызывается изнутри расчёта костей, и лезть оттуда в чужую кинематику
// нельзя. Снимает положения renderable_RenderShadow ниже.
void CActor::ShadowBoneCallback(CBoneInstance* bone)
{
    auto* binding = static_cast<ShadowBoneBinding*>(bone->callback_param());
    if (!binding || binding->source_id == u16(-1))
        return;

    bone->mTransform = binding->transform;
}

void CActor::DestroyShadowVisual()
{
    m_shadow_bones.clear();
    m_shadow_kinematics = nullptr;
    if (m_shadow_visual)
        GEnv.Render->model_Delete(m_shadow_visual);
}

// [DA_PORT] Собрать теневую модель под текущий костюм. Модель берётся из нашей таблицы
// da_port_actor_visual.ltx — той самой, что раньше подменяла видимый визуал целиком.
void CActor::RebuildShadowVisual()
{
    DestroyShadowVisual();

    IKinematics* source = smart_cast<IKinematics*>(Visual());
    if (!source)
        return;

    // Мёртвому теневая модель не нужна: камера уходит в третье лицо и труп рисуется обычным путём.
    if (!g_Alive())
        return;

    CCustomOutfit* outfit = GetOutfit();
    const shared_str full = outfit ? da_actor_full_visual(outfit->cNameSect().c_str()) : shared_str();
    if (!full.size())
        return;

    string_path model_ogf;
    xr_sprintf(model_ogf, "%s.ogf", full.c_str());

    m_shadow_visual = GEnv.Render->model_Create(model_ogf);
    m_shadow_kinematics = smart_cast<IKinematics*>(m_shadow_visual);
    if (!m_shadow_kinematics)
    {
        DestroyShadowVisual();
        return;
    }

    m_shadow_kinematics->LL_SetBonesVisible(u64(-1));
    const u16 bone_count = m_shadow_kinematics->LL_BoneCount();
    m_shadow_bones.resize(bone_count);
    for (u16 bone_id = 0; bone_id < bone_count; ++bone_id)
    {
        ShadowBoneBinding& binding = m_shadow_bones[bone_id];
        binding.source_id = source->LL_BoneID(m_shadow_kinematics->LL_BoneName_dbg(bone_id));
        m_shadow_kinematics->LL_GetBoneInstance(bone_id).set_callback(
            bctCustom, ShadowBoneCallback, &binding, binding.source_id != u16(-1));
    }
}

// [DA_PORT] Ручки перволичных ног. Числа — из Anomaly (player_hud_legs.cpp), там их подбирали
// долго и на живых игроках, так что начинаем с проверенных, а не со своих.
float g_da_legs_fwd = -0.35f; // на сколько метров отодвинуть тело НАЗАД от камеры
float g_da_legs_y = 0.0f;    // подсадить/поднять торс по высоте
int g_da_legs_cam = 1;       // привязать XZ модели к камере, а не к актёру

// [DA_PORT] Поправка на взгляд вниз. Постоянного сдвига мало: на бегу анимация от третьего лица
// клонит тело вперёд, и стоит опустить взгляд — торс подныривает в кадр снизу.
//
// Anomaly объявляет под это `remove_camera_pitch`, то есть снятие наклона камеры с костей, но в её
// же исходнике метод только объявлен и не реализован. Правильнее было бы вычесть наклон из костей
// позвоночника, однако он приходит туда с РАЗНЫМИ множителями (см. p_*_factor в ActorAnimation.cpp),
// и точное обратное преобразование пришлось бы держать в согласии с ними вручную.
//
// Здесь дешевле и устойчивее: чем ниже смотрит игрок, тем сильнее модель уезжает назад и вниз.
// Линейно по углу, ноль на горизонте и полная поправка при взгляде строго под ноги.
//
// Числа сильно уменьшены после того, как перволичной моделью стало туловище БЕЗ ГОЛОВЫ. Большая
// поправка нужна была, пока в камере рисовалась полная модель героя: её торс подныривал в кадр, и
// приходилось отодвигать всё тело почти на метр — вместе с ногами, которых из-за этого не было
// видно. Голову убрали моделью, а не расстоянием, и отодвигать так далеко стало незачем.
float g_da_legs_pitch_fwd = -0.10f;
float g_da_legs_pitch_y = -0.05f;

// Набор скрываемых костей, разбор у CActor::ApplyLegsBoneMask.
//
// Умолчание НОЛЬ, хотя Anomaly прячет шею и плечи. Проверено в игре: при нашем сдвиге назад голова
// и руки за камеру и так не попадают, а скрытие оставляло от себя чёрный клин у таза — стянутую к
// началу модели геометрию спрятанных костей. То есть их набор решал задачу, которую у нас уже решил
// сдвиг, и платил за это артефактом.
int g_da_legs_hide = 0;

void CActor::DestroyLegsVisual()
{
    m_legs_bones.clear();
    m_legs_kinematics = nullptr;
    if (m_legs_visual)
        GEnv.Render->model_Delete(m_legs_visual);
}

// [DA_PORT] Модель для перволичных ног. Порядок поиска авторский из Anomaly: сначала специальный
// ключ костюма, потом его же обычный `actor_visual` (у Dead Air это как раз `actors\legs\*.ogf`),
// и только потом визуал из секции актёра — он с головой, поэтому лишнее придётся прятать.
void CActor::RebuildLegsVisual()
{
    DestroyLegsVisual();

    IKinematics* source = smart_cast<IKinematics*>(Visual());
    if (!source || !g_Alive())
        return;

    // Порядок поиска. Первые два шага — авторские данные (у Dead Air `actor_visual` костюма это и
    // есть туловище без головы). Третий — наша таблица, она закрывает случай «костюма нет»: своих
    // `legs`-моделей мод под это не держит, и без неё откат уходил на базовую модель героя — с
    // головой, которая лезет в кадр на широком угле обзора.
    CCustomOutfit* outfit = GetOutfit();
    LPCSTR outfit_sect = outfit ? outfit->cNameSect().c_str() : nullptr;

    shared_str model;
    if (outfit_sect)
    {
        if (pSettings->line_exist(outfit_sect, "legs_visual"))
            model = pSettings->r_string(outfit_sect, "legs_visual");
        else if (pSettings->line_exist(outfit_sect, "actor_visual"))
            model = pSettings->r_string(outfit_sect, "actor_visual");
    }
    if (!model.size())
        model = da_actor_legs_visual(outfit_sect);
    if (!model.size())
    {
        LPCSTR sect = cNameSect().c_str();
        if (pSettings->line_exist(sect, "legs_visual"))
            model = pSettings->r_string(sect, "legs_visual");
    }
    if (!model.size())
        return; // базовый визуал актёра сюда НЕ берём: он с головой

    string_path model_ogf, mesh_fn;
    xr_sprintf(model_ogf, "%s", model.c_str());
    if (!strext(model_ogf))
        xr_strcat(model_ogf, ".ogf");
    if (!FS.exist(mesh_fn, "$game_meshes$", model_ogf))
        return;

    m_legs_visual = GEnv.Render->model_Create(model_ogf);
    m_legs_kinematics = smart_cast<IKinematics*>(m_legs_visual);
    if (!m_legs_kinematics)
    {
        DestroyLegsVisual();
        return;
    }

    m_legs_kinematics->LL_SetBonesVisible(u64(-1));
    const u16 bone_count = m_legs_kinematics->LL_BoneCount();
    m_legs_bones.resize(bone_count);
    for (u16 bone_id = 0; bone_id < bone_count; ++bone_id)
    {
        ShadowBoneBinding& binding = m_legs_bones[bone_id];
        binding.source_id = source->LL_BoneID(m_legs_kinematics->LL_BoneName_dbg(bone_id));
        m_legs_kinematics->LL_GetBoneInstance(bone_id).set_callback(
            bctCustom, ShadowBoneCallback, &binding, binding.source_id != u16(-1));
    }

    m_legs_hide_applied = -1;
    ApplyLegsBoneMask();
}

// [DA_PORT] Набор скрываемых костей, da_legs_hide.
//
// Уровень 1 — ровно то, что прячет Anomaly: шея и оба плеча. Голова и руки уходят рекурсивно, торс
// остаётся; он не мешает, потому что тело отодвинуто назад.
//
// Дальше уровни на случай, если от скрытых костей остаётся мусор. Скрытая кость не убирает
// геометрию, а стягивает её вершины к началу модели, и на некоторых мешах это видно плоским клином.
// Тогда нужно уносить и ту кость, к которой мусор привязан.
//
// Маска ставится один раз и переставляется, только когда ручку покрутили: это состояние НАШЕЙ
// модели, чужого тут нет.
void CActor::ApplyLegsBoneMask()
{
    if (!m_legs_kinematics || m_legs_hide_applied == g_da_legs_hide)
        return;

    m_legs_hide_applied = g_da_legs_hide;
    m_legs_kinematics->LL_SetBonesVisible(u64(-1));

    if (g_da_legs_hide <= 0)
        return;

    for (cpcstr name : { "bip01_neck", "bip01_l_upperarm", "bip01_r_upperarm" })
    {
        const u16 bone = m_legs_kinematics->LL_BoneID(name);
        if (bone != BI_NONE)
            m_legs_kinematics->LL_SetBoneVisible(bone, FALSE, TRUE);
    }

    if (g_da_legs_hide >= 2)
    {
        const u16 bone = m_legs_kinematics->LL_BoneID("bip01_spine2");
        if (bone != BI_NONE)
            m_legs_kinematics->LL_SetBoneVisible(bone, FALSE, TRUE);
    }
    if (g_da_legs_hide >= 3)
    {
        const u16 bone = m_legs_kinematics->LL_BoneID("bip01_spine1");
        if (bone != BI_NONE)
            m_legs_kinematics->LL_SetBoneVisible(bone, FALSE, TRUE);
    }
    if (g_da_legs_hide >= 4)
    {
        const u16 bone = m_legs_kinematics->LL_BoneID("bip01_spine");
        if (bone != BI_NONE)
            m_legs_kinematics->LL_SetBoneVisible(bone, FALSE, TRUE);
    }
}

// [DA_PORT] Перволичные ноги: поза берётся у актёра, а место — своё.
void CActor::renderable_RenderLegs(u32 context_id, IRenderable* root)
{
    IKinematics* source = smart_cast<IKinematics*>(Visual());
    if (!source || !m_legs_visual || !m_legs_kinematics)
        return;

    ApplyLegsBoneMask(); // ручку могли покрутить между кадрами

    // [DA_PORT] Тот же разбор, что и у теневой модели: без сброса кэша костей актёра.
    source->CalculateBones(TRUE);

    for (ShadowBoneBinding& binding : m_legs_bones)
    {
        if (binding.source_id != u16(-1))
            binding.transform = source->LL_GetTransform(binding.source_id);
    }

    m_legs_kinematics->CalculateBones_Invalidate();
    m_legs_kinematics->CalculateBones(TRUE);

    // Место модели. Привязка к камере по горизонтали важнее, чем кажется: камера вращается вокруг
    // своей оси, модель вокруг своей, и без этого тело при повороте уезжает вбок. Сдвиг назад
    // убирает торс из лица — это и есть замена «отрезать всё выше пояса».
    Fmatrix xf;
    xf.set(XFORM());
    if (g_da_legs_cam)
    {
        xf.c.x = Device.vCameraPosition.x;
        xf.c.z = Device.vCameraPosition.z;
    }

    // Доля взгляда вниз: 0 на горизонте, 1 строго под ноги. Вверх не трогаем — там тела не видно.
    float down = 0.f;
    if (cam_Active())
    {
        down = cam_Active()->pitch / (PI * 0.5f);
        clamp(down, 0.f, 1.f);
    }

    xf.c.y += g_da_legs_y + g_da_legs_pitch_y * down;

    // ⛔ [DA_PORT] Ось сдвига берётся от КАМЕРЫ, а не от тела.
    //
    // Симптом: при быстром повороте тело видно, и видно, как оно ОТСТАЁТ.
    //
    // Почему. Ориентация модели — от актёра (`xf.set(XFORM())`), и она догоняет камеру с инерцией:
    // так и задумано, тело не обязано щёлкать за взглядом. Но сдвиг назад на g_da_legs_fwd — тот
    // самый, что убирает торс из лица, — раньше шёл по оси ТЕЛА (`xf.k`). Пока камера и тело
    // смотрят одинаково, разницы нет. При быстром повороте камера уже смотрит вбок, а тело ещё
    // нет — и сдвиг уводит торс вбок ОТ ВЗГЛЯДА вместо «назад», то есть прямо в кадр.
    //
    // Направление камеры чинит это по построению: куда бы ни смотрело тело, торс всегда уезжает от
    // смотрящего. Поворот модели при этом не трогаем — отставание тела остаётся, оно правдоподобно
    // и видно только на своих же ногах.
    //
    // Ось привязана к тому же выключателю, что и положение: при g_da_legs_cam 0 поведение прежнее,
    // целиком по телу.
    Fvector fwd = g_da_legs_cam ? Device.vCameraDirection : xf.k;
    fwd.y = 0.f;
    fwd.normalize_safe();
    xf.c.mad(fwd, g_da_legs_fwd + g_da_legs_pitch_fwd * down);

    GEnv.Render->add_Visual(context_id, root, m_legs_visual, xf);
}

// [DA_PORT] Теневой проход: целое тело вместо перволичных ног, плюс оружие в его руках.
// Если теневой модели нет (не сопоставлена, не нашлась, актёр мёртв) — рисуем как раньше, тело.
void CActor::renderable_RenderShadow(u32 context_id, IRenderable* root)
{
    IKinematics* source = smart_cast<IKinematics*>(Visual());
    if (source && m_shadow_visual && m_shadow_kinematics)
    {
        // [DA_PORT] Поза снимается ОДИН раз за кадр, а не на каждый теневой проход.
        //
        // Этот метод зовётся из build_subspace под PHASE_SMAP, а фаза ставится в пяти местах, и
        // одно из них — цикл по теневым источникам света. То есть проходов за кадр столько,
        // сколько каскадов солнца плюс теневых ламп: в баре их было 61. Без этой проверки на
        // каждый такой проход шло ДВА полных пересчёта скелета — актёра и теневой модели, — плюс
        // сброс кэша костей актёра, из-за которого он потом считался заново и для своих нужд.
        //
        // Симптом был ровно такой, каким его и увидели: просадка появляется там, где есть NPC,
        // потому что они приносят с собой фонари и лампы, то есть новые теневые проходы. Поза при
        // этом за кадр не меняется — снимать её повторно незачем.
        if (m_shadow_pose_frame != Device.dwFrame)
        {
            m_shadow_pose_frame = Device.dwFrame;

            // [DA_PORT] Скелет актёра берётся КАК ЕСТЬ, без возни с маской видимости.
            //
            // Здесь стояло: показать все кости, пересчитать, снять позу, вернуть маску и сбросить
            // кэш. Последнее и было дорого — сброс обесценивает только что посчитанный скелет, и
            // движок считает его заново уже для своих нужд. Замером r__actor_shadow: этот второй
            // проход давал около 0.6 мс в `move` плюс 0.4 мс в рендере, то есть треть кадра.
            //
            // Возня нужна была на случай костей, скрытых под костюм. У нас таких нет: маску никто
            // не трогает (da_legs_hide по умолчанию 0). Если когда-нибудь начнут — тень унаследует
            // скрытие, и это скорее правильно, чем нет: тени незачем показывать то, чего не видно.
            source->CalculateBones(TRUE);

            for (ShadowBoneBinding& binding : m_shadow_bones)
            {
                if (binding.source_id != u16(-1))
                    binding.transform = source->LL_GetTransform(binding.source_id);
            }

            m_shadow_kinematics->CalculateBones_Invalidate();
            m_shadow_kinematics->CalculateBones(TRUE);
        }

        GEnv.Render->add_Visual(context_id, root, m_shadow_visual, XFORM());
    }
    else
    {
        renderable_RenderBody(context_id, root);
        return;
    }

    CInventoryItem* active_item = inventory().ActiveItem();
    CWeapon* weapon = active_item ? active_item->object().cast_weapon() : nullptr;
    if (!weapon || !source)
        return;

    int source_bone_l = -1;
    int source_bone_r = -1;
    int source_bone_r2 = -1;
    g_WeaponBones(source_bone_l, source_bone_r, source_bone_r2);

    const auto shadow_bone_id = [source, this](int source_bone)
    {
        if (source_bone == -1)
            return -1;

        const u16 bone_id = m_shadow_kinematics->LL_BoneID(source->LL_BoneName_dbg(u16(source_bone)));
        return bone_id == u16(-1) ? -1 : int(bone_id);
    };

    weapon->renderable_RenderShadow(context_id, root, m_shadow_kinematics, XFORM(),
        shadow_bone_id(source_bone_l), shadow_bone_id(source_bone_r), shadow_bone_id(source_bone_r2));
}

void CActor::renderable_Render(u32 context_id, IRenderable* root)
{
    VERIFY(_valid(XFORM()));

    // [DA_PORT] In first person during placement the actor is forced visible purely to get the ghost
    // drawn (see setVisible above) — so skip the body here, or the player would see themselves.
    const bool ghost_only = m_item_placement_active && HUDview();
    if (!ghost_only)
    {
        renderable_RenderBody(context_id, root);
    }

    // [DA_PORT] draw the placement-preview ghost at the crosshair.
    if (m_item_placement_active && m_item_placement_visual)
        GEnv.Render->add_Visual(context_id, root, m_item_placement_visual, m_item_placement_xform);
}

bool CActor::renderable_ShadowGenerate()
{
    if (m_holder)
        return FALSE;

    return inherited::renderable_ShadowGenerate();
}

void CActor::g_PerformDrop()
{
    b_DropActivated = FALSE;

    PIItem pItem = inventory().ActiveItem();
    if (0 == pItem)
        return;

    if (pItem->IsQuestItem())
        return;

    u16 s = inventory().GetActiveSlot();
    if (inventory().SlotIsPersistent(s))
        return;

    pItem->SetDropManual(TRUE);
}

bool CActor::use_default_throw_force()
{
    if (!g_Alive())
        return false;

    return true;
}

float CActor::missile_throw_force() { return 0.f; }

// HUD
void CActor::OnHUDDraw(u32 context_id, CCustomHUD* hud, IRenderable* root)
{
    R_ASSERT(IsFocused());
    if (!((mstate_real & mcLookout) && !IsGameTypeSingle()))
        g_player_hud->render_hud(context_id, root);
}

void CActor::RenderIndicator(Fvector dpos, float r1, float r2, const ui_shader& IndShader)
{
    if (!g_Alive())
        return;

    GEnv.UIRender->StartPrimitive(4, IUIRender::ptTriStrip, IUIRender::pttLIT);

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(u16(m_head));
    Fmatrix M;
    smart_cast<IKinematics*>(Visual())->CalculateBones();
    M.mul(XFORM(), BI.mTransform);

    Fvector pos = M.c;
    pos.add(dpos);
    const Fvector& T = Device.vCameraTop;
    const Fvector& R = Device.vCameraRight;
    Fvector Vr, Vt;
    Vr.x = R.x * r1;
    Vr.y = R.y * r1;
    Vr.z = R.z * r1;
    Vt.x = T.x * r2;
    Vt.y = T.y * r2;
    Vt.z = T.z * r2;

    Fvector a, b, c, d;
    a.sub(Vt, Vr);
    b.add(Vt, Vr);
    c.invert(a);
    d.invert(b);

    GEnv.UIRender->PushPoint(d.x + pos.x, d.y + pos.y, d.z + pos.z, 0xffffffff, 0.f, 1.f);
    GEnv.UIRender->PushPoint(a.x + pos.x, a.y + pos.y, a.z + pos.z, 0xffffffff, 0.f, 0.f);
    GEnv.UIRender->PushPoint(c.x + pos.x, c.y + pos.y, c.z + pos.z, 0xffffffff, 1.f, 1.f);
    GEnv.UIRender->PushPoint(b.x + pos.x, b.y + pos.y, b.z + pos.z, 0xffffffff, 1.f, 0.f);
    // pv->set         (d.x+pos.x,d.y+pos.y,d.z+pos.z, 0xffffffff, 0.f,1.f);        pv++;
    // pv->set         (a.x+pos.x,a.y+pos.y,a.z+pos.z, 0xffffffff, 0.f,0.f);        pv++;
    // pv->set         (c.x+pos.x,c.y+pos.y,c.z+pos.z, 0xffffffff, 1.f,1.f);        pv++;
    // pv->set         (b.x+pos.x,b.y+pos.y,b.z+pos.z, 0xffffffff, 1.f,0.f);        pv++;
    // render
    // dwCount 				= u32(pv-pv_start);
    // RCache.Vertex.Unlock	(dwCount,hFriendlyIndicator->vb_stride);

    GEnv.UIRender->CacheSetXformWorld(Fidentity);
    // RCache.set_xform_world		(Fidentity);
    GEnv.UIRender->SetShader(*IndShader);
    // RCache.set_Shader			(IndShader);
    // RCache.set_Geometry			(hFriendlyIndicator);
    // RCache.Render	   			(D3DPT_TRIANGLESTRIP,dwOffset,0, dwCount, 0, 2);
    GEnv.UIRender->FlushPrimitive();
};

static float mid_size = 0.097f;
//static float fontsize = 15.0f;
static float upsize = 0.33f;

void CActor::RenderText(LPCSTR Text, Fvector dpos, float* pdup, u32 color)
{
    if (!g_Alive())
        return;

    CBoneInstance& BI = smart_cast<IKinematics*>(Visual())->LL_GetBoneInstance(u16(m_head));
    Fmatrix M;
    smart_cast<IKinematics*>(Visual())->CalculateBones();
    M.mul(XFORM(), BI.mTransform);
    //------------------------------------------------
    Fvector v0, v1;
    v0.set(M.c);
    v1.set(M.c);
    Fvector T = Device.vCameraTop;
    v1.add(T);

    Fvector v0r, v1r;
    Device.mFullTransform.transform(v0r, v0);
    Device.mFullTransform.transform(v1r, v1);
    float size = v1r.distance_to(v0r);
    CGameFont* pFont = UI().Font().pFontArial14;
    if (!pFont)
        return;
    //	float OldFontSize = pFont->GetHeight	();
    float delta_up = 0.0f;
    if (size < mid_size)
        delta_up = upsize;
    else
        delta_up = upsize * (mid_size / size);
    dpos.y += delta_up;
    if (size > mid_size)
        size = mid_size;
    //	float NewFontSize = size/mid_size * fontsize;
    //------------------------------------------------
    M.c.y += dpos.y;

    Fvector4 v_res;
    Device.mFullTransform.transform(v_res, M.c);

    if (v_res.z < 0 || v_res.w < 0)
        return;
    if (v_res.x < -1.f || v_res.x > 1.f || v_res.y < -1.f || v_res.y > 1.f)
        return;

    float x = (1.f + v_res.x) / 2.f * (Device.dwWidth);
    float y = (1.f - v_res.y) / 2.f * (Device.dwHeight);

    pFont->SetAligment(CGameFont::alCenter);
    pFont->SetColor(color);
    //	pFont->SetHeight	(NewFontSize);
    pFont->Out(x, y, Text);
    //-------------------------------------------------
    //	pFont->SetHeight(OldFontSize);
    *pdup = delta_up;
};

void CActor::SetPhPosition(const Fmatrix& transform)
{
    if (!m_pPhysicsShell)
    {
        character_physics_support()->movement()->SetPosition(transform.c);
    }
    // else m_phSkeleton->S
}

void CActor::ForceTransform(const Fmatrix& m)
{
    // if( !g_Alive() )
    //			return;
    // VERIFY(_valid(m));
    // XFORM().set( m );
    // if( character_physics_support()->movement()->CharacterExist() )
    //		character_physics_support()->movement()->EnableCharacter();
    // character_physics_support()->set_movement_position( m.c );
    // character_physics_support()->movement()->SetVelocity( 0, 0, 0 );

    character_physics_support()->ForceTransform(m);
    const float block_damage_time_seconds = 2.f;
    if (!IsGameTypeSingle())
        character_physics_support()->movement()->BlockDamageSet(u64(block_damage_time_seconds / fixed_step));
}

void CActor::ForceTransformAndDirection(const Fmatrix& m)
{
    Fvector xyz;
    m.getHPB(xyz);

    ForceTransform(m);
    cam_Active()->Set(-xyz.x, -xyz.y, -xyz.z);
}

//ENGINE_API extern float psHUD_FOV;
float CActor::Radius() const
{
    float R = inherited::Radius();
    CWeapon* W = smart_cast<CWeapon*>(inventory().ActiveItem());
    if (W)
        R += W->Radius();
    //if (HUDview()) R *= 1.f/psHUD_FOV;
    return R;
}

bool CActor::use_bolts() const
{
    if (!IsGameTypeSingle())
        return false;
    return CInventoryOwner::use_bolts();
};

int g_iCorpseRemove = 1;

bool CActor::NeedToDestroyObject() const
{
    if (IsGameTypeSingle())
    {
        return false;
    }
    else
    {
        if (g_Alive())
            return false;
        if (g_iCorpseRemove == -1)
            return false;
        if (g_iCorpseRemove == 0 && m_bAllowDeathRemove)
            return true;
        if (TimePassedAfterDeath() > m_dwBodyRemoveTime && m_bAllowDeathRemove)
            return true;
        else
            return false;
    }
}

ALife::_TIME_ID CActor::TimePassedAfterDeath() const
{
    if (!g_Alive())
        return Level().timeServer() - GetLevelDeathTime();
    else
        return 0;
}

void CActor::OnItemTake(CInventoryItem* inventory_item)
{
    CInventoryOwner::OnItemTake(inventory_item);
    if (OnClient())
        return;
}

void CActor::OnItemDrop(CInventoryItem* inventory_item, bool just_before_destroy)
{
    CInventoryOwner::OnItemDrop(inventory_item, just_before_destroy);

    CCustomOutfit* outfit = smart_cast<CCustomOutfit*>(inventory_item);
    if (outfit && inventory_item->m_ItemCurrPlace.type == eItemPlaceSlot)
    {
        outfit->ApplySkinModel(this, false, false);
    }

    CWeapon* weapon = smart_cast<CWeapon*>(inventory_item);
    if (weapon && inventory_item->m_ItemCurrPlace.type == eItemPlaceSlot)
    {
        weapon->OnZoomOut();
        if (weapon->GetRememberActorNVisnStatus())
            weapon->EnableActorNVisnAfterZoom();
    }

    // [DA_PORT] Dead Air's slot 14 (== GRENADE_SLOT) is a MANUAL utility slot (holds a grenade OR a
    // binocular, equipped/removed by hand), not a stock auto-refilling grenade slot. The stock reslot
    // below immediately refilled the slot from the next grenade in the ruck, so an equipped grenade
    // could not be removed (it came straight back) and got relocated on menu close. Disabled for the
    // actor so the slot is hand-managed; it is still throwable via the grenade key while occupied.
    // (NPC grenade readiness is unaffected - this path is CActor-only.)
    // if (!just_before_destroy && inventory_item->BaseSlot() == GRENADE_SLOT &&
    //     NULL == inventory().ItemFromSlot(GRENADE_SLOT))
    // {
    //     PIItem grenade = inventory().SameSlot(GRENADE_SLOT, inventory_item, true);
    //     if (grenade)
    //         inventory().Slot(GRENADE_SLOT, grenade, true, true);
    // }

    CArtefact* artefact = smart_cast<CArtefact*>(inventory_item);
    if (artefact && artefact->m_ItemCurrPlace.type == eItemPlaceBelt)
        MoveArtefactBelt(artefact, false);
}

void CActor::OnItemDropUpdate()
{
    CInventoryOwner::OnItemDropUpdate();

    for (auto& it : inventory().m_all)
        if (it->IsInvalid() && !attached(it))
            attach(it);
}

void CActor::OnItemRuck(CInventoryItem* inventory_item, const SInvItemPlace& previous_place)
{
    CInventoryOwner::OnItemRuck(inventory_item, previous_place);

    CArtefact* artefact = smart_cast<CArtefact*>(inventory_item);
    if (artefact && previous_place.type == eItemPlaceBelt)
        MoveArtefactBelt(artefact, false);
}

void CActor::OnItemBelt(CInventoryItem* inventory_item, const SInvItemPlace& previous_place)
{
    CInventoryOwner::OnItemBelt(inventory_item, previous_place);

    CArtefact* artefact = smart_cast<CArtefact*>(inventory_item);
    if (artefact)
        MoveArtefactBelt(artefact, true);
}

void CActor::MoveArtefactBelt(const CArtefact* artefact, bool on_belt)
{
    VERIFY(artefact);

    if (on_belt)
    {
        VERIFY(m_ArtefactsOnBelt.end() == std::find(m_ArtefactsOnBelt.begin(), m_ArtefactsOnBelt.end(), artefact));
        m_ArtefactsOnBelt.push_back(artefact);
    }
    else
    {
        // [DA_PORT] Проверка вместо VERIFY, и заодно правильная идиома удаления.
        //
        // std::remove не удаляет ничего — он сдвигает нужное к началу и возвращает новый конец.
        // Стирать надо весь хвост от него до end(), а не один элемент; при отсутствии артефакта
        // remove вернёт end(), и прежний erase(end()) был неопределённым поведением.
        auto it = std::remove(m_ArtefactsOnBelt.begin(), m_ArtefactsOnBelt.end(), artefact);
        if (it != m_ArtefactsOnBelt.end())
            m_ArtefactsOnBelt.erase(it, m_ArtefactsOnBelt.end());
    }
    if (Level().CurrentViewEntity() && Level().CurrentViewEntity() == this && CurrentGameUI()->UIMainIngameWnd->UIArtefactPanel)
        CurrentGameUI()->UIMainIngameWnd->UIArtefactPanel->InitIcons(m_ArtefactsOnBelt);
}

#define ARTEFACTS_UPDATE_TIME 0.100f

void CActor::UpdateArtefactsOnBeltAndOutfit()
{
    static float update_time = 0;

    float f_update_time = 0;

    if (update_time < ARTEFACTS_UPDATE_TIME)
    {
        update_time += conditions().fdelta_time();
        return;
    }
    else
    {
        // [DA_PORT] Dead Air runs artefact and outfit effects at double rate (`update_time*2` in the
        // author's engine). The period itself is unchanged — it is the amount applied per tick that
        // doubles, so healing, bleeding, stamina and satiety from artefacts all bite twice as hard.
        // Without this the configs, which are balanced around the doubled figure, feel inert.
        f_update_time = update_time * 2.0f;
        update_time = 0.0f;
    }

    for (auto& it : inventory().m_belt)
    {
        const auto artefact = smart_cast<CArtefact*>(it);
        if (artefact)
        {
            const float art_cond = artefact->GetCondition();
            conditions().ChangeBleeding((artefact->m_fBleedingRestoreSpeed * art_cond) * f_update_time);
            conditions().ChangeHealth((artefact->m_fHealthRestoreSpeed * art_cond) * f_update_time);
            conditions().ChangePower((artefact->m_fPowerRestoreSpeed * art_cond) * f_update_time);
            conditions().ChangeSatiety((artefact->m_fSatietyRestoreSpeed * art_cond) * f_update_time);

            // [DA_PORT] Artefact radiation is deliberately NOT applied here — Dead Air's own engine has
            // this line commented out too, and for a reason worth spelling out.
            //
            // Stock only irradiates artefacts carried ON THE BELT. Dead Air's rule is that an artefact
            // irradiates from anywhere in the inventory until it goes into a container, and that rule
            // lives in inventory_radiation.script: it walks the WHOLE inventory (iterate_inventory) and
            // adds `get_artefact_radiation()` for every object whose class is artefact. A container is
            // not a special case there — putting an artefact into one replaces both items with a single
            // object of section <artefact>_<container> (itms_manager.container_add), and those sections
            // derive from lead_box_closed, which is class S_PDA with radiation_restore_speed = 0. So the
            // packed artefact simply stops matching the class check and stops irradiating.
            //
            // Leaving the engine line in place would therefore double-count every artefact on the belt:
            // once here, once in the script. The belt is not exempt from the script — m_all includes it.
        }
    }

    // [DA_PORT] Helmets were read but never applied: CHelmet loads health/radiation/power/bleeding/
    // satiety _restore_speed from its section (ActorHelmet.cpp), yet stock only ever walked artefacts
    // and the outfit here, so those values were dead data. Dead Air's release helmets do use them —
    // five of them carry a NEGATIVE power_restore_speed, i.e. wearing one is meant to cost stamina.
    CHelmet* helmet = smart_cast<CHelmet*>(inventory().ItemFromSlot(HELMET_SLOT));
    if (helmet)
    {
        conditions().ChangeBleeding(helmet->m_fBleedingRestoreSpeed * f_update_time);
        conditions().ChangeHealth(helmet->m_fHealthRestoreSpeed * f_update_time);
        // stamina cost applies while sprinting only — same rule as the ported ConditionWalk
        if (mstate_real & mcSprint)
            conditions().ChangePower(helmet->m_fPowerRestoreSpeed * f_update_time);
        conditions().ChangeSatiety(helmet->m_fSatietyRestoreSpeed * f_update_time);
        conditions().ChangeRadiation(helmet->m_fRadiationRestoreSpeed * f_update_time);
    }

    CCustomOutfit* outfit = GetOutfit();
    if (outfit)
    {
        conditions().ChangeBleeding(outfit->m_fBleedingRestoreSpeed * f_update_time);
        conditions().ChangeHealth(outfit->m_fHealthRestoreSpeed * f_update_time);
        // [DA_PORT] as above: the outfit's stamina drain only bites while sprinting
        if (mstate_real & mcSprint)
            conditions().ChangePower(outfit->m_fPowerRestoreSpeed * f_update_time);
        conditions().ChangeSatiety(outfit->m_fSatietyRestoreSpeed * f_update_time);
        conditions().ChangeRadiation(outfit->m_fRadiationRestoreSpeed * f_update_time);
    }
    else
    {
        CHelmet* pHelmet = smart_cast<CHelmet*>(inventory().ItemFromSlot(HELMET_SLOT));
        if (!pHelmet)
        {
            CTorch* pTorch = smart_cast<CTorch*>(inventory().ItemFromSlot(TORCH_SLOT));
            if (pTorch && pTorch->GetNightVisionStatus())
            {
                pTorch->SwitchNightVision(false);
            }
        }
    }
}

// [DA_PORT] How much artefacts take off a hit of this type. ONE function, used both by the damage path
// and by the numbers the inventory shows - they must never disagree, and they already had: this path
// used to ignore wear while the display side multiplied by GetCondition(), so a nearly destroyed
// artefact quietly protected at full strength. Dead Air degrades artefacts continuously through
// artefact_degradation.script, down to a condition of 0.01, which made that gap wide.
//
// ⚠ The belt is not the only place. Dead Air puts real armour data on two BACKPACK-slot items:
//   • "airtank", the oxygen tank - chemical_burn 0.03, radiation 0.003. This is what the mod's own FAQ
//     points at for the gas on the Wild Territory and in the labs ("a suit with a closed breathing
//     system or oxygen tanks in the backpack slot");
//   • "exobackpack" - strike and explosion up, shock and burn down.
// Both are "class = SCRPTART", i.e. artefacts rather than outfits, and their protection reached the
// player through nothing at all: this loop only walked the belt, and the outfit layer
// (CEntityCondition::HitOutfitEffect) smart_casts the backpack-slot item to CCustomOutfit*, which is
// null for an artefact. Meanwhile artefact_degradation.script wears the tank down on every hit - so an
// item costing 12000 degraded while protecting from nothing.
//
// The author's build has the same hole, differently: his belt loop is belt-only too, and his outfit
// layer uses an unchecked C-style cast, which reads armour fields out of an artefact's memory.
//
// Plain backpacks are unaffected - kit_hunt and its derivatives inherit af_base_absorbation, which is
// all zeros.
// [DA_PORT] `with_condition` — это НЕ ручка, а воспроизведение авторского раскола.
//
// У автора и в базе Call of Chernobyl на одно и то же понятие две разные формулы:
//   * HitArtefactsOnBelt (боевой путь)          — `hit_power -= AffectHit(1.0f, type)`, БЕЗ износа;
//   * GetProtection_ArtefactsOnBelt (интерфейс) — `AffectHit(...) * GetCondition()`, С износом.
// То есть изношенный артефакт в бою защищает как целый, а в панели показан ослабленным. Расхождение
// пришло из стока, автор его не трогал.
//
// Порт сначала свёл обе стороны к одной формуле — с износом. Решено вернуть авторское поведение:
// правило «воспроизводим баланс автора» здесь важнее внутренней согласованности, а цифра в панели
// остаётся такой же, какой её видел игрок оригинального мода.
//
// Общим осталось только одно — перебор идёт по поясу И слоту рюкзака (см. разбор выше про баллон),
// это наша правка и она не отменяется.
float CActor::ArtefactProtection(ALife::EHitType hit_type, bool with_condition) const
{
    const auto add = [&](const PIItem item) -> float
    {
        const auto artefact = smart_cast<CArtefact*>(item);
        if (!artefact)
            return 0.0f;
        const float base = artefact->m_ArtefactHitImmunities.AffectHit(1.0f, hit_type);
        return with_condition ? base * artefact->GetCondition() : base;
    };

    float sum = 0.0f;
    for (const auto& it : inventory().m_belt)
        sum += add(it);

    sum += add(inventory().ItemFromSlot(BACKPACK_SLOT));

    return sum;
}

// [DA_PORT] Кислородный баллон гасит химию ПОРОГОМ, а не арифметикой.
//
// Так это устроено у самого мода, и понять это по движку нельзя: на Дикой территории урон от газа даёт
// логика уровня, а разрешение ей выдаёт скрипт `xr_conditions.actor_has_chem_protection` — он просто
// проверяет НАЛИЧИЕ предмета: слот рюкзака, секция ровно `airtank`, износ больше 0.1. Ни иммунитеты,
// ни их сумма там не участвуют, и защита поэтому не ослабевает плавно: выше порога она полная, ниже
// пропадает разом.
//
// В лабораториях урон идёт иначе — настоящими химическими полями (`zone_field_acidic`) через хиты
// движка, — и туда скриптовая проверка не достаёт. Получалось, что один и тот же предмет в двух местах
// игры работает по двум разным законам: на Дикой территории выключателем, в лабораториях слабой
// добавкой к иммунитету (у автора `chemical_burn_immunity = 0.03`, то есть почти ничем).
//
// Приведено к одному закону: порог из скрипта применяется и к химическим хитам. Значение и износ в
// конфигах остаются авторскими — правились раньше данные (`0.03 → 0.55`), и та правка отменена.
//
// ⚠️ Имя секции здесь зашито намеренно: его так же зашивает и сам мод в `actor_has_chem_protection`.
// Признака «это дыхательный аппарат» в конфигах нет, и придумывать его — правка данных.
bool CActor::da_chem_gear_blocks_hit() const
{
    const PIItem item = inventory().ItemFromSlot(BACKPACK_SLOT);
    if (!item)
        return false;

    if (0 != xr_strcmp(item->object().cNameSect().c_str(), "airtank"))
        return false;

    return (item->GetCondition() > 0.1f); // тот же порог, что в скрипте
}

float CActor::HitArtefactsOnBelt(float hit_power, ALife::EHitType hit_type)
{
    // [DA_PORT] Этот слой стоит ДО брони, и потому умеет обнулить хит так, что костюм его вообще не
    // увидит. Прибор `da_hit_log` показывает это явно: иначе «броня не изнашивается» неотличимо от
    // «до брони ничего не дошло». См. EntityCondition.cpp.
    extern int g_da_hit_log;

    if (ALife::eHitTypeChemicalBurn == hit_type && da_chem_gear_blocks_hit())
    {
        if (g_da_hit_log)
            Msg("~ [DA_HIT] химия %.4f ПОГАШЕНА кислородным баллоном в рюкзаке, до брони не дошла",
                hit_power);
        return 0.0f;
    }

    const float before = hit_power;
    hit_power -= ArtefactProtection(hit_type, false); // боевой путь — без износа, как у автора
    clamp(hit_power, 0.0f, flt_max);

    if (g_da_hit_log && before != hit_power)
        Msg("~ [DA_HIT] %s: пояс и рюкзак сняли %.4f (%.4f -> %.4f) до брони",
            ALife::g_cafHitType2String(hit_type), before - hit_power, before, hit_power);

    return hit_power;
}

// Отображаемое число — с износом, как у автора и в базе CoC.
float CActor::GetProtection_ArtefactsOnBelt(ALife::EHitType hit_type) const
{
    return ArtefactProtection(hit_type, true);
}

void CActor::SetZoomRndSeed(s32 Seed)
{
    if (0 != Seed)
        m_ZoomRndSeed = Seed;
    else
        m_ZoomRndSeed = s32(Level().timeServer_Async());
};

void CActor::SetShotRndSeed(s32 Seed)
{
    if (0 != Seed)
        m_ShotRndSeed = Seed;
    else
        m_ShotRndSeed = s32(Level().timeServer_Async());
};

Fvector CActor::GetMissileOffset() const
{
    return m_vMissileOffset;
}

void CActor::SetMissileOffset(const Fvector& vNewOffset)
{
    m_vMissileOffset.set(vNewOffset);
}

void CActor::spawn_supplies()
{
    inherited::spawn_supplies();
    CInventoryOwner::spawn_supplies();
}

void CActor::AnimTorsoPlayCallBack(CBlend* B)
{
    CActor* actor = (CActor*)B->CallbackParam;
    actor->m_bAnimTorsoPlayed = FALSE;
}

void CActor::UpdateMotionIcon(u32 mstate_rl)
{
    CUIMotionIcon* motion_icon = CurrentGameUI()->UIMainIngameWnd->MotionIcon();
    if (mstate_rl & mcClimb)
    {
        motion_icon->ShowState(CUIMotionIcon::stClimb);
    }
    else
    {
        if (mstate_rl & mcCrouch)
        {
            if (!isActorAccelerated(mstate_rl, IsZoomAimingMode()))
                motion_icon->ShowState(CUIMotionIcon::stCreep);
            else
                motion_icon->ShowState(CUIMotionIcon::stCrouch);
        }
        else if (mstate_rl & mcSprint)
            motion_icon->ShowState(CUIMotionIcon::stSprint);
        else if (mstate_rl & mcAnyMove && isActorAccelerated(mstate_rl, IsZoomAimingMode()))
            motion_icon->ShowState(CUIMotionIcon::stRun);
        else
            motion_icon->ShowState(CUIMotionIcon::stNormal);
    }
}

CPHDestroyable* CActor::ph_destroyable() { return smart_cast<CPHDestroyable*>(character_physics_support()); }
CEntityConditionSimple* CActor::create_entity_condition(CEntityConditionSimple* ec)
{
    if (!ec)
        m_entity_condition = xr_new<CActorCondition>(this);
    else
        m_entity_condition = smart_cast<CActorCondition*>(ec);

    return (inherited::create_entity_condition(m_entity_condition));
}

IFactoryObject* CActor::_construct()
{
    m_pPhysics_support = xr_new<CCharacterPhysicsSupport>(CCharacterPhysicsSupport::etActor, this);
    CEntityAlive::_construct();
    CInventoryOwner::_construct();
    CStepManager::_construct();

    return (this);
}

bool CActor::use_center_to_aim() const { return (!!(mstate_real & mcCrouch)); }
bool CActor::can_attach(const CInventoryItem* inventory_item) const
{
    const CAttachableItem* item = smart_cast<const CAttachableItem*>(inventory_item); // XXX: CInventoryItem cannot be casted to CAttachableItem
    if (!item || /*!item->enabled() ||*/ !item->can_be_attached())
        return (false);

    //можно ли присоединять объекты такого типа
    if (m_attach_item_sections.end() ==
        std::find(m_attach_item_sections.begin(), m_attach_item_sections.end(), inventory_item->object().cNameSect()))
        return false;

    //если уже есть присоединённый объект такого типа
    if (attached(inventory_item->object().cNameSect()))
        return false;

    return true;
}

void CActor::OnDifficultyChanged()
{
    // immunities
    VERIFY(g_SingleGameDifficulty >= egdNovice && g_SingleGameDifficulty <= egdMaster);
    pcstr diff_name = get_token_name(difficulty_type_token, g_SingleGameDifficulty);
    string128 tmp;
    strconcat(sizeof(tmp), tmp, "actor_immunities_", diff_name);
    conditions().LoadImmunities(tmp, pSettings);
    // hit probability
    strconcat(sizeof(tmp), tmp, "hit_probability_", diff_name);
    m_hit_probability = pSettings->r_float(cNameSect().c_str(), tmp);
    // two hits death parameters
    strconcat(sizeof(tmp), tmp, "actor_thd_", diff_name);
    conditions().LoadTwoHitsDeathParams(tmp);
}

CVisualMemoryManager* CActor::visual_memory() const { return &memory().visual(); }
float CActor::GetMass()
{
    return g_Alive() ? character_physics_support()->movement()->GetMass() :
                       m_pPhysicsShell ? m_pPhysicsShell->getMass() : 0;
}

bool CActor::is_on_ground()
{
    return character_physics_support()->movement()->Environment() != CPHMovementControl::peInAir;
}

bool CActor::is_ai_obstacle() const
{
    return false; // true);
}

float CActor::GetRestoreSpeed(ALife::EConditionRestoreType const& type)
{
    float res = 0.0f;
    switch (type)
    {
    case ALife::eHealthRestoreSpeed:
    {
        res = conditions().change_v().m_fV_HealthRestore;
        res += conditions().V_SatietyHealth() * (conditions().GetSatiety() > 0.0f ? 1.0f : -1.0f);

        for (auto& it : inventory().m_belt)
        {
            const auto artefact = smart_cast<CArtefact*>(it);
            if (artefact)
                res += artefact->m_fHealthRestoreSpeed * artefact->GetCondition();
        }

        const auto outfit = GetOutfit();
        if (outfit)
            res += outfit->m_fHealthRestoreSpeed;

        break;
    }
    case ALife::eRadiationRestoreSpeed:
    {
        for (auto& it : inventory().m_belt)
        {
            const auto artefact = smart_cast<CArtefact*>(it);
            if (artefact)
                res += artefact->m_fRadiationRestoreSpeed * artefact->GetCondition();
        }

        const auto outfit = GetOutfit();
        if (outfit)
            res += outfit->m_fRadiationRestoreSpeed;

        break;
    }
    case ALife::eSatietyRestoreSpeed:
    {
        res = conditions().V_Satiety();

        for (auto& it : inventory().m_belt)
        {
            const auto artefact = smart_cast<CArtefact*>(it);
            if (artefact)
                res += artefact->m_fSatietyRestoreSpeed * artefact->GetCondition();
        }

        const auto outfit = GetOutfit();
        if (outfit)
            res += outfit->m_fSatietyRestoreSpeed;

        break;
    }
    case ALife::ePowerRestoreSpeed:
    {
        res = conditions().GetSatietyPower();

        for (auto& it : inventory().m_belt)
        {
            const auto artefact = smart_cast<CArtefact*>(it);
            if (artefact)
                res += artefact->m_fPowerRestoreSpeed * artefact->GetCondition();
        }
        auto outfit = GetOutfit();
        if (outfit)
        {
            res += outfit->m_fPowerRestoreSpeed;
            VERIFY(outfit->m_fPowerLoss != 0.0f);
            res /= outfit->m_fPowerLoss;
        }
        else
            res /= 0.5f;

        break;
    }
    case ALife::eBleedingRestoreSpeed:
    {
        res = conditions().change_v().m_fV_WoundIncarnation;

        for (auto& it : inventory().m_belt)
        {
            const auto artefact = smart_cast<CArtefact*>(it);
            if (artefact)
                res += artefact->m_fBleedingRestoreSpeed * artefact->GetCondition();
        }

        const auto outfit = GetOutfit();
        if (outfit)
            res += outfit->m_fBleedingRestoreSpeed;

        break;
    }
    } // switch

    return res;
}

void CActor::On_SetEntity()
{
    auto pOutfit = GetOutfit();
    if (!pOutfit)
    {
        g_player_hud->load_default();
        // [DA_HEAD] On load the actor's visual is restored from the save (DA's headless legs model)
        // and the outfit's OnMoveToSlot does NOT re-fire, so the visual is never corrected. For a
        // naked actor, restore the head-having default visual here so its shadow/corpse has a head.
        shared_str def = GetDefaultVisualOutfit();
        if (def.size() && 0 != xr_strcmp(def.c_str(), cNameVisual().c_str()))
            ChangeVisual(def);
    }
    else
        // [DA_HEAD] re-apply the FULL outfit skin (bHUDOnly=false) on load, not just the HUD, so the
        // head-having model (see CCustomOutfit::ApplySkinModel) is applied without needing a manual
        // re-equip after loading a save.
        pOutfit->ApplySkinModel(this, true, false);
}

bool CActor::unlimited_ammo() { return !!psActorFlags.test(AF_UNLIMITEDAMMO); }
