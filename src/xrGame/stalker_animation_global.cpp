////////////////////////////////////////////////////////////////////////////
//	Module 		: stalker_animation_global.cpp
//	Created 	: 25.02.2003
//  Modified 	: 19.11.2004
//	Author		: Dmitriy Iassenev
//	Description : Stalker animation manager : global animations
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "stalker_animation_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "Inventory.h"
#include "FoodItem.h"
#include "property_storage.h"
#include "stalker_movement_manager_smart_cover.h"
#include "ai/stalker/ai_stalker_space.h"
#include "stalker_animation_data.h"
#include "Weapon.h"
#include "Missile.h"
#include "stalker_animation_manager_impl.h"

using namespace StalkerSpace;

void CStalkerAnimationManager::global_play_callback(CBlend* blend)
{
    CAI_Stalker* object = (CAI_Stalker*)blend->CallbackParam;
    VERIFY(object);

    CStalkerAnimationManager& manager = object->animation();
    CStalkerAnimationPair& pair = manager.global();
    pair.on_animation_end();

    //	std::pair<LPCSTR,LPCSTR>	pair_id =
    // smart_cast<IKinematicsAnimated*>(object->Visual())->LL_MotionDefName_dbg(blend->motionID);
    //	Msg							("[%6d] global callback [%s][%s]", Device.dwTimeGlobal, pair_id.first,
    // pair_id.second);

    if (!manager.m_global_callback)
        return;

    manager.m_call_global_callback = true;
}

MotionID CStalkerAnimationManager::global_critical_hit()
{
    if (!object().critically_wounded())
        return (MotionID());

    if (global().animation())
        return (global().animation());

    CWeapon* weapon = smart_cast<CWeapon*>(object().inventory().ActiveItem());
    VERIFY2(weapon, make_string("current active item: %s", object().inventory().ActiveItem() ?
                            object().inventory().ActiveItem()->object().cName().c_str() :
                            "no active item"));

    // [DA_PORT] Три отказа вместо трёх VERIFY, которых в релизе нет.
    //
    // Здесь вылетала игра: разыменование нуля в CStalkerAnimationPair::select_animation при
    // критическом попадании в сталкера. Индекс набора анимаций считается умножением на слот
    // оружия, и ни одно из слагаемых в релизе не проверено:
    //
    //   * weapon может быть пустым — активный предмет не обязан быть оружием, а VERIFY2 выше
    //     исчезает, и следующая же строка разыменовывает ноль;
    //   * animation_slot приходит из конфига оружия. У модовых стволов он вполне может выйти за
    //     1..3, и тогда 6 * (slot - 1) уводит индекс за массив;
    //   * critical_wound_type() имеет значение critical_wound_type_dummy = u32(-1), и с ним
    //     индекс становится огромным.
    //
    // Границы массива проверяем по факту, а не по формуле: имена анимаций (global_names) и код
    // живут в разных файлах, и подгонять здесь константу под тамошний список значило бы завести
    // второе место, которое надо не забыть поправить.
    //
    // Отказ безопасен: play_global() на пустом MotionID сбрасывает состояние и возвращает false —
    // сталкер просто не проигрывает анимацию критического попадания. Это уже штатный исход, им же
    // заканчивается ветка «не ранен критически» в начале функции.
    if (!weapon)
        return (MotionID());

    const u32 animation_slot = weapon->animation_slot();
    if (animation_slot < 1 || animation_slot > 3)
        return (MotionID());

    const auto& global_animations = m_data_storage->m_part_animations.A[eBodyStateStand].m_global.A;
    const u32 index = object().critical_wound_type() + 6 * (animation_slot - 1);
    if (index >= global_animations.size())
        return (MotionID());

    return (global().select(global_animations[index].A, &object().critical_wound_weights()));
}

MotionID CStalkerAnimationManager::assign_global_animation(bool& animation_movement_controller)
{
    if (m_global_selector)
        return (m_global_selector(animation_movement_controller));

    animation_movement_controller = false;

    if (eMentalStatePanic != object().movement().mental_state())
        return (global_critical_hit());

    if (fis_zero(object().movement().speed(object().character_physics_support()->movement())))
        return (MotionID());

    return (global().select(m_data_storage->m_part_animations.A[body_state()].m_global.A[1].A));
}
