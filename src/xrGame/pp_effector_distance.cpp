#include "StdAfx.h"
#include "pp_effector_distance.h"

////////////////////////////////////////////////////////////////////////////////////
// CPPEffectorDistance
////////////////////////////////////////////////////////////////////////////////////
void CPPEffectorDistance::load(LPCSTR section)
{
    inherited::load(section);

    m_r_min_perc = pSettings->r_float(section, "radius_min");
    m_r_max_perc = pSettings->r_float(section, "radius_max");

    // [DA_PORT] Живая проверка вместо VERIFY. Падения тут нет, но есть тихая порча: в update_factor
    // ниже разность (max - min) стоит В ЗНАМЕНАТЕЛЕ, и при равных радиусах из конфига получается
    // деление на ноль. Итог — NaN, а clamp его НЕ ЧИНИТ: любое сравнение с NaN ложно, и он проходит
    // насквозь в set_factor. Так постэффект ломается без единого сообщения в логе.
    if (m_r_min_perc >= m_r_max_perc)
    {
        Msg("! [DA] эффект по расстоянию [%s]: radius_min %.3f не меньше radius_max %.3f — "
            "разведены принудительно",
            section, m_r_min_perc, m_r_max_perc);
        m_r_min_perc = m_r_max_perc - EPS_L;
    }
}

bool CPPEffectorDistance::check_completion() { return (m_dist > m_radius * m_r_max_perc); }
bool CPPEffectorDistance::check_start_conditions() { return (m_dist < m_radius * m_r_max_perc); }
void CPPEffectorDistance::update_factor()
{
    float factor;
    factor = (m_radius * m_r_max_perc - m_dist) / (m_radius * m_r_max_perc - m_radius * m_r_min_perc);
    clamp(factor, 0.01f, 1.0f);

    m_effector->set_factor(factor);
}

CPPEffectorControlled* CPPEffectorDistance::create_effector() { return xr_new<CPPEffectorControlled>(this, m_state); }
