#include "StdAfx.h"
#include "da_script_cam.h"

// [DA_PORT] Шаги сглаживания по умолчанию. Разбор — в заголовке.
//
// Направление сглаживается мягче положения намеренно: рывок камеры по углу читается глазом заметно
// сильнее, чем такой же рывок по месту. Числа взяты из Monolith и проверены там на этой же задаче.
static constexpr u32 da_cam_direction_smoothing = 12;
static constexpr u32 da_cam_position_smoothing = 6;

CDaScriptCamEffector::CDaScriptCamEffector() : CEffectorCam(cefScriptOverride, 0.f)
{
    m_camera.identity();
    m_hpb.set(0.f, 0.f, 0.f);
    m_position.set(0.f, 0.f, 0.f);
    m_smoothing = 0;
    m_hud_enabled = false;
}

void CDaScriptCamEffector::ema(Fvector& current, const Fvector& target, u32 steps)
{
    // Первый кадр: цепляться не за что, встаём в цель сразу. Иначе камера поехала бы к уступу из
    // начала координат — из-под уровня, через всю карту.
    if (fis_zero(current.x) && fis_zero(current.y) && fis_zero(current.z))
    {
        current.set(target);
        return;
    }

    // Скользящее среднее с поправкой на длительность кадра. Дробь ограничена единицей: при
    // просадке кадра множитель иначе перескочит цель и камера задрожит.
    const float alpha = 2.f / float(steps + 1);
    const float k = std::min(1.f, alpha * (float(Device.dwTimeDelta) / float(steps)));

    current.x += k * (target.x - current.x);
    current.y += k * (target.y - current.y);
    current.z += k * (target.z - current.z);
}

bool CDaScriptCamEffector::ProcessCam(SCamEffectorInfo& info)
{
    Fmatrix target;
    target.identity().setHPB(m_hpb.x, m_hpb.y, m_hpb.z).translate_over(m_position);

    if (m_smoothing == 1)
    {
        // Без сглаживания: скрипт сам считает траекторию покадрово и любое доглаживание с его
        // стороны выглядело бы запаздыванием.
        m_camera.j = target.j;
        m_camera.k = target.k;
        m_camera.c = target.c;
    }
    else
    {
        const u32 dir = m_smoothing ? m_smoothing : da_cam_direction_smoothing;
        const u32 pos = m_smoothing ? m_smoothing : da_cam_position_smoothing;
        ema(m_camera.j, target.j, dir);
        ema(m_camera.k, target.k, dir);
        ema(m_camera.c, target.c, pos);
    }

    // j — «вверх», k — «вперёд», c — положение. Порядок именно такой, перепутать n и d значит
    // положить камеру набок.
    info.n.set(m_camera.j);
    info.d.set(m_camera.k);
    info.p.set(m_camera.c);
    return true;
}
