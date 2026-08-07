#include "pch_script.h"
#include "xrEngine/FDemoRecord.h"
#include "xrEngine/FDemoPlay.h"
#include "xrEngine/Environment.h"
#include "xrEngine/IGame_Persistent.h"
#include "ParticlesObject.h"
#include "Level.h"
#include "HUDManager.h"
#include "xrServer.h"
#include "NET_Queue.h"
#include "game_cl_base.h"
#include "entity_alive.h"
#include "ai_space.h"
// [DA_PORT] Для прогрева визуалов: реестр объектов ALife и серверные сущности с именем модели.
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "xrServer_Objects_ALife.h"
#include "ai_debug.h"
#include "ShootingObject.h"
#include "GametaskManager.h"
#include "Level_Bullet_Manager.h"
#include "xrScriptEngine/script_process.hpp"
#include "xrScriptEngine/script_engine.hpp"
#include "team_base_zone.h"
#include "InfoPortion.h"
#include "xrAICore/Navigation/PatrolPath/patrol_path_storage.h"
#include "date_time.h"
#include "space_restriction_manager.h"
#include "seniority_hierarchy_holder.h"
#include "space_restrictor.h"
#include "client_spawn_manager.h"
#include "autosave_manager.h"
#include "ClimableObject.h"
#include "xrAICore/Navigation/level_graph.h"
#include "mt_config.h"
#include "map_manager.h"
#include "xrEngine/CameraManager.h"
#include "level_sounds.h"
#include "Car.h"
#include "trade_parameters.h"
#include "game_cl_base_weapon_usage_statistic.h"
#include "MainMenu.h"
#include "xrEngine/XR_IOConsole.h"
#include "Actor.h"
#include "player_hud.h"
#include "ui/UIGameTutorial.h"
#include "file_transfer.h"
#include "Message_Filter.h"
#include "DemoPlay_Control.h"
#include "DemoInfo.h"
#include "CustomDetector.h"
#include "xrPhysics/IPHWorld.h"
#include "xrPhysics/PHCommander.h"
#include "xrPhysics/console_vars.h"
#include "xrNetServer/NET_Messages.h"
#include "xrEngine/GameFont.h"
#include "da_memory_probe.h" // [DA_PORT]

#ifdef DEBUG
#include "level_debug.h"
#include "ai/stalker/ai_stalker.h"
#include "debug_renderer.h"
#include "PhysicObject.h"
#include "PHDebug.h"
#include "debug_text_tree.h"
#include "LevelGraphDebugRender.hpp"
#endif

extern CUISequencer* g_tutorial;
extern CUISequencer* g_tutorial2;

float g_cl_lvInterp = 0.1;
u32 lvInterpSteps = 0;

CLevel::CLevel()
    : IPureClient(Device.GetTimerGlobal())
#ifdef CONFIG_PROFILE_LOCKS
      ,
      DemoCS(MUTEX_PROFILE_ID(DemoCS))
#endif
{
    ZoneScoped;

    g_bDebugEvents = strstr(Core.Params, "-debug_ge") != nullptr;
    game_events = xr_new<NET_Queue_Event>();
    eChangeRP = Engine.Event.Handler_Attach("LEVEL:ChangeRP", this);
    eDemoPlay = Engine.Event.Handler_Attach("LEVEL:PlayDEMO", this);
    eChangeTrack = Engine.Event.Handler_Attach("LEVEL:PlayMusic", this);
    eEnvironment = Engine.Event.Handler_Attach("LEVEL:Environment", this);
    eEntitySpawn = Engine.Event.Handler_Attach("LEVEL:spawn", this);
    m_pBulletManager = xr_new<CBulletManager>();
    if (!GEnv.isDedicatedServer)
    {
        m_map_manager = xr_new<CMapManager>();
        m_game_task_manager = xr_new<CGameTaskManager>();
    }
    m_dwDeltaUpdate = u32(fixed_step * 1000);
    m_seniority_hierarchy_holder = xr_new<CSeniorityHierarchyHolder>();
    if (!GEnv.isDedicatedServer)
    {
        m_level_sound_manager = xr_new<CLevelSoundManager>();
        m_space_restriction_manager = xr_new<CSpaceRestrictionManager>();
        m_client_spawn_manager = xr_new<CClientSpawnManager>();
        m_autosave_manager = xr_new<CAutosaveManager>();
#ifdef DEBUG
        m_debug_renderer = xr_new<CDebugRenderer>();
        levelGraphDebugRender = xr_new<LevelGraphDebugRender>();
        m_level_debug = xr_new<CLevelDebug>();
#endif
    }
    m_ph_commander = xr_new<CPHCommander>();
    m_ph_commander_scripts = xr_new<CPHCommander>();
    pObjects4CrPr.clear();
    pActors4CrPr.clear();
    pHUD = xr_new<CHUDManager>();
    g_player_hud = xr_new<player_hud>();
    g_player_hud->load_default();
}

CLevel::~CLevel()
{
    ZoneScoped;

    xr_delete(g_player_hud);
    xr_delete(pHUD);
    delete_data(hud_zones_list);
    hud_zones_list = nullptr;
    Msg("- Destroying level");
    Engine.Event.Handler_Detach(eEntitySpawn, this);
    Engine.Event.Handler_Detach(eEnvironment, this);
    Engine.Event.Handler_Detach(eChangeTrack, this);
    Engine.Event.Handler_Detach(eDemoPlay, this);
    Engine.Event.Handler_Detach(eChangeRP, this);
    if (physics_world())
    {
        destroy_physics_world();
    }
    // destroy PSs
    for (auto& ps : m_StaticParticles)
        CParticlesObject::Destroy(ps);
    m_StaticParticles.clear();
    // Unload sounds
    // unload prefetched sounds
    sound_registry.clear();
    // unload static sounds
    for (auto& sound : static_Sounds)
    {
        sound->destroy();
        xr_delete(sound);
    }
    static_Sounds.clear();
    xr_delete(m_level_sound_manager);
    xr_delete(m_space_restriction_manager);
    xr_delete(m_seniority_hierarchy_holder);
    xr_delete(m_client_spawn_manager);
    xr_delete(m_autosave_manager);
#ifdef DEBUG
    xr_delete(levelGraphDebugRender);
    xr_delete(m_debug_renderer);
#endif
    if (!GEnv.isDedicatedServer)
        GEnv.ScriptEngine->remove_script_process(ScriptProcessor::Level);
    xr_delete(game);
    xr_delete(game_events);
    xr_delete(m_pBulletManager);
    xr_delete(pStatGraphR);
    xr_delete(pStatGraphS);
    xr_delete(m_ph_commander);
    xr_delete(m_ph_commander_scripts);
    pObjects4CrPr.clear();
    pActors4CrPr.clear();
    ai().unload();
#ifdef DEBUG
    xr_delete(m_level_debug);
#endif
    xr_delete(m_map_manager);
    delete_data(m_game_task_manager);
    // here we clean default trade params
    // because they should be new for each saved/loaded game
    // and I didn't find better place to put this code in
    // XXX nitrocaster: find better place for this clean()
    CTradeParameters::clean();
    if (g_tutorial && g_tutorial->m_pStoredInputReceiver == this)
        g_tutorial->m_pStoredInputReceiver = nullptr;
    if (g_tutorial2 && g_tutorial2->m_pStoredInputReceiver == this)
        g_tutorial2->m_pStoredInputReceiver = nullptr;
    if (IsDemoPlay())
    {
        StopPlayDemo();
        if (m_reader)
        {
            FS.r_close(m_reader);
            m_reader = nullptr;
        }
    }
    xr_delete(m_msg_filter);
    xr_delete(m_demoplay_control);
    xr_delete(m_demo_info);
    if (IsDemoSave())
    {
        StopSaveDemo();
    }
    deinit_compression();
}

shared_str CLevel::name() const { return map_data.m_name; }
void CLevel::GetLevelInfo(CServerInfo* si)
{
    if (Server && game)
    {
        Server->GetServerInfo(si);
    }
}

void CLevel::PrefetchSound(LPCSTR name)
{
    // preprocess sound name
    string_path tmp;
    xr_strcpy(tmp, name);
    xr_strlwr(tmp);
    if (strext(tmp))
        *strext(tmp) = 0;
    shared_str snd_name = tmp;
    // find in registry
    auto it = sound_registry.find(snd_name);
    // if find failed - preload sound
    if (it == sound_registry.end())
        sound_registry[snd_name].create(snd_name.c_str(), st_Effect, sg_SourceType);
}

// Game interface ////////////////////////////////////////////////////
int CLevel::get_RPID(LPCSTR /**name**/)
{
    /*
    // Gain access to string
    LPCSTR	params = pLevel->r_string("respawn_point",name);
    if (0==params)	return -1;

    // Read data
    Fvector4	pos;
    int			team;
    sscanf		(params,"%f,%f,%f,%d,%f",&pos.x,&pos.y,&pos.z,&team,&pos.w); pos.y += 0.1f;

    // Search respawn point
    svector<Fvector4,maxRP>	&rp = Level().get_team(team).RespawnPoints;
    for (int i=0; i<(int)(rp.size()); ++i)
    if (pos.similar(rp[i],EPS_L))	return i;
    */
    return -1;
}

bool g_bDebugEvents = false;

void CLevel::cl_Process_Event(u16 dest, u16 type, NET_Packet& P)
{
    ZoneScoped;

    // Msg("--- event[%d] for [%d]",type,dest);
    IGameObject* O = Objects.net_Find(dest);
    if (0 == O)
    {
#ifdef DEBUG
        Msg("* WARNING: c_EVENT[%d] to [%d]: unknown dest", type, dest);
#endif
        return;
    }
    CGameObject* GO = smart_cast<CGameObject*>(O);
    if (!GO)
    {
#ifndef MASTER_GOLD
        Msg("! ERROR: c_EVENT[%d] : non-game-object", dest);
#endif
        return;
    }
    if (type != GE_DESTROY_REJECT)
    {
        if (type == GE_DESTROY)
        {
            Game().OnDestroy(GO);
        }
        GO->OnEvent(P, type);
    }
    else
    {
        // handle GE_DESTROY_REJECT here
        u32 pos = P.r_tell();
        u16 id = P.r_u16();
        P.r_seek(pos);
        bool ok = true;
        IGameObject* D = Objects.net_Find(id);
        if (0 == D)
        {
#ifndef MASTER_GOLD
            Msg("! ERROR: c_EVENT[%d] : unknown dest", id);
#endif
            ok = false;
        }
        CGameObject* GD = smart_cast<CGameObject*>(D);
        if (!GD)
        {
#ifndef MASTER_GOLD
            Msg("! ERROR: c_EVENT[%d] : non-game-object", id);
#endif
            ok = false;
        }
        GO->OnEvent(P, GE_OWNERSHIP_REJECT);
        if (ok)
        {
            Game().OnDestroy(GD);
            GD->OnEvent(P, GE_DESTROY);
        }
    }
}

void CLevel::ProcessGameEvents()
{
    ZoneScoped;

    // Game events
    {
        NET_Packet P;
        u32 svT = timeServer() - NET_Latency;

        // [DA_PORT] Бюджет на разбор очереди событий: остаток уходит в следующий кадр.
        //
        // Цикл вычерпывал ВСЮ накопившуюся очередь за один заход. При массовом спавне -- игрок вошёл
        // в новый район, ALife разом перевела десятки объектов в онлайн -- это давало выброс: замер
        // da_perf_watch поймал 9.71 мс в одном кадре, при обычных нулях.
        //
        // События помечены серверным временем и разбираются по готовности, поэтому перенос остатка на
        // следующий кадр их не теряет: они просто дождутся своей очереди. Проверка стоит В КОНЦЕ тела
        // цикла, чтобы каждый заход обрабатывал хотя бы одно событие и очередь не могла застрять.
        //
        // da_events_budget_ms 0 возвращает прежнее поведение -- разбирать всё разом.
        extern ENGINE_API float ps_da_events_budget_ms;
        CTimer da_events_timer;
        da_events_timer.Start();
        bool da_was_spawn = false;

        while (game_events->available(svT))
        {
            u16 ID, dest, type;
            game_events->get(ID, dest, type, P);
            switch (ID)
            {
            case M_SPAWN:
            {
                u16 dummy16;
                P.r_begin(dummy16);
                cl_Process_Spawn(P);
                da_was_spawn = true; // [DA_PORT] см. бюджет в конце цикла
                break;
            }
            case M_EVENT:
            {
                cl_Process_Event(dest, type, P);
                break;
            }
            case M_MOVE_PLAYERS:
            {
                u8 Count = P.r_u8();
                for (u8 i = 0; i < Count; i++)
                {
                    u16 ID = P.r_u16();
                    Fvector NewPos, NewDir;
                    P.r_vec3(NewPos);
                    P.r_vec3(NewDir);
                    CActor* OActor = smart_cast<CActor*>(Objects.net_Find(ID));
                    if (0 == OActor)
                        break;
                    OActor->MoveActor(NewPos, NewDir);
                }
                NET_Packet PRespond;
                PRespond.w_begin(M_MOVE_PLAYERS_RESPOND);
                Send(PRespond, net_flags(TRUE, TRUE));
                break;
            }
            case M_STATISTIC_UPDATE:
            {
                if (GameID() != eGameIDSingle)
                    Game().m_WeaponUsageStatistic->OnUpdateRequest(&P);
                break;
            }
            case M_FILE_TRANSFER:
            {
                if (m_file_transfer) // in case of net_Stop
                    m_file_transfer->on_message(&P);
                break;
            }
            case M_GAMEMESSAGE:
            {
                Game().OnGameMessage(P);
                break;
            }
            default:
            {
                VERIFY(0);
                break;
            }
            }

            // [DA_PORT] Бюджет действует ТОЛЬКО в игре, не под загрузкой.
            //
            // Под экраном загрузки очередь спавна обязана вычерпаться целиком: следом идёт прогрев
            // планировщика, и он проходит по всем объектам уровня. Оборванная очередь оставляет часть
            // объектов несозданными, и прогрев падает на них -- чтение по нулевому адресу со стеком,
            // упирающимся в CSheduler::ProcessStep. Именно так я и уронил игру, поставив бюджет без
            // этой проверки.
            //
            // Признак -- dwPrecacheFrame: пока он не ноль, идёт предзагрузка, и торопиться некуда.
            // Обрываем ТОЛЬКО если время съел спавн -- взято у движка Anomaly (SPAWN_ANTIFREEZE),
            // там откладывается именно он. Прочие события (движение, попадания, сетевые ответы)
            // задерживать нельзя: они дешёвые, а опоздание у них видно сразу.
            if (da_was_spawn && ps_da_events_budget_ms > 0.f && Device.dwPrecacheFrame == 0 &&
                da_events_timer.GetElapsed_sec() * 1000.f > ps_da_events_budget_ms)
                break;
        }
    }
    if (OnServer() && GameID() != eGameIDSingle)
        Game().m_WeaponUsageStatistic->Send_Check_Respond();
}

void CLevel::MakeReconnect()
{
    if (!Engine.Event.Peek("KERNEL:disconnect"))
    {
        Engine.Event.Defer("KERNEL:disconnect");
        char const* server_options = nullptr;
        char const* client_options = nullptr;
        if (m_caServerOptions.c_str())
        {
            server_options = xr_strdup(m_caServerOptions.c_str());
        }
        else
        {
            server_options = xr_strdup("");
        }
        if (m_caClientOptions.c_str())
        {
            client_options = xr_strdup(m_caClientOptions.c_str());
        }
        else
        {
            client_options = xr_strdup("");
        }
        Engine.Event.Defer("KERNEL:start", size_t(server_options), size_t(client_options));
    }
}


// [DA_PORT] Части обновления уровня. Замер da_perf_watch: в выбросах "уровень" доходил до 379 мс, а
// обновление объектов внутри него оставалось мелким -- значит время в остальных частях, которых
// никто не мерил. Читает разбор кадра (Device.cpp).
extern ENGINE_API float g_da_ms_bullets;
extern ENGINE_API float g_da_ms_gameevents;
extern ENGINE_API float g_da_ms_map;
extern ENGINE_API float g_da_ms_tasks;

namespace
{
struct da_lvl_part
{
    CTimer t;
    float& dst;
    explicit da_lvl_part(float& d) : dst(d) { t.Start(); }
    ~da_lvl_part() { dst += t.GetElapsed_sec() * 1000.f; }
};
} // namespace


// [DA_PORT] ---- Прогрев визуалов: модели готовятся под экраном загрузки ---------------------------
//
// Зачем. Замер da_perf_watch поймал кадры, где разбор игровых событий стоил 9.4 мс. Бюджет их не
// вылечил, и это само по себе оказалось ответом: бюджет проверяется ПОСЛЕ события, значит суммарное
// время не может превысить порог плюс стоимость последнего спавна. Раз вышло 9.4 при пороге 3 --
// один спавн стоил около семи миллисекунд сам по себе. Такой неделим, резать его снаружи нечем.
//
// Дорог он ровно один раз: при первом появлении объекта данного вида грузятся модель, текстуры и
// анимации. Второй такой же объект появляется дёшево -- модель уже в пуле. Значит лечится не
// бюджетом, а прогревом: пройти по списку объектов уровня, создать каждый визуал заранее и вернуть
// в пул. Тот же приём, что у нас уже работает для шейдеров и описаний персонажей.
//
// Делается ВО ВРЕМЯ предзагрузки, порциями по времени: CLevel::OnFrame работает и там, пока висит
// экран загрузки. Поэтому прогрев не удлиняет ни один кадр -- он растворяется в загрузке.
//
// da_visual_warmup_ms отрицательным выключает; 0 -- весь список за один заход.
namespace
{
xr_vector<shared_str> g_da_warm_list;
u32 g_da_warm_index = 0;
bool g_da_warm_built = false;
u32 g_da_warm_done = 0;
float g_da_warm_ms = 0.f;

void da_collect_visuals()
{
    g_da_warm_built = true;
    g_da_warm_ms = 0.f;

    if (!ai().get_alife())
        return;

    // Только текущий уровень: у объекта есть вершина игрового графа, у вершины -- номер уровня.
    // Без этого фильтра пришлось бы готовить визуалы всей Зоны, а это тысячи лишних моделей.
    const GameGraph::_LEVEL_ID level = ai().level_graph().level_id();

    xr_set<shared_str> unique;
    for (const auto& it : ai().alife().objects().objects())
    {
        const CSE_ALifeDynamicObject* const obj = it.second;
        if (!obj)
            continue;
        if (!ai().game_graph().valid_vertex_id(obj->m_tGraphID))
            continue;
        if (ai().game_graph().vertex(obj->m_tGraphID)->level_id() != level)
            continue;

        const CSE_Visual* const visual = smart_cast<const CSE_Visual*>(obj);
        if (!visual || !visual->visual_name.size())
            continue;

        unique.insert(visual->visual_name);
    }

    g_da_warm_list.assign(unique.begin(), unique.end());
    Msg("* [DA_PORT] прогрев визуалов: к загрузке готовится %u разных моделей", u32(g_da_warm_list.size()));
}
} // namespace

void CLevel::da_warmup_visuals()
{
    extern ENGINE_API float ps_da_visual_warmup_ms;
    if (ps_da_visual_warmup_ms < 0.f) // отрицательное -- прогрев выключен
        return;
    if (Device.dwPrecacheFrame == 0) // предзагрузка кончилась -- поздно и уже незачем
        return;

    if (!g_da_warm_built)
        da_collect_visuals();

    if (g_da_warm_index >= g_da_warm_list.size())
        return;

    CTimer t;
    t.Start();
    while (g_da_warm_index < g_da_warm_list.size())
    {
        // Создаём и сразу возвращаем в пул: модель остаётся готовой, а ссылку никто не держит.
        // Именно так пул и рассчитан -- model_Delete кладёт модель обратно, а не уничтожает.
        IRenderVisual* v = GEnv.Render->model_Create(g_da_warm_list[g_da_warm_index].c_str());
        if (v)
        {
            GEnv.Render->model_Delete(v, false);
            ++g_da_warm_done;
        }
        ++g_da_warm_index;

        // Ноль -- значит без потолка: догнать спавн важнее, чем сгладить кадр загрузочного экрана.
        if (ps_da_visual_warmup_ms > 0.f && t.GetElapsed_sec() * 1000.f > ps_da_visual_warmup_ms)
            break;
    }

    g_da_warm_ms += t.GetElapsed_sec() * 1000.f;

    if (g_da_warm_index >= g_da_warm_list.size())
        Msg("* [DA_PORT] прогрев визуалов: готово %u из %u за %.0f мс", g_da_warm_done,
            u32(g_da_warm_list.size()), g_da_warm_ms);
}

void CLevel::OnFrame()
{
    da_warmup_visuals(); // [DA_PORT] см. выше
    // Счётчики частей обнуляются здесь же, раз в кадр.
    g_da_ms_bullets = g_da_ms_gameevents = g_da_ms_map = g_da_ms_tasks = 0.f;
    {
        extern ENGINE_API float g_da_ms_spawn_prep;
        extern ENGINE_API float g_da_ms_spawn_create;
        extern ENGINE_API float g_da_ms_spawn_net;
        extern ENGINE_API u32 g_da_spawn_count;
        g_da_ms_spawn_prep = g_da_ms_spawn_create = g_da_ms_spawn_net = 0.f;
        g_da_spawn_count = 0;
    }
    ZoneScoped;
    // [DA_PORT] Обновление уровня целиком. Вместе со счётчиками физики и объектов даёт остаток —
    // то, что не объясняется ни тем, ни другим. Читает Device.cpp.
    extern ENGINE_API float g_da_ms_level;
    CTimer da_lvl_timer;
    da_lvl_timer.Start();
    struct da_lvl_scope
    {
        CTimer& t;
        ~da_lvl_scope() { g_da_ms_level += t.GetElapsed_sec() * 1000.f; }
    } da_lvl_scope_inst{ da_lvl_timer };


    DA_MemTick(); // [DA_PORT] досчитать объекты после спавна, см. da_memory_probe.h

#ifdef DEBUG
    DBG_RenderUpdate();
#endif
    Fvector temp_vector;
    m_feel_deny.feel_touch_update(temp_vector, 0.f);
    if (GameID() != eGameIDSingle)
        psDeviceFlags.set(rsDisableObjectsAsCrows, true);
    else
        psDeviceFlags.set(rsDisableObjectsAsCrows, false);
    // commit events from bullet manager from prev-frame
    {
        da_lvl_part __da(g_da_ms_bullets);
        stats.BulletManagerCommit.Begin();
        BulletManager().CommitEvents();
        stats.BulletManagerCommit.End();
    }
    // Client receive
    if (net_isDisconnected())
    {
        if (OnClient() && GameID() != eGameIDSingle)
        {
#ifdef DEBUG
            Msg("--- I'm disconnected, so clear all objects...");
#endif
            ClearAllObjects();
        }
        Engine.Event.Defer("kernel:disconnect");
        return;
    }
    else
    {
        stats.ClientRecv.Begin();
        ClientReceive();
        stats.ClientRecv.End();
    }
    { da_lvl_part __da(g_da_ms_gameevents); ProcessGameEvents(); }
    if (m_bNeed_CrPr)
        make_NetCorrectionPrediction();
    if (!GEnv.isDedicatedServer)
    {
        if (g_mt_config.test(mtMap))
        {
            R_ASSERT(m_map_manager);
            Device.seqParallel.push_back(
                fastdelegate::FastDelegate0<>(m_map_manager, &CMapManager::Update));
        }
        else
    { da_lvl_part __da(g_da_ms_map); MapManager().Update(); }
        if (IsGameTypeSingle() && Device.dwPrecacheFrame == 0)
        {
            // XXX nitrocaster: was enabled in x-ray 1.5; to be restored or removed
            // if (g_mt_config.test(mtMap))
            //{
            //    Device.seqParallel.push_back(fastdelegate::FastDelegate0<>(
            //    m_game_task_manager,&CGameTaskManager::UpdateTasks));
            //}
            // else
    { da_lvl_part __da(g_da_ms_tasks); GameTaskManager().UpdateTasks(); }
        }
    }
    // Inherited update
    inherited::OnFrame();
    // Draw client/server stats
    if (!GEnv.isDedicatedServer && psDeviceFlags.test(rsStatistic))
    {
        CGameFont* F = UI().Font().pFontDI;
        if (!psNET_direct_connect)
        {
            if (IsServer())
            {
                const IServerStatistic* S = Server->GetStatistic();
                F->SetHeightI(0.015f);
                F->OutSetI(0.0f, 0.5f);
                F->SetColor(color_xrgb(0, 255, 0));
                F->OutNext("IN:  %4d/%4d (%2.1f%%)", S->bytes_in_real, S->bytes_in,
                    100.f * float(S->bytes_in_real) / float(S->bytes_in));
                F->OutNext("OUT: %4d/%4d (%2.1f%%)", S->bytes_out_real, S->bytes_out,
                    100.f * float(S->bytes_out_real) / float(S->bytes_out));
                F->OutNext("client_2_sever ping: %d", net_Statistic.getPing());
                F->OutNext("SPS/Sended : %4d/%4d", S->dwBytesPerSec, S->dwBytesSended);
                F->OutNext("sv_urate/cl_urate : %4d/%4d", psNET_ServerUpdate, psNET_ClientUpdate);
                F->SetColor(color_xrgb(255, 255, 255));
                struct net_stats_functor
                {
                    xrServer* m_server;
                    CGameFont* F;
                    void operator()(IClient* C)
                    {
                        m_server->UpdateClientStatistic(C);
                        F->OutNext("0x%08x: P(%d), BPS(%2.1fK), MRR(%2d), MSR(%2d), Retried(%2d), Blocked(%2d)",
                            // Server->game->get_option_s(*C->Name,"name",*C->Name),
                            C->ID.value(), C->stats.getPing(),
                            float(C->stats.getBPS()), // /1024,
                            C->stats.getMPS_Receive(), C->stats.getMPS_Send(), C->stats.getRetriedCount(),
                            C->stats.dwTimesBlocked);
                    }
                };
                net_stats_functor tmp_functor;
                tmp_functor.m_server = Server;
                tmp_functor.F = F;
                Server->ForEachClientDo(tmp_functor);
            }
            if (IsClient())
            {
                IPureClient::UpdateStatistic();
                F->SetHeightI(0.015f);
                F->OutSetI(0.0f, 0.5f);
                F->SetColor(color_xrgb(0, 255, 0));
                F->OutNext("client_2_sever ping: %d", net_Statistic.getPing());
                F->OutNext("sv_urate/cl_urate : %4d/%4d", psNET_ServerUpdate, psNET_ClientUpdate);
                F->SetColor(color_xrgb(255, 255, 255));
                F->OutNext("BReceivedPs(%2d), BSendedPs(%2d), Retried(%2d), Blocked(%2d)",
                    net_Statistic.getReceivedPerSec(), net_Statistic.getSendedPerSec(), net_Statistic.getRetriedCount(),
                    net_Statistic.dwTimesBlocked);
#ifdef DEBUG
                if (!pStatGraphR)
                {
                    pStatGraphR = xr_new<CStatGraph>();
                    pStatGraphR->SetRect(50, 700, 300, 68, 0xff000000, 0xff000000);
                    // m_stat_graph->SetGrid(0, 0.0f, 10, 1.0f, 0xff808080, 0xffffffff);
                    pStatGraphR->SetMinMax(0.0f, 65536.0f, 1000);
                    pStatGraphR->SetStyle(CStatGraph::stBarLine);
                    pStatGraphR->AppendSubGraph(CStatGraph::stBarLine);
                }
                pStatGraphR->AppendItem(float(net_Statistic.getBPS()), 0xff00ff00, 0);
                F->OutSet(20.f, 700.f);
                F->OutNext("64 KBS");
#endif
            }
        }
    }
    else
    {
#ifdef DEBUG
        if (pStatGraphR)
            xr_delete(pStatGraphR);
#endif
    }
    g_pGamePersistent->Environment().SetGameTime(GetEnvironmentGameDayTimeSec(), game->GetEnvironmentGameTimeFactor());
    if (!GEnv.isDedicatedServer)
        GEnv.ScriptEngine->script_process(ScriptProcessor::Level)->update();
    m_ph_commander->update();
    m_ph_commander_scripts->update();
    stats.BulletManagerCommit.Begin();
    BulletManager().CommitRenderSet();
    stats.BulletManagerCommit.End();
    // update static sounds
    if (!GEnv.isDedicatedServer)
    {
        if (g_mt_config.test(mtLevelSounds))
        {
            R_ASSERT(m_level_sound_manager);
            Device.seqParallel.push_back(
                fastdelegate::FastDelegate0<>(m_level_sound_manager, &CLevelSoundManager::Update));
        }
        else
            m_level_sound_manager->Update();

        // [DA_PORT] Сборка мусора Lua — ТОЛЬКО в главном потоке, флаг mtLUA_GC больше не спрашивается.
        //
        // Он отправлял script_gc в seqParallel, то есть на рабочий поток. Сборщик мусора не просто
        // читает состояние интерпретатора — он переставляет в нём объекты, пока главный поток по
        // этим объектам ходит через биндеры. Та же гонка, что и с mtALife (см. shedule_Update в
        // alife_update_manager.cpp), только последствия грязнее: портится не стек, а куча Lua.
        //
        // Флаг не проверяем вовсе: сохранённое значение из user.ltx перебило бы любое умолчание.
        script_gc();
    }
    if (pStatGraphR)
    {
        static float fRPC_Mult = 10.0f;
        static float fRPS_Mult = 1.0f;
        pStatGraphR->AppendItem(float(m_dwRPC) * fRPC_Mult, 0xffff0000, 1);
        pStatGraphR->AppendItem(float(m_dwRPS) * fRPS_Mult, 0xff00ff00, 0);
    }
}

// [DA_PORT] 10, which is what the author's Dead Air uses. OpenXRay raised it to 100 upstream and left
// the old value in a comment; carrying that over made every frame do ten times as much Lua garbage
// collection as the mod was built for.
//
// Measured on the swamps: 18.46ms per frame in lua_gc against 8.18ms for the whole engine and 4.24ms
// for the renderer. That is also exactly the shape of the complaint - 200-300 fps right after a load,
// sagging to 70 over ten or twenty seconds and staying there. The heap starts small and cheap to
// sweep; as the scripts fill it to some thirty megabytes each step costs proportionally more, until
// it settles at whatever the heap settles at.
int psLUA_GCSTEP = 10;
int psLUA_GCTIMEOUT = 1000;

u32 ps_lua_gc_method = 1;

void CLevel::script_gc()
{
    ZoneScoped;
    da_seq_probe _probe("script_gc (сборка мусора Lua)"); // [DA_PORT] ловушка da_seq_trap
    AIStats.LuaGC.Begin();

    switch (ps_lua_gc_method)
    {
    case 0:
        break;

    case 2:
        if (lua_gc(GEnv.ScriptEngine->lua(), LUA_GCTIMEOUT, psLUA_GCTIMEOUT) >= 0)
            break;
        // LUA_GCTIMEOUT is unsupported, fallback to LUA_GCSTEP
        [[fallthrough]];

    default:
        ps_lua_gc_method = 1;

    case 1:
        lua_gc(GEnv.ScriptEngine->lua(), LUA_GCSTEP, psLUA_GCSTEP);
        break;
    }

    AIStats.LuaGC.End();
}

#ifdef DEBUG_PRECISE_PATH
void test_precise_path();
#endif

#ifdef DEBUG
extern Flags32 dbg_net_Draw_Flags;
#endif

void CLevel::OnRender()
{
    ZoneScoped;

    GEnv.Render->BeforeWorldRender();	//--#SM+#-- +SecondVP+

#ifdef DEBUG
    CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(Level().CurrentEntity());
    if (stalker)
        stalker->ShouldProcessOnRender(true);
#endif

    inherited::OnRender();
    if (!game)
        return;
    Game().OnRender();
    // Device.Statistic->TEST1.Begin();
    BulletManager().Render();
    // Device.Statistic->TEST1.End();

    GEnv.Render->AfterWorldRender(); //--#SM+#-- +SecondVP+
    WorldRendered(true);

    if (!Device.IsAnselActive)
        HUD().RenderUI();

#ifdef DEBUG
    physics_world()->OnRender();
#endif
#ifdef DEBUG
    if (ai().get_level_graph())
        levelGraphDebugRender->Render(ai().game_graph(), ai().level_graph());
#ifdef DEBUG_PRECISE_PATH
    test_precise_path();
#endif
    if (stalker)
    {
        stalker->OnRender();
        stalker->ShouldProcessOnRender(false);
    }
    if (bDebug)
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            IGameObject* _O = Level().Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(_O);
            if (stalker)
                stalker->OnRender();
            CCustomMonster* monster = smart_cast<CCustomMonster*>(_O);
            if (monster)
                monster->OnRender();
            CPhysicObject* physic_object = smart_cast<CPhysicObject*>(_O);
            if (physic_object)
                physic_object->OnRender();
            CSpaceRestrictor* space_restrictor = smart_cast<CSpaceRestrictor*>(_O);
            if (space_restrictor)
                space_restrictor->OnRender();
            CClimableObject* climable = smart_cast<CClimableObject*>(_O);
            if (climable)
                climable->OnRender();
            CTeamBaseZone* team_base_zone = smart_cast<CTeamBaseZone*>(_O);
            if (team_base_zone)
                team_base_zone->OnRender();
            if (GameID() != eGameIDSingle)
            {
                CInventoryItem* pIItem = smart_cast<CInventoryItem*>(_O);
                if (pIItem)
                    pIItem->OnRender();
            }
            if (dbg_net_Draw_Flags.test(dbg_draw_skeleton)) // draw skeleton
            {
                CGameObject* pGO = smart_cast<CGameObject*>(_O);
                if (pGO && pGO != Level().CurrentViewEntity() && !pGO->H_Parent())
                {
                    if (pGO->Position().distance_to_sqr(Device.vCameraPosition) < 400.0f)
                    {
                        pGO->dbg_DrawSkeleton();
                    }
                }
            }
        }
        //  [7/5/2005]
        if (Server && Server->GetGameState())
            Server->GetGameState()->OnRender();
        //  [7/5/2005]
        ObjectSpace.dbgRender();
        UI().Font().pFontStat->OutSet(170, 630);
        UI().Font().pFontStat->SetHeight(16.0f);
        UI().Font().pFontStat->SetColor(0xffff0000);
        if (Server)
            UI().Font().pFontStat->OutNext("Client Objects:      [%d]", Server->GetEntitiesNum());
        UI().Font().pFontStat->OutNext("Server Objects:      [%d]", Objects.o_count());
        UI().Font().pFontStat->OutNext("Interpolation Steps: [%d]", Level().GetInterpolationSteps());
        if (Server)
        {
            UI().Font().pFontStat->OutNext("Server updates size: [%d]", Server->GetLastUpdatesSize());
        }
        UI().Font().pFontStat->SetHeight(8.0f);
    }
#endif

#ifdef DEBUG
    if (bDebug)
    {
        DBG().draw_object_info();
        DBG().draw_text();
        DBG().draw_level_info();
    }
    debug_renderer().render();
    DBG().draw_debug_text();
    if (psAI_Flags.is(aiVision))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            IGameObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;
            stalker->dbg_draw_vision();
        }
    }

    if (psAI_Flags.test(aiDrawVisibilityRays))
    {
        for (u32 I = 0; I < Level().Objects.o_count(); I++)
        {
            IGameObject* object = Objects.o_get_by_iterator(I);
            CAI_Stalker* stalker = smart_cast<CAI_Stalker*>(object);
            if (!stalker)
                continue;
            stalker->dbg_draw_visibility_rays();
        }
    }
#endif
}

void CLevel::OnEvent(EVENT E, u64 P1, u64 /**P2**/)
{
    ZoneScoped;

    if (E == eEntitySpawn)
    {
        char Name[128];
        Name[0] = 0;
        sscanf(LPCSTR(P1), "%s", Name);
        Level().g_cl_Spawn(Name, 0xff, M_SPAWN_OBJECT_LOCAL, Fvector().set(0, 0, 0));
    }
    else if (E == eChangeRP && P1)
    {
    }
    else if (E == eDemoPlay && P1)
    {
        char* name = (char*)P1;
        string_path RealName;
        xr_strcpy(RealName, name);
        xr_strcat(RealName, ".xrdemo");
        Cameras().AddCamEffector(xr_new<CDemoPlay>(RealName, 1.3f, 0));
    }
    else if (E == eChangeTrack && P1)
    {
        // int id = atoi((char*)P1);
        // Environment->Music_Play(id);
    }
    else if (E == eEnvironment)
    {
        // int id=0; float s=1;
        // sscanf((char*)P1,"%d,%f",&id,&s);
        // Environment->set_EnvMode(id,s);
    }
}

void CLevel::DumpStatistics(IGameFont& font, IPerformanceAlert* alert)
{
    inherited::DumpStatistics(font, alert);
    stats.FrameEnd();
    font.OutNext("Client:");
    font.OutNext("- receive:    %2.2fms, %d", stats.ClientRecv.result, stats.ClientRecv.count);
    font.OutNext("- send:       %2.2fms, %d", stats.ClientSend.result, stats.ClientSend.count);
    font.OutNext("- compress:   %2.2fms", stats.ClientCompressor.result);
    font.OutNext("- int send:   %2.2fms, %d", stats.ClientSendInternal.result, stats.ClientSendInternal.count);
    font.OutNext("- bmcommit:   %2.2fms, %d", stats.BulletManagerCommit.result, stats.BulletManagerCommit.count);
    stats.FrameStart();
    if (Server)
        Server->DumpStatistics(font, alert);
    AIStats.FrameEnd();
    font.OutNext("AI think:     %2.2fms, %d", AIStats.Think.result, AIStats.Think.count);
    font.OutNext("- range:      %2.2fms, %d", AIStats.Range.result, AIStats.Range.count);
    font.OutNext("- path:       %2.2fms, %d", AIStats.Path.result, AIStats.Path.count);
    font.OutNext("- node:       %2.2fms, %d", AIStats.Node.result, AIStats.Node.count);
    font.OutNext("AI vision:    %2.2fms, %d", AIStats.Vis.result, AIStats.Vis.count);
    font.OutNext("- query:      %2.2fms", AIStats.VisQuery.result);
    font.OutNext("- rayCast:    %2.2fms", AIStats.VisRayTests.result);
    font.OutNext("LUA GC:       %d Kb, %2.2fms", lua_gc(GEnv.ScriptEngine->lua(), LUA_GCCOUNT, 0), AIStats.LuaGC.result);
    AIStats.FrameStart();
}

void CLevel::AddObject_To_Objects4CrPr(CGameObject* pObj)
{
    if (!pObj)
        return;
    for (CGameObject* obj : pObjects4CrPr)
    {
        if (obj == pObj)
            return;
    }
    pObjects4CrPr.push_back(pObj);
}
void CLevel::AddActor_To_Actors4CrPr(CGameObject* pActor)
{
    if (!pActor)
        return;
    if (!smart_cast<CActor*>(pActor))
        return;
    for (CGameObject* act : pActors4CrPr)
    {
        if (act == pActor)
            return;
    }
    pActors4CrPr.push_back(pActor);
}

void CLevel::RemoveObject_From_4CrPr(CGameObject* pObj)
{
    if (!pObj)
        return;
    auto objIt = std::find(pObjects4CrPr.begin(), pObjects4CrPr.end(), pObj);
    if (objIt != pObjects4CrPr.end())
    {
        pObjects4CrPr.erase(objIt);
    }
    auto aIt = std::find(pActors4CrPr.begin(), pActors4CrPr.end(), pObj);
    if (aIt != pActors4CrPr.end())
    {
        pActors4CrPr.erase(aIt);
    }
}

void CLevel::make_NetCorrectionPrediction()
{
    ZoneScoped;

    m_bNeed_CrPr = false;
    m_bIn_CrPr = true;
    u64 NumPhSteps = physics_world()->StepsNum();
    physics_world()->StepsNum() -= m_dwNumSteps;
    if (ph_console::g_bDebugDumpPhysicsStep && m_dwNumSteps > 10)
    {
        Msg("!!! TOO MANY PHYSICS STEPS FOR CORRECTION PREDICTION = %d !!!", m_dwNumSteps);
        m_dwNumSteps = 10;
    }
    physics_world()->Freeze();
    // setting UpdateData and determining number of PH steps from last received update
    for (CGameObject* obj : pObjects4CrPr)
    {
        if (!obj)
            continue;
        obj->PH_B_CrPr();
    }
    // first prediction from "delivered" to "real current" position
    // making enought PH steps to calculate current objects position based on their updated state
    for (u32 i = 0; i < m_dwNumSteps; i++)
    {
        physics_world()->Step();

        for (CGameObject* act : pActors4CrPr)
        {
            if (!act || act->CrPr_IsActivated())
                continue;
            act->PH_B_CrPr();
        }
    }
    for (CGameObject* obj : pObjects4CrPr)
    {
        if (!obj)
            continue;
        obj->PH_I_CrPr();
    }
    if (!InterpolationDisabled())
    {
        for (u32 i = 0; i < lvInterpSteps; i++) // second prediction "real current" to "future" position
        {
            physics_world()->Step();
        }
        for (CGameObject* obj : pObjects4CrPr)
        {
            if (!obj)
                continue;
            obj->PH_A_CrPr();
        }
    }
    physics_world()->UnFreeze();
    physics_world()->StepsNum() = NumPhSteps;
    m_dwNumSteps = 0;
    m_bIn_CrPr = false;
    pObjects4CrPr.clear();
    pActors4CrPr.clear();
}

u32 CLevel::GetInterpolationSteps() { return lvInterpSteps; }
void CLevel::UpdateDeltaUpd(u32 LastTime)
{
    u32 CurrentDelta = LastTime - m_dwLastNetUpdateTime;
    if (CurrentDelta < m_dwDeltaUpdate)
        CurrentDelta = iFloor(float(m_dwDeltaUpdate * 10 + CurrentDelta) / 11);
    m_dwLastNetUpdateTime = LastTime;
    m_dwDeltaUpdate = CurrentDelta;
    if (0 == g_cl_lvInterp)
        ReculcInterpolationSteps();
    else if (g_cl_lvInterp > 0)
    {
        lvInterpSteps = iCeil(g_cl_lvInterp / fixed_step);
    }
}

void CLevel::ReculcInterpolationSteps()
{
    lvInterpSteps = iFloor(float(m_dwDeltaUpdate) / (fixed_step * 1000));
    if (lvInterpSteps > 60)
        lvInterpSteps = 60;
    if (lvInterpSteps < 3)
        lvInterpSteps = 3;
}

bool CLevel::InterpolationDisabled() { return g_cl_lvInterp < 0; }
void CLevel::PhisStepsCallback(u32 Time0, u32 Time1)
{
    if (!Level().game)
        return;
    if (GameID() == eGameIDSingle)
        return;
    //#pragma todo("Oles to all: highly inefficient and slow!!!")
    // fixed (Andy)
    /*
    for (xr_vector<IGameObject*>::iterator O=Level().Objects.objects.begin(); O!=Level().Objects.objects.end(); ++O)
    {
    if( smart_cast<CActor*>((*O)){
    CActor* pActor = smart_cast<CActor*>(*O);
    if (!pActor || pActor->Remote()) continue;
    pActor->UpdatePosStack(Time0, Time1);
    }
    };
    */
}

void CLevel::SetNumCrSteps(u32 NumSteps)
{
    m_bNeed_CrPr = true;
    if (m_dwNumSteps > NumSteps)
        return;
    m_dwNumSteps = NumSteps;
    if (m_dwNumSteps > 1000000)
    {
        VERIFY(0);
    }
}

ALife::_TIME_ID CLevel::GetStartGameTime() { return (game->GetStartGameTime()); }

ALife::_TIME_ID CLevel::GetGameTime() { return (game->GetGameTime()); }

ALife::_TIME_ID CLevel::GetEnvironmentGameTime() const
{
    if (!game)
        return 0;
    return (game->GetEnvironmentGameTime());
}

u8 CLevel::GetDayTime()
{
    u32 dummy32, hours;
    GetGameDateTime(dummy32, dummy32, dummy32, hours, dummy32, dummy32, dummy32);
    VERIFY(hours < 256);
    return u8(hours);
}

float CLevel::GetGameDayTimeSec() { return (float(s64(GetGameTime() % (24 * 60 * 60 * 1000))) / 1000.f); }

u32 CLevel::GetGameDayTimeMS() { return (u32(s64(GetGameTime() % (24 * 60 * 60 * 1000)))); }

float CLevel::GetEnvironmentGameDayTimeSec() const
{
    return (float(s64(GetEnvironmentGameTime() % (24 * 60 * 60 * 1000))) / 1000.f);
}

void CLevel::GetGameDateTime(u32& year, u32& month, u32& day, u32& hours, u32& mins, u32& secs, u32& milisecs)
{
    split_time(GetGameTime(), year, month, day, hours, mins, secs, milisecs);
}

float CLevel::GetGameTimeFactor()
{
    return game->GetGameTimeFactor();
}

void CLevel::SetGameTimeFactor(const float fTimeFactor)
{
    game->SetGameTimeFactor(fTimeFactor);
}

void CLevel::SetGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor)
{
    game->SetGameTimeFactor(GameTime, fTimeFactor);
}

float CLevel::GetEnvironmentTimeFactor() const
{
    if (!game)
        return 0.0f;
    return game->GetEnvironmentGameTimeFactor();
}

void CLevel::SetEnvironmentTimeFactor(const float fTimeFactor)
{
    if (!game)
        return;
    game->SetEnvironmentGameTimeFactor(fTimeFactor);
}

void CLevel::SetEnvironmentGameTimeFactor(u64 const& GameTime, float const& fTimeFactor)
{
    if (!game)
        return;
    game->SetEnvironmentGameTimeFactor(GameTime, fTimeFactor);
}

bool CLevel::IsServer()
{
    if (!Server || IsDemoPlayStarted())
        return false;
    return true;
}

bool CLevel::IsClient()
{
    if (IsDemoPlayStarted())
        return true;
    if (Server)
        return false;
    return true;
}

void CLevel::OnAlifeSimulatorUnLoaded()
{
    MapManager().ResetStorage();
    GameTaskManager().ResetStorage();
}

void CLevel::OnAlifeSimulatorLoaded()
{
    MapManager().ResetStorage();
    GameTaskManager().ResetStorage();
}

void CLevel::OnSessionTerminate(pcstr reason) { MainMenu()->OnSessionTerminate(reason); }
u32 GameID() { return Game().Type(); }
CZoneList* CLevel::create_hud_zones_list()
{
    hud_zones_list = xr_new<CZoneList>();
    hud_zones_list->clear();
    return hud_zones_list;
}

bool CZoneList::feel_touch_contact(IGameObject* O)
{
    TypesMapIt it = m_TypesMap.find(O->cNameSect());
    bool res = (it != m_TypesMap.end());
    CCustomZone* pZone = smart_cast<CCustomZone*>(O);
    if (pZone && !pZone->IsEnabled())
    {
        res = false;
    }
    return res;
}

CZoneList::CZoneList() {}
CZoneList::~CZoneList()
{
    clear();
    destroy();
}
