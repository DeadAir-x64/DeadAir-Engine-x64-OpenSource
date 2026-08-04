#include "StdAfx.h"

#include "CustomOutfit.h"
#include "xrPhysics/PhysicsShell.h"
#include "inventory_space.h"
#include "Inventory.h"
#include "Actor.h"
#include "game_cl_base.h"
#include "Level.h"
#include "BoneProtections.h"
#include "Include/xrRender/Kinematics.h"
#include "player_hud.h"
#include "ActorHelmet.h"

CCustomOutfit::CCustomOutfit()
{
    m_flags.set(FUsingCondition, TRUE);

    m_HitTypeProtection.resize(ALife::eHitTypeMax);
    for (int i = 0; i < static_cast<int>(ALife::eHitTypeMax); i++)
        m_HitTypeProtection[i] = 1.0f;
}

bool CCustomOutfit::net_Spawn(CSE_Abstract* DC)
{
    if (IsGameTypeSingle())
        ReloadBonesProtection();

    BOOL res = inherited::net_Spawn(DC);
    return (res);
}

void CCustomOutfit::net_Export(NET_Packet& P)
{
    inherited::net_Export(P);
    P.w_float_q8(GetCondition(), 0.0f, 1.0f);
}

void CCustomOutfit::net_Import(NET_Packet& P)
{
    inherited::net_Import(P);
    float _cond;
    P.r_float_q8(_cond, 0.0f, 1.0f);
    SetCondition(_cond);
}

void CCustomOutfit::OnH_A_Chield()
{
    inherited::OnH_A_Chield();
    if (!IsGameTypeSingle())
        ReloadBonesProtection();
}

void CCustomOutfit::Load(LPCSTR section)
{
    inherited::Load(section);

    m_HitTypeProtection[ALife::eHitTypeBurn] = pSettings->r_float(section, "burn_protection");
    m_HitTypeProtection[ALife::eHitTypeStrike] = pSettings->r_float(section, "strike_protection");
    m_HitTypeProtection[ALife::eHitTypeShock] = pSettings->r_float(section, "shock_protection");
    m_HitTypeProtection[ALife::eHitTypeWound] = pSettings->r_float(section, "wound_protection");
    m_HitTypeProtection[ALife::eHitTypeRadiation] = pSettings->r_float(section, "radiation_protection");
    m_HitTypeProtection[ALife::eHitTypeTelepatic] = pSettings->r_float(section, "telepatic_protection");
    m_HitTypeProtection[ALife::eHitTypeChemicalBurn] = pSettings->r_float(section, "chemical_burn_protection");
    m_HitTypeProtection[ALife::eHitTypeExplosion] = pSettings->r_float(section, "explosion_protection");
    // fire_wound_protection isn't used in hit calculations code, bone protections are used instead.
    // This is used as a virtual value in the UI, and possibly in Lua scripts (which can do some real calculations).
    m_HitTypeProtection[ALife::eHitTypeFireWound] = pSettings->read_if_exists<float>(section, "fire_wound_protection", 0.0f);
    m_HitTypeProtection[ALife::eHitTypePhysicStrike] = pSettings->read_if_exists<float>(
        section, "physic_strike_protection", m_HitTypeProtection[ALife::eHitTypeStrike]);
    m_HitTypeProtection[ALife::eHitTypeLightBurn] = m_HitTypeProtection[ALife::eHitTypeBurn];

    if (pSettings->line_exist(section, "hit_fraction_actor"))
    {
        m_boneProtection.m_fHitFrac = pSettings->r_float(section, "hit_fraction_actor");

        // [DA_PORT] Формула у Dead Air всегда COP-овская. Апстрим выбирает её по наличию
        // `fire_wound_protection` и сам называет эту эвристику ненадёжной для модов (авторский
        // комментарий сохранён ниже) — мод в неё и попал: ключ у костюмов есть, и весь расчёт уходил
        // в ветку CS.
        //
        // Сверка трёх деревьев показала, что ветвления не существует вовсе:
        //   * coc_base — одна ветка, без switch;
        //   * da_alpha — то же самое, автор правит в ней только `one` (снимает делитель 0.1);
        //   * наш порт — switch OpenXRay из трёх веток, и ветка COP совпадает с авторским кодом
        //     строка в строку, а CS расходится с ним в трёх местах сразу:
        //       1) в износ уходит остаток после защиты, а не пришедший урон;
        //       2) защита домножается на костный коэффициент;
        //       3) пробившая пуля в одиночной игре режется на (ap-BoneArmor)/ap, тогда как у автора
        //          она наносит полный урон, а броня решает только «пробила / не пробила».
        //
        // Поэтому тип назначается жёстко. Ветка CS ниже остаётся на месте и приведена к авторскому
        // поведению по пункту 1, но при данных Dead Air она недостижима.
        //
        // Оригинальный комментарий апстрима:
        // Since hit_fraction_actor exists both in CS and COP, but fire_wound_protection was removed in COP,
        // We can use this hacky solution to determine which damage formula to use.
        // It not robust for mods, because they can have fire_wound_protection in configs, despite that
        // original COP engine doesn't read it.
        m_boneProtection.m_hitFracType = SBoneProtections::HitFractionActorCOP;
    }

    if (pSettings->line_exist(section, "nightvision_sect"))
        m_NightVisionSect = pSettings->r_string(section, "nightvision_sect");
    else
        m_NightVisionSect = "";

    if (pSettings->line_exist(section, "actor_visual"))
        m_ActorVisual = pSettings->r_string(section, "actor_visual");
    else
        m_ActorVisual = nullptr;

    m_ef_equipment_type = pSettings->r_u32(section, "ef_equipment_type");
    m_fPowerLoss = READ_IF_EXISTS(pSettings, r_float, section, "power_loss", 1.0f);
    clamp(m_fPowerLoss, 0.0f, 1.0f);

    m_additional_weight = pSettings->r_float(section, "additional_inventory_weight");
    m_additional_weight2 = pSettings->r_float(section, "additional_inventory_weight2");

    m_fHealthRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "health_restore_speed", 0.0f);
    m_fRadiationRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "radiation_restore_speed", 0.0f);
    m_fSatietyRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "satiety_restore_speed", 0.0f);
    m_fPowerRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "power_restore_speed", 0.0f);
    m_fBleedingRestoreSpeed = READ_IF_EXISTS(pSettings, r_float, section, "bleeding_restore_speed", 0.0f);

    m_full_icon_name = pSettings->r_string(section, "full_icon_name");
    m_artefact_count = READ_IF_EXISTS(pSettings, r_u32, section, "artefact_count", 0);
    clamp(m_artefact_count, (u32)0, (u32)5);

    m_BonesProtectionSect = READ_IF_EXISTS(pSettings, r_string, section, "bones_koeff_protection", "");
    bIsHelmetAvaliable = !!READ_IF_EXISTS(pSettings, r_bool, section, "helmet_avaliable", true);
    // [DA_PORT] Dead Air: outfit may forbid a backpack (scientific suit). Default true (allowed), like helmet.
    bIsBackpackAvaliable = !!READ_IF_EXISTS(pSettings, r_bool, section, "backpack_avaliable", true);

    // Added by Axel, to enable optional condition use on any item
    m_flags.set(FUsingCondition, READ_IF_EXISTS(pSettings, r_bool, section, "use_condition", true));
}

void CCustomOutfit::ReloadBonesProtection()
{
    IGameObject* parent = H_Parent();
    if (IsGameTypeSingle())
        parent = smart_cast<IGameObject*>(Level().CurrentViewEntity());

    if (parent && parent->Visual() && m_BonesProtectionSect.size())
        m_boneProtection.reload(m_BonesProtectionSect, smart_cast<IKinematics*>(parent->Visual()));
}

void CCustomOutfit::Hit(float hit_power, ALife::EHitType hit_type)
{
    hit_power *= GetHitImmunity(hit_type);

    // [DA_PORT] Пол в ноль — авторская строка, которой в апстриме нет, и она не декоративная.
    // Апгрейды складывают иммунитеты через `immunities_sect_add`, а прибавки в данных мода
    // ОТРИЦАТЕЛЬНЫЕ: у сталкерского костюма база chemical_burn_immunity = 0.005, а секцию
    // `sect_stalker_outfit_immunities_chemical_burn_add` (-0.002) дёргают три разных узла апгрейда.
    // Полностью прокачанный костюм выходит на -0.001, и без этой строки износ становится
    // отрицательным: костюм ЧИНИЛСЯ бы от стояния в кислоте. У «Свободы» лёгкой сумма даёт ровно 0,
    // то есть костюм переставал изнашиваться вообще.
    clamp(hit_power, 0.0f, hit_power);

    ChangeCondition(-hit_power);
}

float CCustomOutfit::GetDefHitTypeProtection(ALife::EHitType hit_type) const
{
    return m_HitTypeProtection[hit_type] * GetCondition();
}

float CCustomOutfit::GetHitTypeProtection(ALife::EHitType hit_type, s16 element) const
{
    const float base = m_HitTypeProtection[hit_type] * GetCondition();
    const float bone = m_boneProtection.getBoneProtection(element);
    return base * bone;
}

float CCustomOutfit::GetBoneArmor(s16 element) const
{
    return m_boneProtection.getBoneArmor(element);
}

float CCustomOutfit::HitThroughArmor(float hit_power, s16 element, float ap, bool& add_wound, ALife::EHitType hit_type)
{
    float NewHitPower = hit_power;

    switch (m_boneProtection.m_hitFracType)
    {
    default:
    case SBoneProtections::HitFractionActorCOP:
    {
        if (hit_type == ALife::eHitTypeFireWound)
        {
            const float ba = GetBoneArmor(element);
            if (ba < 0.0f)
                return NewHitPower;

            float BoneArmor = ba * GetCondition();
            if (/*!fis_zero(ba, EPS) &&*/ ap > BoneArmor)
            {
                //пуля пробила бронь
                if (!IsGameTypeSingle())
                {
                    float hit_fraction = (ap - BoneArmor) / ap;
                    if (hit_fraction < m_boneProtection.m_fHitFrac)
                        hit_fraction = m_boneProtection.m_fHitFrac;

                    NewHitPower *= hit_fraction;
                    NewHitPower *= m_boneProtection.getBoneProtection(element);
                }

                VERIFY(NewHitPower >= 0.0f);
            }
            else
            {
                //пуля НЕ пробила бронь
                NewHitPower *= m_boneProtection.m_fHitFrac;
                add_wound = false; 	//раны нет
            }
        }
        else
        {
            // [DA_PORT] Stock scaled non-bullet protection (radiation, chemical burn, psi, burn, shock)
            // down by 10x, leaving only strike/wound/explosion at full strength. Dead Air sets this to
            // 1.0 and balances its configs around that: helm_respirator's radiation_protection = 0.019
            // becomes 0.0019 under the old 0.1 factor, i.e. effectively no protection at all. Since our
            // data is Dead Air's 1:1, the engine has to apply it the way those numbers were tuned for.
            float one = 1.0f; // was 0.1f
            if (hit_type == ALife::eHitTypeStrike ||
                hit_type == ALife::eHitTypeWound ||
                hit_type == ALife::eHitTypeWound_2 ||
                hit_type == ALife::eHitTypeExplosion)
            {
                one = 1.0f;
            }
            const float protect = GetDefHitTypeProtection(hit_type);
            NewHitPower -= protect * one;

            if (NewHitPower < 0.f)
                NewHitPower = 0.f;
        }

        //увеличить изношенность костюма
        Hit(hit_power, hit_type);
        break;
    }
    case SBoneProtections::HitFractionActorCS:
    {
        if (hit_type == ALife::eHitTypeFireWound)
        {
            const float BoneArmor = GetBoneArmor(element) * GetCondition();

            if (ap > EPS && ap > BoneArmor)
            {
                //пуля пробила бронь
                const float d_ap = ap - BoneArmor;
                NewHitPower *= (d_ap / ap);

                if (NewHitPower < m_boneProtection.m_fHitFrac)
                    NewHitPower = m_boneProtection.m_fHitFrac;

                if (!IsGameTypeSingle())
                {
                    NewHitPower *= m_boneProtection.getBoneProtection(element);
                }

                if (NewHitPower < 0.0f)
                    NewHitPower = 0.0f;
            }
            else
            {
                //пуля НЕ пробила бронь
                NewHitPower *= m_boneProtection.m_fHitFrac;
                add_wound = false; //раны нет
            }
        }
        else
        {
            // [DA_PORT] Тот же делитель на 10, что снят выше в ветке COP — и ЭТА ветка как раз
            // рабочая, а починили сначала только соседнюю.
            //
            // Ветку выбирает наличие `fire_wound_protection` в секции костюма (см. Load): есть —
            // CS, нет — COP. У научного костюма он есть (0.29), значит весь расчёт идёт сюда, и
            // делитель оставался в силе. Замером da_rad_log в Рыжем лесу: пришло 0.0890, после
            // снаряжения 0.0830, то есть вычлось 0.0060 при `radiation_protection = 0.060` —
            // ровно в десять раз меньше. В игре это выглядело как «сквозь научный костюм
            // радиация всё равно проходит».
            //
            // Эвристика выбора ветки для модов ненадёжна, о чём сказано в комментарии автора
            // движка рядом с ней; поэтому обе ветки обязаны считать ОДИНАКОВО.
            float one = 1.0f; // was 0.1f
            if (hit_type == ALife::eHitTypeWound ||
                hit_type == ALife::eHitTypeWound_2 ||
                hit_type == ALife::eHitTypeExplosion)
            {
                one = 1.0f;
            }

            const float protect = GetHitTypeProtection(hit_type, element);
            NewHitPower -= protect * one;
            if (NewHitPower < 0.0f)
                NewHitPower = 0.0f;
        }

        // [DA_PORT] Износ считается от ПРИШЕДШЕГО урона, а не от остатка после защиты.
        //
        // Здесь стояло Hit(NewHitPower). Сверка трёх деревьев показала, что это чужое:
        //   * coc_base   — одна ветка без switch, в конце Hit(hit_power, hit_type);
        //   * da_alpha   — то же самое, автор трогает только `one` (снимает делитель 0.1);
        //   * наш порт   — switch OpenXRay из трёх веток, и ТОЛЬКО ветка CS передаёт остаток.
        // То есть ни у автора, ни в базе Call of Chernobyl износ от остатка никогда не считался, и
        // две другие ветки этого же switch тоже передают hit_power. Расходится ровно одна.
        //
        // Разница не косметическая. Кислотное поле бьёт 0.30 за удар (max_start_power 3 при
        // интервале 0.1 и attenuation 1 в центре), а авторский порог защиты — ровно 0.30 суммарно.
        // Значит у костюма, который поле держит, остаток NewHitPower равен НУЛЮ, и износ выходил
        // нулевым: чем лучше костюм защищает от химии, тем меньше он от неё изнашивается, а тот,
        // что защищает полностью, не изнашивается вовсе. FAQ мода описывает обратное — «костюм
        // быстро умирает в аномальном поле».
        //
        // Ветку выбирает наличие `fire_wound_protection` в секции костюма — у Dead Air он есть,
        // поэтому работает именно CS. Сам выбор апстрим в комментарии выше называет ненадёжным для
        // модов, что и подтвердилось: мод попал в ветку с изменённой формулой.
        Hit(hit_power, hit_type);
        break;
    }
    case SBoneProtections::HitFraction:
    {
        if (hit_type == ALife::eHitTypeFireWound)
        {
            const float BoneArmor = GetBoneArmor(element) * GetCondition() * (1 - ap);
            NewHitPower -= BoneArmor;
            if (NewHitPower < hit_power * m_boneProtection.m_fHitFrac)
                NewHitPower = hit_power * m_boneProtection.m_fHitFrac;
        }
        else
        {
            NewHitPower -= GetHitTypeProtection(hit_type, element);
        }

        //увеличить изношенность костюма
        Hit(hit_power, hit_type);
        break;
    }
    } // switch (m_boneProtection.m_hitFracType)

    return NewHitPower;
}

bool CCustomOutfit::BonePassBullet(int boneID) { return m_boneProtection.getBonePassBullet(s16(boneID)); }
#include "Torch.h"
void CCustomOutfit::OnMoveToSlot(const SInvItemPlace& prev)
{
    if (m_pInventory)
    {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor)
        {
            ApplySkinModel(pActor, true, false);
            if (prev.type == eItemPlaceSlot && !bIsHelmetAvaliable)
            {
                CTorch* pTorch = smart_cast<CTorch*>(pActor->inventory().ItemFromSlot(TORCH_SLOT));
                if (pTorch && pTorch->GetNightVisionStatus())
                    pTorch->SwitchNightVision(true, false);
            }
            PIItem pHelmet = pActor->inventory().ItemFromSlot(HELMET_SLOT);
            if (pHelmet && !bIsHelmetAvaliable)
                pActor->inventory().Ruck(pHelmet, false);
            // [DA_PORT] mirror the helmet kick for the backpack: putting on an outfit that forbids a
            // backpack moves the currently-worn one back to the ruck.
            PIItem pBackpack = pActor->inventory().ItemFromSlot(BACKPACK_SLOT);
            if (pBackpack && !bIsBackpackAvaliable)
                pActor->inventory().Ruck(pBackpack, false);
        }
    }
}

void CCustomOutfit::ApplySkinModel(CActor* pActor, bool bDress, bool bHUDOnly)
{
    if (bDress)
    {
        if (!bHUDOnly && m_ActorVisual.size())
        {
            shared_str NewVisual = NULL;
            const auto TeamSection = Game().getTeamSection(pActor->g_Team());
            if (TeamSection)
            {
                if (pSettings->line_exist(TeamSection, cNameSect().c_str()))
                {
                    NewVisual = pSettings->r_string(TeamSection, cNameSect().c_str());
                    string256 SkinName;

                    xr_strcpy(SkinName, pSettings->r_string("mp_skins_path", "skin_path"));
                    xr_strcat(SkinName, NewVisual.c_str());
                    xr_strcat(SkinName, ".ogf");
                    NewVisual._set(SkinName);
                }
            }
            if (!NewVisual.size())
            {
                // [DA_PORT] Видимый визуал — авторский `actor_visual` костюма, то есть перволичное
                // тело `actors\legs\*.ogf`: ноги с торсом, без рук и головы. В первом лице это ровно
                // то, что нужно — руки там HUD-модель оружия, а своей головы игрок не видит.
                //
                // Раньше здесь стояла подмена на полную NPC-модель, потому что одним визуалом
                // обслуживались сразу три вещи: камера, тень и труп. Ноги в камере были платой за
                // приличную тень. Теперь тень рисуется отдельной моделью
                // (CActor::renderable_RenderShadow), труп подменяется на смерти
                // (CActor::Die), и видимому визуалу можно вернуть авторский.
                NewVisual = m_ActorVisual;
            }

            pActor->ChangeVisual(NewVisual);
        }

        if (pActor == Level().CurrentViewEntity())
        {
            if (pSettings->line_exist(cNameSect(), "player_hud_section"))
                g_player_hud->load(pSettings->r_string(cNameSect(), "player_hud_section"));
            else
                g_player_hud->load_default();
        }
    }
    else
    {
        if (!bHUDOnly && m_ActorVisual.size())
        {
            shared_str DefVisual = pActor->GetDefaultVisualOutfit();
            if (DefVisual.size())
            {
                pActor->ChangeVisual(DefVisual);
            };
        }

        if (pActor == Level().CurrentViewEntity())
            g_player_hud->load_default();
    }
}

void CCustomOutfit::OnMoveToRuck(const SInvItemPlace& prev)
{
    if (m_pInventory && prev.type == eItemPlaceSlot)
    {
        CActor* pActor = smart_cast<CActor*>(H_Parent());
        if (pActor)
        {
            ApplySkinModel(pActor, false, false);
            CTorch* pTorch = smart_cast<CTorch*>(pActor->inventory().ItemFromSlot(TORCH_SLOT));
            if (pTorch && !bIsHelmetAvaliable)
                pTorch->SwitchNightVision(false);
        }
    }
};

u32 CCustomOutfit::ef_equipment_type() const
{
    return m_ef_equipment_type;
}

float CCustomOutfit::GetPowerLoss() const
{
    // Hit fraction and power loss are unrelated,
    // but it's the only way we can distinguish between SOC/CS and COP.
    // Sorry.
    if (m_boneProtection.m_hitFracType != SBoneProtections::HitFractionActorCOP)
    {
        if (m_fPowerLoss < 1 && GetCondition() <= 0)
        {
            return 1.0f;
        }
    }
    return m_fPowerLoss;
};

bool CCustomOutfit::install_upgrade_impl(LPCSTR section, bool test)
{
    bool result = inherited::install_upgrade_impl(section, test);

    result |= process_if_exists(
        section, "burn_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeBurn], test);
    result |= process_if_exists(
        section, "shock_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeShock], test);
    result |= process_if_exists(
        section, "strike_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeStrike], test);
    result |= process_if_exists(
        section, "wound_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeWound], test);
    result |= process_if_exists(
        section, "radiation_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeRadiation], test);
    result |= process_if_exists(
        section, "telepatic_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeTelepatic], test);
    result |= process_if_exists(section, "chemical_burn_protection", &CInifile::r_float,
                                m_HitTypeProtection[ALife::eHitTypeChemicalBurn], test);
    result |= process_if_exists(
        section, "explosion_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeExplosion], test);
    result |= process_if_exists(
        section, "fire_wound_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypeFireWound], test);
    result |= process_if_exists(
        section, "physic_strike_protection", &CInifile::r_float, m_HitTypeProtection[ALife::eHitTypePhysicStrike], test);

    LPCSTR str{};
    bool result2 = process_if_exists_set(section, "nightvision_sect", &CInifile::r_string, str, test);
    if (result2 && !test)
    {
        m_NightVisionSect._set(str);
    }
    result |= result2;

    result2 = process_if_exists_set(section, "bones_koeff_protection", &CInifile::r_string, str, test);
    if (result2 && !test)
    {
        m_BonesProtectionSect = str;
        ReloadBonesProtection();
    }
    result2 = process_if_exists_set(section, "bones_koeff_protection_add", &CInifile::r_string, str, test);
    if (result2 && !test)
        AddBonesProtection(str);
    result |= result2;

    if (m_boneProtection.m_hitFracType == SBoneProtections::HitFractionActorCS ||
        m_boneProtection.m_hitFracType == SBoneProtections::HitFractionActorCOP)
    {
        result |= process_if_exists(section, "hit_fraction_actor", &CInifile::r_float, m_boneProtection.m_fHitFrac, test);
    }

    result |= process_if_exists(section, "additional_inventory_weight", &CInifile::r_float, m_additional_weight, test);
    result |=
        process_if_exists(section, "additional_inventory_weight2", &CInifile::r_float, m_additional_weight2, test);

    result |= process_if_exists(section, "health_restore_speed", &CInifile::r_float, m_fHealthRestoreSpeed, test);
    result |= process_if_exists(section, "radiation_restore_speed", &CInifile::r_float, m_fRadiationRestoreSpeed, test);
    result |= process_if_exists(section, "satiety_restore_speed", &CInifile::r_float, m_fSatietyRestoreSpeed, test);
    result |= process_if_exists(section, "power_restore_speed", &CInifile::r_float, m_fPowerRestoreSpeed, test);
    result |= process_if_exists(section, "bleeding_restore_speed", &CInifile::r_float, m_fBleedingRestoreSpeed, test);

    result |= process_if_exists(section, "power_loss", &CInifile::r_float, m_fPowerLoss, test);
    clamp(m_fPowerLoss, 0.0f, 1.0f);

    result |= process_if_exists(section, "artefact_count", &CInifile::r_u32, m_artefact_count, test);
    clamp(m_artefact_count, (u32)0, (u32)5);

    return result;
}

void CCustomOutfit::AddBonesProtection(LPCSTR bones_section)
{
    IGameObject* parent = H_Parent();
    if (IsGameTypeSingle())
        parent = smart_cast<IGameObject*>(Level().CurrentViewEntity());

    if (parent && parent->Visual() && m_BonesProtectionSect.size())
        m_boneProtection.add(bones_section, smart_cast<IKinematics*>(parent->Visual()));
}

// [DA_PORT] Полная NPC-модель под секцию костюма — см. объявление в CustomOutfit.h.
//
// Таблица gamedata\configs\da_port_actor_visual.ltx сопоставляет каждому костюму стоковую модель
// фракции: голова, руки и броня того же вида. Раньше она подменяла видимый визуал актёра целиком и
// ценой этого были пропавшие ноги в первом лице; теперь по ней строится теневая модель и труп, а в
// камере остаётся авторское перволичное тело.
//
// Файл читается один раз за сессию и держится до выхода: он крошечный, а спрашивают его при каждой
// смене костюма.
shared_str da_actor_full_visual(LPCSTR outfit_section)
{
    if (!outfit_section || !outfit_section[0])
        return shared_str();

    static bool s_map_tried = false;
    static CInifile* s_map = nullptr;
    if (!s_map_tried)
    {
        s_map_tried = true;
        string_path map_fn;
        if (FS.exist(map_fn, "$game_config$", "da_port_actor_visual.ltx"))
            s_map = xr_new<CInifile>(map_fn);
    }

    if (!s_map || !s_map->section_exist("da_actor_visual_3d") ||
        !s_map->line_exist("da_actor_visual_3d", outfit_section))
        return shared_str();

    LPCSTR model = s_map->r_string("da_actor_visual_3d", outfit_section);
    if (!model || !model[0])
        return shared_str();

    // Модели может не быть в поставке — тогда молча отказываемся, вместо падения на загрузке меша.
    string_path model_ogf, mesh_fn;
    xr_sprintf(model_ogf, "%s.ogf", model);
    if (!FS.exist(mesh_fn, "$game_meshes$", model_ogf))
        return shared_str();

    return shared_str(model);
}

// [DA_PORT] Та же таблица, другая секция: модель без головы для перволичного тела. См. CustomOutfit.h.
shared_str da_actor_legs_visual(LPCSTR outfit_section)
{
    static bool s_map_tried = false;
    static CInifile* s_map = nullptr;
    if (!s_map_tried)
    {
        s_map_tried = true;
        string_path map_fn;
        if (FS.exist(map_fn, "$game_config$", "da_port_actor_visual.ltx"))
            s_map = xr_new<CInifile>(map_fn);
    }

    if (!s_map || !s_map->section_exist("da_actor_legs_3d"))
        return shared_str();

    LPCSTR model = nullptr;
    if (outfit_section && outfit_section[0] && s_map->line_exist("da_actor_legs_3d", outfit_section))
        model = s_map->r_string("da_actor_legs_3d", outfit_section);
    else if (s_map->line_exist("da_actor_legs_3d", "default"))
        model = s_map->r_string("da_actor_legs_3d", "default");

    if (!model || !model[0])
        return shared_str();

    string_path model_ogf, mesh_fn;
    xr_sprintf(model_ogf, "%s.ogf", model);
    if (!FS.exist(mesh_fn, "$game_meshes$", model_ogf))
        return shared_str();

    return shared_str(model);
}
