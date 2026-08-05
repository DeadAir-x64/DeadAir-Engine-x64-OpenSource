#include "pch_script.h"
#include "xrServer_Objects_ALife_All.h"
#include "Level.h"
#include "game_cl_base.h"
#include "NET_Queue.h"
#include "ai_space.h"
#include "xrAICore/Navigation/game_level_cross_table.h"
#include "xrAICore/Navigation/level_graph.h"
#include "client_spawn_manager.h"
#include "xrEngine/xr_object.h"
#include "xrEngine/IGame_Persistent.h"
#include "xrNetServer/NET_Messages.h"

// [DA_PORT] Счётчики частей спавна — определены в движке (Device.cpp), читает разбор кадра.
extern ENGINE_API float g_da_ms_spawn_prep;
extern ENGINE_API float g_da_ms_spawn_create;
extern ENGINE_API float g_da_ms_spawn_net;
extern ENGINE_API u32 g_da_spawn_count;

// [DA_PORT] Кто именно дорог. Общая цифра называет направление, а не адрес: 2306 спавнов дают 2.2
// секунды, но какие из них — по одной сумме не видно. Копим по секции конфига, печатает da_spawn_dump.
namespace
{
struct da_spawn_stat
{
    float ms = 0.f;
    float max_ms = 0.f; // самый дорогой экземпляр: отделяет разовую загрузку ресурсов от цены штуки
    u32 count = 0;
};
xr_map<shared_str, da_spawn_stat> g_da_spawn_by_section;
}

void da_spawn_dump_print()
{
    xr_vector<std::pair<shared_str, da_spawn_stat>> rows(
        g_da_spawn_by_section.begin(), g_da_spawn_by_section.end());
    std::sort(rows.begin(), rows.end(),
        [](const std::pair<shared_str, da_spawn_stat>& a, const std::pair<shared_str, da_spawn_stat>& b)
        { return a.second.ms > b.second.ms; });

    float total = 0.f;
    u32 total_n = 0;
    for (const auto& r : rows)
    {
        total += r.second.ms;
        total_n += r.second.count;
    }

    Msg("~ [DA_SPAWN] всего %.0f мс на %u спавнов, разных секций %u", total, total_n, u32(rows.size()));

    // Разбор лидера дампа — ограничителя пространства, см. space_restrictor.cpp.
    {
        extern float g_da_ms_sr_shape;
        extern float g_da_ms_sr_base;
        extern float g_da_ms_sr_reg;
        extern u32 g_da_sr_count;
        Msg("~ [DA_SPAWN] ограничители x%u: форма %.1f, общий net_Spawn %.1f, регистрация %.1f мс",
            g_da_sr_count, g_da_ms_sr_shape, g_da_ms_sr_base, g_da_ms_sr_reg);

        extern float g_da_ms_reg_default;
        extern float g_da_ms_reg_shape;
        extern float g_da_ms_reg_insert;
        extern u32 g_da_reg_default_count;
        extern u32 g_da_reg_names_max;
        Msg("~ [DA_SPAWN] регистрация: список %.1f (x%u, имён до %u из 128), форма %.1f, вставка %.1f мс",
            g_da_ms_reg_default, g_da_reg_default_count, g_da_reg_names_max, g_da_ms_reg_shape,
            g_da_ms_reg_insert);
        g_da_ms_reg_default = g_da_ms_reg_shape = g_da_ms_reg_insert = 0.f;
        g_da_reg_default_count = g_da_reg_names_max = 0;

        extern u32 g_da_border_waits;
        Msg("~ [DA_SPAWN] границ строилось параллельно (ждали на замке): %u", g_da_border_waits);
        g_da_border_waits = 0;
        g_da_ms_sr_shape = g_da_ms_sr_base = g_da_ms_sr_reg = 0.f;
        g_da_sr_count = 0;
    }
    u32 shown = 0;
    for (const auto& r : rows)
    {
        // «макс» рядом со средним читается сразу: если максимум почти равен сумме, платил первый
        // экземпляр (загрузка модели, анимаций, звуков), и чинить в спавне нечего.
        Msg("~ [DA_SPAWN] %8.1f мс  x%-5u  сред %6.2f  макс %6.2f  %s", r.second.ms, r.second.count,
            r.second.count ? r.second.ms / float(r.second.count) : 0.f, r.second.max_ms,
            r.first.c_str());
        if (++shown >= 30)
            break;
    }
    g_da_spawn_by_section.clear();
}


void CLevel::cl_Process_Spawn(NET_Packet& P)
{
    // [DA_PORT] Подготовка сущности и чтение пакета — до создания объекта.
    CTimer da_spawn_prep;
    da_spawn_prep.Start();
    // Begin analysis
    shared_str s_name;
    P.r_stringZ(s_name);

    // Create DC (xrSE)
    CSE_Abstract* E = F_entity_Create(s_name.c_str());
    R_ASSERT2(E, s_name.c_str());

    E->Spawn_Read(P);
    if (E->s_flags.is(M_SPAWN_UPDATE))
        E->UPDATE_Read(P);

    if (!E->match_configuration())
    {
        F_entity_Destroy(E);
        return;
    }
    //-------------------------------------------------
    //.	Msg ("M_SPAWN - %s[%d][%x] - %d %d", *s_name,  E->ID, E,E->ID_Parent, Device.dwFrame);
    //-------------------------------------------------
    // force object to be local for server client
    if (OnServer())
    {
        E->s_flags.set(M_SPAWN_OBJECT_LOCAL, TRUE);
    };

    /*
    game_spawn_queue.push_back(E);
    if (g_bDebugEvents)		ProcessGameSpawns();
    /*/
    g_da_ms_spawn_prep += da_spawn_prep.GetElapsed_sec() * 1000.f;
    ++g_da_spawn_count;

    g_sv_Spawn(E);

    F_entity_Destroy(E);
    //*/
};

void CLevel::g_cl_Spawn(LPCSTR name, u8 rp, u16 flags, Fvector pos)
{
    ZoneScoped;

    // Create
    CSE_Abstract* E = F_entity_Create(name);
    VERIFY(E);

    // Fill
    E->s_name = name;
    E->set_name_replace("");
    //.	E->s_gameid			=	u8(GameID());
    E->s_RP = rp;
    E->ID = 0xffff;
    E->ID_Parent = 0xffff;
    E->ID_Phantom = 0xffff;
    E->s_flags.assign(flags);
    E->RespawnTime = 0;
    E->o_Position = pos;

    // Send
    NET_Packet P;
    E->Spawn_Write(P, TRUE);
    Send(P, net_flags(TRUE));

    // Destroy
    F_entity_Destroy(E);
}

#ifdef DEBUG
extern Flags32 psAI_Flags;
extern float debug_on_frame_gather_stats_frequency;
#include "ai_debug.h"
#endif // DEBUG

void CLevel::g_sv_Spawn(CSE_Abstract* E)
{
    ZoneScoped;

//	CTimer		T(false);

#ifdef DEBUG
//	Msg					("* CLIENT: Spawn: %s, ID=%d", *E->s_name, E->ID);
#endif

    // Optimization for single-player only	- minimize traffic between client and server
    if (GameID() == eGameIDSingle)
        psNET_Flags.set(NETFLAG_MINIMIZEUPDATES, TRUE);
    else
        psNET_Flags.set(NETFLAG_MINIMIZEUPDATES, FALSE);

    // Client spawn
    //	T.Start		();
    // [DA_PORT] Создание объекта фабрикой и net_Spawn — порознь: первое строит класс и привязку к
    // скриптам, второе грузит визуал, форму столкновений и ставит объект в схемы.
    CTimer da_spawn_create;
    da_spawn_create.Start();

    IGameObject* O = Objects.Create(E->s_name.c_str());

    const float da_create_ms = da_spawn_create.GetElapsed_sec() * 1000.f;
    g_da_ms_spawn_create += da_create_ms;

    CTimer da_spawn_net;
    da_spawn_net.Start();
// Msg				("--spawn--CREATE: %f ms",1000.f*T.GetAsync());

//	T.Start		();
    const bool da_spawn_failed = (0 == O || (!O->net_Spawn(E)));

    const float da_net_ms = da_spawn_net.GetElapsed_sec() * 1000.f;
    g_da_ms_spawn_net += da_net_ms;

    {
        da_spawn_stat& s = g_da_spawn_by_section[E->s_name];
        const float da_one_ms = da_create_ms + da_net_ms;
        s.ms += da_one_ms;
        if (da_one_ms > s.max_ms)
            s.max_ms = da_one_ms;
        ++s.count;
    }

    if (da_spawn_failed)
    {
        O->net_Destroy();
        if (!GEnv.isDedicatedServer)
            client_spawn_manager().clear(O->ID());
        Objects.Destroy(O);
        Msg("! Failed to spawn entity '%s'", E->s_name.c_str());
    }
    else
    {
        if (!GEnv.isDedicatedServer)
            client_spawn_manager().callback(O);
        // Msg			("--spawn--SPAWN: %f ms",1000.f*T.GetAsync());

        if ((E->s_flags.is(M_SPAWN_OBJECT_LOCAL)) && (E->s_flags.is(M_SPAWN_OBJECT_ASPLAYER)))
        {
            if (IsDemoPlayStarted())
            {
                if (E->s_flags.is(M_SPAWN_OBJECT_PHANTOM))
                {
                    SetControlEntity(O);
                    SetEntity(O); // do not switch !!!
                    SetDemoSpectator(O);
                }
            }
            else
            {
                if (CurrentEntity() != NULL)
                {
                    CGameObject* pGO = smart_cast<CGameObject*>(CurrentEntity());
                    if (pGO)
                        pGO->On_B_NotCurrentEntity();
                }
                SetControlEntity(O);
                SetEntity(O); // do not switch !!!
            }
        }

        if (0xffff != E->ID_Parent)
        {
            /*
            // Generate ownership-event
            NET_Packet			GEN;
            GEN.w_begin			(M_EVENT);
            GEN.w_u32			(E->m_dwSpawnTime);//-NET_Latency);
            GEN.w_u16			(GE_OWNERSHIP_TAKE);
            GEN.w_u16			(E->ID_Parent);
            GEN.w_u16			(u16(O->ID()));
            game_events->insert	(GEN);
            /*/
            NET_Packet GEN;
            GEN.write_start();
            GEN.read_start();
            GEN.w_u16(u16(O->ID()));
            cl_Process_Event(E->ID_Parent, GE_OWNERSHIP_TAKE, GEN);
            //*/
        }
    }

    /*if (E->s_flags.is(M_SPAWN_UPDATE)) {
		NET_Packet				temp;
		temp.B.count			= 0;
		E->UPDATE_Write			(temp);
		if (temp.B.count > 0)
		{
			temp.r_seek				(0);
			O->net_Import			(temp);
		}
		}*/ //:(

    //---------------------------------------------------------
    Game().OnSpawn(O);
}

CSE_Abstract* CLevel::spawn_item(
    LPCSTR section, const Fvector& position, u32 level_vertex_id, u16 parent_id, bool return_item)
{
    ZoneScoped;

    CSE_Abstract* abstract = F_entity_Create(section);
    R_ASSERT3(abstract, "Cannot find item with section", section);
    CSE_ALifeDynamicObject* dynamic_object = smart_cast<CSE_ALifeDynamicObject*>(abstract);
    if (dynamic_object && ai().get_level_graph())
    {
        dynamic_object->m_tNodeID = level_vertex_id;
        if (ai().level_graph().valid_vertex_id(level_vertex_id) && ai().get_game_graph() && ai().get_cross_table())
            dynamic_object->m_tGraphID = ai().cross_table().vertex(level_vertex_id).game_vertex_id();
    }

    //оружие спавним с полным магазинои
    CSE_ALifeItemWeapon* weapon = smart_cast<CSE_ALifeItemWeapon*>(abstract);
    if (weapon)
        weapon->a_elapsed = weapon->get_ammo_magsize();

    // Fill
    abstract->s_name = section;
    abstract->set_name_replace(section);
    //.	abstract->s_gameid		= u8(GameID());
    abstract->o_Position = position;
    abstract->s_RP = 0xff;
    abstract->ID = 0xffff;
    abstract->ID_Parent = parent_id;
    abstract->ID_Phantom = 0xffff;
    abstract->s_flags.assign(M_SPAWN_OBJECT_LOCAL);
    abstract->RespawnTime = 0;

    if (!return_item)
    {
        NET_Packet P;
        abstract->Spawn_Write(P, TRUE);
        Send(P, net_flags(TRUE));
        F_entity_Destroy(abstract);
        return (0);
    }
    else
        return (abstract);
}

void CLevel::ProcessGameSpawns()
{
    ZoneScoped;

    while (!game_spawn_queue.empty())
    {
        CSE_Abstract* E = game_spawn_queue.front();

        g_sv_Spawn(E);

        F_entity_Destroy(E);

        game_spawn_queue.pop_front();
    }
}
