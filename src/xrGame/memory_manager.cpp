////////////////////////////////////////////////////////////////////////////
//	Module 		: memory_manager.cpp
//	Created 	: 02.10.2001
//  Modified 	: 19.11.2003
//	Author		: Dmitriy Iassenev
//	Description : Memory manager
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"
#include "memory_manager.h"
#include "visual_memory_manager.h"
#include "sound_memory_manager.h"
#include "hit_memory_manager.h"
#include "enemy_manager.h"
#include "item_manager.h"
#include "danger_manager.h"
#include "ai/stalker/ai_stalker.h"
#include "ai/stalker/ai_stalker_impl.h"
#include "agent_manager.h"
#include "agent_member_manager.h"
#include "memory_space_impl.h"
#include "xrAICore/Navigation/ai_object_location.h"
#include "xrAICore/Navigation/level_graph.h"
#include "xrEngine/profiler.h"
#include "agent_enemy_manager.h"
#include "script_game_object.h"

CMemoryManager::CMemoryManager(CEntityAlive* entity_alive, CSound_UserDataVisitor* visitor)
{
    VERIFY(entity_alive);
    m_object = smart_cast<CCustomMonster*>(entity_alive);
    m_stalker = smart_cast<CAI_Stalker*>(m_object);

    if (m_stalker)
        m_visual = xr_new<CVisualMemoryManager>(m_stalker);
    else
        m_visual = xr_new<CVisualMemoryManager>(m_object);

    m_sound = xr_new<CSoundMemoryManager>(m_object, m_stalker, visitor);
    m_hit = xr_new<CHitMemoryManager>(m_object, m_stalker);
    m_enemy = xr_new<CEnemyManager>(m_object);
    m_item = xr_new<CItemManager>(m_object);
    m_danger = xr_new<CDangerManager>(m_object);
}

CMemoryManager::~CMemoryManager()
{
    xr_delete(m_visual);
    xr_delete(m_sound);
    xr_delete(m_hit);
    xr_delete(m_enemy);
    xr_delete(m_item);
    xr_delete(m_danger);
}

void CMemoryManager::Load(LPCSTR section)
{
    sound().Load(section);
    hit().Load(section);
    enemy().Load(section);
    item().Load(section);
    danger().Load(section);
}

void CMemoryManager::reinit()
{
    visual().reinit();
    sound().reinit();
    hit().reinit();
    enemy().reinit();
    item().reinit();
    danger().reinit();
}

void CMemoryManager::reload(LPCSTR section)
{
    visual().reload(section);
    sound().reload(section);
    hit().reload(section);
    enemy().reload(section);
    item().reload(section);
    danger().reload(section);
}

#ifdef _DEBUG
extern bool g_enemy_manager_second_update;
#endif // _DEBUG

extern int ps_da_memory_dump; // [DA_PORT] см. console_commands.cpp

// [DA_PORT] ---- Разбор ПАМЯТИ NPC по частям: da_memory_dump <кадров> --------------------------
//
// Зачем. da_stalker_dump показал: после боя фаза «память» съедает 75% обновления сталкеров даже
// после того, как мёртвым перестали пересчитывать видимость каждый цикл (34.93 -> 10.93 мс на
// кадр). Один вызов всё ещё стоит 1.51 мс против здоровых 0.12 -- значит внутри есть что-то ещё,
// и «память дорогая» снова оказывается направлением, а не адресом.
//
// Части взяты по границам самой update: зрительная, слуховая, память о попаданиях, разбор
// запомненного, работа с врагами, предметы с опасностями.
namespace
{
struct da_mem_phase
{
    double total_ms = 0.0;
    double max_ms = 0.0;
    u32 calls = 0;
};

enum
{
    da_mem_visual = 0,
    da_mem_sound,
    da_mem_hit,
    da_mem_scan,
    da_mem_enemies,
    da_mem_items,
    da_mem_enemy_sel,
    da_mem_distribute,
    da_mem_rescan,
    da_mem_count
};

pcstr const g_da_mem_name[da_mem_count] = {
    "зрительная память", "слуховая память", "память о попаданиях",
    "разбор запомненного", "враги", "предметы и опасности",
    "  враги: выбор цели", "  враги: раздача по отряду", "  враги: повторный разбор",
};

da_mem_phase g_da_mem[da_mem_count];
u32 g_da_mem_frame = 0;
u32 g_da_mem_frames = 0;

struct da_mem_timer
{
    CTimer t;
    int idx;
    bool on;
    da_mem_timer(int i, bool active) : idx(i), on(active)
    {
        if (on)
            t.Start();
    }
    ~da_mem_timer()
    {
        if (!on)
            return;
        const double ms = t.GetElapsed_sec() * 1000.0;
        g_da_mem[idx].total_ms += ms;
        ++g_da_mem[idx].calls;
        if (ms > g_da_mem[idx].max_ms)
            g_da_mem[idx].max_ms = ms;
    }
};

void da_mem_report()
{
    double all = 0.0;
    for (const auto& ph : g_da_mem)
        all += ph.total_ms;
    const u32 frames = _max(g_da_mem_frames, 1u);
    Msg("~ [DA_MEM] ---- итог за %u кадров ----", g_da_mem_frames);
    Msg("~ [DA_MEM] память всех NPC: %.2f мс на кадр", float(all / frames));
    for (int i = 0; i < da_mem_count; ++i)
        Msg("~ [DA_MEM]   %-24s %6.2f мс на кадр (%4.1f%%), вызовов %6u, худший %5.2f мс",
            g_da_mem_name[i], float(g_da_mem[i].total_ms / frames),
            float(all > 0.0 ? 100.0 * g_da_mem[i].total_ms / all : 0.0), g_da_mem[i].calls,
            float(g_da_mem[i].max_ms));
    for (auto& ph : g_da_mem)
        ph = da_mem_phase();
    g_da_mem_frames = 0;
}
} // namespace

void CMemoryManager::update_enemies(const bool& registered_in_combat)
{
    // [DA_PORT] Три части внутри работы с врагами -- см. da_memory_dump. Считаются отдельно от
    // общего счётчика "враги", поэтому в отчёте идут с отступом и в сумму не входят.
    const bool da_probe = ::ps_da_memory_dump > 0;
#ifdef _DEBUG
    g_enemy_manager_second_update = false;
#endif // _DEBUG
    { da_mem_timer __da(da_mem_enemy_sel, da_probe); enemy().update(); }

    if (m_stalker && (!enemy().selected() || (smart_cast<const CAI_Stalker*>(enemy().selected()) &&
                                                 smart_cast<const CAI_Stalker*>(enemy().selected())->wounded())) &&
        registered_in_combat)
    {
        { da_mem_timer __da(da_mem_distribute, da_probe); m_stalker->agent_manager().enemy().distribute_enemies(); }

        {
            da_mem_timer __da(da_mem_rescan, da_probe);
            if (visual().enabled())
                update(visual().objects(), true);

            update(sound().objects(), true);
            update(hit().objects(), true);
        }

#ifdef _DEBUG
        g_enemy_manager_second_update = true;
#endif // _DEBUG
        enemy().update();
    }
}

void CMemoryManager::update(float time_delta)
{
    const bool da_probe = ::ps_da_memory_dump > 0;
    if (da_probe && g_da_mem_frame != Device.dwFrame)
    {
        g_da_mem_frame = Device.dwFrame;
        ++g_da_mem_frames;
        if (--::ps_da_memory_dump == 0)
            da_mem_report();
    }

    START_PROFILE("Memory Manager")

    { da_mem_timer __da(da_mem_visual, da_probe); visual().update(time_delta); }
    { da_mem_timer __da(da_mem_sound, da_probe); sound().update(); }
    { da_mem_timer __da(da_mem_hit, da_probe); hit().update(); }

    bool registered_in_combat = false;
    if (m_stalker)
        registered_in_combat = m_stalker->agent_manager().member().registered_in_combat(m_stalker);

    // update enemies and items
    enemy().reset();
    item().reset();

    {
        da_mem_timer __da(da_mem_scan, da_probe);
        if (visual().enabled())
            update(visual().objects(), true);

        update(sound().objects(), registered_in_combat ? true : false);
        update(hit().objects(), registered_in_combat ? true : false);
    }

    { da_mem_timer __da(da_mem_enemies, da_probe); update_enemies(registered_in_combat); }
    {
        da_mem_timer __da(da_mem_items, da_probe);
        item().update();
        danger().update();
    }

    STOP_PROFILE
}

void CMemoryManager::enable(const IGameObject* object, bool enable)
{
    visual().enable(object, enable);
    sound().enable(object, enable);
    hit().enable(object, enable);
}

template <typename T>
void CMemoryManager::update(const xr_vector<T>& objects, bool add_enemies)
{
    squad_mask_type mask = m_stalker ? m_stalker->agent_manager().member().mask(m_stalker) : 0;
    auto I = objects.cbegin();
    auto E = objects.cend();
    for (; I != E; ++I)
    {
        if (!(*I).m_enabled)
            continue;

        if (m_stalker && !(*I).m_squad_mask.test(mask))
            continue;

        danger().add(*I);

        if (add_enemies)
        {
            const CEntityAlive* entity_alive = smart_cast<const CEntityAlive*>((*I).m_object);
            if (entity_alive && enemy().add(entity_alive))
                continue;
        }

        const CAI_Stalker* stalker = smart_cast<const CAI_Stalker*>((*I).m_object);
        if (m_stalker && stalker)
            continue;

        if ((*I).m_object)
            item().add((*I).m_object);
    }
}

CMemoryInfo CMemoryManager::memory(const IGameObject* object) const
{
    CMemoryInfo result;
    if (!this->object().g_Alive())
        return (result);

    u32 level_time = 0;
    const CGameObject* game_object = smart_cast<const CGameObject*>(object);
    VERIFY(game_object);
    squad_mask_type mask = m_stalker ? m_stalker->agent_manager().member().mask(m_stalker) : squad_mask_type(-1);

    {
        xr_vector<CVisibleObject>::const_iterator I =
            std::find(visual().objects().begin(), visual().objects().end(), object_id(object));
        if (visual().objects().end() != I)
        {
            (CMemoryObject<CGameObject>&)result = (CMemoryObject<CGameObject>&)(*I);
            [[maybe_unused]] const bool isVisible = result.visible((*I).visible(mask)); // XXX: this may be wrong, maybe code author wanted to SET visibility, not GET???
            result.m_visual_info = true;
            level_time = (*I).m_level_time;
            VERIFY(result.m_object);
        }
    }

    {
        xr_vector<CSoundObject>::const_iterator I =
            std::find(sound().objects().begin(), sound().objects().end(), object_id(object));
        if ((sound().objects().end() != I) && (level_time < (*I).m_level_time))
        {
            (CMemoryObject<CGameObject>&)result = (CMemoryObject<CGameObject>&)(*I);
            result.m_sound_info = true;
            level_time = (*I).m_level_time;
            VERIFY(result.m_object);
        }
    }

    {
        xr_vector<CHitObject>::const_iterator I =
            std::find(hit().objects().begin(), hit().objects().end(), object_id(object));
        if ((hit().objects().end() != I) && (level_time < (*I).m_level_time))
        {
            (CMemoryObject<CGameObject>&)result = (CMemoryObject<CGameObject>&)(*I);
            result.m_object = game_object;
            result.m_hit_info = true;
            VERIFY(result.m_object);
        }
    }

    return (result);
}

u32 CMemoryManager::memory_time(const IGameObject* object) const
{
    u32 result = 0;
    if (!this->object().g_Alive())
        return (0);

    [[maybe_unused]] auto game_object = smart_cast<const CGameObject*>(object);
    VERIFY(game_object);

    {
        xr_vector<CVisibleObject>::const_iterator I =
            std::find(visual().objects().begin(), visual().objects().end(), object_id(object));
        if (visual().objects().end() != I)
            result = (*I).m_level_time;
    }

    {
        xr_vector<CSoundObject>::const_iterator I =
            std::find(sound().objects().begin(), sound().objects().end(), object_id(object));
        if ((sound().objects().end() != I) && (result < (*I).m_level_time))
            result = (*I).m_level_time;
    }

    {
        xr_vector<CHitObject>::const_iterator I =
            std::find(hit().objects().begin(), hit().objects().end(), object_id(object));
        if ((hit().objects().end() != I) && (result < (*I).m_level_time))
            result = (*I).m_level_time;
    }

    return (result);
}

Fvector CMemoryManager::memory_position(const IGameObject* object) const
{
    u32 time = 0;
    Fvector result = Fvector().set(0.f, 0.f, 0.f);
    if (!this->object().g_Alive())
        return (result);

    [[maybe_unused]] auto game_object = smart_cast<const CGameObject*>(object);
    VERIFY(game_object);

    {
        xr_vector<CVisibleObject>::const_iterator I =
            std::find(visual().objects().begin(), visual().objects().end(), object_id(object));
        if (visual().objects().end() != I)
        {
            time = (*I).m_level_time;
            result = (*I).m_object_params.m_position;
        }
    }

    {
        xr_vector<CSoundObject>::const_iterator I =
            std::find(sound().objects().begin(), sound().objects().end(), object_id(object));
        if ((sound().objects().end() != I) && (time < (*I).m_level_time))
        {
            time = (*I).m_level_time;
            result = (*I).m_object_params.m_position;
        }
    }

    {
        xr_vector<CHitObject>::const_iterator I =
            std::find(hit().objects().begin(), hit().objects().end(), object_id(object));
        if ((hit().objects().end() != I) && (time < (*I).m_level_time))
        {
            time = (*I).m_level_time;
            result = (*I).m_object_params.m_position;
        }
    }

    return (result);
}

void CMemoryManager::remove_links(IGameObject* object)
{
    if (m_object->g_Alive())
    {
        visual().remove_links(object);
        sound().remove_links(object);
        hit().remove_links(object);
    }

    danger().remove_links(object);
    enemy().remove_links(object);
    item().remove_links(object);
}

void CMemoryManager::on_restrictions_change()
{
    if (!m_object->g_Alive())
        return;

    //	danger().on_restrictions_change	();
    //	enemy().on_restrictions_change	();
    item().on_restrictions_change();
}

void CMemoryManager::make_object_visible_somewhen(const CEntityAlive* enemy)
{
    squad_mask_type mask = stalker().agent_manager().member().mask(&stalker());
    MemorySpace::CVisibleObject* obj = visual().visible_object(enemy);
    //	if (obj) {
    //		Msg						("------------------------------------------------------");
    //		Msg						("[%6d] make_object_visible_somewhen [%s] =
    //%x",Device.dwTimeGlobal,*enemy->cName(),obj->m_squad_mask.get());
    //	}
    //	LogStackTrace				("-------------make_object_visible_somewhen-------------");
    bool prev = obj ? obj->visible(mask) : false;
    visual().add_visible_object(enemy, .001f, true);
    MemorySpace::CVisibleObject* obj1 = object().memory().visual().visible_object(enemy);
    VERIFY(obj1);
    //	if (obj1)
    //		Msg						("[%6d] make_object_visible_somewhen [%s] =
    //%x",Device.dwTimeGlobal,*enemy->cName(),obj1->m_squad_mask.get());
    obj1->visible(mask, prev);
}

void CMemoryManager::save(NET_Packet& packet) const
{
    visual().save(packet);
    sound().save(packet);
    hit().save(packet);
    danger().save(packet);
}

void CMemoryManager::load(IReader& packet)
{
    visual().load(packet);
    sound().load(packet);
    hit().load(packet);
    danger().load(packet);
}

// we do this due to the limitation of client spawn manager
// should be revisited from the acrhitectural point of view
void CMemoryManager::on_requested_spawn(IGameObject* object)
{
    visual().on_requested_spawn(object);
    sound().on_requested_spawn(object);
    hit().on_requested_spawn(object);
}
