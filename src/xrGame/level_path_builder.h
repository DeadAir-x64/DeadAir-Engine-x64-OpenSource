////////////////////////////////////////////////////////////////////////////
//	Module 		: level_path_builder.h
//  Modified 	: 21.02.2005
//  Modified 	: 21.02.2005
//	Author		: Dmitriy Iassenev
//	Description : Level path builder
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "movement_manager.h"
#include "level_path_manager.h"
#include "detail_path_builder.h"

class CLevelPathBuilder : public CDetailPathBuilder
{
private:
    typedef CDetailPathBuilder inherited;

private:
    Fvector m_temp;
    u32 m_start_vertex_id;
    u32 m_dest_vertex_id;
    const Fvector* m_precise_position;
    u32 m_last_fail_time;
    bool m_extrapolate_path;
    bool m_use_delay_after_fail;

    // [DA_PORT] Consecutive failures, for the hopeless case - see process().
    u32 m_consecutive_fails{};

    // [DA_PORT] Собственный отступ, отдельно от штатного m_last_fail_time.
    //
    // Раньше отступ выражался через штатное поле: `m_last_fail_time = now + 2000 - 500`. Знак
    // оказался не тот. Сторож ниже сравнивает с `m_last_fail_time + 2000`, поэтому молчание
    // длилось до now+3500 вместо задуманных 500 мс - в семь раз дольше, чем написано в
    // комментарии рядом. Отдельное поле убирает и арифметику со смещениями, и её главную
    // ловушку: `now - 1500` на беззнаковом в первые полторы секунды сеанса заворачивается в
    // огромное число, и отступ становится вечным.
    u32 m_da_backoff_until{};

    enum
    {
        da_backoff_ms = u32(500),
        da_fails_before_backoff = u32(20),
    };

    IC bool da_backing_off() const
    {
        return Device.dwTimeGlobal < m_last_fail_time + time_to_wait_after_fail ||
            Device.dwTimeGlobal < m_da_backoff_until;
    }

private:
    enum
    {
        time_to_wait_after_fail = u32(2000),
    };

public:
    IC CLevelPathBuilder(CMovementManager* object)
        : inherited(object), m_last_fail_time(0), m_use_delay_after_fail(true)
    {
    }

    IC const u32& dest_vertex_id() const { return (m_dest_vertex_id); }
    IC void use_delay_after_fail(bool const value) { m_use_delay_after_fail = value; }
    IC void setup(
        const u32& start_vertex_id, const u32& dest_vertex_id, bool extrapolate_path, const Fvector* precise_position)
    {
        VERIFY(ai().level_graph().valid_vertex_id(start_vertex_id));
        m_start_vertex_id = start_vertex_id;

        VERIFY(ai().level_graph().valid_vertex_id(dest_vertex_id));
        m_dest_vertex_id = dest_vertex_id;

        m_extrapolate_path = extrapolate_path;
        if (!precise_position)
            m_precise_position = 0;
        else
        {
            m_temp = *precise_position;
            m_precise_position = &m_temp;
        }
    }

    void register_to_process()
    {
        // [DA_PORT] Флаг взводится, ТОЛЬКО если задача действительно поставлена в очередь.
        //
        // Прежний порядок - взвести, потом проверить отступ - оставлял флаг взведённым на раннем
        // выходе. И это не задержка, а ЗАЩЁЛКА: читает флаг ровно одно место,
        // CMovementManager::update_path, и при взведённом оно выходит сразу; а register_to_process
        // зовётся только оттуда, и снимает флаг только process_impl, которого в этой ветке нет.
        // Дальше NPC не пересчитывает путь ВООБЩЕ - до смены рестрикторов или уничтожения.
        //
        // Наша правка про повторные неудачи впервые завела сюда сталкеров: штатный отступ они
        // выключают (use_delay_after_fail(false)), поэтому раньше сторож у них не срабатывал.
        if (da_backing_off())
        {
            m_object->m_wait_for_distributed_computation = false;
            return;
        }

        m_object->m_wait_for_distributed_computation = true;
        Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(this, &CLevelPathBuilder::process));
    }

    void process_impl()
    {
        m_object->m_wait_for_distributed_computation = false;
        m_object->level_path().build_path(m_start_vertex_id, m_dest_vertex_id);

        if (m_object->level_path().failed())
        {
            if (m_use_delay_after_fail)
                m_last_fail_time = Device.dwTimeGlobal;

            m_object->m_path_state = CMovementManager::ePathStateBuildLevelPath;
            return;
        }

        m_object->level_path().select_intermediate_vertex();

        m_object->m_path_state = CMovementManager::ePathStateBuildDetailPath;

        m_object->detail().set_state_patrol_path(m_extrapolate_path);
        m_object->detail().set_start_position(m_object->object().Position());
        m_object->detail().set_start_direction(Fvector().setHP(-m_object->m_body.current.yaw, 0));

        if (m_precise_position)
            m_object->detail().set_dest_position(*m_precise_position);

        inherited::setup(m_object->level_path().path(), m_object->level_path().intermediate_index());
        inherited::process_impl(false);
    }

    void process()
    {
        if (da_backing_off())
            return;

        // [DA_PORT] Counted because this is the last unmeasured occupant of the parallel sequence, and
        // the sequence is the frame. Note the delay above: after a failed search this is supposed to
        // stand down for two seconds - but m_last_fail_time is only set in process_impl, while THIS
        // path goes through build_level_path, which fails on its own without ever arming it. Three
        // stalkers on the swamps fail a hundred times a second each, which is what that means in
        // practice, and a failed search is the expensive kind: it has to exhaust the reachable set
        // before it can conclude there is no way through.
        if (g_bEnableStatGather)
        {
            CTimer t;
            t.Start();
            m_object->build_level_path();
            g_da_lpb_ms += t.GetElapsed_sec() * 1000.0;
            ++g_da_lpb_calls;
        }
        else
            m_object->build_level_path();

        // [DA_PORT] Back off only once a stalker has failed REPEATEDLY.
        //
        // Measured on the swamps: three stalkers who cannot reach their target ran three searches a
        // frame, 12.3ms between them, against a 13ms frame. Ninety-five per cent of the frame spent
        // discovering, sixty times a second, that there is still no way across the water. A failed
        // search is the expensive kind - to conclude nothing is reachable it must visit everything
        // that is.
        //
        // The engine already has a delay for this, and stalkers deliberately switch it off
        // (stalker_movement_manager_obstacles.cpp). That is a reasonable call for an ordinary failure:
        // a door swings shut, someone stands in a doorway, and waiting two seconds would just make the
        // stalker look stupid. It is the wrong call for a stalker whose target is across water, where
        // there is no moment worth waiting for and no arrival to be late to.
        //
        // So the two cases are separated by how often it has just failed, rather than by a flag set
        // once at construction. A transient failure retries next frame exactly as before; twenty in a
        // row earns a short rest, which is enough to take this from thirty searches a second to three.
        // Deliberately not the full two seconds: half of one keeps them responsive if the world does
        // open up.
        if (m_object->level_path().failed())
        {
            if (++m_consecutive_fails >= da_fails_before_backoff)
                m_da_backoff_until = Device.dwTimeGlobal + da_backoff_ms;
        }
        else
            m_consecutive_fails = 0;
    }

    IC void remove()
    {
        if (m_object->m_wait_for_distributed_computation)
            m_object->m_wait_for_distributed_computation = false;

        Device.remove_from_seq_parallel(fastdelegate::FastDelegate0<>(this, &CLevelPathBuilder::process));
    }
};
