// WeaponDispersion.cpp: разбос при стрельбе
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "Weapon.h"
#include "InventoryOwner.h"
#include "Actor.h"
#include "inventory_item_impl.h"

#include "ActorEffector.h"
#include "EffectorShot.h"
#include "EffectorShotX.h"

// [DA_PORT] Dead Air replaced the stock formula entirely, and the port had kept the stock one.
//
// Stock scales dispersion by numeric wear: a gun at 50% condition simply shoots twice as wide, and a
// pristine one returns 1.0 as a neutral multiplier. Dead Air throws that out and drives dispersion off
// the MALFUNCTION MASK instead - a gun is accurate until something specific is actually broken, and
// then it is the break that costs accuracy, not the wear percentage.
//
// That is why the return value changed meaning: this is now an ADDITIVE term in radians (0.0 when
// nothing is broken), not a multiplier. GetBaseDispersion below adds it instead of multiplying - the
// two changes only make sense together, and porting one without the other would either wipe out
// dispersion entirely (0.0 as a multiplier) or add a permanent constant to every weapon.
//
// Bit numbers and weights are the author's, copied as-is from
// _engine_diff/da_alpha/src_/xrGame/WeaponDispersion.cpp:18. The mask lives in m_weapon_condition_type
// (u32 here, see the u8->u32 fix; the author calls it m_conditionType).
float CWeapon::GetConditionDispersionFactor() const
{
    float cond = 0.f;

    if (m_weapon_condition_type & (1 << 6))
        cond += 0.025f;
    if (m_weapon_condition_type & (1 << 7))
        cond += 0.025f;
    if (m_weapon_condition_type & (1 << 10))
        cond += 0.025f;
    if (m_weapon_condition_type & (1 << 11))
        cond += 0.05f;
    if (m_weapon_condition_type & (1 << 12))
        cond += 0.1f;

    return cond;
    // Stock, kept for reference: return (1.f + fireDispersionConditionFactor * (1.f - GetCondition()));
}

float CWeapon::GetFireDispersion(bool with_cartridge, bool for_crosshair)
{
    if (!with_cartridge)
        return GetFireDispersion(1.0f, for_crosshair);
    if (!m_magazine.empty())
        m_fCurrentCartirdgeDisp = m_magazine.back().param_s.kDisp;
    return GetFireDispersion(m_fCurrentCartirdgeDisp, for_crosshair);
}

float CWeapon::GetBaseDispersion(float cartridge_k)
{
    // [DA_PORT] PLUS, not times - see GetConditionDispersionFactor above. The factor is now an additive
    // term that is zero on an unbroken weapon, so multiplying by it would collapse dispersion to nothing.
    return fireDispersionBase * cur_silencer_koef.fire_dispersion * cartridge_k + GetConditionDispersionFactor();
}

//текущая дисперсия (в радианах) оружия с учетом используемого патрона
float CWeapon::GetFireDispersion(float cartridge_k, bool for_crosshair)
{
    //учет базовой дисперсии, состояние оружия и влияение патрона
    float fire_disp = GetBaseDispersion(cartridge_k);

    //вычислить дисперсию, вносимую самим стрелком
    if (H_Parent())
    {
        // [DA_PORT] Проверялся РОДИТЕЛЬ, а не результат приведения.
        //
        // Держать оружие может не только владелец инвентаря: ящик, физический контейнер, аномалия.
        // Тогда `smart_cast` честно отдаёт ноль, и `pOwner->GetWeaponAccuracy()` падает — при том
        // что сам `H_Parent()` не пуст и проверку проходит.
        //
        // ⭐ Соседняя `RemoveShotEffector` двенадцатью строками ниже проверяет ровно это же
        // приведение (`if (pInventoryOwner)`). Здесь проверку не поставили.
        //
        // Без владельца разброса от стрелка нет — остаётся только базовый, и это верный ответ.
        const CInventoryOwner* pOwner = smart_cast<const CInventoryOwner*>(H_Parent());
        if (pOwner)
            fire_disp += pOwner->GetWeaponAccuracy();
    }

    return fire_disp;
}

//////////////////////////////////////////////////////////////////////////
// Для эффекта отдачи оружия
void CWeapon::AddShotEffector() { inventory_owner().on_weapon_shot_start(this); }
void CWeapon::RemoveShotEffector()
{
    CInventoryOwner* pInventoryOwner = smart_cast<CInventoryOwner*>(H_Parent());
    if (pInventoryOwner)
        pInventoryOwner->on_weapon_shot_remove(this);
}

void CWeapon::ClearShotEffector()
{
    CInventoryOwner* pInventoryOwner = smart_cast<CInventoryOwner*>(H_Parent());
    if (pInventoryOwner)
        pInventoryOwner->on_weapon_hide(this);
}

void CWeapon::StopShotEffector()
{
    CInventoryOwner* pInventoryOwner = smart_cast<CInventoryOwner*>(H_Parent());
    if (pInventoryOwner)
        pInventoryOwner->on_weapon_shot_stop();
}
