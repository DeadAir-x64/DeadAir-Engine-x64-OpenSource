#include "StdAfx.h"
#include "CustomZone.h"
#include "Include/xrRender/KinematicsAnimated.h"
#include "ZoneVisual.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "Include/xrRender/RenderVisual.h"

CVisualZone::CVisualZone() {}
CVisualZone::~CVisualZone() {}
bool CVisualZone::net_Spawn(CSE_Abstract* DC)
{
    if (!inherited::net_Spawn(DC))
        return (FALSE);

    CSE_Abstract* e = (CSE_Abstract*)(DC);
    CSE_ALifeZoneVisual* Z = smart_cast<CSE_ALifeZoneVisual*>(e);
    IKinematicsAnimated* SA = smart_cast<IKinematicsAnimated*>(Visual());

    // [DA_PORT] Отсутствие анимации у аномалии больше НЕ убивает сессию.
    //
    // Вылет у тестера: FATAL ERROR, m_attack_animation.valid(), object[zone_student54018]:
    // cannot find attack animation[] in model[dynamicsnomalynomaly_studen]. Имя анимации
    // ПУСТОЕ — оно приходит не из конфига, а из данных уровня (CSE_ALifeZoneVisual::STATE_Read),
    // и чтение из секции в движке закомментировано ещё в стоке.
    //
    // То есть это пробел в данных уровня, а чинить его правкой level.spawn мы не можем. Но
    // отсутствующая анимация — не повод отнимать у игрока сессию: R_ASSERT2 живёт и в релизе.
    //
    // Отступаем по-хорошему: нет анимации атаки — играем ту же, что в покое (аномалия просто не
    // меняет вид при срабатывании). Нет и её — не играем ничего, все места воспроизведения ниже
    // теперь проверяют пригодность. Про пробел сообщаем в лог, чтобы он не потерялся.
    m_idle_animation = SA->ID_Cycle_Safe(Z->startup_animation);
    m_attack_animation = SA->ID_Cycle_Safe(Z->attack_animation);

    if (!m_attack_animation.valid())
    {
        Msg("! [DA_PORT] %s: нет анимации атаки [%s] в модели [%s] — беру анимацию покоя",
            cName().c_str(), Z->attack_animation.c_str(), cNameVisual().c_str());
        m_attack_animation = m_idle_animation;
    }

    if (!m_idle_animation.valid())
        Msg("! [DA_PORT] %s: нет анимации покоя [%s] в модели [%s] — аномалия останется без анимации",
            cName().c_str(), Z->startup_animation.c_str(), cNameVisual().c_str());

    if (m_idle_animation.valid())
        SA->PlayCycle(m_idle_animation);

    setVisible(TRUE);

    return (TRUE);
}

void CVisualZone::SwitchZoneState(EZoneState new_state)
{
    if (m_eZoneState == eZoneStateBlowout && new_state != eZoneStateBlowout)
    {
        //	IKinematicsAnimated*	SA=smart_cast<IKinematicsAnimated*>(Visual());
        if (m_idle_animation.valid()) // [DA_PORT] см. net_Spawn
            smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(m_idle_animation);
    }

    inherited::SwitchZoneState(new_state);
}
void CVisualZone::Load(LPCSTR section)
{
    inherited::Load(section);
    m_dwAttackAnimaionStart = pSettings->r_u32(section, "attack_animation_start");
    m_dwAttackAnimaionEnd = pSettings->r_u32(section, "attack_animation_end");
    VERIFY2(m_dwAttackAnimaionStart < m_dwAttackAnimaionEnd,
        "attack_animation_start must be less then attack_animation_end");
}

void CVisualZone::UpdateBlowout()
{
    inherited::UpdateBlowout();
    if (m_dwAttackAnimaionStart >= (u32)m_iPreviousStateTime && m_dwAttackAnimaionStart < (u32)m_iStateTime &&
        m_attack_animation.valid()) // [DA_PORT] см. net_Spawn
        smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(m_attack_animation);

    if (m_dwAttackAnimaionEnd >= (u32)m_iPreviousStateTime && m_dwAttackAnimaionEnd < (u32)m_iStateTime &&
        m_idle_animation.valid()) // [DA_PORT] см. net_Spawn
        smart_cast<IKinematicsAnimated*>(Visual())->PlayCycle(m_idle_animation);
}
