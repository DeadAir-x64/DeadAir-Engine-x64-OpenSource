#include "pch_script.h"
#include "xrCore/da_heap_guard.h" // [DA_PORT] карантин освобождённой памяти
#include "Grenade.h" // [DA_PORT] da_grenade_test: воспроизведение вылета в физике
#include "xrEngine/Engine.h" // [DA_PORT] da_dev_mode()
#include "xrEngine/XR_IOConsole.h"
#include "xrEngine/xr_ioc_cmd.h"
#include "xrEngine/CustomHUD.h"
#include "xrEngine/FDemoRecord.h"
#include "xrEngine/FDemoPlay.h"
#include "xrMessages.h"
#include "xrServer.h"
#include "Level.h"
#include "xrScriptEngine/script_debugger.hpp"
#include "ai_debug.h"
#include "alife_simulator.h"
#include "game_cl_base.h"
#include "game_cl_single.h"
#include "game_sv_single.h"
#include "Hit.h"
#include "PHDestroyable.h"
#include "Actor.h"
#include "Actor_Flags.h"
#include "CustomZone.h"
#include "xrScriptEngine/script_engine.hpp"
#include "xrScriptEngine/script_profiler.hpp"
#include "xrScriptEngine/script_process.hpp"
#include "xrServer_Objects.h"
#include "ui/UIMainIngameWnd.h"
#include "xrPhysics/IPHWorld.h"
#include "autosave_manager.h"
#include "ai_space.h"
#include "ai/monsters/basemonster/base_monster.h"
#include "date_time.h"
// [DA_PORT] Нужен для Level_ID в da_level_probe: GamePersistent.h подключается сильно ниже по
// файлу, уже после этой команды.
#include "xrEngine/IGame_Persistent.h"
#include "mt_config.h"
#include "ui/UIOptConCom.h"
#include "UIGameSP.h"
#include "ui/UIActorMenu.h"
#include "xrUICore/Static/UIStatic.h"
#include "xrUICore/ui_styles.h"
#include "zone_effector.h"
#include "GameTask.h"
#include "MainMenu.h"
#include "saved_game_wrapper.h"
#include "xrAICore/Navigation/level_graph.h"
#include "xrNetServer/NET_Messages.h"

#include "CameraLook.h"
#include "character_hit_animations_params.h"
#include "inventory_upgrade_manager.h"

#include "xrGameSpy/GameSpy_Full.h"

#include "ai_debug_variables.h"
#include "xrPhysics/console_vars.h"
#include "GametaskManager.h"

#include "da_memory_probe.h" // [DA_PORT] инструменты замера и воспроизведения
#include "player_hud.h" // [DA_PORT] da_aim_dump читает живые замеры оружия в руках

#ifdef DEBUG
#include "PHDebug.h"
#include "ui/UIDebugFonts.h"
#include "xrAICore/Navigation/game_graph.h"
#include "LevelGraphDebugRender.hpp"
#include "CharacterPhysicsSupport.h"
#endif // DEBUG

// ⚠️ [DA_PORT] Ниже — ВНЕ блока #ifdef DEBUG, и это намеренно: проверять отчёт о вылете
// надо на той сборке, которая уходит игрокам, а не на отладочной.

// [DA_PORT] Отложенная команда: da_after_load <кадров> <команда с аргументами>.
//
// Единственный способ выполнить что-то В ИГРЕ без рук: user.ltx отрабатывает до появления уровня.
// Нужно для воспроизведения вылетов («загрузил сейв — сбросил устройство») и для проверок из
// дорожной карты, которые иначе приходится делать вручную по десять раз.
class CCC_DaAfterLoad : public IConsole_Command
{
public:
    CCC_DaAfterLoad(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(LPCSTR args) override
    {
        if (!args || !*args)
        {
            DA_AfterLoadArm(-1, nullptr);
            return;
        }
        int frames = 0;
        char cmd[512] = {};
        // Хвост строки забираем целиком: у команды могут быть свои аргументы с пробелами.
        if (sscanf(args, "%d %511[^\r\n]", &frames, cmd) != 2)
        {
            Msg("! da_after_load <кадров> <команда>   — например: da_after_load 300 vid_restart");
            return;
        }
        DA_AfterLoadArm(frames, cmd);
    }
};

// [DA_PORT] Намеренная авария: проверка отчёта о вылете на той же сборке, что у игроков.
class CCC_DaCrashTest : public IConsole_Command
{
public:
    CCC_DaCrashTest(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(LPCSTR /*args*/) override
    {
        Msg("~ [DA_PORT] ===== НАМЕРЕННАЯ АВАРИЯ (da_crash_test) =====");
        Msg("~ [DA_PORT] Это проверка отчёта о вылете, а НЕ дефект. Если такой лог пришёл от");
        Msg("~ [DA_PORT] игрока - значит команду набрали руками. В расследование не брать.");
        FlushLog();

        // volatile, иначе компилятор вправе выбросить разыменование нуля как неопределённое
        // поведение и оставить нас без аварии - проверять было бы нечего.
        volatile int* p = nullptr;
        *p = 0;
    }
};


string_path g_last_saved_game;

#ifdef DEBUG
extern float air_resistance_epsilon;
#endif // #ifdef DEBUG

extern void show_smart_cast_stats();
extern void clear_smart_cast_stats();
extern void release_smart_cast_stats();

extern u64 g_qwStartGameTime;
extern u64 g_qwEStartGameTime;

ENGINE_API
extern float psHUD_FOV;
extern float g_scope_fov; // Actor.cpp [DA_PORT] CoC-Xray compat
extern float psSqueezeVelocity;
extern int psLUA_GCSTEP;
extern int psLUA_GCTIMEOUT;
extern int psLUA_GCCEIL; // [DA_PORT] потолок кучи Lua, МБ — предохранитель к gc_timeout
extern int psLUA_GCMUL;
extern int psLUA_GCMIN;
extern int psLUA_GCMAX;
extern u32 ps_lua_gc_method;
extern int g_auto_ammo_unload;

extern int x_m_x;
extern int x_m_z;
extern BOOL net_cl_inputguaranteed;
extern BOOL net_sv_control_hit;
extern int g_dwInputUpdateDelta;
#ifdef DEBUG
extern BOOL g_ShowAnimationInfo;
#endif // DEBUG
extern BOOL g_bShowHitSectors;
// extern	BOOL	g_bDebugDumpPhysicsStep	;
extern ESingleGameDifficulty g_SingleGameDifficulty;
//-----------------------------------------------------------
extern float g_fTimeFactor;
extern int g_da_time_log;
extern float ps_da_torch_hand_delay;
extern int g_da_cell_bar_debug;
extern BOOL b_toggle_weapon_aim;

extern float g_smart_cover_factor;
extern int g_upgrades_log;
extern float g_smart_cover_animation_speed_factor;

extern BOOL g_ai_use_old_vision;
float g_aim_predict_time = 0.40f;
int g_keypress_on_start = 1;

ENGINE_API extern float g_console_sensitive;

//Alundaio
extern BOOL g_ai_die_in_anomaly;
int g_inv_highlight_equipped = 0;
//-Alundaio

// [DA_PORT] see WeaponMagazined::state_Fire - weapons pick up breakages while being fired.
//
// ON by default (27.07), deliberately. The mechanic is the author's own, written and then left
// commented out in his sources, which is why weapons never wear into faults in the original. With it on
// the three ported systems that read the malfunction mask finally do something: wear accelerates,
// rate of fire goes ragged, dispersion widens - and CheckForMisfire starts producing real jams.
int g_weapon_malfunctions = 1;

int g_first_person_death = 0;
int g_normalize_mouse_sens = 0;
int g_normalize_upgrade_mouse_sens = 0;

// [DA_PORT] Дальше этого расстояния (в метрах) инверсная кинематика ног не считается: у дальнего
// NPC подгонка стоп к рельефу невидима, а стоит она дороже всего остального в его физике.
// Разбор — в CCharacterPhysicsSupport::in_UpdateCL.
//
// 15, а не 30: тридцать отсекало меньшинство сталкеров (число вызовов подгонки при пороге 30 не
// изменилось совсем — почти все стояли ближе), и выигрыш выходил вдвое меньше обещанного. Пятнадцать
// проверено в игре: на глаз разницы не видно, а частота кадров заметно выше.
//
// Где смотреть, если сомнения вернутся: наклонные поверхности и лестницы на дистанции 15-25 м —
// именно там неподогнанная стопа висит над рельефом заметнее всего. На ровной земле не видно нигде.
// 0 возвращает прежнее поведение точь-в-точь.
float ps_da_ik_dist = 15.f;

// [DA_PORT] Отказ по компонентам связности графа уровня. 0 возвращает прежнее поведение
// точь-в-точь: каждый безнадёжный запрос снова идёт в полный поиск.
int ps_da_path_islands = 1;

// [DA_PORT] Потолок обойдённых узлов для поиска пути ПО УРОВНЮ.
//
// Штатное значение 65500 — фактически «без предела»: безнадёжный поиск честно обходит полграфа,
// прежде чем сдаться. Замеры по двум уровням, по 1500 кадров:
//
//   болота (532К вершин):  удачных 173, максимум 1701 узел, среднее 124;
//                          неудачных 114, из них 113 в 16К..64К, среднее 64961.
//   Юпитер (1.49М вершин): удачных 402, максимум 4919 узлов, среднее 207;
//                          неудачных НОЛЬ.
//
// 🪤 Первое значение было 8192 — по одним болотам, где худший удачный поиск занял 1701 узел, и
// запас казался почти пятикратным. Юпитер показал 4919, а Затон, самый большой уровень игры
// (1 851 251 вершина), — 8753. То есть 8192 уже сегодня отрезал бы честный маршрут.
//
// ⛔ Оценка «по числу вершин» здесь НЕ работает: линейная экстраполяция обещала для Затона 6100,
// вышло 8753 — промах на 43% ВНИЗ. Каждый новый уровень давал максимум выше предыдущего, так что
// настоящего худшего случая мы, скорее всего, ещё не видели.
//
// 12288 — 1.4× над измеренным максимумом. Запас скромный СОЗНАТЕЛЬНО: отказ по исчерпанию бюджета
// теперь не тихий, ниже стоит строка в лог. Пока она у тестеров не появляется, число можно
// опускать; появится — поднимем по факту, а не по запасу «на всякий случай». 65500 возвращает
// прежнее поведение точь-в-точь.
int ps_da_path_max_nodes = 12288;

// [DA_PORT] Как часто пересчитывать видимость МЁРТВЫХ объектов, в миллисекундах.
//
// После боя фаза «память» съедала 83% обновления сталкеров: каждый NPC гонял проверку линии
// взгляда до каждого трупа каждый цикл. Тело не двигается, поэтому секунды достаточно. Разбор —
// в CVisualMemoryManager::add_visible_object. 0 возвращает прежнее поведение.
int ps_da_dead_vision_ms = 1000;

// [DA_PORT] Сколько кадров подряд разбирать ПАМЯТЬ NPC по частям. См. memory_manager.cpp.
int ps_da_memory_dump = 0;

void register_mp_console_commands();
//-----------------------------------------------------------

BOOL g_bCheckTime = FALSE;
int net_cl_inputupdaterate = 50;
// [DA_PORT] mtALife убран из умолчаний: обновление ALife НЕЛЬЗЯ выносить в рабочий поток.
//
// С этим флагом CALifeUpdateManager::update уходит в seqParallel, то есть исполняется на рабочем
// потоке TaskManager. А внутри оно доходит до CSE_ALifeDynamicObject::try_switch_online(), которое
// зовёт can_switch_online() — виртуальный метод, и у скриптовых серверных классов (а их в Dead Air
// много) он уходит В LUA через luabind.
//
// lua_State однопоточен по построению. Главный поток в этот же момент крутит биндеры через
// CSheduler::Update, и два потока работают с одним состоянием интерпретатора. Стек Lua портится, а
// падает потом где угодно и без всякой связи с виновником — в логах это выглядело тремя разными
// авариями подряд:
//   lua_rawgeti по адресу -1  (luabind::weak_ref::get из рабочего потока)
//   memcpy внутри lua_remove  (luabind::pcall из биндера актёра)
//   lj_err_optype / lj_meta_tget (тот же биндер, другой кадр)
// Ни одной ошибки скрипта при этом в логе нет: это порча памяти, а не ошибка в Lua-коде.
//
// Ручка mt_alife остаётся — выключенной. Кому нужен старый режим, включит сам и получит те же
// падения.
//
// mtLUA_GC убран по той же причине и он даже опаснее: с ним CLevel::script_gc уходит в тот же
// рабочий поток, а это сборка мусора Lua. Она не просто читает состояние интерпретатора, а
// переставляет объекты в нём — параллельно с главным потоком, который по этим объектам ходит.
Flags32 g_mt_config = {mtLevelPath | mtDetailPath | mtObjectHandler | mtSoundPlayer | mtAiVision | mtBullets |
    mtLevelSounds | mtMap};
#ifdef DEBUG
Flags32 dbg_net_Draw_Flags{};
#endif

#ifdef DEBUG
BOOL g_bDebugNode = FALSE;
u32 g_dwDebugNodeSource = 0;
u32 g_dwDebugNodeDest = 0;
extern BOOL g_bDrawBulletHit;
extern BOOL g_bDrawFirstBulletCrosshair;

float debug_on_frame_gather_stats_frequency = 0.f;
#endif
#ifdef DEBUG
extern pstr dbg_stalker_death_anim;
extern BOOL b_death_anim_velocity;
extern BOOL death_anim_debug;
extern BOOL dbg_imotion_draw_skeleton;
extern BOOL dbg_imotion_draw_velocity;
extern BOOL dbg_imotion_collide_debug;
extern float dbg_imotion_draw_velocity_scale;
#endif
int g_AI_inactive_time = 0;
Flags32 g_uCommonFlags;
enum E_COMMON_FLAGS
{
    flAiUseTorchDynamicLights = 1
};

const xr_token lua_gc_method_token[] =
{
    { "gc_disable", 0 },
    { "gc_step", 1 },
    { "gc_timeout", 2 },
    { "gc_full", 3 },
    { "gc_adaptive", 4 }, // [DA_PORT] шаг пропорционален выделенному
    { nullptr, -1 }
};

CUIOptConCom g_OptConCom;


static void full_memory_stats()
{
    Memory.mem_compact();
    u32 m_base = 0, c_base = 0, m_lmaps = 0, c_lmaps = 0;
    GEnv.Render->ResourcesGetMemoryUsage(m_base, c_base, m_lmaps, c_lmaps);
    log_vminfo();
    size_t _process_heap = ::Memory.mem_usage();
    const auto [_eco_strings_bytes, _eco_strings_count] = g_pStringContainer->stat_economy();
    int _eco_smem = (int)g_pSharedMemoryContainer->stat_economy();
    Msg("* [ render ]: textures[%d K]", (m_base + m_lmaps) / 1024);
    Msg("* [ x-ray  ]: process heap[%u K]", _process_heap / 1024);
    Msg("* [ x-ray  ]: shared strings: memory[%ld K], count[%lu]", _eco_strings_bytes / 1024, _eco_strings_count);
    Msg("* [ x-ray  ]: shared memory[%ld K]", _eco_smem);
#ifdef FS_DEBUG
    Msg("* [ x-ray  ]: file mapping: memory[%d K], count[%d]", g_file_mapped_memory / 1024, g_file_mapped_count);
    dump_file_mappings();
#endif
}

// [DA_PORT] Dump the UI xml files the game actually loads, straight through the engine's VFS, into
// appdata\logs\vfs_ui\. Offline unpacking of Dead Air's archives has proven unreliable for these —
// the copies it produced were a different, older revision than what the VFS serves (they were missing
// nodes the shipped scripts require), and editing a menu on top of that wrong base breaks the options
// screen. This is the same trick already used to obtain the trustworthy script dump.
class CCC_DumpUIXml : public IConsole_Command
{
public:
    CCC_DumpUIXml(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR /*args*/)
    {
        FS_FileSet files;
        FS.file_list(files, "$game_config$", FS_ListFiles, "ui" DELIMITER "*.xml");

        u32 exported = 0;
        u32 unreadable = 0;
        for (const FS_File& f : files)
        {
            // The listing already carries the "ui\" part of the mask, so f.name is used as-is here;
            // prefixing it a second time is what made the first run report 0 of 165 exported.
            string_path src;
            FS.update_path(src, "$game_config$", f.name.c_str());

            IReader* r = FS.r_open(src);
            if (!r)
            {
                ++unreadable;
                if (unreadable <= 3)
                    Msg("~ [DA_PORT] ui dump: cannot open [%s]", src);
                continue;
            }

            // Flatten into one folder: names come through as "ui\foo.xml", and w_open will not create
            // nested directories for us.
            pcstr base = f.name.c_str();
            if (pcstr slash = strrchr(base, DELIMITER[0]))
                base = slash + 1;

            string_path dst;
            xr_sprintf(dst, "vfs_ui" DELIMITER "%s", base);
            if (IWriter* w = FS.w_open("$logs$", dst))
            {
                w->w(r->pointer(), r->length());
                FS.w_close(w);
                ++exported;
            }
            else if (exported == 0)
                Msg("~ [DA_PORT] ui dump: cannot write [%s]", dst);

            FS.r_close(r);
        }
        Msg("~ [DA_PORT] dumped %u/%u ui xml files to appdata" DELIMITER "logs" DELIMITER "vfs_ui" DELIMITER,
            exported, (u32)files.size());
        FlushLog();
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dump ui xml files served by the VFS into logs\\vfs_ui"); }
};

// [DA_PORT] Same trick for shaders, needed to edit the G-buffer output structure for motion vectors.
// Offline carving is not an option here: the archive's own index decompresses correctly for only part
// of its entries (the tail comes out as garbage), the shader sources are not among the entries that do
// survive, and the archive holds more than one copy of the r3 sources. Asking the VFS is exact by
// construction — it hands back the very bytes the renderer compiles, loose overrides included.
class CCC_DumpShaders : public IConsole_Command
{
public:
    CCC_DumpShaders(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args)
    {
        // Which renderer's folder to dump; defaults to the one in use.
        string64 sub;
        if (args && xr_strlen(args))
            xr_sprintf(sub, "%s", args);
        else
            xr_strcpy(sub, "r3");

        string_path mask;
        xr_sprintf(mask, "%s" DELIMITER "*.*", sub);

        FS_FileSet files;
        FS.file_list(files, "$game_shaders$", FS_ListFiles, mask);

        u32 exported = 0, unreadable = 0;
        for (const FS_File& f : files)
        {
            string_path src;
            FS.update_path(src, "$game_shaders$", f.name.c_str());

            IReader* r = FS.r_open(src);
            if (!r)
            {
                ++unreadable;
                if (unreadable <= 3)
                    Msg("~ [DA_PORT] shader dump: cannot open [%s]", src);
                continue;
            }

            // Flattened like the ui dump: w_open will not create nested directories.
            pcstr base = f.name.c_str();
            if (pcstr slash = strrchr(base, DELIMITER[0]))
                base = slash + 1;

            string_path dst;
            xr_sprintf(dst, "vfs_shaders" DELIMITER "%s", base);
            if (IWriter* w = FS.w_open("$logs$", dst))
            {
                w->w(r->pointer(), r->length());
                FS.w_close(w);
                ++exported;
            }
            else if (exported == 0)
                Msg("~ [DA_PORT] shader dump: cannot write [%s]", dst);

            FS.r_close(r);
        }
        Msg("~ [DA_PORT] dumped %u/%u shader files from [%s] to appdata" DELIMITER "logs" DELIMITER
            "vfs_shaders" DELIMITER, exported, (u32)files.size(), sub);
        FlushLog();
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dump shader sources served by the VFS into logs\\vfs_shaders [r1|r2|r3]"); }
};

class CCC_MemStats : public IConsole_Command
{
public:
    CCC_MemStats(LPCSTR N) : IConsole_Command(N)
    {
        bEmptyArgsHandled = TRUE;
        xrDebug::SetOutOfMemoryCallback(full_memory_stats);
    };
    virtual void Execute(LPCSTR args) { full_memory_stats(); }
};

// [DA_PORT] Поиск утечки памяти. Подробности и протокол — в da_memory_probe.h.
class CCC_DaMemDump : public IConsole_Command
{
public:
    CCC_DaMemDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { DA_MemDump(); }
};

// [DA_PORT] Поимённая цена спавна: какие секции конфига стоят дороже всего. Копится всегда,
// печатается и обнуляется по команде — так один прогон закрывает и загрузку, и переходы.
void da_spawn_dump_print();
int ps_da_registry_log = 0; // [DA_PORT] см. xrServer::entity_Destroy
// [DA_PORT] Имя объекта, который СЕЙЧАС создаётся из спавн-пакета — печатается до того,
// как за него возьмётся net_Spawn. Нужно, когда падение приходит внутри спавна: стек там
// бесполезен (в быстрой сборке нет карты линковщика, и символ берётся ближайший, то есть
// чужой), а имя секции называет виновника сразу. Печатать всё подряд дорого — по флагу.
int ps_da_spawn_trace = 0; // [DA_PORT] см. CLevel::g_sv_Spawn

class CCC_DaSpawnDump : public IConsole_Command
{
public:
    CCC_DaSpawnDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { da_spawn_dump_print(); }
};

// [DA_PORT] Цена обновления ALife по секциям — печать ПО ТРЕБОВАНИЮ.
//
// Копится, пока включён da_seq_trap (замер вооружается им же), а печаталась статистика только
// в деструкторе CALifeUpdateManager, то есть при выгрузке уровня. Для вопроса «что грузит игру
// первые секунды ПОСЛЕ загрузки сейва» это бесполезно: чтобы увидеть числа, надо выйти, а к
// тому моменту в сумме уже весь сеанс. Отсюда команда: загрузился, подождал, напечатал.
void da_alife_dump_update_stats();

// [DA_PORT] Санитар реестра ALife: отчёт + немедленная чистка (см. alife_object_registry.cpp).
class CCC_DaSanitar : public IConsole_Command
{
public:
    CCC_DaSanitar(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/)
    {
        if (!ai().get_alife())
        {
            Msg("~ [DA_SANITAR] ALife не запущен — чистить нечего");
            return;
        }
        const_cast<CALifeSimulator*>(ai().get_alife())->da_sanitize_now();
    }
};

class CCC_DaAlifeDump : public IConsole_Command
{
public:
    CCC_DaAlifeDump(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { da_alife_dump_update_stats(); }
};

class CCC_DaMemReset : public IConsole_Command
{
public:
    CCC_DaMemReset(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { DA_MemReset(); }
};

// [DA_PORT] Крутилки, которые НЕ сохраняются в user.ltx и живут ровно один запуск, регистрируются
// через CCC_DaDebugInteger / CCC_DaDebugFloat (xr_ioc_cmd.h) — там же и рассказано, почему
// сохранённая диагностика дважды роняла игру и один раз уехала к игрокам.

// [DA_PORT] Снимок посреди игры: замер для ПОКАДРОВЫХ утечек, которые протокол загрузок не видит.
class CCC_DaMemSnap : public IConsole_Command
{
public:
    CCC_DaMemSnap(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { DA_MemSnap(); }
};

// [DA_PORT] Счётчик выделений.
//   da_alloc_stat        — напечатать накопленное с начала процесса (или с последнего сброса)
//   da_alloc_stat reset  — напечатать и начать окно заново
//   da_alloc_stat <N>       — сбросить молча и отчитаться через N кадров ← рабочий режим
//   da_alloc_stat <N> bench — то же и следом замерить цену операции (кадр встанет на пару секунд)
//   da_alloc_stat <N> quit  — и выйти сразу после отчёта: на стенде это экономит целый таймаут
//
// ⚠️ Мерить надо ТРЕТЬИМ. Первые два дают числа за окно, в которое попала загрузка уровня, а она
// перекашивает всё: на старте движка получалось 831 тысяча выделений в секунду, и к игре это
// отношения не имело. `da_alloc_stat 600` открывает окно с текущего кадра — числа получаются про
// игру.
// [DA_PORT] Распределение обойдённых узлов в поиске пути по уровню.
//
// Ради одного числа: max_visited_node_count у пути по уровню стоит 65500, и безнадёжный поиск
// честно обходит десятки тысяч вершин, прежде чем сдаться. Опускать этот потолок наугад нельзя —
// срежем настоящие дальние маршруты. Смотрим, сколько узлов на самом деле нужно УДАЧНОМУ поиску,
// и ставим потолок выше его хвоста.
//
//   da_path_stat        — напечатать накопленное
//   da_path_stat reset  — напечатать и обнулить
class CCC_DaPathStat : public IConsole_Command
{
public:
    CCC_DaPathStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        static const char* const names[DA_LP_BUCKETS] = {
            "0..63", "64..255", "256..1К", "1К..4К", "4К..16К", "16К..64К", "64К+"
        };

        u32 total_ok = 0, total_fail = 0;
        for (u32 i = 0; i < DA_LP_BUCKETS; ++i)
        {
            total_ok += g_da_lp_nodes_ok[i];
            total_fail += g_da_lp_nodes_fail[i];
        }

        Msg("~ [DA] поиск пути по уровню: удачных %u, неудачных %u", total_ok, total_fail);
        if (!total_ok && !total_fail)
        {
            Msg("~ [DA]   поисков не было — походите, пока NPC живут рядом");
            return;
        }

        Msg("~ [DA]   %-10s %10s %10s", "узлов", "удачно", "неудачно");
        u32 run_ok = 0;
        for (u32 i = 0; i < DA_LP_BUCKETS; ++i)
        {
            run_ok += g_da_lp_nodes_ok[i];
            const float pct = total_ok ? 100.f * float(run_ok) / float(total_ok) : 0.f;
            Msg("~ [DA]   %-10s %10u %10u   (удачных накопленным итогом %.1f%%)", names[i],
                g_da_lp_nodes_ok[i], g_da_lp_nodes_fail[i], pct);
        }

        Msg("~ [DA]   потолок удачного %u, среднее %u", g_da_lp_max_ok,
            total_ok ? u32(g_da_lp_sum_ok / total_ok) : 0u);
        Msg("~ [DA]   потолок неудачного %u, среднее %u", g_da_lp_max_fail,
            total_fail ? u32(g_da_lp_sum_fail / total_fail) : 0u);
        extern int ps_da_path_max_nodes;
        Msg("~ [DA]   потолок сейчас: da_path_max_nodes %d (штатный 65500)", ps_da_path_max_nodes);

        if (args && xr_strlen(args) && strstr(args, "reset"))
        {
            ZeroMemory(g_da_lp_nodes_ok, sizeof(g_da_lp_nodes_ok));
            ZeroMemory(g_da_lp_nodes_fail, sizeof(g_da_lp_nodes_fail));
            g_da_lp_max_ok = g_da_lp_max_fail = 0;
            g_da_lp_sum_ok = g_da_lp_sum_fail = 0;
            Msg("~ [DA]   счётчики обнулены");
        }
    }
};

class CCC_DaAllocStat : public IConsole_Command
{
public:
    CCC_DaAllocStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR args)
    {
        if (args && xr_strlen(args))
        {
            if (strstr(args, "reset"))
            {
                da_alloc_stat_dump(true);
                return;
            }
            const int frames = atoi(args);
            if (frames > 0)
            {
                DA_AllocStatWindow(frames, strstr(args, "bench") != nullptr, strstr(args, "quit") != nullptr);
                return;
            }
        }
        da_alloc_stat_dump(false);
    }
};

// [DA_PORT] Состояние карантина освобождённой памяти. Разбор — в xrCore/da_heap_guard.h.
//
// ⚠️ Команда только ПОКАЗЫВАЕТ. Включается карантин переменной окружения ДО запуска игры
// (DA_HEAP_GUARD=1), и иначе быть не может: консоль появляется многократно позже первых выделений.
// Готовый запуск — da_port/tools/heap_guard/run_heap_guard.cmd.
class CCC_DaHeapGuardStat : public IConsole_Command
{
public:
    CCC_DaHeapGuardStat(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { da_heap_guard_stat(); }
};

// [DA_PORT] Самопроверка карантина В ЖИВОЙ ИГРЕ: НАМЕРЕННОЕ обращение к освобождённой памяти.
//
// ЗАЧЕМ, если есть сценарий в прогонном стенде. Стенд доказывает, что работает БИБЛИОТЕКА. А здесь
// проверяется вся цепочка целиком: карантин отравил → игра разыменовала → обработчик поймал →
// в лог легли адрес и стек, по которым дефект можно найти. Порваться она может в любом звене, и
// молчание любого из них выглядит одинаково — как «ошибок нет».
//
// ⛔ Игра после этой команды УПАДЁТ. Это и есть ожидаемый исход, ровно как у da_crash_test.
// В логе обязан появиться адрес вида 0xDDDDDDDDDDDDDDDD — по нему и опознаётся, что упало именно
// обращение к мертвецу, а не разыменование нуля.
class CCC_DaHeapGuardTest : public IConsole_Command
{
public:
    CCC_DaHeapGuardTest(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/)
    {
        if (!da_heap_guard_enabled())
        {
            Msg("! [DA_HEAP_GUARD] карантин выключен — проверять нечего. Запускать надо через "
                "ЗАПУСК_контроль_кучи.cmd (DA_HEAP_GUARD=1).");
            return;
        }

        // Блок с указателем внутри — так выглядит любой объект движка: первым полем таблица
        // виртуальных функций или ссылка на владельца.
        void** victim = (void**)Memory.mem_alloc(256);
        victim[0] = victim; // осмысленный указатель, пока объект жив

        Memory.mem_free(victim); // отравление + карантин

        Msg("~ [DA_HEAP_GUARD] самопроверка: читаю указатель из ОСВОБОЖДЁННОГО блока %p", (void*)victim);
        Msg("~ [DA_HEAP_GUARD] ожидается падение по адресу 00007ddddddddddd");
        FlushLog();

        // volatile, иначе оптимизатор вправе выбросить и чтение, и запись целиком.
        void* volatile stale = victim[0];    // = 0xDDDDDDDDDDDDDDDD
        *(volatile int*)stale = 1;           // <- падение здесь

        Msg("! [DA_HEAP_GUARD] ПРОВАЛ: падения не случилось, карантин не отравил блок");
    }
};

// [DA_PORT] Цена операции аллокатора. Зовётся ПОСЛЕ da_alloc_stat <N>: тогда наблюдённая частота
// уже есть, и отчёт пересчитает её в миллисекунды на секунду игры.
// ⚠️ Кадр на время замера встанет — это несколько секунд синтетической нагрузки.
class CCC_DaAllocBench : public IConsole_Command
{
public:
    CCC_DaAllocBench(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR /*args*/) { da_alloc_bench(0); }
};

// [DA_PORT] Разбор мусора Lua по размерам блоков. Обычно печатается сам вместе с da_alloc_stat <N>;
// отдельная команда нужна, когда окно уже открыто и хочется взглянуть посреди него.
//   da_lua_mem        напечатать
//   da_lua_mem reset  начать счёт заново
class CCC_DaLuaMem : public IConsole_Command
{
public:
    CCC_DaLuaMem(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR args)
    {
        if (args && xr_strlen(args) && strstr(args, "reset"))
        {
            da_lua_alloc_reset();
            Msg("~ [DA_LUAMEM] счёт начат заново");
            return;
        }
        da_lua_alloc_dump(0);
    }
};

// [DA_PORT] Весь протокол одной командой: перезагружает последнее сохранение подряд нужное число
// раз и печатает вывод. Руками это делается ровно так же, но легко сбиться — а перезапуск игры
// обнуляет накопленное, потому что таблица живёт в памяти процесса.
class CCC_DaMemTest : public IConsole_Command
{
public:
    CCC_DaMemTest(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
    virtual void Execute(LPCSTR args)
    {
        const int runs = (args && xr_strlen(args)) ? atoi(args) : 3;
        DA_MemTestStart(runs);
    }
};

class CCC_GameDifficulty : public CCC_Token
{
public:
    CCC_GameDifficulty(LPCSTR N) : CCC_Token(N, (u32*)&g_SingleGameDifficulty, difficulty_type_token){};
    virtual void Execute(LPCSTR args)
    {
        CCC_Token::Execute(args);
        if (g_pGameLevel && Level().game)
        {
            //#ifndef	DEBUG
            if (GameID() != eGameIDSingle)
            {
                Msg("For this game type difficulty level is disabled.");
                return;
            };
            //#endif

            game_cl_Single* game = smart_cast<game_cl_Single*>(Level().game);
            VERIFY(game);
            game->OnDifficultyChanged();
        }
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "game difficulty"); }
};

class CCC_GameLanguage : public CCC_Token
{
public:
    CCC_GameLanguage(pcstr N) : CCC_Token(N, (u32*)&CStringTable::LanguageID, nullptr) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        StringTable().ReloadLanguage();

        Device.seqUIReset.Process();

        if (!g_pGameLevel)
            return;

        for (u16 id = 0; id < 0xffff; id++)
        {
            IGameObject* gameObj = Level().Objects.net_Find(id);
            if (gameObj)
            {
                if (CInventoryItem* invItem = gameObj->cast_inventory_item())
                    invItem->ReloadNames();
            }
        }
    }

    const xr_token* GetToken() noexcept override
    {
        tokens = StringTable().GetLanguagesToken();
        if(!tokens) // Prevent failure without usage Nifty counters
        {
            Msg("GetToken: token missing");
            StringTable().Destroy();
            StringTable().Init();

            tokens = StringTable().GetLanguagesToken();
        }
        return CCC_Token::GetToken();
    }
};

#ifdef DEBUG
class CCC_ALifePath : public IConsole_Command
{
public:
    CCC_ALifePath(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if (!ai().get_level_graph())
            Msg("! there is no graph!");
        else
        {
            int id1 = -1, id2 = -1;
            sscanf(args, "%d %d", &id1, &id2);
            if ((-1 != id1) && (-1 != id2))
                if (_max(id1, id2) > (int)ai().game_graph().header().vertex_count() - 1)
                    Msg("! there are only %d vertexes!", ai().game_graph().header().vertex_count());
                else if (_min(id1, id2) < 0)
                    Msg("! invalid vertex number (%d)!", _min(id1, id2));
                else
                {
                    //						Sleep				(1);
                    //						CTimer				timer;
                    //						timer.Start			();
                    //						float				fValue = ai().m_tpAStar->ffFindMinimalPath(id1,id2);
                    //						Msg					("* %7.2f[%d] : %11I64u cycles (%.3f
                    // microseconds)",fValue,ai().m_tpAStar->m_tpaNodes.size(),timer.GetElapsed_ticks(),timer.GetElapsed_ms()*1000.f);
                }
            else
                Msg("! not enough parameters!");
        }
    }
};
#endif // DEBUG

class CCC_ALifeTimeFactor : public IConsole_Command
{
public:
    CCC_ALifeTimeFactor(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        float id1 = 0.0f;
        sscanf(args, "%f", &id1);
        if (id1 < EPS_L)
            Msg("Invalid time factor! (%.4f)", id1);
        else
        {
            if (!OnServer())
                return;

            Level().SetGameTimeFactor(id1);
        }
    }

    virtual void Save(IWriter* F){};
    void GetStatus(TStatus& S) override
    {
        if (!g_pGameLevel)
            return;

        float v = Level().GetGameTimeFactor();
        xr_sprintf(S, sizeof(S), "%3.5f", v);
        while (xr_strlen(S) && ('0' == S[xr_strlen(S) - 1]))
            S[xr_strlen(S) - 1] = 0;
    }
    virtual void Info(TInfo& I)
    {
        if (!OnServer())
            return;
        float v = Level().GetGameTimeFactor();
        xr_sprintf(I, sizeof(I), " value = %3.5f", v);
    }
    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        if (!OnServer())
            return;
        float v = Level().GetGameTimeFactor();

        TStatus str;
        xr_sprintf(str, sizeof(str), "%3.5f  (current)  [0.0,1000.0]", v);
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

class CCC_ALifeSwitchDistance : public IConsole_Command
{
public:
    CCC_ALifeSwitchDistance(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if ((GameID() == eGameIDSingle) && ai().get_alife())
        {
            float id1 = 0.0f;
            sscanf(args, "%f", &id1);
            if (id1 < 2.0f)
                Msg("Invalid online distance! (%.4f)", id1);
            else
            {
                NET_Packet P;
                P.w_begin(M_SWITCH_DISTANCE);
                P.w_float(id1);
                Level().Send(P, net_flags(TRUE, TRUE));
            }
        }
        else
            Log("!Not a single player game!");
    }
};

class CCC_ALifeProcessTime : public IConsole_Command
{
public:
    CCC_ALifeProcessTime(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if ((GameID() == eGameIDSingle) && ai().get_alife())
        {
            game_sv_Single* tpGame = smart_cast<game_sv_Single*>(Level().Server->GetGameState());
            VERIFY(tpGame);
            int id1 = 0;
            sscanf(args, "%d", &id1);
            if (id1 < 1)
                Msg("Invalid process time! (%d)", id1);
            else
                tpGame->alife().set_process_time(id1);
        }
        else
            Log("!Not a single player game!");
    }
};

class CCC_ALifeObjectsPerUpdate : public IConsole_Command
{
public:
    CCC_ALifeObjectsPerUpdate(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if ((GameID() == eGameIDSingle) && ai().get_alife())
        {
            game_sv_Single* tpGame = smart_cast<game_sv_Single*>(Level().Server->GetGameState());
            VERIFY(tpGame);
            int id1 = 0;
            sscanf(args, "%d", &id1);
            tpGame->alife().objects_per_update(id1);
        }
        else
            Log("!Not a single player game!");
    }
};

class CCC_ALifeSwitchFactor : public IConsole_Command
{
public:
    CCC_ALifeSwitchFactor(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if ((GameID() == eGameIDSingle) && ai().get_alife())
        {
            game_sv_Single* tpGame = smart_cast<game_sv_Single*>(Level().Server->GetGameState());
            VERIFY(tpGame);
            float id1 = 0;
            sscanf(args, "%f", &id1);
            clamp(id1, .1f, 1.f);
            tpGame->alife().set_switch_factor(id1);
        }
        else
            Log("!Not a single player game!");
    }
};

//-----------------------------------------------------------------------
class CCC_DemoRecord : public IConsole_Command
{
public:
    CCC_DemoRecord(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR args)
    {
        if (!g_pGameLevel) // level not loaded
        {
            Log("Demo Record is disabled when level is not loaded.");
            return;
        }

        Console->Hide();

        // close main menu if it is open
        if (MainMenu()->IsActive())
            MainMenu()->Activate(false);

        pstr fn_;
        STRCONCAT(fn_, args, ".xrdemo");
        string_path fn;
        FS.update_path(fn, "$game_saves$", fn_);

        g_pGameLevel->Cameras().AddCamEffector(xr_new<CDemoRecord>(fn));
    }
};

class CCC_DemoRecordSetPos : public CCC_Vector3
{
    static Fvector p;

public:
    CCC_DemoRecordSetPos(LPCSTR N)
        : CCC_Vector3(N, &p, Fvector().set(-FLT_MAX, -FLT_MAX, -FLT_MAX), Fvector().set(FLT_MAX, FLT_MAX, FLT_MAX)){};
    virtual void Execute(LPCSTR args)
    {
#ifndef DEBUG
// if (GameID() != eGameIDSingle)
//{
//	Msg("For this game type Demo Record is disabled.");
//	return;
//};
#endif
        CDemoRecord::GetGlobalPosition(p);
        CCC_Vector3::Execute(args);
        CDemoRecord::SetGlobalPosition(p);
    }
    virtual void Save(IWriter* F) { ; }
};
Fvector CCC_DemoRecordSetPos::p = {0, 0, 0};

class CCC_DemoPlay : public IConsole_Command
{
public:
    CCC_DemoPlay(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args)
    {
#ifndef DEBUG
// if (GameID() != eGameIDSingle)
//{
//	Msg("For this game type Demo Play is disabled.");
//	return;
//};
#endif
        if (0 == g_pGameLevel)
        {
            Msg("! There are no level(s) started");
        }
        else
        {
            Console->Hide();
            string_path fn;
            u32 loops = 0;
            pstr comma = strchr(const_cast<pstr>(args), ',');
            if (comma)
            {
                loops = atoi(comma + 1);
                *comma = 0; //. :)
            }
            strconcat(sizeof(fn), fn, args, ".xrdemo");
            FS.update_path(fn, "$game_saves$", fn);
            g_pGameLevel->Cameras().AddCamEffector(xr_new<CDemoPlay>(fn, 1.0f, loops));
        }
    }
};

class CCC_Spawn : public IConsole_Command
{
public:
    CCC_Spawn(pcstr name) : IConsole_Command(name) {}

    void Execute(pcstr args) override
    {
        if (!g_pGameLevel)
            return;

        if (!IsGameTypeSingle())
        {
            Log("Spawn command is available only in singleplayer mode.");
            return;
        }

        if (!pSettings->section_exist(args))
        {
            Msg("! Section [%s] doesn't exist...", args);
            return;
        }

        // [DA_PORT] Was Level().g_cl_Spawn(args, 0xff, M_SPAWN_OBJECT_LOCAL, pos), a purely client-side
        // spawn that never touches the ALife vertices. Anything created that way keeps m_tGraphID at
        // its initial 0xFFFF - the "no vertex" sentinel - and once such an object lands in a save, the
        // next load feeds 65535 into CALifeGraphRegistry::add, which indexes a VECTOR with it. That is
        // an out-of-bounds read, and the game died inside libstdc++ with no message; see the bounds
        // guard in alife_graph_registry.cpp. Found by spawning a boar to test mutant field dressing,
        // quicksaving, and reloading.
        //
        // spawn_item is the path that does it properly: given a valid level vertex it looks the game
        // vertex up through the cross table, so the object is registered like any other. Handing it the
        // actor's own vertex - not 0, which is a real but arbitrary vertex somewhere else on the level -
        // puts the spawn where the player is standing. Parent 0xFFFF means "no parent", i.e. into the
        // world rather than into an inventory, which is what this command has always done.
        const Fvector pos = Actor()->Position();
        Level().spawn_item(args, pos, Actor()->ai_location().level_vertex_id(), 0xffff);
    }

    void Info(TInfo& I) override
    {
        xr_strcpy(I, "valid name of an entity or item that can be spawned");
    }
};

class CCC_SpawnToInventory : public IConsole_Command
{
public:
    CCC_SpawnToInventory(pcstr name) : IConsole_Command(name) {}

    void Execute(pcstr args) override
    {
        if (!g_pGameLevel)
            return;

        if (!IsGameTypeSingle())
        {
            Log("Spawn command is available only in singleplayer mode.");
            return;
        }

        if (!pSettings->section_exist(args))
        {
            Msg("! Section [%s] doesn't exist...", args);
            return;
        }

        Level().spawn_item(args, Actor()->Position(), false, Actor()->ID());
    }

    void Info(TInfo& I) override
    {
        xr_strcpy(I, "valid name of an item that can be spawned");
    }
};
// helper functions --------------------------------------------

bool valid_saved_game_name(LPCSTR file_name)
{
    LPCSTR I = file_name;
    LPCSTR E = file_name + xr_strlen(file_name);
    for (; I != E; ++I)
    {
        if (!strchr("/" DELIMITER ":*?\"<>|^()[]%", *I))
            continue;

        return (false);
    };

    return (true);
}

void get_files_list(xr_vector<shared_str>& files, LPCSTR dir, LPCSTR file_ext)
{
    VERIFY(dir && file_ext);
    files.clear();

    FS_Path* P = FS.get_path(dir);
    P->m_Flags.set(FS_Path::flNeedRescan, TRUE);
    FS.m_Flags.set(CLocatorAPI::flNeedCheck, TRUE);
    FS.rescan_pathes();

    LPCSTR fext;
    STRCONCAT(fext, "*", file_ext);

    FS_FileSet files_set;
    FS.file_list(files_set, dir, FS_ListFiles, fext);
    u32 len_str_ext = xr_strlen(file_ext);

    auto itb = files_set.begin();
    auto ite = files_set.end();

    for (; itb != ite; ++itb)
    {
        LPCSTR fn_ext = (*itb).name.c_str();
        VERIFY(xr_strlen(fn_ext) > len_str_ext);
        string_path fn;
        strncpy_s(fn, sizeof(fn), fn_ext, xr_strlen(fn_ext) - len_str_ext);
        files.push_back(fn);
    }
    FS.m_Flags.set(CLocatorAPI::flNeedCheck, FALSE);
}

#include "UIGameCustom.h"

// [DA_PORT] Walk the live in-game HUD window tree and report what is actually on screen: every widget's
// name, its absolute rectangle and whether it is shown. Layout faults look identical from the outside -
// a widget that was never created, one placed off screen, one covered by another and one simply hidden
// all render as "nothing there" - and the xml alone cannot tell them apart. This prints the answer.
class CCC_DumpHud : public IConsole_Command
{
    static void dump(CUIWindow* w, int depth)
    {
        if (!w)
            return;

        Frect r;
        w->GetAbsoluteRect(r);

        string256 pad{};
        const int indent = _min(depth * 2, 40);
        for (int i = 0; i < indent; ++i)
            pad[i] = ' ';
        pad[indent] = 0;

        pcstr name = w->WindowName().c_str();
        Msg("~ [DA_PORT] %s%s [%s] shown=%d abs=(%.0f,%.0f)-(%.0f,%.0f) size=%.0fx%.0f", pad, name ? name : "<noname>",
            w->GetDebugType(), w->IsShown() ? 1 : 0, r.x1, r.y1, r.x2, r.y2, w->GetWidth(), w->GetHeight());

        for (auto* child : w->GetChildWndList())
            dump(child, depth + 1);
    }

public:
    CCC_DumpHud(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        Msg("~ [DA_PORT] hud flags: draw=%d draw_info=%d draw_map=%d info=%d", psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
            psHUD_Flags.test(HUD_DRAW_INFO) ? 1 : 0, psHUD_Flags.test(HUD_DRAW_MAP) ? 1 : 0,
            psHUD_Flags.test(HUD_INFO) ? 1 : 0);

        CUIGameCustom* ui = CurrentGameUI();
        if (!ui || !ui->UIMainIngameWnd)
        {
            Msg("! [DA_PORT] no in-game HUD right now - run this while in the game world");
            return;
        }

        Msg("~ [DA_PORT] --- HUD tree ---");
        dump(ui->UIMainIngameWnd, 0);
        Msg("~ [DA_PORT] --- end of HUD tree ---");
    }
};

// [DA_PORT] Report what every belt item actually gives the actor.
//
// Dead Air carries artefacts inside containers, so the belt item is a combined section
// (af_medusa_af_iam) that inherits the container, NOT the artefact - the real values are pushed in at
// runtime by bind_artefact.script from the server object. That is a long chain (config -> se_artefact ->
// binder -> engine field -> UpdateArtefactsOnBeltAndOutfit), and when a boost "does nothing" the only
// useful question is which link came out zero. This prints the engine-side numbers as they stand now.
#include "Artefact.h"
#include "Inventory.h"
#include "ActorCondition.h"
class CCC_DumpBelt : public IConsole_Command
{
public:
    CCC_DumpBelt(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        if (!g_pGameLevel) // typed from the main menu - Level() would not be there to ask
        {
            Msg("! [DA_PORT] no level loaded - run this in the game world");
            return;
        }

        CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
        if (!actor)
        {
            Msg("! [DA_PORT] no actor - run this in the game world");
            return;
        }

        Msg("~ [DA_PORT] --- belt contents (%u item(s)) ---", (u32)actor->inventory().m_belt.size());

        for (const auto& it : actor->inventory().m_belt)
        {
            pcstr sect = it->object().cNameSect().c_str();
            const auto artefact = smart_cast<CArtefact*>(it);
            if (!artefact)
            {
                Msg("~ [DA_PORT]   %s : NOT a CArtefact - contributes nothing", sect);
                continue;
            }

            Msg("~ [DA_PORT]   %s : cond=%.3f power=%.5f health=%.5f satiety=%.5f bleed=%.5f rad=%.5f addw=%.2f", sect,
                artefact->GetCondition(), artefact->m_fPowerRestoreSpeed, artefact->m_fHealthRestoreSpeed,
                artefact->m_fSatietyRestoreSpeed, artefact->m_fBleedingRestoreSpeed,
                artefact->m_fRadiationRestoreSpeed, artefact->AdditionalInventoryWeight());
        }

        Msg("~ [DA_PORT]   summed power restore = %.5f/s (the artefact tick applies it at double rate)",
            actor->GetRestoreSpeed(ALife::ePowerRestoreSpeed));

        // The load decides the sprint cost, so print it alongside the gains - the two numbers are only
        // meaningful next to each other.
        Msg("~ [DA_PORT]   load = %.2f kg, carry limit = %.2f, walk limit = %.2f, power now = %.3f",
            actor->inventory().TotalWeight(), actor->MaxCarryWeight(), actor->MaxWalkWeight(),
            actor->conditions().GetPower());
        Msg("~ [DA_PORT] --- end of belt ---");
    }
};

class CCC_ALifeSave : public IConsole_Command
{
public:
    CCC_ALifeSave(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
#if 0
        if (!Level().autosave_manager().ready_for_autosave()) {
            Msg		("! Cannot save the game right now!");
            return;
        }
#endif
        if (!IsGameTypeSingle())
        {
            Msg("for single-mode only");
            return;
        }
        if (!g_actor || !Actor()->g_Alive())
        {
            Msg("cannot make saved game because actor is dead :(");
            return;
        }

        Console->Execute("stat_memory");

        string_path S, S1;
        S[0] = 0;
        strncpy_s(S, sizeof(S), args, MAX_PATH - 1);

#ifdef DEBUG
        CTimer timer;
        timer.Start();
#endif
        if (!xr_strlen(S))
        {
            strconcat(sizeof(S), S, Core.UserName, " - ", "quicksave");
            NET_Packet net_packet;
            net_packet.w_begin(M_SAVE_GAME);
            net_packet.w_stringZ(S);
            net_packet.w_u8(0);
            Level().Send(net_packet, net_flags(TRUE));
        }
        else
        {
            if (!valid_saved_game_name(S))
            {
                Msg("! Save failed: invalid file name - %s", S);
                return;
            }

            NET_Packet net_packet;
            net_packet.w_begin(M_SAVE_GAME);
            net_packet.w_stringZ(S);
            net_packet.w_u8(1);
            Level().Send(net_packet, net_flags(TRUE));
        }
#ifdef DEBUG
        Msg("Game save overhead  : %f milliseconds", timer.GetElapsed_sec() * 1000.f);
#endif
        const bool compat = ClearSkyMode || ShadowOfChernobylMode;
        StaticDrawableWrapper* _s = CurrentGameUI()->AddCustomStatic("game_saved", true, compat ? 3.0f : -1.0f);

        pstr save_name;
        STRCONCAT(save_name, StringTable().translate("st_game_saved").c_str(), ": ", S);
        _s->wnd()->TextItemControl()->SetText(save_name);

        xr_strcat(S, ".dds");
        FS.update_path(S1, "$game_saves$", S);

#ifdef DEBUG
        timer.Start();
#endif
        MainMenu()->Screenshot(IRender::SM_FOR_GAMESAVE, S1);

#ifdef DEBUG
        Msg("Screenshot overhead : %f milliseconds", timer.GetElapsed_sec() * 1000.f);
#endif
    } // virtual void Execute

    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        if (ShadowOfChernobylMode || ClearSkyMode)
            get_files_list(tips, "$game_saves$", SAVE_EXTENSION_LEGACY);
        else
            get_files_list(tips, "$game_saves$", SAVE_EXTENSION);
    }
}; // CCC_ALifeSave

class CCC_ALifeLoadFrom : public IConsole_Command
{
public:
    CCC_ALifeLoadFrom(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        string_path saved_game;
        strncpy_s(saved_game, sizeof(saved_game), args, MAX_PATH - 1);

        if (!ai().get_alife())
        {
            Log("! ALife simulator has not been started yet");
            return;
        }

        if (!xr_strlen(saved_game))
        {
            Log("! Specify file name!");
            return;
        }

        if (!CSavedGameWrapper::saved_game_exist(saved_game))
        {
            Msg("! Cannot find saved game %s", saved_game);
            return;
        }

        if (!CSavedGameWrapper::valid_saved_game(saved_game))
        {
            Msg("! Cannot load saved game %s, version mismatch or saved game is corrupted", saved_game);
            return;
        }

        if (!valid_saved_game_name(saved_game))
        {
            Msg("! Cannot load saved game %s, invalid file name", saved_game);
            return;
        }

        /*     moved to level_network_messages.cpp
                CSavedGameWrapper			wrapper(args);
                if (wrapper.level_id() == ai().level_graph().level_id()) {
                    if (Device.Paused())
                        Device.Pause		(FALSE, TRUE, TRUE, "CCC_ALifeLoadFrom");

                    Level().remove_objects	();

                    game_sv_Single			*game = smart_cast<game_sv_Single*>(Level().Server->game);
                    R_ASSERT				(game);
                    game->restart_simulator	(saved_game);

                    return;
                }
        */

        if (MainMenu()->IsActive())
            MainMenu()->Activate(false);

        Console->Execute("stat_memory");

        if (Device.Paused())
            Device.Pause(FALSE, TRUE, TRUE, "CCC_ALifeLoadFrom");

        NET_Packet net_packet;
        net_packet.w_begin(M_LOAD_GAME);
        net_packet.w_stringZ(saved_game);
        Level().Send(net_packet, net_flags(TRUE));
    }

    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        if (ShadowOfChernobylMode || ClearSkyMode)
            get_files_list(tips, "$game_saves$", SAVE_EXTENSION_LEGACY);
        else
            get_files_list(tips, "$game_saves$", SAVE_EXTENSION);
    }
}; // CCC_ALifeLoadFrom

class CCC_LoadLastSave : public IConsole_Command
{
public:
    CCC_LoadLastSave(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args)
    {
        string_path saved_game = "";
        if (args)
        {
            strncpy_s(saved_game, sizeof(saved_game), args, MAX_PATH - 1);
        }

        if (*saved_game)
        {
            xr_strcpy(g_last_saved_game, saved_game);
            return;
        }

        if (!*g_last_saved_game)
        {
            Msg("! cannot load last saved game since it hasn't been specified");
            return;
        }

        if (!CSavedGameWrapper::saved_game_exist(g_last_saved_game))
        {
            Msg("! Cannot find saved game %s", g_last_saved_game);
            return;
        }

        if (!CSavedGameWrapper::valid_saved_game(g_last_saved_game))
        {
            Msg("! Cannot load saved game %s, version mismatch or saved game is corrupted", g_last_saved_game);
            return;
        }

        if (!valid_saved_game_name(g_last_saved_game))
        {
            Msg("! Cannot load saved game %s, invalid file name", g_last_saved_game);
            return;
        }

        pstr command;
        if (ai().get_alife())
        {
            STRCONCAT(command, "load ", g_last_saved_game);
            Console->Execute(command);
            return;
        }

        STRCONCAT(command, "start server(", g_last_saved_game, "/single/alife/load)");
        Console->Execute(command);
    }

    virtual void Save(IWriter* F)
    {
        if (!*g_last_saved_game)
            return;

        F->w_printf("%s %s\r\n", cName, g_last_saved_game);
    }
};

class CCC_FlushLog : public IConsole_Command
{
public:
    CCC_FlushLog(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR /**args**/)
    {
        FlushLog();
        Msg("* Log file has been saved successfully!");
    }
};

class CCC_ClearLog : public IConsole_Command
{
public:
    CCC_ClearLog(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR)
    {
        LogFile.clear();
        FlushLog();
        Msg("* Log file has been cleaned successfully!");
    }
};

class CCC_FloatBlock : public CCC_Float
{
public:
    CCC_FloatBlock(LPCSTR N, float* V, float _min = 0, float _max = 1) : CCC_Float(N, V, _min, _max){};

    virtual void Execute(LPCSTR args)
    {
#ifdef _DEBUG
        CCC_Float::Execute(args);
#else
        if (!g_pGameLevel || GameID() == eGameIDSingle)
            CCC_Float::Execute(args);
        else
        {
            Msg("! Command disabled for this type of game");
        }
#endif
    }
};

class CCC_Net_CL_InputUpdateRate : public CCC_Integer
{
protected:
    int* value_blin;

public:
    CCC_Net_CL_InputUpdateRate(LPCSTR N, int* V, int _min = 0, int _max = 999)
        : CCC_Integer(N, V, _min, _max), value_blin(V){};

    virtual void Execute(LPCSTR args)
    {
        CCC_Integer::Execute(args);
        if ((*value_blin > 0) && g_pGameLevel)
        {
            g_dwInputUpdateDelta = 1000 / (*value_blin);
        };
    }
};

#ifdef DEBUG

class CCC_DrawGameGraphAll : public IConsole_Command
{
public:
    CCC_DrawGameGraphAll(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args)
    {
        if (!ai().get_level_graph())
            return;

        Level().GetLevelGraphDebugRender()->SetupCurrentLevel(-1);
    }
};

class CCC_DrawGameGraphCurrent : public IConsole_Command
{
public:
    CCC_DrawGameGraphCurrent(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(LPCSTR args)
    {
        if (!ai().get_level_graph())
            return;

        Level().GetLevelGraphDebugRender()->SetupCurrentLevel(ai().level_graph().level_id());
    }
};

class CCC_DrawGameGraphLevel : public IConsole_Command
{
public:
    CCC_DrawGameGraphLevel(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR args)
    {
        if (!ai().get_level_graph())
            return;

        if (!*args)
        {
            Level().GetLevelGraphDebugRender()->SetupCurrentLevel(-1);
            return;
        }

        const GameGraph::SLevel* level = ai().game_graph().header().level(args, true);
        if (!level)
        {
            Msg("! There is no level %s in the game graph", args);
            return;
        }

        Level().GetLevelGraphDebugRender()->SetupCurrentLevel(level->id());
    }
};

#if defined(USE_DEBUGGER)
class CCC_ScriptDbg : public IConsole_Command
{
public:
    CCC_ScriptDbg(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        if (strstr(cName, "script_debug_break") == cName)
        {
            CScriptDebugger* d = GEnv.ScriptEngine->debugger();
            if (d)
            {
                if (d->Active())
                    d->initiateDebugBreak();
                else
                    Msg("Script debugger not active.");
            }
            else
                Msg("Script debugger not present.");
        }
        else if (strstr(cName, "script_debug_stop") == cName)
        {
            GEnv.ScriptEngine->stopDebugger();
        }
        else if (strstr(cName, "script_debug_restart") == cName)
        {
            GEnv.ScriptEngine->restartDebugger();
        };
    };

    virtual void Info(TInfo& I)
    {
        if (strstr(cName, "script_debug_break") == cName)
            xr_strcpy(I, "initiate script debugger [DebugBreak] command");

        else if (strstr(cName, "script_debug_stop") == cName)
            xr_strcpy(I, "stop script debugger activity");

        else if (strstr(cName, "script_debug_restart") == cName)
            xr_strcpy(I, "restarts script debugger or start if no script debugger presents");
    }
};
#endif // #if defined(USE_DEBUGGER)

class CCC_DumpInfos : public IConsole_Command
{
public:
    CCC_DumpInfos(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        CActor* A = smart_cast<CActor*>(Level().CurrentEntity());
        if (A)
            A->DumpInfo();
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dumps all infoportions that actor have"); }
};
class CCC_DumpTasks : public IConsole_Command
{
public:
    CCC_DumpTasks(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        CActor* A = smart_cast<CActor*>(Level().CurrentEntity());
        if (A)
            A->DumpTasks();
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dumps all tasks that actor have"); }
};
#include "map_manager.h"
class CCC_DumpMap : public IConsole_Command
{
public:
    CCC_DumpMap(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args) { Level().MapManager().Dump(); }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dumps all currentmap locations"); }
};

#include "alife_graph_registry.h"
class CCC_DumpCreatures : public IConsole_Command
{
public:
    CCC_DumpCreatures(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        typedef CSafeMapIterator<ALife::_OBJECT_ID, CSE_ALifeDynamicObject>::_REGISTRY::const_iterator const_iterator;

        const_iterator I = ai().alife().graph().level().objects().begin();
        const_iterator E = ai().alife().graph().level().objects().end();
        for (; I != E; ++I)
        {
            CSE_ALifeCreatureAbstract* obj = smart_cast<CSE_ALifeCreatureAbstract*>(I->second);
            if (obj)
            {
                Msg("\"%s\",", obj->name_replace());
            }
        }
    }
    virtual void Info(TInfo& I) { xr_strcpy(I, "dumps all creature names"); }
};

class CCC_DebugFonts : public IConsole_Command
{
public:
    CCC_DebugFonts(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    ~CCC_DebugFonts()
    {
        xr_free(m_ui);
    }

    virtual void Execute(LPCSTR args)
    {
        if (!m_ui)
            m_ui = xr_new<CUIDebugFonts>();

        m_ui->ShowDialog(true);
    }

private:
    CUIDebugFonts* m_ui;
};

class CCC_DebugNode : public IConsole_Command
{
public:
    CCC_DebugNode(LPCSTR N) : IConsole_Command(N){};

    virtual void Execute(LPCSTR args)
    {
        string128 param1, param2;
        VERIFY(xr_strlen(args) < sizeof(string128));

        _GetItem(args, 0, param1, ' ');
        _GetItem(args, 1, param2, ' ');

        u32 value1;
        u32 value2;

        sscanf(param1, "%u", &value1);
        sscanf(param2, "%u", &value2);

        if ((value1 > 0) && (value2 > 0))
        {
            g_bDebugNode = TRUE;
            g_dwDebugNodeSource = value1;
            g_dwDebugNodeDest = value2;
        }
        else
        {
            g_bDebugNode = FALSE;
        }
    }
};

class CCC_ShowMonsterInfo : public IConsole_Command
{
public:
    CCC_ShowMonsterInfo(LPCSTR N) : IConsole_Command(N){};

    virtual void Execute(LPCSTR args)
    {
        string128 param1, param2;
        VERIFY(xr_strlen(args) < sizeof(string128));

        _GetItem(args, 0, param1, ' ');
        _GetItem(args, 1, param2, ' ');

        IGameObject* obj = Level().Objects.FindObjectByName(param1);
        CBaseMonster* monster = smart_cast<CBaseMonster*>(obj);
        if (!monster)
            return;

        u32 value2;

        sscanf(param2, "%u", &value2);
        monster->set_show_debug_info(u8(value2));
    }
};

void PH_DBG_SetTrackObject();
extern string64 s_dbg_trace_obj_name;
class CCC_DbgPhTrackObj : public CCC_String
{
public:
    CCC_DbgPhTrackObj(LPCSTR N) : CCC_String(N, s_dbg_trace_obj_name, sizeof(s_dbg_trace_obj_name)){};
    virtual void Execute(LPCSTR args)
    {
        CCC_String::Execute(args);
        if (!xr_strcmp(args, "none"))
        {
            ph_dbg_draw_mask1.set(ph_m1_DbgTrackObject, FALSE);
            return;
        }
        ph_dbg_draw_mask1.set(ph_m1_DbgTrackObject, TRUE);
        PH_DBG_SetTrackObject();
        // IGameObject* O= Level().Objects.FindObjectByName(args);
        // if(O)
        //{
        //	PH_DBG_SetTrackObject(*(O->cName()));
        //	ph_dbg_draw_mask1.set(ph_m1_DbgTrackObject,TRUE);
        //}
    }

    // virtual void	Info	(TInfo& I)
    //{
    //	xr_strcpy(I,"restart game fast");
    //}
};
#endif

class CCC_PHIterations : public CCC_Integer
{
public:
    CCC_PHIterations(LPCSTR N) : CCC_Integer(N, &phIterations, 15, 50){};
    virtual void Execute(LPCSTR args)
    {
        CCC_Integer::Execute(args);
        // dWorldSetQuickStepNumIterations(NULL,phIterations);
        if (physics_world())
            physics_world()->StepNumIterations(phIterations);
    }
};

#ifdef DEBUG
class CCC_PHGravity : public IConsole_Command
{
public:
    CCC_PHGravity(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if (!physics_world())
            return;
#ifndef DEBUG
        if (g_pGameLevel && Level().game && GameID() != eGameIDSingle)
        {
            Msg("Command is not available in Multiplayer");
            return;
        }
#endif
        physics_world()->SetGravity(float(atof(args)));
    }
    void GetStatus(TStatus& S) override
    {
        if (physics_world())
            xr_sprintf(S, "%3.5f", physics_world()->Gravity());
        else
            xr_sprintf(S, "%3.5f", default_world_gravity);
        while (xr_strlen(S) && ('0' == S[xr_strlen(S) - 1]))
            S[xr_strlen(S) - 1] = 0;
    }
};
#endif // DEBUG

class CCC_PHFps : public CCC_Float
{
#ifndef DEBUG
    static constexpr float MIN_FPS = 50;
    static constexpr float MAX_FPS = 200;
#else
    static constexpr float MIN_FPS = 1;
    static constexpr float MAX_FPS = 1000;
#endif

    float m_dummy = 1.f / ph_console::ph_step_time;

public:
    CCC_PHFps(pcstr name) : CCC_Float(name, &m_dummy, MIN_FPS, MAX_FPS) { }

    void Execute(pcstr args) override
    {
        CCC_Float::Execute(args);

        ph_console::ph_step_time = 1.f / m_dummy;
        if (physics_world())
            physics_world()->SetStep(ph_console::ph_step_time);
    }
};

#ifdef DEBUG

struct CCC_ShowSmartCastStats : public IConsole_Command
{
    CCC_ShowSmartCastStats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args) { show_smart_cast_stats(); }
};

struct CCC_ClearSmartCastStats : public IConsole_Command
{
    CCC_ClearSmartCastStats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args) { clear_smart_cast_stats(); }
};

#endif

#ifndef MASTER_GOLD
/*
struct CCC_NoClip : public CCC_Mask
{
public:
    CCC_NoClip(LPCSTR N, Flags32* V, u32 M):CCC_Mask(N,V,M){};
    virtual	void Execute(LPCSTR args)
    {
        CCC_Mask::Execute(args);
        if (EQ(args,"on") || EQ(args,"1"))
        {
            if(g_pGameLevel && Level().CurrentViewEntity())
            {
                CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
                actor->character_physics_support()->SetRemoved();
            }
        }
    };
};
*/

struct CCC_ToggleNoClip : public IConsole_Command
{
    CCC_ToggleNoClip(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; };

    void Execute(pcstr /*args*/) override
    {
        psActorFlags.invert(AF_NO_CLIP);

        if (!g_pGameLevel)
            return;

        CActor* actor = smart_cast<CActor*>(Level().CurrentViewEntity());
        if (!actor)
            return;

        // Workaround for actor has no physics at all until first move
        Fvector accel{};
        Actor()->g_Physics(accel, 0.0f, 0.0f);
    }
};

#include "xrAICore/Navigation/game_graph.h"
struct CCC_JumpToLevel : public IConsole_Command
{
    CCC_JumpToLevel(LPCSTR N) : IConsole_Command(N){};

    virtual void Execute(LPCSTR level)
    {
        if (!ai().get_alife())
        {
            Msg("! ALife simulator is needed to perform specified command!");
            return;
        }

        GameGraph::LEVEL_MAP::const_iterator I = ai().game_graph().header().levels().begin();
        GameGraph::LEVEL_MAP::const_iterator E = ai().game_graph().header().levels().end();
        for (; I != E; ++I)
            if (!xr_strcmp((*I).second.name(), level))
            {
                ai().alife().jump_to_level(level);
                return;
            }
        Msg("! There is no level \"%s\" in the game graph!", level);
    }

    virtual void Save(IWriter* F){};
    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        if (!ai().get_alife())
        {
            Msg("! ALife simulator is needed to perform specified command!");
            return;
        }

        GameGraph::LEVEL_MAP::const_iterator itb = ai().game_graph().header().levels().begin();
        GameGraph::LEVEL_MAP::const_iterator ite = ai().game_graph().header().levels().end();
        for (; itb != ite; ++itb)
        {
            tips.push_back((*itb).second.name());
        }
    }
};

// [DA_PORT] Воспроизведение вылета в физике после подрыва гранаты (задача #66).
//
//     da_grenade_test [сколько] [через сколько кадров рвать]
//
// ЗАЧЕМ. Вылет случался «иногда после взрыва»: за заход тестера 3 подрыва и 1 падение. Руками такое
// ловится долго и ненадёжно, а обход локаций гранат не бросает. Здесь подрывы делаются пачкой и
// столько раз, сколько нужно.
//
// ПОЧЕМУ ИМЕННО ТАК. Гранаты кладутся НА ЗЕМЛЮ вокруг актёра и подрываются все разом через
// заданное число кадров. Смысл в кольце: взрыв должен застать рядом чужую физику — самого актёра и
// то, что вокруг, — потому что падало в обработчике СТОЛКНОВЕНИЯ, а не в самом взрыве.
//
// ⚠️ Подрыв отложен намеренно. Спавн доходит до клиента не в том же кадре, и рвать сразу означает
// не найти ни одной гранаты — проверка молча отчиталась бы об успехе, ничего не проверив.
// [DA_PORT] Воспроизведение вылета «child registered but not found» (задача #74).
//
// ЗАЧЕМ ОТДЕЛЬНАЯ КОМАНДА. Вылет нашёлся случайно — прежней версией проверки гранаты, которая
// выдавала предметы через alife().spawn_item с родителем-актёром. Такой потомок числится за
// актёром, но до клиента не доходит, и на выходе уборка натыкается на номер без сущности.
//
// ⛔ Когда я перевёл проверку гранаты на Level().spawn_item, сценарий пропал вместе с ошибкой — и
// три прогона «без фатала» перестали что-либо доказывать: заслон не срабатывал ни разу. Поэтому
// путь, порождающий сироту, сохранён здесь ЯВНО и отдельно. Проверка починки без него — самообман.
class CCC_DaOrphanTest : public IConsole_Command
{
public:
    CCC_DaOrphanTest(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        int count = 8;
        if (args && xr_strlen(args))
            sscanf(args, "%d", &count);
        clamp(count, 1, 64);

        if (!g_pGameLevel || !Actor() || !ai().get_alife())
        {
            Msg("! [DA_ORPHAN] нужен загруженный уровень с ALife");
            return;
        }

        u32 ok = 0;
        for (int i = 0; i < count; ++i)
        {
            CSE_Abstract* se = const_cast<CALifeSimulator*>(ai().get_alife())
                                   ->spawn_item("grenade_f1", Actor()->Position(),
                                       Actor()->ai_location().level_vertex_id(),
                                       Actor()->ai_location().game_vertex_id(), Actor()->ID());
            if (se)
                ++ok;
        }

        Msg("~ [DA_ORPHAN] создано потомков актёра через ALife: %u из %d", ok, count);
        FlushLog();

        DA_AfterLoadArm(300, "quit");
    }
};

// ⛔ ПЕРВАЯ ВЕРСИЯ ЭТОЙ ПРОВЕРКИ БЫЛА НЕГОДНОЙ, и это стоило 96 подрывов впустую.
//
// Она раскладывала гранаты НА ЗЕМЛЮ и рвала их напрямую через CGrenade::Destroy(). Отчёт вышел
// бодрый: «8 прогонов, 96 подрывов, ноль вылетов». А в логах не оказалось НИ ОДНОЙ строки
// «Destroying local grenade» — то есть путь, на котором падает (`State(eThrowEnd)` с
// `xr_delete(m_pPhysicsShell)`), не проходился вообще. Проверка отчиталась об успехе, ничего не
// проверив.
//
// Разница в том, что удаление оболочки живёт под `if (m_thrown)`, а поле это ПРИВАТНОЕ и ставится
// только настоящим броском. Значит и бросать надо по-настоящему: граната в инвентарь, в руки,
// выдернуть чеку, отпустить. Обходного пути нет, и искать его не нужно — как раз в обходе и была
// ошибка.
//
// Второе условие вылета — из стека: CAI_Stalker::UpdateCL -> move_along_path -> CollideDynamics.
// Нужен СТАЛКЕР, который идёт и задевает геометрию в момент её сноса. Поэтому вокруг актёра
// заранее расставляются сталкеры, и без них проверка честно скажет, что она бессмысленна.
namespace
{
enum
{
    da_gr_off = -1,
    da_gr_give = 0,   // выдать гранату в инвентарь
    da_gr_arm = 1,    // взять в руки
    da_gr_pull = 2,   // выдернуть чеку
    da_gr_throw = 3,  // отпустить (бросок)
    da_gr_settle = 4, // дать взрыву и физике отработать
};

int g_da_gr_stage = da_gr_off;
int g_da_gr_left = 0;
int g_da_gr_settle = 0;
u32 g_da_gr_thrown = 0;
u32 g_da_gr_revived = 0;
u16 g_da_gr_id = 0xffff; // номер выданной гранаты: её надо ВРУЧНУЮ положить в слот
}

class CCC_DaGrenadeStep : public IConsole_Command
{
public:
    CCC_DaGrenadeStep(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR /*args*/)
    {
        if (da_gr_off == g_da_gr_stage)
            return;

        if (!g_pGameLevel || !Actor())
        {
            Msg("! [DA_GRENADE] актёра нет — проверка прервана");
            g_da_gr_stage = da_gr_off;
            DA_AfterLoadArm(60, "quit");
            return;
        }

        // ⛔ НЕ прерываемся на смерти, а поднимаем здоровье. Прежняя версия выходила с сообщением
        // «актёр мёртв», и это оказалось не редким случаем, а НОРМОЙ: строка
        // «cannot make saved game because actor is dead» есть даже в логах обхода локаций, где ни
        // гранат, ни сталкеров не было. Гибнет он по разным причинам — от прыжка на уровень, от
        // собственных взрывов, от расставленных сталкеров, — и проверка каждый раз упиралась в это
        // вместо своей работы. g_god оказался бессилен, поэтому лечим прямо.
        //
        // ⚠️ Здоровье поднимается на КАЖДОМ шаге: между шагами проходят сотни кадров, и умереть за
        // это время он успевает.
        if (Actor()->GetfHealth() < 0.9f)
        {
            ++g_da_gr_revived;
            Actor()->SetfHealth(1.0f);
        }

        switch (g_da_gr_stage)
        {
        case da_gr_give:
        {
            // ⚠️ Именно Level().spawn_item, а НЕ alife().spawn_item. Через ALife предмет создаётся
            // серверным объектом-потомком и до клиента за сотню кадров так и не доходит — проверено:
            // net_Find по его номеру возвращал ноль, и бросать было нечего. Level().spawn_item —
            // тот же путь, которым пользуется штатная консольная команда выдачи в инвентарь.
            //
            // ⚠️ Номер он не возвращает, поэтому гранату потом ищем в самом инвентаре.
            Level().spawn_item("grenade_f1", Actor()->Position(), false, Actor()->ID());
            g_da_gr_id = 0xffff;
            g_da_gr_stage = da_gr_arm;
            DA_AfterLoadArm(120, "da_grenade_step"); // дать предмету дойти до клиента
            break;
        }

        case da_gr_arm:
        {
            // ⛔ Гранату надо положить в слот РУКАМИ, и виновата в этом НАША ЖЕ правка. В Dead Air
            // слот 14 (GRENADE_SLOT) сделан ручным, а стоковое автозаполнение мы отключили
            // намеренно (Actor.cpp, OnItemDropUpdate) — иначе снятую гранату тут же возвращало
            // обратно. Поэтому выданная граната ложится в рюкзак, а Activate(GRENADE_SLOT)
            // включает ПУСТОЙ слот.
            //
            // Из-за этого прошлый прогон отчитался «бросок 1, бросок 2» и не бросил ничего:
            // команды ушли в пустоту, `Destroying local grenade` в логе не появилось.
            // Ищем гранату в самом инвентаре: номера у нас нет, зато список предметов — вот он.
            PIItem item = nullptr;
            for (PIItem it : Actor()->inventory().m_all)
                if (smart_cast<CGrenade*>(it))
                {
                    item = it;
                    break;
                }

            if (!item)
            {
                Msg("! [DA_GRENADE] гранаты в инвентаре нет — бросок невозможен");
                g_da_gr_stage = da_gr_settle;
                g_da_gr_settle = 1;
                DA_AfterLoadArm(30, "da_grenade_step");
                break;
            }

            const bool slotted = Actor()->inventory().Slot(GRENADE_SLOT, item, false, true);
            Actor()->inventory().Activate(GRENADE_SLOT);
            Msg("~ [DA_GRENADE] граната [%u] в слот: %s", u32(g_da_gr_id), slotted ? "да" : "НЕТ");
            FlushLog();

            g_da_gr_stage = da_gr_pull;
            DA_AfterLoadArm(120, "da_grenade_step"); // анимация доставания
            break;
        }

        case da_gr_pull:
        {
            // ⚠️ Бросок начинается ТОЛЬКО из состояния eIdle (CMissile::Action, kWPN_FIRE). Поэтому
            // печатаем, что сейчас в руках и в каком оно состоянии: без этого «нажали, а броска
            // нет» неотличимо от «нажали не туда».
            PIItem active = Actor()->inventory().ActiveItem();
            CHudItem* hud = active ? smart_cast<CHudItem*>(active) : nullptr;
            // Число состояния печатаем как есть: бросок начинается из eIdle, а её значение
            // сравнивать здесь не с чем — перечисление живёт в заголовке предмета, тянуть его
            // сюда ради одной строки незачем.
            Msg("~ [DA_GRENADE] в руках: %s, состояние %d",
                active ? active->object().cName().c_str() : "ПУСТО", hud ? int(hud->GetState()) : -1);
            FlushLog();

            Actor()->inventory().Action(kWPN_FIRE, CMD_START);
            g_da_gr_stage = da_gr_throw;
            DA_AfterLoadArm(45, "da_grenade_step");
            break;
        }

        case da_gr_throw:
            Actor()->inventory().Action(kWPN_FIRE, CMD_STOP); // отпустили — полетела
            ++g_da_gr_thrown;
            Msg("~ [DA_GRENADE] бросок %u (здоровье %.2f)", g_da_gr_thrown, Actor()->GetfHealth());
            FlushLog();
            g_da_gr_stage = da_gr_settle;
            // ⚠️ Ждём долго и ЧАСТЯМИ: вылет случается не во взрыве, а позже — когда физика
            // наткнётся на снесённую геометрию. Дробим, чтобы между кусками успевать лечить актёра:
            // одним куском в 400 кадров он успевает умереть, и дальше начинается не проверка, а
            // разбор экрана смерти.
            g_da_gr_settle = 5;
            DA_AfterLoadArm(100, "da_grenade_step");
            break;

        case da_gr_settle:
            if (--g_da_gr_settle > 0)
            {
                DA_AfterLoadArm(100, "da_grenade_step");
                break;
            }

            if (--g_da_gr_left > 0)
            {
                g_da_gr_stage = da_gr_give;
                DA_AfterLoadArm(30, "da_grenade_step");
            }
            else
            {
                Msg("~ [DA_GRENADE] ИТОГ: бросков выполнено %u, поднимали здоровье %u раз",
                    g_da_gr_thrown, g_da_gr_revived);
                FlushLog();
                g_da_gr_stage = da_gr_off;
                DA_AfterLoadArm(120, "quit");
            }
            break;
        }
    }
};

class CCC_DaGrenadeTest : public IConsole_Command
{
public:
    CCC_DaGrenadeTest(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        int throws = 4;
        int npc = 6;
        if (args && xr_strlen(args))
            sscanf(args, "%d %d", &throws, &npc);
        clamp(throws, 1, 64);
        clamp(npc, 0, 32);

        if (!g_pGameLevel || !Actor() || !ai().get_alife())
        {
            Msg("! [DA_GRENADE] нужен загруженный уровень с ALife");
            return;
        }

        // ⛔ Неуязвимость обязательна, и это выяснилось прогоном. Расставленные сталкеры оказались
        // враждебными и убили актёра за 150 кадров — до первого броска. В логе осталось
        // «cannot make saved game because actor is dead», и проверка честно прервалась, ничего не
        // проверив. Мёртвый актёр не бросает гранат.
        Console->Execute("g_god 1");

        const Fvector center = Actor()->Position();
        const u32 actor_lv = Actor()->ai_location().level_vertex_id();
        const GameGraph::_GRAPH_ID actor_gv = Actor()->ai_location().game_vertex_id();

        // Сталкеры кольцом в шести метрах: достаточно близко, чтобы взрыв их задел и они пошли, и
        // достаточно далеко, чтобы не оказаться внутри актёра.
        u32 spawned = 0;
        for (int i = 0; i < npc; ++i)
        {
            const float a = PI_MUL_2 * float(i) / float(npc);
            Fvector p = center;
            p.x += 6.0f * _cos(a);
            p.z += 6.0f * _sin(a);

            const u32 lv = ai().level_graph().vertex(actor_lv, p);
            if (!ai().level_graph().valid_vertex_id(lv))
                continue;

            if (const_cast<CALifeSimulator*>(ai().get_alife())
                    ->spawn_item("stalker", p, lv, actor_gv, 0xffff))
                ++spawned;
        }

        // ⛔ Говорим вслух, если сталкеров нет. Без них проверка всё равно отработает и напишет
        // «чисто» — но проверять будет нечего: в стеке вылета стоит CAI_Stalker.
        if (!spawned)
            Msg("! [DA_GRENADE] ⛔ сталкеров расставить НЕ УДАЛОСЬ — вылет из задачи #66 так не "
                "воспроизвести, в его стеке CAI_Stalker::UpdateCL");
        else
            Msg("~ [DA_GRENADE] расставлено сталкеров: %u", spawned);

        Msg("~ [DA_GRENADE] бросков запланировано: %d", throws);
        FlushLog();

        g_da_gr_left = throws;
        g_da_gr_thrown = 0;
        g_da_gr_stage = da_gr_give;
        DA_AfterLoadArm(150, "da_grenade_step"); // дать сталкерам появиться и разойтись
    }
};

// [DA_PORT] Перечислить все локации графа игры — для обхода уровней прогонным стендом.
//
// Зачем командой, а не чтением конфигов: список берётся ИЗ ТОГО ЖЕ графа, по которому работает
// jump_to_level. Любой другой источник (levels\, game.ltx, архивы) может разойтись с графом, и тогда
// обход молча пропустит локацию или споткнётся на несуществующей.
class CCC_DaListLevels : public IConsole_Command
{
public:
    CCC_DaListLevels(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        if (!ai().get_alife())
        {
            Msg("! [DA_LEVELS] нужен ALife: сначала загрузите игру");
            return;
        }

        u32 count = 0;
        GameGraph::LEVEL_MAP::const_iterator I = ai().game_graph().header().levels().begin();
        GameGraph::LEVEL_MAP::const_iterator E = ai().game_graph().header().levels().end();
        for (; I != E; ++I, ++count)
            Msg("~ [DA_LEVELS] %s", (*I).second.name().c_str());

        Msg("~ [DA_LEVELS] всего локаций: %u", count);
        FlushLog();

        // `da_list_levels quit` — выйти сразу после перечисления. Нужно прогонному стенду: иначе
        // заход за списком приходится снимать по таймауту, а это минута впустую и вердикт «зависла».
        if (args && strstr(args, "quit"))
            DA_AfterLoadArm(10, "quit");
    }
};

// [DA_PORT] Один заход обхода локаций: прыгнуть на уровень, пожить там и выйти.
//
//     da_level_probe <локация> [кадров] [ускорение времени]
//
// ЗАЧЕМ ОТДЕЛЬНАЯ КОМАНДА. Сценарий состоит из ДВУХ отложенных шагов — «прыгнуть» и потом «выйти», —
// а у da_after_load одна ячейка, цепочку в неё не уложить (это уже разобрано в da_memory_probe.cpp).
// Здесь прыжок выполняется сразу, а выход взводится заново: к этому моменту прежняя ячейка уже
// отработала и свободна.
//
// ⭐ Ускорение времени не для скорости прогона, а ради НАГРУЗКИ: при time_factor 1000 ALife начинает
// молотить спавн и удаление объектов, переселять отряды и гонять события — то есть ровно те пути,
// где и живут обращения к освобождённой памяти. За те же кадры проверяется многократно больше.
class CCC_DaLevelProbe : public IConsole_Command
{
public:
    CCC_DaLevelProbe(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

    virtual void Execute(LPCSTR args)
    {
        string128 level = {};
        int frames = 600;
        int factor = 1000;

        if (!args || !xr_strlen(args) || sscanf(args, "%127s %d %d", level, &frames, &factor) < 1)
        {
            Msg("! da_level_probe <локация> [кадров] [ускорение времени]");
            return;
        }

        if (!ai().get_alife())
        {
            Msg("! [DA_TOUR] нужен ALife: сначала загрузите игру");
            return;
        }

        // Локация обязана быть в графе. Молча промахнуться нельзя: прогон отчитался бы «чисто»,
        // не побывав нигде.
        bool found = false;
        GameGraph::LEVEL_MAP::const_iterator I = ai().game_graph().header().levels().begin();
        GameGraph::LEVEL_MAP::const_iterator E = ai().game_graph().header().levels().end();
        for (; I != E; ++I)
            if (!xr_strcmp((*I).second.name(), level))
            {
                found = true;
                break;
            }

        if (!found)
        {
            Msg("! [DA_TOUR] ПРОВАЛ: локации \"%s\" нет в графе игры", level);
            FlushLog();
            return;
        }

        // [DA_PORT] Есть в графе — ещё не значит, что есть на диске. В Dead Air такие записи
        // остались от базовой игры: l07_military числится в графе, но данных уровня под неё нет —
        // мод заменил её своей new_military. Игрок туда не попадёт (переходы ведут на замену), а
        // обход прыгает по ВСЕМ записям графа подряд, включая мёртвые.
        //
        // Без этой проверки заход отчитывался ВЫЛЕТОМ — и был прав по букве (игра действительно
        // падала), но вводил в заблуждение по сути: дефекта в движке нет, есть мёртвая запись в
        // данных мода. Такое надо отличать от настоящей поломки, иначе обход каждый раз показывает
        // один и тот же «вылет», к которому все привыкают и перестают смотреть.
        //
        // Проверяем БЕЗ bSet: нам нужен только ответ «есть каталог или нет», переключать путь
        // здесь нельзя — это сделает сама смена уровня.
        //
        // ⚠️ Метка локации ставится ЗДЕСЬ, до проверки, а не перед прыжком: обход ищет свой лог
        // именно по ней. Пропущенный заход тоже должен быть опознан — иначе он выглядит как
        // «лог не найден», то есть неотличимо от сбоя самой оснастки.
        Msg("~ [DA_TOUR] локация: %s | кадров: %d | ускорение времени: %d", level, frames, factor);

        if (g_pGamePersistent->Level_ID(level, "1.0", false) < 0)
        {
            Msg("~ [DA_TOUR] ПРОПУСК: у локации \"%s\" нет данных уровня — запись в графе игры "
                "есть, а каталога нет (в Dead Air так осталось от базовой игры)", level);
            FlushLog();
            Console->Execute("quit");
            return;
        }

        if (factor > 0)
        {
            string64 tf;
            xr_sprintf(tf, sizeof(tf), "time_factor %d", factor);
            Console->Execute(tf);
        }

        // ⚠️ Сброс лога на диск ДО прыжка: если на этой локации случится вылет, метка выше уже
        // будет в файле, и по ней сразу видно, где именно. Без сброса хвост лога теряется.
        FlushLog();

        ai().alife().jump_to_level(level);
        DA_AfterLoadArm(frames, "quit");
    }
};

//#ifndef MASTER_GOLD
class CCC_Script : public IConsole_Command
{
public:
    CCC_Script(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; };
    virtual void Execute(LPCSTR args)
    {
        if (!xr_strlen(args))
        {
            Log("* Specify script name!");
        }
        else
        {
            // rescan pathes
            FS_Path* P = FS.get_path("$game_scripts$");
            P->m_Flags.set(FS_Path::flNeedRescan, TRUE);
            FS.rescan_pathes();
            // run script
            if (GEnv.ScriptEngine->script_process(ScriptProcessor::Level))
                GEnv.ScriptEngine->script_process(ScriptProcessor::Level)->add_script(args, false, true);
        }
    }

    void GetStatus(TStatus& S) override { xr_strcpy(S, "<script_name> (Specify script name!)"); }
    virtual void Save(IWriter* F) {}
    virtual void fill_tips(vecTips& tips, u32 mode) { get_files_list(tips, "$game_scripts$", ".script"); }
};

class CCC_ScriptCommand : public IConsole_Command
{
public:
    CCC_ScriptCommand(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; bLowerCaseArgs = false; }
    virtual void Execute(LPCSTR args)
    {
        if (!xr_strlen(args))
            Log("* Specify string to run!");
        else
        {
            if (GEnv.ScriptEngine->script_process(ScriptProcessor::Level))
            {
                GEnv.ScriptEngine->script_process(ScriptProcessor::Level)->add_script(args, true, true);
                return;
            }

            string4096 S;
            shared_str m_script_name = "console command";
            xr_sprintf(S, "%s\n", args);
            int l_iErrorCode = luaL_loadbuffer(GEnv.ScriptEngine->lua(), S, xr_strlen(S), "@console_command");
            if (!l_iErrorCode)
            {
                l_iErrorCode = lua_pcall(GEnv.ScriptEngine->lua(), 0, 0, 0);
                if (l_iErrorCode)
                {
                    GEnv.ScriptEngine->print_output(GEnv.ScriptEngine->lua(), m_script_name.c_str(), l_iErrorCode);
                    GEnv.ScriptEngine->on_error(GEnv.ScriptEngine->lua());
                    return;
                }
            }

            GEnv.ScriptEngine->print_output(GEnv.ScriptEngine->lua(), m_script_name.c_str(), l_iErrorCode);
        }
    } // void	Execute

    void GetStatus(TStatus& S) override { xr_strcpy(S, "<script_name.function()> (Specify script and function name!)"); }
    virtual void Save(IWriter* F) {}
    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        if (mode == 1)
        {
            get_files_list(tips, "$game_scripts$", ".script");
            return;
        }

        IConsole_Command::fill_tips(tips, mode);
    }
};

// [DA_PORT] Выбранный ИГРОКОМ ход времени. Нужен отдельно от `Device.time_factor()`, потому что
// последний временно перебивает интерфейс: `UITimeDilator` замедляет игру, пока открыт инвентарь или
// КПК, а по закрытии возвращал время к жёсткой единице — не к тому, что стояло до открытия. Из-за
// этого любое значение `time_factor` жило до первого открытия рюкзака и молча пропадало.
//
// Здесь хранится «куда вернуться». Читает это `UITimeDilator::stopTimeDilation`.
float g_da_time_factor_user = 1.0f;

class CCC_TimeFactor : public IConsole_Command
{
public:
    CCC_TimeFactor(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; }
    virtual void Execute(LPCSTR args)
    {
        float time_factor = (float)atof(args);
        clamp(time_factor, EPS, 1000.f);
        g_da_time_factor_user = time_factor;
        Device.time_factor(time_factor);
    }
    // [DA_PORT] Сохраняется в user.ltx: иначе значение пришлось бы задавать заново каждый запуск.
    void Save(IWriter* F) override
    {
        F->w_printf("%s %3.5f\r\n", cName, g_da_time_factor_user);
    }
    void GetStatus(TStatus& S) override { xr_sprintf(S, sizeof(S), "%f", Device.time_factor()); }
    virtual void Info(TInfo& I) { xr_strcpy(I, "[0.001 - 1000.0]"); }
    virtual void fill_tips(vecTips& tips, u32 mode)
    {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%3.3f  (current)  [0.001 - 1000.0]", Device.time_factor());
        tips.push_back(str);
        IConsole_Command::fill_tips(tips, mode);
    }
};

#endif // MASTER_GOLD

class CCC_LuaGCMethod : public CCC_Token
{
public:
    CCC_LuaGCMethod(pcstr name) : CCC_Token(name, &ps_lua_gc_method, lua_gc_method_token) {}

    void Execute(pcstr args) override
    {
        const auto prev = *value;
        CCC_Token::Execute(args);

        switch (*value)
        {
        case 0:
            lua_gc(GEnv.ScriptEngine->lua(), LUA_GCSTOP, 0);
            break;
        case 1:
        case 2:
        case 4:
            if (prev == 0)
                lua_gc(GEnv.ScriptEngine->lua(), LUA_GCRESTART, 0);
            break;
        case 3:
            // Perform a full garbage collection cycle and return to previous strategy.
            lua_gc(GEnv.ScriptEngine->lua(), LUA_GCCOLLECT, 0);
            *value = prev;
            break;
        }
    }
};

#include "GamePersistent.h"

class CCC_MainMenu : public IConsole_Command
{
public:
    CCC_MainMenu(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR args)
    {
        bool bWhatToDo = TRUE;
        if (0 == xr_strlen(args))
        {
            bWhatToDo = !MainMenu()->IsActive();
        };

        if (EQ(args, "on") || EQ(args, "1"))
            bWhatToDo = TRUE;

        if (EQ(args, "off") || EQ(args, "0"))
            bWhatToDo = FALSE;

        MainMenu()->Activate(bWhatToDo);
    }
};

class CCC_UIStyle : public CCC_Token
{
    u32 m_id = 0;

public:
    CCC_UIStyle(pcstr name) : CCC_Token(name, &m_id, nullptr) { }

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        UIStyles->SetupStyle(m_id);
    }

    const xr_token* GetToken() noexcept override // may throw exceptions!
    {
        return UIStyles->GetToken().data();
    }
};

class CCC_UIRestart : public IConsole_Command
{
public:
    CCC_UIRestart(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

    void Execute(pcstr /*args*/) override
    {
        UIStyles->Reset();
    }
};

struct CCC_StartTimeSingle : public IConsole_Command
{
    CCC_StartTimeSingle(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        u32 year = 1, month = 1, day = 1, hours = 0, mins = 0, secs = 0, milisecs = 0;
        sscanf(args, "%d.%d.%d %d:%d:%d.%d", &year, &month, &day, &hours, &mins, &secs, &milisecs);
        year = _max(year, 1);
        month = _max(month, 1);
        day = _max(day, 1);
        g_qwStartGameTime = generate_time(year, month, day, hours, mins, secs, milisecs);

        if (!g_pGameLevel)
            return;

        if (!Level().Server)
            return;

        if (!Level().Server->GetGameState())
            return;

        Level().SetGameTimeFactor(g_qwStartGameTime, g_fTimeFactor);
    }

    void GetStatus(TStatus& S) override
    {
        u32 year = 1, month = 1, day = 1, hours = 0, mins = 0, secs = 0, milisecs = 0;
        split_time(g_qwStartGameTime, year, month, day, hours, mins, secs, milisecs);
        xr_sprintf(S, "%d.%d.%d %d:%d:%d.%d", year, month, day, hours, mins, secs, milisecs);
    }
};

struct CCC_TimeFactorSingle : public CCC_Float
{
    CCC_TimeFactorSingle(LPCSTR N, float* V, float _min = 0.f, float _max = 1.f) : CCC_Float(N, V, _min, _max){};

    virtual void Execute(LPCSTR args)
    {
        // user.ltx exec vs any other caller, and what g_fTimeFactor is right before/after.
        FlushLog();
        CCC_Float::Execute(args);
        FlushLog();

        if (!g_pGameLevel)
            return;

        if (!Level().Server)
            return;

        if (!Level().Server->GetGameState())
            return;

        Level().SetGameTimeFactor(g_fTimeFactor);
    }
};

#ifdef DEBUG
class CCC_RadioGroupMask2;
class CCC_RadioMask : public CCC_Mask
{
    CCC_RadioGroupMask2* group;

public:
    CCC_RadioMask(LPCSTR N, Flags32* V, u32 M) : CCC_Mask(N, V, M) { group = NULL; }
    void SetGroup(CCC_RadioGroupMask2* G) { group = G; }
    virtual void Execute(LPCSTR args);

    IC void Set(BOOL V) { value->set(mask, V); }
};

class CCC_RadioGroupMask2
{
    CCC_RadioMask* mask0;
    CCC_RadioMask* mask1;

public:
    CCC_RadioGroupMask2(CCC_RadioMask* m0, CCC_RadioMask* m1)
    {
        mask0 = m0;
        mask1 = m1;
        mask0->SetGroup(this);
        mask1->SetGroup(this);
    }
    void Execute(CCC_RadioMask& m, LPCSTR args)
    {
        BOOL value = m.GetValue();
        if (value)
        {
            mask0->Set(!value);
            mask1->Set(!value);
        }
        m.Set(value);
    }
};

void CCC_RadioMask::Execute(LPCSTR args)
{
    CCC_Mask::Execute(args);
    VERIFY2(group, "CCC_RadioMask: group not set");
    group->Execute(*this, args);
}

#define CMD_RADIOGROUPMASK2(p1, p2, p3, p4, p5, p6)                                        \
    \
{                                                                                   \
        \
static CCC_RadioMask x##CCC_RadioMask1(p1, p2, p3);                                        \
        Console->AddCommand(&x##CCC_RadioMask1);                                           \
        \
static CCC_RadioMask x##CCC_RadioMask2(p4, p5, p6);                                        \
        Console->AddCommand(&x##CCC_RadioMask2);                                           \
        \
static CCC_RadioGroupMask2 x##CCC_RadioGroupMask2(&x##CCC_RadioMask1, &x##CCC_RadioMask2); \
    \
}

struct CCC_DbgBullets : public CCC_Integer
{
    CCC_DbgBullets(LPCSTR N, int* V, int _min = 0, int _max = 999) : CCC_Integer(N, V, _min, _max){};

    virtual void Execute(LPCSTR args)
    {
        extern xr_vector<Fvector> g_hit[];
        g_hit[0].clear();
        g_hit[1].clear();
        g_hit[2].clear();
        CCC_Integer::Execute(args);
    }
};

#include "attachable_item.h"
#include "attachment_owner.h"
#include "InventoryOwner.h"
#include "Inventory.h"

class CCC_TuneAttachableItem : public IConsole_Command
{
public:
    CCC_TuneAttachableItem(LPCSTR N) : IConsole_Command(N){};
    virtual void Execute(LPCSTR args)
    {
        if (CAttachableItem::m_dbgItem)
        {
            CAttachableItem::m_dbgItem = NULL;
            Msg("CCC_TuneAttachableItem switched to off");
            return;
        };

        IGameObject* obj = Level().CurrentViewEntity();
        VERIFY(obj);
        shared_str ssss = args;

        CAttachmentOwner* owner = smart_cast<CAttachmentOwner*>(obj);
        CAttachableItem* itm = owner->attachedItem(ssss);
        if (itm)
        {
            CAttachableItem::m_dbgItem = itm;
        }
        else
        {
            CInventoryOwner* iowner = smart_cast<CInventoryOwner*>(obj);
            PIItem active_item = iowner->m_inventory->ActiveItem();
            if (active_item && active_item->object().cNameSect() == ssss)
                CAttachableItem::m_dbgItem = active_item->cast_attachable_item();
        }

        if (CAttachableItem::m_dbgItem)
            Msg("CCC_TuneAttachableItem switched to ON for [%s]", args);
        else
            Msg("CCC_TuneAttachableItem cannot find attached item [%s]", args);
    }

    virtual void Info(TInfo& I)
    {
        xr_sprintf(I,
            "allows to change bind rotation and position offsets for attached item, <section_name> given as arguments");
    }
};

class CCC_Crash : public IConsole_Command
{
public:
    CCC_Crash(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR /**args**/)
    {
        VERIFY3(false, "This is a test crash", "Do not post it as a bug");
        int* pointer = 0;
        *pointer = 0; //-V522
    }
};

class CCC_DumpModelBones : public IConsole_Command
{
public:
    CCC_DumpModelBones(LPCSTR N) : IConsole_Command(N) {}
    virtual void Execute(LPCSTR arguments)
    {
        if (!arguments || !*arguments)
        {
            Msg("! no arguments passed");
            return;
        }

        LPCSTR name;

        if (0 == strext(arguments))
            STRCONCAT(name, arguments, ".ogf");
        else
            STRCONCAT(name, arguments);

        string_path fn;

        if (!FS.exist(arguments) && !FS.exist(fn, "$level$", name) && !FS.exist(fn, "$game_meshes$", name))
        {
            Msg("! Cannot find visual \"%s\"", arguments);
            return;
        }

        IRenderVisual* visual = GEnv.Render->model_Create(arguments);
        IKinematics* kinematics = smart_cast<IKinematics*>(visual);
        if (!kinematics)
        {
            GEnv.Render->model_Delete(visual);
            Msg("! Invalid visual type \"%s\" (not a IKinematics)", arguments);
            return;
        }

        Msg("bones for model \"%s\"", arguments);
        for (u16 i = 0, n = kinematics->LL_BoneCount(); i < n; ++i)
            Msg("%s", kinematics->LL_GetData(i).name.c_str());

        GEnv.Render->model_Delete(visual);
    }
};

extern void show_animation_stats();

class CCC_ShowAnimationStats : public IConsole_Command
{
public:
    CCC_ShowAnimationStats(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR) { show_animation_stats(); }
};

class CCC_InvUpgradesHierarchy : public IConsole_Command
{
public:
    CCC_InvUpgradesHierarchy(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args)
    {
        if (ai().get_alife())
        {
            ai().alife().inventory_upgrade_manager().log_hierarchy();
        }
    }
};

class CCC_InvUpgradesCurItem : public IConsole_Command
{
public:
    CCC_InvUpgradesCurItem(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args)
    {
        if (!g_pGameLevel)
        {
            return;
        }
        CUIGameSP* ui_game_sp = smart_cast<CUIGameSP*>(CurrentGameUI());
        if (!ui_game_sp)
        {
            return;
        }
        PIItem item = ui_game_sp->GetActorMenu().get_upgrade_item();
        if (item)
        {
            item->log_upgrades();
        }
        else
        {
            Msg("- Current item in ActorMenu is unknown!");
        }
    }
};

class CCC_InvDropAllItems : public IConsole_Command
{
public:
    CCC_InvDropAllItems(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; };
    virtual void Execute(LPCSTR args)
    {
        if (!g_pGameLevel)
        {
            return;
        }
        CUIGameSP* ui_game_sp = smart_cast<CUIGameSP*>(CurrentGameUI());
        if (!ui_game_sp)
        {
            return;
        }
        int d = 0;
        sscanf(args, "%d", &d);
        if (ui_game_sp->GetActorMenu().DropAllItemsFromRuck(d == 1))
        {
            Msg("- All items from ruck of Actor is dropping now.");
        }
        else
        {
            Msg("! ActorMenu is not in state `Inventory`");
        }
    }
}; // CCC_InvDropAllItems

#endif // DEBUG

class CCC_DumpObjects : public IConsole_Command
{
public:
    CCC_DumpObjects(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(LPCSTR) { Level().Objects.dump_all_objects(); }
};

class CCC_GSCheckForUpdates : public IConsole_Command
{
    bool m_informNoPatch = true;

    void SetupCallParams(pcstr args)
    {
        m_informNoPatch = true;
        if (args && *args)
        {
            int bInfo = 1;
            sscanf(args, "%d", &bInfo);
            m_informNoPatch = (bInfo != 0);
        }
    }

public:
    CCC_GSCheckForUpdates(LPCSTR N) : IConsole_Command(N)
    {
        bEmptyArgsHandled = true;
    };

    virtual void Execute(LPCSTR arguments)
    {
        auto mm = MainMenu();
        if (mm == nullptr)
            return;

        SetupCallParams(arguments);

        if (m_informNoPatch)
        {
            mm->OnPatchCheck(false);
        }
    }
};

class CCC_Net_SV_GuaranteedPacketMode : public CCC_Integer
{
protected:
    int* value_blin;

public:
    CCC_Net_SV_GuaranteedPacketMode(LPCSTR N, int* V, int _min = 0, int _max = 2)
        : CCC_Integer(N, V, _min, _max), value_blin(V){};

    virtual void Execute(LPCSTR args) { CCC_Integer::Execute(args); }
};
#ifdef DEBUG
void DBG_CashedClear();
class CCC_DBGDrawCashedClear : public IConsole_Command
{
public:
    CCC_DBGDrawCashedClear(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
private:
    virtual void Execute(LPCSTR args) { DBG_CashedClear(); }
};

#endif

class CCC_DbgVar : public IConsole_Command
{
public:
    CCC_DbgVar(LPCSTR N) : IConsole_Command(N) { bEmptyArgsHandled = false; };
    virtual void Execute(LPCSTR arguments)
    {
        if (!arguments || !*arguments)
        {
            return;
        }

        if (_GetItemCount(arguments, ' ') == 1)
        {
            ai_dbg::show_var(arguments);
        }
        else
        {
            char name[1024];
            float f;
            sscanf(arguments, "%s %f", name, &f);
            ai_dbg::set_var(name, f);
        }
    }
};

class CCC_CleanupTasks : public IConsole_Command
{
public:
    CCC_CleanupTasks(pcstr name) : IConsole_Command(name) {}
    void Execute(pcstr /*args*/) override
    {
        Level().GameTaskManager().CleanupTasks();
    }
};

class CCC_UI_Time_Dilation_Mode : public IConsole_Command
{
    UITimeDilator::UIMode mode;
    bool isEnable;

public:
    CCC_UI_Time_Dilation_Mode(pcstr name, UITimeDilator::UIMode mode) : IConsole_Command(name), mode(mode) {};

    void Execute(pcstr args) override
    {
        if (EQ(args, "on") || EQ(args, "1"))
        {
            TimeDilator()->SetModeEnability(mode, true);
            isEnable = true;
        }
        else if (EQ(args, "off") || EQ(args, "0"))
        {
            TimeDilator()->SetModeEnability(mode, false);
            isEnable = false;
        }
        else
            InvalidSyntax();
    }

    void GetStatus(TStatus& status) override
    {
        xr_strcpy(status, isEnable ? "on" : "off");
    }

    void Info(TInfo& info) override
    {
        xr_strcpy(info, "'on/off' or '1/0'");
    }

    void fill_tips(vecTips& tips, u32 /*mode*/) override
    {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%s (current) [on/off]", isEnable ? "on" : "off");
        tips.push_back(str);
    }
};

class CCC_UI_Time_Factor : public IConsole_Command
{
    float uiTimeFactor = 1.0;

public:
    CCC_UI_Time_Factor(pcstr name) : IConsole_Command(name){};

    void Execute(pcstr args) override
    {
        float time_factor = (float)atof(args);
        clamp(time_factor, EPS, 1.f);
        TimeDilator()->SetUiTimeFactor(time_factor);
        uiTimeFactor = time_factor;
    }

    void Info(TInfo& info) override
    {
        xr_strcpy(info, "[0.001 - 1.0]");
    }

    void fill_tips(vecTips& tips, u32 mode) override
    {
        TStatus str;
        xr_sprintf(str, sizeof(str), "%3.3f (current) [0.001 - 1.0]", uiTimeFactor);
        tips.push_back(str);
    }

    void GetStatus(TStatus& status) override
    {
        xr_sprintf(status, sizeof(status), "%f", uiTimeFactor);
    }
};

class CCC_LuaProfiler : public IConsole_Command
{
public:
    constexpr static cpcstr COMMAND_LUA_PROFILER_STATUS = "lua_profiler_status";
    constexpr static cpcstr COMMAND_LUA_PROFILER_START = "lua_profiler_start";
    constexpr static cpcstr COMMAND_LUA_PROFILER_START_HOOK_MODE = "lua_profiler_start_hook_mode";
    constexpr static cpcstr COMMAND_LUA_PROFILER_START_SAMPLING_MODE = "lua_profiler_start_sampling_mode";
    constexpr static cpcstr COMMAND_LUA_PROFILER_STOP = "lua_profiler_stop";
    constexpr static cpcstr COMMAND_LUA_PROFILER_RESET = "lua_profiler_reset";
    constexpr static cpcstr COMMAND_LUA_PROFILER_LOG = "lua_profiler_log";
    constexpr static cpcstr COMMAND_LUA_PROFILER_SAVE = "lua_profiler_save";

    CCC_LuaProfiler(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }

    void Execute(pcstr args) override
    {
        CScriptProfiler* profiler = GEnv.ScriptEngine->m_profiler;

        if (strstr(cName, COMMAND_LUA_PROFILER_STATUS) == cName)
        {
            Msg("[P] Profiler status: %s, type - %s", profiler->IsActive() ? "on" : "off",
                profiler->GetTypeString().c_str());
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_HOOK_MODE) == cName)
        {
            profiler->StartHookMode();
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_SAMPLING_MODE) == cName)
        {
            u32 interval = atoi(args);

            profiler->StartSamplingMode(interval ? interval : CScriptProfiler::PROFILE_SAMPLING_INTERVAL_DEFAULT);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START) == cName)
        {
            u32 profiler_type = atoi(args);

            profiler->Start(profiler_type ? (CScriptProfilerType)profiler_type : CScriptProfiler::PROFILE_TYPE_DEFAULT);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_STOP) == cName)
        {
            profiler->Stop();
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_RESET) == cName)
        {
            profiler->Reset();
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_LOG) == cName)
        {
            u32 limit = atoi(args);

            profiler->LogReport(limit ? limit : CScriptProfiler::PROFILE_ENTRIES_LOG_LIMIT_DEFAULT);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_SAVE) == cName)
        {
            profiler->SaveReport();
        }
    }

    void fill_tips(vecTips& tips, u32 /*mode*/) override
    {
        TStatus status_buffer;

        if (strstr(cName, COMMAND_LUA_PROFILER_STATUS) == cName)
        {
            // No arguments.
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_HOOK_MODE) == cName)
        {
            // No arguments.
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_SAMPLING_MODE) == cName)
        {
            xr_sprintf(status_buffer, "%d (default) [1-%d] - sampling interval",
                CScriptProfiler::PROFILE_SAMPLING_INTERVAL_DEFAULT, CScriptProfiler::PROFILE_SAMPLING_INTERVAL_MAX);
            tips.emplace_back(status_buffer);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_START) == cName)
        {
            xr_sprintf(status_buffer, "%d - hooks based profiler", CScriptProfilerType::Hook);
            tips.emplace_back(status_buffer);

            xr_sprintf(status_buffer, "%d - sampling based profiler", CScriptProfilerType::Sampling);
            tips.emplace_back(status_buffer);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_STOP) == cName)
        {
            // No arguments.
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_RESET) == cName)
        {
            // No arguments.
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_LOG) == cName)
        {
            xr_sprintf(status_buffer, "%d (default) - count of profiling entries to print",
                CScriptProfiler::PROFILE_ENTRIES_LOG_LIMIT_DEFAULT, CScriptProfiler::PROFILE_SAMPLING_INTERVAL_MAX);
            tips.emplace_back(status_buffer);
        }
        else if (strstr(cName, COMMAND_LUA_PROFILER_SAVE) == cName)
        {
            // No arguments.
        }
    }

    void Info(TInfo& info) override
    {
        if (strstr(cName, COMMAND_LUA_PROFILER_STATUS) == cName)
            xr_strcpy(info, "no arguments : print lua profiler status");
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_HOOK_MODE) == cName)
            xr_strcpy(info, "no arguments : start lua script profiling in hook mode");
        else if (strstr(cName, COMMAND_LUA_PROFILER_START_SAMPLING_MODE) == cName)
            xr_strcpy(info,
                "integer value in range [1,1000] : start lua script profiling in sampling mode with provided sampling "
                "interval");
        else if (strstr(cName, COMMAND_LUA_PROFILER_START) == cName)
            xr_strcpy(info, "integer value in range [0,2] : start lua script profiling in provided mode");
        else if (strstr(cName, COMMAND_LUA_PROFILER_STOP) == cName)
            xr_strcpy(info, "no arguments : stop lua script profiling");
        else if (strstr(cName, COMMAND_LUA_PROFILER_RESET) == cName)
            xr_strcpy(info, "no arguments : reset lua script profiling stats");
        else if (strstr(cName, COMMAND_LUA_PROFILER_LOG) == cName)
            xr_strcpy(info, "integer value : log lua script profiling stats, limit entries with argument");
        else if (strstr(cName, COMMAND_LUA_PROFILER_SAVE) == cName)
            xr_strcpy(info, "no arguments : save lua script profiling stats in a file");
    }
};

// [DA_PORT] Замерочное вращение камеры: воспроизводимый проезд вместо «покрутил мышкой».
//
// Два прогона, снятые вручную, несравнимы между собой — камера каждый раз идёт иначе, а значит и
// набор видимой геометрии другой. С этой командой прогон повторяется в точности, и разница в кадрах
// означает разницу от правки, а не от того, как повели мышкой.
//
// Пример: cam_yaw_rotate 30 12 — полный оборот за двенадцать секунд.
class CCC_CameraYawRotate final : public IConsole_Command
{
public:
    explicit CCC_CameraYawRotate(LPCSTR name) : IConsole_Command(name) {}

    void Execute(LPCSTR args) override
    {
        float speed{};
        float duration{ -1.f };
        const int parsed = sscanf(args, "%f %f", &speed, &duration);
        if (parsed < 1 || (parsed > 1 && duration < 0.f && !fsimilar(duration, -1.f)))
        {
            Msg("! Usage: %s <градусов_в_секунду> [секунд], -1 = пока не остановят", cName);
            return;
        }

        ConfigureActorCameraYawRotation(speed, duration);
        Msg("* %s: скорость %.3f град/с, длительность %.3f с", cName, speed, duration);
    }

    void Info(TInfo& info) override { xr_strcpy(info, "degrees_per_second [duration_seconds=-1]"); }
};

void CCC_RegisterCommands()
{
    ZoneScoped;

    // options
    g_OptConCom.Init();

    CMD1(CCC_MemStats, "stat_memory");
    CMD1(CCC_DaMemDump, "da_mem_dump");   // [DA_PORT] таблица памяти по загрузкам
    CMD1(CCC_DaMemReset, "da_mem_reset"); // [DA_PORT] забыть накопленное
    CMD1(CCC_DaSpawnDump, "da_spawn_dump"); // [DA_PORT] цена спавна по секциям
    CMD1(CCC_DaAlifeDump, "da_alife_dump"); // [DA_PORT] цена обновления ALife по секциям
    CMD1(CCC_DaSanitar, "da_alife_sanitize"); // [DA_PORT] чистка висячих записей реестра
    {
        // [DA_PORT] расхождение серверного реестра и реестра ALife, см. xrServer::entity_Destroy
        extern int ps_da_registry_log;
        CMD4(CCC_DaDebugInteger, "da_registry_log", &ps_da_registry_log, 0, 1);
        extern int ps_da_spawn_trace;
        CMD4(CCC_DaDebugInteger, "da_spawn_trace", &ps_da_spawn_trace, 0, 1);

        // [DA_PORT] Сторож висячих выдач: кто спрашивает у реестра уже удалённый объект.
        extern int ps_da_dangling_watch;
        CMD4(CCC_DaDebugInteger, "da_dangling_watch", &ps_da_dangling_watch, 0, 1);

        // [DA_PORT] «Почему не открылось»: что дал луч на нажатие использования.
        // Разбор трёх подозреваемых — у ps_da_use_log в ActorInput.cpp.
        extern int ps_da_use_log;
        CMD4(CCC_DaDebugInteger, "da_use_log", &ps_da_use_log, 0, 1);
    }
    {
        // [DA_PORT] Свой огонёк источника света от первого лица: 0 — не показывать (по умолчанию),
        // 1 — как было. Обычная настройка, а не отладочная: должна сохраняться между запусками.
        // Разбор — у места применения в CTorch::UpdateCL.
        extern int ps_da_torch_glow_fp;
        CMD4(CCC_Integer, "da_torch_glow_fp", &ps_da_torch_glow_fp, 0, 1);
        extern int ps_da_npc_torch_shadow; // [DA_PORT] тень фонаря сталкера, разбор в Torch.cpp
        CMD4(CCC_Integer, "da_npc_torch_shadow", &ps_da_npc_torch_shadow, 0, 1);
    }
    {
        // [DA_PORT] Поправка игрока к положению оружия в руках, МЕТРЫ. Разбор — у места применения
        // в player_hud::update.
        //
        // Оси: X вправо, Y вверх, Z ВПЕРЁД от камеры. Отрицательный Z придвигает оружие к лицу,
        // положительный отодвигает. Предел ±0.3 м взят с запасом к разбросу самих конфигов: там
        // значения порядка 0.1–0.2, и большее уводило бы ствол за пределы видимого.
        //
        // Обычные настройки, а не отладочные: сохраняются между запусками.
        extern float ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z;
        CMD4(CCC_Float, "da_hud_pos_x", &ps_da_hud_pos_x, -0.3f, 0.3f);
        CMD4(CCC_Float, "da_hud_pos_y", &ps_da_hud_pos_y, -0.3f, 0.3f);
        CMD4(CCC_Float, "da_hud_pos_z", &ps_da_hud_pos_z, -0.3f, 0.3f);

        // [DA_PORT] Гасить ли поправку при прицеливании. Разбор — у места применения в player_hud.
        extern int ps_da_hud_pos_in_aim;
        CMD4(CCC_Integer, "da_hud_pos_in_aim", &ps_da_hud_pos_in_aim, 0, 1);

        // [DA_PORT] Доворот ствола к перекрестию. Разбор — у объявления в player_hud.cpp.
        extern float ps_da_aim_align;
        CMD4(CCC_Float, "da_aim_align", &ps_da_aim_align, 0.f, 1.f);

        // [DA_PORT] ВРЕМЕННЫЙ след анимаций рук: какой цикл, у какого ствола, на какие части модели.
        // Разбор — у объявления в player_hud.cpp.
        extern int ps_da_anim_trace;
        CMD4(CCC_Integer, "da_anim_trace", &ps_da_anim_trace, 0, 1);

        // [DA_PORT] Скорость переезда посадки руки в скриптовой сцене: вход и выход, доля в секунду.
        extern float ps_da_scene_seat_in, ps_da_scene_seat_out;
        CMD4(CCC_Float, "da_scene_seat_in", &ps_da_scene_seat_in, 0.5f, 20.f);
        CMD4(CCC_Float, "da_scene_seat_out", &ps_da_scene_seat_out, 0.5f, 40.f);

        // [DA_PORT] Печать подобранного положения оружия В ГОТОВОМ ВИДЕ ДЛЯ .ltx.
        //
        // Зачем именно так. Во всей линейке X-Ray выравнивание мушки — РУЧНАЯ работа по конфигу
        // каждого ствола: движок ничего не выравнивает сам, а `hud_adjust` в Anomaly это инструмент
        // подбора, а не автоматика. Значит цикл должен замыкаться: покрутил ползунки — получил
        // строку — вставил в конфиг. Без последнего шага подгонка остаётся «посмотреть».
        //
        // Числа берём из ЖИВЫХ замеров (attachable_hud_item::m_measures), а не перечитываем конфиг:
        // там уже разрешён вариант _16x9 и применены все правки. Повороты в замерах хранятся в
        // градусах — ровно как в конфиге, пересчёт не нужен (см. ypr.mul(PI/180) у места применения).
        class CCC_DaAimDump : public IConsole_Command
        {
        public:
            CCC_DaAimDump(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr) override
            {
                attachable_hud_item* hi = g_player_hud ? g_player_hud->attached_item(0) : nullptr;
                if (!hi || !hi->m_parent_hud_item)
                {
                    Msg("! [DA_PORT] da_aim_dump: в руках ничего нет — печатать нечего.");
                    return;
                }

                extern float ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z;
                extern ENGINE_API float psHUD_FOV;

                const bool wide = UICore::is_widescreen();
                pcstr sfx = wide ? "_16x9" : "";

                Fvector pos = hi->m_measures.m_hands_attach[0];
                Fvector rot = hi->m_measures.m_hands_attach[1];
                Fvector aim_pos = hi->m_measures.m_hands_offset[0][1];
                Fvector aim_rot = hi->m_measures.m_hands_offset[1][1];

                // ⚠️ Живая поправка ложится ТОЛЬКО в hands_position, и это не выбор, а факт: ползунки
                // прибавляются к положению РУК (player_hud::update, tmp += ps_da_hud_pos_*), тогда как
                // aim_hud_offset применяется в СОБСТВЕННОЙ системе оружия (Weapon.cpp,
                // trans.mulB_43(hud_rotation)). Системы координат разные, и складывать поправку с
                // прицельной строкой нельзя — напечатанный конфиг не воспроизвёл бы подобранное.
                //
                // На этом я уже обжёгся: пересчитал замеренное отклонение дула из системы камеры и
                // вписал в aim_hud_offset_pos как есть — оружие уехало через пол-экрана.
                const u8 idx = hi->m_parent_hud_item->GetCurrentHudOffsetIdx();
                const Fvector live = { ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z };
                pos.add(live);

                // Прицельную поправку складываем ОТДЕЛЬНО: она задаётся ровно там же, где движок
                // читает aim_hud_offset_pos, поэтому система координат совпадает по построению и
                // напечатанная строка воспроизводит подобранное один в один.
                extern Fvector g_da_aim_offset_delta;
                aim_pos.add(g_da_aim_offset_delta);

                Msg("~ [DA_PORT] da_aim_dump: секция [%s], экран %s, hud_fov %.3f, состояние %s",
                    hi->m_sect_name.c_str(), wide ? "широкий (_16x9)" : "4:3", psHUD_FOV,
                    idx > 0 ? "ПРИЦЕЛИВАНИЕ" : "от бедра");
                Msg("~   поправка ползунков (%.4f, %.4f, %.4f) внесена в hands_position (система РУК);"
                    " состояние сейчас: %s",
                    live.x, live.y, live.z, idx > 0 ? "прицеливание" : "от бедра");
                Msg("~   прицельная поправка стрелками (%.4f, %.4f, %.4f) внесена в aim_hud_offset_pos",
                    g_da_aim_offset_delta.x, g_da_aim_offset_delta.y, g_da_aim_offset_delta.z);
                Msg("~ --- скопировать в конфиг ствола ---");
                Msg("[%s]", hi->m_sect_name.c_str());
                Msg("hands_position%s        = %.6f,%.6f,%.6f", sfx, pos.x, pos.y, pos.z);
                Msg("hands_orientation%s     = %.6f,%.6f,%.6f", sfx, rot.x, rot.y, rot.z);
                Msg("aim_hud_offset_pos%s    = %.6f,%.6f,%.6f", sfx, aim_pos.x, aim_pos.y, aim_pos.z);
                Msg("aim_hud_offset_rot%s    = %.6f,%.6f,%.6f", sfx, aim_rot.x, aim_rot.y, aim_rot.z);
                Msg("~ --- конец ---");
                Msg("~   после вставки верните ползунки в ноль: da_hud_pos_x 0; da_hud_pos_y 0; da_hud_pos_z 0");

                // [DA_PORT] Живое состояние прицеливания — В ЛОГ, а не только на экран.
                //
                // Панель da_aim_debug рисуется поверх игры и в лог не попадает, поэтому сравнение
                // «сломанный сейв против нормального» упиралось в скриншоты, а они то тёмные, то не
                // доходят. Одна команда должна класть в файл всё, что нужно для сравнения.
                //
                // Ключевое здесь — доля прицеливания. Она живёт в объекте оружия, не сохраняется и
                // обнуляется в конструкторе; застрянь она ненулевой при опущенном оружии — всё
                // прицельное смещение применится от бедра, а загрузка сейва это вылечит.
                {
                    extern float g_da_zoom_factor;
                    extern bool g_da_zoom_weapon, g_da_zoom_actor;
                    extern u8 g_da_zoom_idx;
                    extern bool g_da_aim_allowed;
                    extern u32 g_da_aim_frozen_frames;
                    extern float g_da_aim_angle_deg, g_da_aim_shift, g_da_aim_shift_max;

                    const bool stuck = (!g_da_zoom_weapon && g_da_zoom_factor > EPS) ||
                        (g_da_zoom_weapon != g_da_zoom_actor);

                    Msg("~ состояние: доля прицеливания %.4f | оружие в прицеле %s | актёр целится %s "
                        "| idx %u%s",
                        g_da_zoom_factor, g_da_zoom_weapon ? "да" : "нет", g_da_zoom_actor ? "да" : "нет",
                        u32(g_da_zoom_idx), stuck ? "   <-- РАСХОЖДЕНИЕ" : "");
                    extern float g_da_zoom_hip_peak;
                    extern u32 g_da_zoom_mismatch_frames;
                    // ⭐ Главная строка. Доля обязана быть НУЛЁМ всё время, пока оружие опущено;
                    // ненулевой пик означает, что прицельное смещение применялось от бедра.
                    Msg("~ ЗА СЕССИЮ: пик доли при ОПУЩЕННОМ оружии %.4f%s | кадров с расхождением "
                        "флагов %u",
                        g_da_zoom_hip_peak, g_da_zoom_hip_peak > 0.01f ? "   <-- НЕ НОЛЬ" : "",
                        g_da_zoom_mismatch_frames);
                    Msg("~ инерция: догон %s, кадров стоит %u, угол %.2f град, сдвиг %.4f м (пик %.4f)",
                        g_da_aim_allowed ? "взведён" : "СНЯТ", g_da_aim_frozen_frames, g_da_aim_angle_deg,
                        g_da_aim_shift, g_da_aim_shift_max);

                    // ⭐ ИТОГ ЦЕПОЧКИ — где оружие стоит и куда смотрит ОТНОСИТЕЛЬНО ГЛАЗА.
                    //
                    // Зачем это, когда всё выше уже совпало. Первая пара дампов показала, что в
                    // прицеливании совпадают ВСЕ измеренные входные величины: конфиг, доля
                    // прицеливания, флаги, вклад инерции. А картинка при этом разная. Значит
                    // различие приходит из последнего неизмеренного звена — позы рук: оружие висит
                    // на кости модели рук (calc_transform берёт LL_GetTransform(m_ancors)), и куда
                    // встала кость, туда встал и ствол.
                    //
                    // Матрица предмета — конец всей цепочки, после неё положение уже не меняется.
                    // Если эти пять чисел разойдутся между сейвами, а всё выше совпало, то виновата
                    // именно поза рук, и дальше искать надо там.
                    Fvector d;
                    d.sub(hi->m_item_transform.c, Device.vCameraPosition);
                    const float rt = d.dotproduct(Device.vCameraRight);
                    const float up = d.dotproduct(Device.vCameraTop);
                    const float fw = d.dotproduct(Device.vCameraDirection);

                    Fvector k = hi->m_item_transform.k;
                    k.normalize_safe();
                    const float yaw = rad2deg(atan2f(k.dotproduct(Device.vCameraRight),
                        k.dotproduct(Device.vCameraDirection)));
                    const float pitch = rad2deg(asinf(k.dotproduct(Device.vCameraTop)));

                    {
                        extern float g_da_shot_sum_yaw, g_da_shot_sum_pitch, g_da_shot_disp_deg;
                        extern u32 g_da_shot_count;
                        if (g_da_shot_count)
                            Msg("~ ВЫСТРЕЛЫ: %u шт | СРЕДНЕЕ отклонение от перекрестия %+.3f / %+.3f "
                                "град | разброс ствола %.3f град",
                                g_da_shot_count, g_da_shot_sum_yaw / float(g_da_shot_count),
                                g_da_shot_sum_pitch / float(g_da_shot_count), g_da_shot_disp_deg);
                        else
                            Msg("~ ВЫСТРЕЛЫ: ни одного — прибор пуст, пострелять надо ДО дампа");
                    }

                    Msg("~ ИТОГ: оружие от глаза  вправо %+.4f  вверх %+.4f  вперёд %+.4f  (метры)",
                        rt, up, fw);
                    Msg("~ ИТОГ: ствол относительно взгляда  по горизонтали %+.3f  по вертикали %+.3f (град)",
                        yaw, pitch);

                    // ⭐ ТОЧКА ВЫЛЕТА — то есть сама геометрия, а не корень модели.
                    //
                    // Замер выше показал, что КОРЕНЬ оружия в обоих сейвах стоит одинаково. Но мушка
                    // это геометрия, и её двигают КОСТИ: анимация рук и оружия уводит видимую часть,
                    // не трогая корень. Анимация как раз и сбрасывается загрузкой сейва — значит
                    // проверять надо именно её.
                    //
                    // Считаем ровно как setup_firedeps: кость fire_bone -> смещение fire_point ->
                    // матрица предмета. Это точка у дульного среза, рядом с мушкой.
                    if (hi->m_measures.m_prop_flags.test(hud_item_measures::e_fire_point) && hi->m_model)
                    {
                        Fvector fp;
                        Fmatrix fire_mat = hi->m_model->LL_GetTransform(hi->m_measures.m_fire_bone);
                        fire_mat.transform_tiny(fp, hi->m_measures.m_fire_point_offset);
                        hi->m_item_transform.transform_tiny(fp);

                        Fvector fd2;
                        fd2.sub(fp, Device.vCameraPosition);
                        Msg("~ ИТОГ: дуло от глаза  вправо %+.4f  вверх %+.4f  вперёд %+.4f  (метры)",
                            fd2.dotproduct(Device.vCameraRight), fd2.dotproduct(Device.vCameraTop),
                            fd2.dotproduct(Device.vCameraDirection));

                        // Куда дуло уводит от центра экрана, в градусах — это и есть «мушка съехала».
                        const float fx = fd2.dotproduct(Device.vCameraRight);
                        const float fy = fd2.dotproduct(Device.vCameraTop);
                        const float fz = fd2.dotproduct(Device.vCameraDirection);
                        if (fz > EPS)
                            Msg("~ ИТОГ: дуло от центра экрана  по горизонтали %+.3f  по вертикали %+.3f (град)",
                                rad2deg(atan2f(fx, fz)), rad2deg(atan2f(fy, fz)));
                    }
                    else
                        Msg("~ ИТОГ: у ствола нет fire_bone — точку вылета не посчитать");
                }
                FlushLog();
            }
        };
        CMD1(CCC_DaAimDump, "da_aim_dump");

        // [DA_PORT] Вернуть подгонку к значениям по умолчанию. Кнопка «Вернуть стандартные» в окне.
        //
        // Зачем отдельной командой, а не списком в скрипте: значения должны лежать в ОДНОМ месте,
        // рядом с движком, откуда они и взяты. Разбросанные по Lua числа разойдутся с движком при
        // первой же правке, и «стандартные» перестанут быть стандартными молча.
        //
        // Источники значений (проверены по коду, не выдуманы):
        //   psHUD_FOV = 0.45, g_fov = 67.5                     — xr_ioc_cmd.cpp
        //   ps_gamma = ps_brightness = ps_contrast = 1.0        — xr_ioc_cmd.cpp
        //   ps_r_color_base_r/g/b = 1.04 / 1.00 / 0.96          — xrRender_console.cpp
        //   ps_r2_vibrance_val = 0.18                           — xrRender_console.cpp
        //   ps_da_hud_pos_* = 0                                 — player_hud.cpp (наша добавка)
        //
        // Идём через консоль, а не по указателям: часть величин живёт в DLL рендера, откуда xrGame
        // их не видит, и там же на записи висят побочные действия (гамма-рампа, профиль цвета).
        class CCC_DaTuneDefaults : public IConsole_Command
        {
        public:
            CCC_DaTuneDefaults(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }

            // [DA_PORT] Без аргументов — вернуть ВСЁ, как было. С аргументами `da_tune_defaults <имя>
            // <имя>...` — только перечисленные величины.
            //
            // Зачем: кнопка «Вернуть стандартные» стоит на экране с вкладками, и сбрасывать заодно
            // цветокоррекцию, когда игрок правит прицел, — это отменять чужую работу. Какие величины
            // относятся к вкладке, знает скрипт окна (таблица PAGES в ui_da_tune.script) — он их и
            // передаёт. Числа при этом остаются здесь, в одном месте: скрипт присылает ИМЕНА, а не
            // значения, и разойтись им не с чем.
            static bool wanted(pcstr args, pcstr name)
            {
                if (!args || !args[0])
                    return true; // без списка — сбрасываем всё

                // Ищем имя как ОТДЕЛЬНОЕ слово: без этого "fov" совпал бы с "hud_fov", и правка
                // одной вкладки утащила бы величину соседней.
                const size_t n = xr_strlen(name);
                for (pcstr p = args; (p = strstr(p, name)) != nullptr; p += n)
                {
                    const bool left = (p == args) || p[-1] == ' ';
                    const bool right = (p[n] == 0) || p[n] == ' ';
                    if (left && right)
                        return true;
                }
                return false;
            }

            void Execute(pcstr args) override
            {
                static const struct { pcstr name; float value; } da_defaults[] = {
                    // Обзор, изображение и цветокоррекция — НАШИ значения, подобранные глазами в
                    // игре, а не заводские из движка. Заводские (0.45 / 67.5 / 1.04,1.00,0.96 /
                    // 0.18) оставлены в комментариях: если понадобится вернуться к стоку, менять
                    // надо здесь, а не разыскивать их по исходникам движка заново.
                    { "hud_fov", 0.365f },        // сток 0.45
                    { "da_hud_pos_x", 0.f },
                    { "da_hud_pos_y", 0.f },
                    { "da_hud_pos_z", 0.f },
                    { "fov", 90.f },              // сток 67.5
                    { "rs_c_gamma", 1.1f },       // сток 1.0
                    { "rs_c_contrast", 1.0f },
                    { "rs_c_brightness", 1.0f },
                    // [DA_PORT] Тонировка. Ноль доли ACES и точка белого 1.7 — это ровно то
                    // поведение, что было зашито в шейдер до появления ручек, то есть «стандартное»
                    // здесь буквально означает «как будто их и нет».
                    { "r__tonemap_aces", 0.f },
                    { "r__tonemap_white", 1.7f },
                    // [DA_PORT] Линейное пространство — отладочная величина, стандартное = выключено.
                    // [DA_PORT] Профиль. Ноль — сток; кнопка «вернуть стандартные» ставит именно
                    // его, а не «наш»: стандартное должно значить «как в моде без нас».
                    { "r__grade_preset", 0.f },
                    { "r__linear_light", 0.f },
                    // [DA_PORT] Цвет — АВТОРСКИЕ значения Dead Air, как и в профиле 0 «Оригинал»
                    // (xrRender_console.cpp): именно их мод отгружал дефолтами консоли. «Вернуть
                    // стандартные» должно давать вид, задуманный автором, а не пустое преобразование.
                    { "r__color_base_r", 1.04f },
                    { "r__color_base_g", 1.00f },
                    { "r__color_base_b", 0.96f },
                    // [DA_PORT] Остальные две трети ASC CDL. Нейтраль у сдвига — ноль, у степени —
                    // единица; при них преобразование тождественно, картинка не меняется.
                    { "r__color_add_r", 0.f },
                    { "r__color_add_g", 0.f },
                    { "r__color_add_b", 0.f },
                    { "r__color_power_r", 1.f },
                    { "r__color_power_g", 1.f },
                    { "r__color_power_b", 1.f },
                    { "r2_vibrance_val", 0.18f },   // авторское значение мода

                    // Прицел — СТОКОВЫЕ значения Dead Air, разбор у объявлений в HUDCrosshair.cpp.
                    // Сброс возвращает привычную марку мода, а не то, что нам показалось удобным.
                    { "da_cross_style", 0.f },
                    { "da_cross_thick", 1.f },
                    { "da_cross_len", 2.f },
                    { "da_cross_gap", 0.f },
                    { "da_cross_dot", 0.f },
                    { "da_cross_outline", 0.f },
                    { "da_cross_dynamic", 1.f },
                    { "da_cross_r", 178.f },
                    { "da_cross_g", 178.f },
                    { "da_cross_b", 178.f },
                    { "da_cross_a", 178.f },
                    { "da_cross_relation", 1.f },
                };

                u32 done = 0, missing = 0, skipped = 0;
                for (const auto& d : da_defaults)
                {
                    if (!wanted(args, d.name))
                    {
                        ++skipped;
                        continue;
                    }

                    if (!Console || !Console->GetCommand(d.name))
                    {
                        // Команды может не быть — например, величины рендера при другом рендерере.
                        // Молчать нельзя: игрок нажал «вернуть стандартные», а часть не вернулась.
                        Msg("! [DA_PORT] da_tune_defaults: команда [%s] не найдена, пропущена", d.name);
                        ++missing;
                        continue;
                    }
                    string128 buf;
                    // Целочисленные команды разбирают аргумент как целое: печатать им "0.00000" значит
                    // отдать строку, которую они прочтут как 0 в лучшем случае. Дробную часть
                    // оставляем только там, где она есть.
                    const bool whole = fsimilar(d.value, floorf(d.value));
                    if (whole)
                        xr_sprintf(buf, sizeof(buf), "%s %d", d.name, int(d.value));
                    else
                        xr_sprintf(buf, sizeof(buf), "%s %.5f", d.name, d.value);
                    Console->ExecuteCommand(buf, false);
                    ++done;
                }
                Msg("~ [DA_PORT] da_tune_defaults: возвращено к стандартным значениям: %u%s%s", done,
                    skipped ? " (остальные не запрашивались)" : "",
                    missing ? " (часть пропущена, см. выше)" : "");
            }
        };
        CMD1(CCC_DaTuneDefaults, "da_tune_defaults");

        // [DA_PORT] Настраиваемое перекрестие: вид, размеры, цвет. Разбор — в HUDCrosshair.cpp.
        // Вид -1 прячет марку целиком; 0 — заводской крест, то есть по умолчанию ничего не меняется.
        extern int ps_da_cross_style, ps_da_cross_thick, ps_da_cross_len, ps_da_cross_gap;
        extern int ps_da_cross_dot, ps_da_cross_outline, ps_da_cross_dynamic;
        extern int ps_da_cross_r, ps_da_cross_g, ps_da_cross_b, ps_da_cross_a;
        extern int da_cross_style_count();
        CMD4(CCC_Integer, "da_cross_style", &ps_da_cross_style, -1, da_cross_style_count() - 1);
        CMD4(CCC_Integer, "da_cross_thick", &ps_da_cross_thick, 1, 12);
        CMD4(CCC_Integer, "da_cross_len", &ps_da_cross_len, 1, 200);
        CMD4(CCC_Integer, "da_cross_gap", &ps_da_cross_gap, 0, 200);
        CMD4(CCC_Integer, "da_cross_dot", &ps_da_cross_dot, 0, 1);
        CMD4(CCC_Integer, "da_cross_outline", &ps_da_cross_outline, 0, 1);
        CMD4(CCC_Integer, "da_cross_dynamic", &ps_da_cross_dynamic, 0, 1);
        CMD4(CCC_Integer, "da_cross_r", &ps_da_cross_r, 0, 255);
        CMD4(CCC_Integer, "da_cross_g", &ps_da_cross_g, 0, 255);
        CMD4(CCC_Integer, "da_cross_b", &ps_da_cross_b, 0, 255);
        CMD4(CCC_Integer, "da_cross_a", &ps_da_cross_a, 0, 255);
        extern int ps_da_cross_relation;
        CMD4(CCC_Integer, "da_cross_relation", &ps_da_cross_relation, 0, 1);
        // Взводится окном подгонки на время правки, в обычной игре всегда ноль.
        extern int ps_da_cross_preview;
        CMD4(CCC_DaDebugInteger, "da_cross_preview", &ps_da_cross_preview, 0, 1);

        // [DA_PORT] Точка вылета и направление пули. Имена и смысл как в движке Anomaly, чтобы
        // привычные значения работали так же. Разбор — у определения в Actor_Weapon.cpp.
        extern int g_firepos, g_firepos_zoom, g_aimpos;
        CMD4(CCC_Integer, "g_firepos", &g_firepos, 0, 1);
        CMD4(CCC_Integer, "g_firepos_zoom", &g_firepos_zoom, 0, 1);
        CMD4(CCC_Integer, "g_aimpos", &g_aimpos, 0, 1);

        // [DA_PORT] Стрелки двигают оружие прямо во время прицеливания. Разбор — у
        // da_tune_keys_handle в player_hud.cpp. Не сохраняется: это режим подбора, а не настройка.
        extern int ps_da_tune_keys;
        CMD4(CCC_DaDebugInteger, "da_tune_keys", &ps_da_tune_keys, 0, 1);

        // [DA_PORT] Комната предпросмотра: увести игрока на fake_start и настраивать при живой
        // картинке.
        //
        // Зачем отдельной командой. Ручки выше применяются на лету, но в меню настроек их не видно:
        // на время меню уровень снимается с отрисовки, и за ним пусто. Развести паузу и отрисовку
        // мы пробовали — это дало вылет в расчёте видимости на рабочем потоке: отрисовка опирается
        // на кадровое обновление уровня, а его в меню нет.
        //
        // Поэтому идём другим путём, тем же, каким сообщество закрыло этот вопрос в CS2: не
        // показывать мир за меню, а дать ОТДЕЛЬНОЕ место, где настраивают при обычной игре. У Dead
        // Air такое место уже есть — технический уровень `fake_start`, с которого начинается новая
        // игра.
        //
        // ⚠️ Внутри — ровно та команда, которую выполняет кнопка «Новая игра» в меню мода
        // (ui_main_menu.script). Ничего своего в путь запуска уровня не добавляется намеренно: это
        // единственный путь, который игра проходит каждый раз и который заведомо рабочий.
        class CCC_DaHudPreview : public IConsole_Command
        {
        public:
            CCC_DaHudPreview(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr) override
            {
                extern float ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z;
                Msg("~ [DA_PORT] комната предпросмотра: начинается НОВАЯ ИГРА на fake_start.");
                Msg("~   Текущая игра НЕ сохраняется — сохранитесь заранее, если она нужна.");
                Msg("~   Настройки положения оружия сейчас: x %.3f, y %.3f, z %.3f (метры).",
                    ps_da_hud_pos_x, ps_da_hud_pos_y, ps_da_hud_pos_z);
                Msg("~   Крутите da_hud_pos_x/y/z и hud_fov прямо в игре — видно сразу.");
                FlushLog();

                Console->Execute("start server(all/single/alife/new) client(localhost)");
            }
        };
        CMD1(CCC_DaHudPreview, "da_hud_preview");

        // [DA_PORT] Открыть окно подгонки обзора и цвета, не заходя в меню.
        //
        // Само окно живёт в скрипте (ui_da_tune.script): ползунки, вкладки и мышь — это готовые
        // виджеты интерфейса, писать их заново на C++ незачем. Здесь только вход для тех, кому
        // быстрее набрать команду, чем идти Настройки → Видео → «Настроить в игре».
        class CCC_DaTune : public IConsole_Command
        {
        public:
            CCC_DaTune(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = TRUE; }
            void Execute(pcstr) override
            {
                // Окно ложится поверх игрового интерфейса, и без загруженного уровня его некуда
                // положить. Молча промолчать нельзя: игрок решил бы, что команда сломана.
                if (!g_pGameLevel)
                {
                    Msg("! [DA_PORT] da_tune: окно работает только в игре — загрузите сохранение "
                        "или начните новую игру.");
                    Msg("~   Отдельная комната для подгонки: da_hud_preview");
                    return;
                }

                luabind::functor<void> show;
                if (GEnv.ScriptEngine->functor("ui_da_tune.show", show))
                    show();
                else
                    Msg("! [DA_PORT] da_tune: скрипт ui_da_tune не загружен — открывать нечем.");
            }
        };
        CMD1(CCC_DaTune, "da_tune");
    }
    {
        // [DA_PORT] сверить быстрый поиск ближайшей вершины с прежним полным перебором
        extern XRAICORE_API int ps_da_vertex_search_verify;
        CMD4(CCC_DaDebugInteger, "da_vertex_search_verify", &ps_da_vertex_search_verify, 0, 1);
    }
    CMD1(CCC_DaMemTest, "da_mem_test");   // [DA_PORT] авто-прогон: N загрузок подряд
    CMD1(CCC_DaMemSnap, "da_mem_snap");   // [DA_PORT] снимок посреди игры: покадровые утечки
    CMD1(CCC_DaAllocStat, "da_alloc_stat"); // [DA_PORT] счётчик выделений: нужен ли другой аллокатор
    CMD1(CCC_DaHeapGuardStat, "da_heap_guard_stat"); // [DA_PORT] карантин освобождённой памяти
    CMD1(CCC_DaHeapGuardTest, "da_heap_guard_test"); // [DA_PORT] самопроверка: НАМЕРЕННОЕ падение
    CMD1(CCC_DaListLevels, "da_list_levels");        // [DA_PORT] обход локаций: список из графа игры
    CMD1(CCC_DaLevelProbe, "da_level_probe");        // [DA_PORT] обход локаций: один заход
    CMD1(CCC_DaGrenadeTest, "da_grenade_test");      // [DA_PORT] воспроизведение вылета в физике (#66)
    CMD1(CCC_DaGrenadeStep, "da_grenade_step");      // [DA_PORT] шаги броска, зовутся сами
    CMD1(CCC_DaOrphanTest, "da_orphan_test");        // [DA_PORT] воспроизведение вылета уборки (#74)
    CMD1(CCC_DaPathStat, "da_path_stat"); // [DA_PORT] сколько узлов обходит поиск пути по уровню
    CMD1(CCC_DaAllocBench, "da_alloc_bench"); // [DA_PORT] цена операции: штуки -> миллисекунды
    CMD1(CCC_DaLuaMem, "da_lua_mem"); // [DA_PORT] мусор Lua по размерам блоков
    {
        // [DA_PORT] Размытие при прицеливании и перезарядке. По умолчанию выключено: эффект
        // приходит из данных ствола (zoom_dof / reload_dof), нравится далеко не всем, а вернуть
        // его — одна команда. Конфиги при этом не тронуты.
        extern int g_weapon_dof;
        CMD4(CCC_Integer, "g_weapon_dof", &g_weapon_dof, 0, 1);
    }
    {
        // [DA_PORT] Разбор КАЖДОГО радиационного хита по актёру: сколько пришло, сколько сняли
        // рюкзак/костюм/шлем, сколько артефакты, сколько дошло. Отвечает на вопрос «костюм не
        // работает или просто слаб» цифрами, а не рассуждением: защита ВЫЧИТАЕТСЯ, поэтому
        // источник сильнее суммы защит проходит насквозь и это штатно.
        //
        // Штатная отладочная печать в HitOutfitEffect для этого не годится: она под `bDebug`, а он
        // в релизе `#define bDebug 0`, то есть мёртв.
        extern int g_da_rad_log;
        CMD4(CCC_DaDebugInteger, "da_rad_log", &g_da_rad_log, 0, 1);
        // [DA_PORT] Разбор ЛЮБОГО хита по актёру: тип, кость, ap, сколько пришло, сколько осталось
        // после брони, и прочность костюма и шлема до и после. Отвечает на вопрос «броня не
        // изнашивается или изнашивается незаметно медленно» числами. Нулевое «пришло» — само по
        // себе диагноз: хит погашен ДО брони, в CActor::HitArtefactsOnBelt.
        extern int g_da_hit_log;
        CMD4(CCC_DaDebugInteger, "da_hit_log", &g_da_hit_log, 0, 1);
        // [DA_PORT] Перволичное тело — наша добавка поверх мода: модель актёра рисуется в главном
        // проходе, а не только в теневом. Настройка игрока, поэтому обычный CCC_Integer, с
        // сохранением: погасить её должно быть можно навсегда, а не до перезапуска.
        extern int g_da_fp_body;
        CMD4(CCC_Integer, "da_fp_body", &g_da_fp_body, 0, 1);
        // [DA_PORT] Размещение перволичного тела. Умолчания — числа Anomaly (player_hud_legs.cpp),
        // они там подобраны на живых игроках; свои подбирать поверх проверенных смысла нет.
        extern float g_da_legs_fwd;
        extern float g_da_legs_y;
        extern int g_da_legs_cam;
        CMD4(CCC_Float, "da_legs_fwd", &g_da_legs_fwd, -2.0f, 2.0f);
        CMD4(CCC_Float, "da_legs_y", &g_da_legs_y, -1.0f, 1.0f);
        CMD4(CCC_Integer, "da_legs_cam", &g_da_legs_cam, 0, 1);
        // Поправка на взгляд вниз: добавка к сдвигу назад и по высоте при взгляде под ноги.
        extern float g_da_legs_pitch_fwd;
        extern float g_da_legs_pitch_y;
        CMD4(CCC_Float, "da_legs_pitch_fwd", &g_da_legs_pitch_fwd, -2.0f, 2.0f);
        CMD4(CCC_Float, "da_legs_pitch_y", &g_da_legs_pitch_y, -1.0f, 1.0f);
        // Набор скрываемых костей: 1 шея и плечи (как в Anomaly), дальше вверх по позвоночнику.
        extern int g_da_legs_hide;
        CMD4(CCC_Integer, "da_legs_hide", &g_da_legs_hide, 0, 4);
        extern float g_da_legs_yaw_limit;
        CMD4(CCC_Float, "da_legs_yaw_limit", &g_da_legs_yaw_limit, 0.f, 180.f);
        // Экранный отчёт числами для подгонки — диагностика, в user.ltx не уходит.
        extern int g_da_fp_body_debug;
        CMD4(CCC_DaDebugInteger, "da_fp_body_debug", &g_da_fp_body_debug, 0, 1);

        // [DA_PORT] Прибор увода прицела. Разбор — у объявления переменных в player_hud.cpp,
        // как читать показания — у DaRenderAimStats в HUDManager.cpp.
        extern int g_da_aim_debug;
        CMD4(CCC_DaDebugInteger, "da_aim_debug", &g_da_aim_debug, 0, 1);
        extern int g_da_mem_probe; // [DA_PORT] выключатель автоматических отметок
        CMD4(CCC_DaDebugInteger, "da_mem_probe", &g_da_mem_probe, 0, 1);
        extern int g_da_mem_heapwalk; // [DA_PORT] обход куч: живые аллокации вместо закоммиченного
        CMD4(CCC_DaDebugInteger, "da_mem_heapwalk", &g_da_mem_heapwalk, 0, 1);
        extern int g_da_mem_trap_size; // [DA_PORT] размер блока, содержимое которого показываем
        // ⚠️ Потолок у всех трёх «размерных» крутилок ниже — 512 МБ, а не мегабайт, как было
        // сначала. Мегабайт поставили, когда охотились за блоками по 16 КБ, и он молча закрыл
        // охоту на КРУПНЫЕ блоки: попытка навести ловушку на утечку в 15 624 208 байт отвечала
        // «Invalid syntax» и ловушка оставалась невзведённой, а прогон при этом шёл целиком и
        // выглядел исправным. Диапазон крутилки — это тоже интерфейс, и слишком узкий диапазон
        // выглядит как поломка инструмента.
        CMD4(CCC_DaDebugInteger, "da_mem_trap_size", &g_da_mem_trap_size, 0, 512 * 1024 * 1024);
        // [DA_PORT] Ловушка в самом аллокаторе: печатает стек в момент выделения блока заданного
        // размера. Ради неё всё и затевалось — оба известных пула пакетов оказались пусты, и кто
        // держит блоки, статикой не находится.
        extern int g_da_alloc_trap_size;
        extern int g_da_alloc_trap_left;
        CMD4(CCC_DaDebugInteger, "da_alloc_trap_size", &g_da_alloc_trap_size, 0, 512 * 1024 * 1024);
        CMD4(CCC_DaDebugInteger, "da_alloc_trap_count", &g_da_alloc_trap_left, 0, 64);
        extern int g_da_alloc_trap_slack; // допуск: блок в куче больше запрошенного на заголовок
        // Допуск тоже расширен: узкое окно нужно для точного размера, широкое — когда размер
        // известен только вилкой («блок где-то между 8 и 16 МБ»), и это рабочий приём.
        CMD4(CCC_DaDebugInteger, "da_alloc_trap_slack", &g_da_alloc_trap_slack, 0, 16 * 1024 * 1024);
        extern int g_da_alloc_trap_every; // прореживание: брать каждое N-е совпадение
        CMD4(CCC_DaDebugInteger, "da_alloc_trap_every", &g_da_alloc_trap_every, 1, 100000);
    }

    // game
    CMD3(CCC_Mask, "g_crouch_toggle", &psActorFlags, AF_CROUCH_TOGGLE);
    CMD1(CCC_GameDifficulty, "g_game_difficulty");
    CMD1(CCC_GameLanguage, "g_language");
    CMD3(CCC_String, "g_language_ltx", CStringTable::LanguageIDInLTX, std::size(CStringTable::LanguageIDInLTX));

    CMD3(CCC_Mask, "g_backrun", &psActorFlags, AF_RUN_BACKWARD);

    CMD3(CCC_Mask, "g_multi_item_pickup", &psActorFlags, AF_MULTI_ITEM_PICKUP);

    // alife
#ifdef DEBUG
    CMD1(CCC_ALifePath, "al_path"); // build path

#endif // DEBUG

    CMD1(CCC_ALifeSave, "save"); // save game
    CMD1(CCC_ALifeLoadFrom, "load"); // load game from ...
    CMD1(CCC_LoadLastSave, "load_last_save"); // load last saved game from ...
    CMD1(CCC_CameraYawRotate, "cam_yaw_rotate"); // [DA_PORT] замерочное вращение камеры

    CMD1(CCC_FlushLog, "flush"); // flush log
    CMD1(CCC_ClearLog, "clear_log");

#ifndef MASTER_GOLD
    CMD1(CCC_ALifeTimeFactor, "al_time_factor"); // set time factor
    CMD1(CCC_ALifeSwitchDistance, "al_switch_distance"); // set switch distance
    CMD1(CCC_ALifeProcessTime, "al_process_time"); // set process time
    CMD1(CCC_ALifeObjectsPerUpdate, "al_objects_per_update"); // set process time
    CMD1(CCC_ALifeSwitchFactor, "al_switch_factor"); // set switch factor
#endif // #ifndef MASTER_GOLD

    CMD3(CCC_Mask, "hud_weapon", &psHUD_Flags, HUD_WEAPON);
    CMD3(CCC_Mask, "hud_info", &psHUD_Flags, HUD_INFO);
    CMD3(CCC_Mask, "hud_draw", &psHUD_Flags, HUD_DRAW);

    // [DA_PORT] Dead Air compatibility aliases
    // "hud_draw_info" is a flag of its own, not a second name for "hud_info": it hides the bottom-left
    // readout (health, stamina, ammo, fire mode, weapon icon), while HUD_INFO only controls the
    // look-at target info. Pointing it at HUD_INFO made the option in the gameplay menu toggle the
    // wrong thing and left the readout ungated.
    CMD3(CCC_Mask, "hud_draw_info", &psHUD_Flags, HUD_DRAW_INFO);
    // [DA_PORT] "hud_draw_map" used to be mapped onto the shared HUD_DRAW bit - toggling it off
    // (as DA's map/PDA scripts do) silently killed the entire main indicators HUD (health/boosts)
    // forever, since the off state got persisted to user.ltx. Give it its own dedicated bit.
    CMD3(CCC_Mask, "hud_draw_map", &psHUD_Flags, HUD_DRAW_MAP);

    // hud
    psHUD_Flags.set(HUD_CROSSHAIR, true);
    psHUD_Flags.set(HUD_WEAPON, true);
    psHUD_Flags.set(HUD_DRAW, true);
    psHUD_Flags.set(HUD_INFO, true);
    psHUD_Flags.set(HUD_DRAW_INFO, true); // [DA_PORT] bottom-left readout is on unless the player says otherwise

    CMD3(CCC_Mask, "hud_crosshair", &psHUD_Flags, HUD_CROSSHAIR);
    CMD3(CCC_Mask, "hud_crosshair_dist", &psHUD_Flags, HUD_CROSSHAIR_DIST);
    CMD3(CCC_Mask, "hud_left_handed", &psHUD_Flags, HUD_LEFT_HANDED);

    CMD4(CCC_Float, "hud_fov", &psHUD_FOV, 0.1f, 1.0f);
    // [DA_PORT] nearwall weapon-collision HUD FOV (opt-in, off by default; vars defined in HudItem.cpp)
    {
        extern int g_hud_nearwall;
        extern float g_hud_nearwall_dist_min, g_hud_nearwall_dist_max, g_hud_nearwall_target_fov,
            g_hud_nearwall_speed;
        CMD4(CCC_Integer, "hud_nearwall", &g_hud_nearwall, 0, 1);
        CMD4(CCC_Float, "hud_nearwall_dist_min", &g_hud_nearwall_dist_min, 0.0f, 2.0f);
        CMD4(CCC_Float, "hud_nearwall_dist_max", &g_hud_nearwall_dist_max, 0.1f, 5.0f);
        CMD4(CCC_Float, "hud_nearwall_target_fov", &g_hud_nearwall_target_fov, 0.05f, 1.0f);
        CMD4(CCC_Float, "hud_nearwall_speed", &g_hud_nearwall_speed, 0.5f, 40.0f);
    }
    CMD4(CCC_Float, "fov", &g_fov, 5.0f, 180.0f);
    CMD4(CCC_Float, "scope_fov", &g_scope_fov, 5.0f, 180.0f); // [DA_PORT] CoC-Xray compat

    // [DA_PORT] Weapons pick up breakages while firing - Dead Air's own mechanic, which its author left
    // commented out. Off by default: it changes the balance of every firefight, so it is opted into.
    // Deliberately outside the MASTER_GOLD cheat block below - this is a gameplay setting, not a cheat.
    {
        extern int g_weapon_malfunctions;
        CMD4(CCC_Integer, "g_weapon_malfunctions", &g_weapon_malfunctions, 0, 1);
    }

    // Demo
    CMD1(CCC_DemoPlay, "demo_play");
    CMD1(CCC_DemoRecord, "demo_record");
    CMD1(CCC_DemoRecordSetPos, "demo_set_cam_position");

#ifndef MASTER_GOLD
    // ai
    CMD3(CCC_Mask, "mt_ai_vision", &g_mt_config, mtAiVision);
    CMD3(CCC_Mask, "mt_level_path", &g_mt_config, mtLevelPath);
    CMD3(CCC_Mask, "mt_detail_path", &g_mt_config, mtDetailPath);
    CMD3(CCC_Mask, "mt_object_handler", &g_mt_config, mtObjectHandler);
    CMD3(CCC_Mask, "mt_sound_player", &g_mt_config, mtSoundPlayer);
    CMD3(CCC_Mask, "mt_bullets", &g_mt_config, mtBullets);
    CMD3(CCC_Mask, "mt_script_gc", &g_mt_config, mtLUA_GC);
    CMD3(CCC_Mask, "mt_level_sounds", &g_mt_config, mtLevelSounds);
    CMD3(CCC_Mask, "mt_alife", &g_mt_config, mtALife);
    CMD3(CCC_Mask, "mt_map", &g_mt_config, mtMap);
#endif // MASTER_GOLD

#ifndef MASTER_GOLD
    CMD3(CCC_Mask, "ai_obstacles_avoiding", &psAI_Flags, aiObstaclesAvoiding);
    CMD3(CCC_Mask, "ai_obstacles_avoiding_static", &psAI_Flags, aiObstaclesAvoidingStatic);
    CMD3(CCC_Mask, "ai_use_smart_covers", &psAI_Flags, aiUseSmartCovers);
    CMD3(CCC_Mask, "ai_use_smart_covers_animation_slots", &psAI_Flags, (u32)aiUseSmartCoversAnimationSlot);
    CMD4(CCC_Float, "ai_smart_factor", &g_smart_cover_factor, 0.f, 1000000.f);
#endif // MASTER_GOLD

    CMD3(CCC_Mask, "lua_debug", &g_LuaDebug, 1);
    CMD4(CCC_Integer, "lua_dump_depth", &g_LuaDumpDepth, 0, 16);

    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_STATUS);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_START);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_START_SAMPLING_MODE);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_START_HOOK_MODE);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_STOP);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_RESET);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_LOG);
    CMD1(CCC_LuaProfiler, CCC_LuaProfiler::COMMAND_LUA_PROFILER_SAVE);

    CMD1(CCC_LuaGCMethod, "lua_gc_method");
    CMD4(CCC_Integer, "lua_gcstep", &psLUA_GCSTEP, 1, 1000);
    CMD4(CCC_Integer, "lua_gc_timeout", &psLUA_GCTIMEOUT, 1000, 16000);
    CMD4(CCC_Integer, "lua_gc_ceiling", &psLUA_GCCEIL, 0, 1024); // [DA_PORT] потолок кучи Lua, МБ
    CMD4(CCC_Integer, "lua_gc_mul", &psLUA_GCMUL, 50, 1000);  // [DA_PORT] % от скорости выделения
    CMD4(CCC_Integer, "lua_gc_min", &psLUA_GCMIN, 0, 4096);   // [DA_PORT] нижняя граница шага, КБ
    CMD4(CCC_Integer, "lua_gc_max", &psLUA_GCMAX, 16, 16384); // [DA_PORT] верхняя граница шага, КБ

#ifdef DEBUG
    CMD3(CCC_Mask, "ai_debug", &psAI_Flags, aiDebug);
    CMD3(CCC_Mask, "ai_dbg_brain", &psAI_Flags, aiBrain);
    CMD3(CCC_Mask, "ai_dbg_motion", &psAI_Flags, aiMotion);
    CMD3(CCC_Mask, "ai_dbg_frustum", &psAI_Flags, aiFrustum);
    CMD3(CCC_Mask, "ai_dbg_funcs", &psAI_Flags, aiFuncs);
    CMD3(CCC_Mask, "ai_dbg_alife", &psAI_Flags, aiALife);
    CMD3(CCC_Mask, "ai_dbg_goap", &psAI_Flags, aiGOAP);
    CMD3(CCC_Mask, "ai_dbg_goap_script", &psAI_Flags, aiGOAPScript);
    CMD3(CCC_Mask, "ai_dbg_goap_object", &psAI_Flags, aiGOAPObject);
    CMD3(CCC_Mask, "ai_dbg_cover", &psAI_Flags, aiCover);
    CMD3(CCC_Mask, "ai_dbg_anim", &psAI_Flags, aiAnimation);
    CMD3(CCC_Mask, "ai_dbg_vision", &psAI_Flags, aiVision);
    CMD3(CCC_Mask, "ai_dbg_monster", &psAI_Flags, aiMonsterDebug);
    CMD3(CCC_Mask, "ai_dbg_stalker", &psAI_Flags, aiStalker);
    CMD3(CCC_Mask, "ai_stats", &psAI_Flags, aiStats);
    CMD3(CCC_Mask, "ai_dbg_destroy", &psAI_Flags, aiDestroy);
    CMD3(CCC_Mask, "ai_dbg_serialize", &psAI_Flags, aiSerialize);
    CMD3(CCC_Mask, "ai_dbg_dialogs", &psAI_Flags, aiDialogs);
    CMD3(CCC_Mask, "ai_dbg_infoportion", &psAI_Flags, aiInfoPortion);

    CMD3(CCC_Mask, "ai_draw_game_graph", &psAI_Flags, aiDrawGameGraph);
    CMD3(CCC_Mask, "ai_draw_game_graph_stalkers", &psAI_Flags, aiDrawGameGraphStalkers);
    CMD3(CCC_Mask, "ai_draw_game_graph_objects", &psAI_Flags, aiDrawGameGraphObjects);
    CMD3(CCC_Mask, "ai_draw_game_graph_real_pos", &psAI_Flags, aiDrawGameGraphRealPos);

    // XXX: register from script engine
    // CMD3(CCC_Mask,				"lua_nil_object_access",	&psAI_Flags,	aiNilObjectAccess);

    CMD3(CCC_Mask, "ai_draw_visibility_rays", &psAI_Flags, aiDrawVisibilityRays);
    CMD3(CCC_Mask, "ai_animation_stats", &psAI_Flags, aiAnimationStats);

    /////////////////////////////////////////////HIT ANIMATION////////////////////////////////////////////////////
    // float						power_factor				= 2.f;
    // float						rotational_power_factor		= 3.f;
    // float						side_sensitivity_threshold	= 0.2f;
    // float						anim_channel_factor			= 3.f;

    CMD4(CCC_Float, "hit_anims_power", &ghit_anims_params.power_factor, 0.0f, 100.0f);
    CMD4(CCC_Float, "hit_anims_rotational_power", &ghit_anims_params.rotational_power_factor, 0.0f, 100.0f);
    CMD4(CCC_Float, "hit_anims_side_sensitivity_threshold", &ghit_anims_params.side_sensitivity_threshold, 0.0f, 10.0f);
    CMD4(CCC_Float, "hit_anims_channel_factor", &ghit_anims_params.anim_channel_factor, 0.0f, 100.0f);
    // float	block_blend					= 0.1f;
    // float	reduce_blend				= 0.5f;
    // float	reduce_power_factor			= 0.5f;
    CMD4(CCC_Float, "hit_anims_block_blend", &ghit_anims_params.block_blend, 0.f, 1.f);
    CMD4(CCC_Float, "hit_anims_reduce_blend", &ghit_anims_params.reduce_blend, 0.f, 1.f);
    CMD4(CCC_Float, "hit_anims_reduce_blend_factor", &ghit_anims_params.reduce_power_factor, 0.0f, 1.0f);
    CMD4(CCC_Integer, "hit_anims_tune", &tune_hit_anims, 0, 1);
/////////////////////////////////////////////HIT ANIMATION END////////////////////////////////////////////////////

    CMD1(CCC_DumpModelBones, "debug_dump_model_bones");

    CMD1(CCC_DrawGameGraphAll, "ai_draw_game_graph_all");
    CMD1(CCC_DrawGameGraphCurrent, "ai_draw_game_graph_current_level");
    CMD1(CCC_DrawGameGraphLevel, "ai_draw_game_graph_level");

    CMD4(CCC_Integer, "ai_dbg_inactive_time", &g_AI_inactive_time, 0, 1000000);

    CMD1(CCC_DebugNode, "ai_dbg_node");
#if defined(USE_DEBUGGER)
    CMD1(CCC_ScriptDbg, "script_debug_break");
    CMD1(CCC_ScriptDbg, "script_debug_stop");
    CMD1(CCC_ScriptDbg, "script_debug_restart");
#endif // #if defined(USE_DEBUGGER)

    CMD1(CCC_ShowMonsterInfo, "ai_monster_info");
    CMD1(CCC_DebugFonts, "debug_fonts");
    CMD1(CCC_TuneAttachableItem, "dbg_adjust_attachable_item");

    CMD1(CCC_ShowAnimationStats, "ai_show_animation_stats");
#endif // DEBUG

    // ai_ignore_actor moved down to the developer block - it is a cheat like the rest.

    // Physics
    // [DA_PORT] Дальность, за которой не считается подгонка стоп к рельефу (инверсная кинематика
    // ног). Разбор — в CCharacterPhysicsSupport::in_UpdateCL. 0 возвращает прежнее поведение.
    CMD4(CCC_Float, "ph_ik_dist", &ps_da_ik_dist, 0.f, 300.f);
    CMD4(CCC_Integer, "da_path_islands", &ps_da_path_islands, 0, 1);
    CMD4(CCC_Integer, "da_path_max_nodes", &ps_da_path_max_nodes, 256, 65500);
    CMD4(CCC_Integer, "ai_dead_vision_ms", &ps_da_dead_vision_ms, 0, 10000);
    CMD4(CCC_Integer, "da_memory_dump", &ps_da_memory_dump, 0, 2000);
    CMD1(CCC_PHFps, "ph_frequency");
    CMD1(CCC_PHIterations, "ph_iterations");

#ifdef DEBUG
    CMD1(CCC_PHGravity, "ph_gravity");
    CMD4(CCC_FloatBlock, "ph_timefactor", &phTimefactor, 0.000001f, 1000.f);
    CMD4(CCC_FloatBlock, "ph_break_common_factor", &ph_console::phBreakCommonFactor, 0.f, 1000000000.f);
    CMD4(CCC_FloatBlock, "ph_rigid_break_weapon_factor", &ph_console::phRigidBreakWeaponFactor, 0.f, 1000000000.f);
    CMD4(CCC_Integer, "ph_tri_clear_disable_count", &ph_console::ph_tri_clear_disable_count, 0, 255);
    CMD4(CCC_FloatBlock, "ph_tri_query_ex_aabb_rate", &ph_console::ph_tri_query_ex_aabb_rate, 1.01f, 3.f);
#endif // DEBUG

    // [DA_PORT] Developer commands: registered only when the game was started with "-dev".
    //
    // The original hid these by building ReleaseMasterGold, and that would work here too - but that
    // configuration also compiles out exceptions, and with them the recovery from a Lua error, which
    // for a mod of this size costs far more than the commands are worth. See da_dev_mode() in
    // Engine.h. Not registering them gives the same result from the player's side: the console
    // answers "unknown command".
    if (da_dev_mode())
    {
        CMD1(CCC_JumpToLevel, "jump_to_level");
        CMD3(CCC_Mask, "g_god", &psActorFlags, AF_GODMODE);
        CMD1(CCC_ToggleNoClip, "g_no_clip");
        CMD3(CCC_Mask, "g_unlimitedammo", &psActorFlags, AF_UNLIMITEDAMMO);
        CMD1(CCC_Spawn, "g_spawn");
        CMD1(CCC_SpawnToInventory, "g_spawn_to_inventory");
        CMD1(CCC_Script, "run_script");
        CMD1(CCC_ScriptCommand, "run_string");
        CMD3(CCC_Mask, "ai_ignore_actor", &psAI_Flags, aiIgnoreActor);
        Msg("~ [DA_PORT] developer mode: cheat and script commands registered");
    }

    // [DA_PORT] Ход времени доступен в обычной игре, а не только в режиме разработчика: это не читерская
    // команда вроде бессмертия или спавна, а настройка темпа — ей пользуются и при обычной игре, и при
    // проверке всего, что завязано на сутки (погода, торговцы, выбросы). Значение сохраняется в user.ltx.
    CMD1(CCC_TimeFactor, "time_factor");

    CMD3(CCC_Mask, "g_autopickup", &psActorFlags, AF_AUTOPICKUP);
    CMD3(CCC_Mask, "g_dynamic_music", &psActorFlags, AF_DYNAMIC_MUSIC);
    CMD3(CCC_Mask, "g_important_save", &psActorFlags, AF_IMPORTANT_SAVE);
    CMD3(CCC_Mask, "g_loading_stages", &psActorFlags, AF_LOADING_STAGES);
    CMD3(CCC_Mask, "g_always_use_attitude_sensors", &psActorFlags, AF_ALWAYS_USE_ATTITUDE_SENSORS);
    CMD3(CCC_Mask, "g_use_tracers", &psActorFlags, AF_USE_TRACERS);

    CMD4(CCC_Integer, "g_inv_highlight_equipped", &g_inv_highlight_equipped, 0, 1);
    CMD4(CCC_Integer, "g_first_person_death", &g_first_person_death, 0, 1);
    CMD4(CCC_Integer, "g_unload_ammo_after_pick_up", &g_auto_ammo_unload, 0, 1);
    CMD4(CCC_Integer, "g_normalize_mouse_sens", &g_normalize_mouse_sens, 0, 1);
    CMD4(CCC_Integer, "g_normalize_upgrade_mouse_sens", &g_normalize_upgrade_mouse_sens, 0, 1);

    CMD4(CCC_Float, "g_look_intensity_min", &psLookIntensityMin, 10.f, 100.f);
    CMD4(CCC_Float, "g_look_intensity_max", &psLookIntensityMax, 10.f, 100.f);
    CMD4(CCC_Float, "g_look_intensity_step", &psLookIntensityStep, 0.f, 10.f);

    CMD4(CCC_Float, "g_cursor_intensity_min", &psCursorIntensityMin, 1.f, 100.f);
    CMD4(CCC_Float, "g_cursor_intensity_max", &psCursorIntensityMax, 1.f, 100.f);
    CMD4(CCC_Float, "g_cursor_intensity_step", &psCursorIntensityStep, 0.f, 10.f);

    CMD1(CCC_CleanupTasks, "dbg_cleanup_tasks");

#ifdef DEBUG
    CMD1(CCC_ShowSmartCastStats, "show_smart_cast_stats");
    CMD1(CCC_ClearSmartCastStats, "clear_smart_cast_stats");

    CMD3(CCC_Mask, "dbg_draw_actor_alive", &dbg_net_Draw_Flags, dbg_draw_actor_alive);
    CMD3(CCC_Mask, "dbg_draw_actor_dead", &dbg_net_Draw_Flags, dbg_draw_actor_dead);
    CMD3(CCC_Mask, "dbg_draw_customzone", &dbg_net_Draw_Flags, dbg_draw_customzone);
    CMD3(CCC_Mask, "dbg_draw_teamzone", &dbg_net_Draw_Flags, dbg_draw_teamzone);
    CMD3(CCC_Mask, "dbg_draw_invitem", &dbg_net_Draw_Flags, dbg_draw_invitem);
    CMD3(CCC_Mask, "dbg_draw_actor_phys", &dbg_net_Draw_Flags, dbg_draw_actor_phys);
    CMD3(CCC_Mask, "dbg_draw_customdetector", &dbg_net_Draw_Flags, dbg_draw_customdetector);
    CMD3(CCC_Mask, "dbg_destroy", &dbg_net_Draw_Flags, dbg_destroy);
    CMD3(CCC_Mask, "dbg_draw_autopickupbox", &dbg_net_Draw_Flags, dbg_draw_autopickupbox);
    CMD3(CCC_Mask, "dbg_draw_rp", &dbg_net_Draw_Flags, dbg_draw_rp);
    CMD3(CCC_Mask, "dbg_draw_climbable", &dbg_net_Draw_Flags, dbg_draw_climbable);
    CMD3(CCC_Mask, "dbg_draw_skeleton", &dbg_net_Draw_Flags, dbg_draw_skeleton);

    CMD3(CCC_Mask, "dbg_draw_ph_contacts", &ph_dbg_draw_mask, phDbgDrawContacts);
    CMD3(CCC_Mask, "dbg_draw_ph_enabled_aabbs", &ph_dbg_draw_mask, phDbgDrawEnabledAABBS);
    CMD3(CCC_Mask, "dbg_draw_ph_intersected_tries", &ph_dbg_draw_mask, phDBgDrawIntersectedTries);
    CMD3(CCC_Mask, "dbg_draw_ph_saved_tries", &ph_dbg_draw_mask, phDbgDrawSavedTries);
    CMD3(CCC_Mask, "dbg_draw_ph_tri_trace", &ph_dbg_draw_mask, phDbgDrawTriTrace);
    CMD3(CCC_Mask, "dbg_draw_ph_positive_tries", &ph_dbg_draw_mask, phDBgDrawPositiveTries);
    CMD3(CCC_Mask, "dbg_draw_ph_negative_tries", &ph_dbg_draw_mask, phDBgDrawNegativeTries);
    CMD3(CCC_Mask, "dbg_draw_ph_tri_test_aabb", &ph_dbg_draw_mask, phDbgDrawTriTestAABB);
    CMD3(CCC_Mask, "dbg_draw_ph_tries_changes_sign", &ph_dbg_draw_mask, phDBgDrawTriesChangesSign);
    CMD3(CCC_Mask, "dbg_draw_ph_tri_point", &ph_dbg_draw_mask, phDbgDrawTriPoint);
    CMD3(CCC_Mask, "dbg_draw_ph_explosion_position", &ph_dbg_draw_mask, phDbgDrawExplosionPos);
    CMD3(CCC_Mask, "dbg_draw_ph_statistics", &ph_dbg_draw_mask, phDbgDrawObjectStatistics);
    CMD3(CCC_Mask, "dbg_draw_ph_mass_centres", &ph_dbg_draw_mask, phDbgDrawMassCenters);
    CMD3(CCC_Mask, "dbg_draw_ph_death_boxes", &ph_dbg_draw_mask, phDbgDrawDeathActivationBox);
    CMD3(CCC_Mask, "dbg_draw_ph_hit_app_pos", &ph_dbg_draw_mask, phHitApplicationPoints);
    CMD3(CCC_Mask, "dbg_draw_ph_cashed_tries_stats", &ph_dbg_draw_mask, phDbgDrawCashedTriesStat);
    CMD3(CCC_Mask, "dbg_draw_ph_car_dynamics", &ph_dbg_draw_mask, phDbgDrawCarDynamics);
    CMD3(CCC_Mask, "dbg_draw_ph_car_plots", &ph_dbg_draw_mask, phDbgDrawCarPlots);
    CMD3(CCC_Mask, "dbg_ph_ladder", &ph_dbg_draw_mask, phDbgLadder);
    CMD3(CCC_Mask, "dbg_draw_ph_explosions", &ph_dbg_draw_mask, phDbgDrawExplosions);
    CMD3(CCC_Mask, "dbg_draw_car_plots_all_trans", &ph_dbg_draw_mask, phDbgDrawCarAllTrnsm);
    CMD3(CCC_Mask, "dbg_draw_ph_zbuffer_disable", &ph_dbg_draw_mask, phDbgDrawZDisable);
    CMD3(CCC_Mask, "dbg_ph_obj_collision_damage", &ph_dbg_draw_mask, phDbgDispObjCollisionDammage);
    CMD_RADIOGROUPMASK2("dbg_ph_ai_always_phmove", &ph_dbg_draw_mask, phDbgAlwaysUseAiPhMove, "dbg_ph_ai_never_phmove",
        &ph_dbg_draw_mask, phDbgNeverUseAiPhMove);
    CMD3(CCC_Mask, "dbg_ph_ik", &ph_dbg_draw_mask, phDbgIK);
    CMD3(CCC_Mask, "dbg_ph_ik_off", &ph_dbg_draw_mask1, phDbgIKOff);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_goal", &ph_dbg_draw_mask, phDbgDrawIKGoal);
    CMD3(CCC_Mask, "dbg_ph_ik_limits", &ph_dbg_draw_mask, phDbgIKLimits);
    CMD3(CCC_Mask, "dbg_ph_character_control", &ph_dbg_draw_mask, phDbgCharacterControl);
    CMD3(CCC_Mask, "dbg_draw_ph_ray_motions", &ph_dbg_draw_mask, phDbgDrawRayMotions);
    CMD4(CCC_Float, "dbg_ph_vel_collid_damage_to_display", &dbg_vel_collid_damage_to_display, 0.f, 1000.f);
    CMD4(CCC_DbgBullets, "dbg_draw_bullet_hit", &g_bDrawBulletHit, 0, 1);
    CMD4(CCC_Integer, "dbg_draw_fb_crosshair", &g_bDrawFirstBulletCrosshair, 0, 1);
    CMD1(CCC_DbgPhTrackObj, "dbg_track_obj");
    CMD3(CCC_Mask, "dbg_ph_actor_restriction", &ph_dbg_draw_mask1, ph_m1_DbgActorRestriction);
    CMD3(CCC_Mask, "dbg_draw_ph_hit_anims", &ph_dbg_draw_mask1, phDbgHitAnims);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_limits", &ph_dbg_draw_mask1, phDbgDrawIKLimits);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_predict", &ph_dbg_draw_mask1, phDbgDrawIKPredict);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_collision", &ph_dbg_draw_mask1, phDbgDrawIKCollision);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_shift_object", &ph_dbg_draw_mask1, phDbgDrawIKSHiftObject);
    CMD3(CCC_Mask, "dbg_draw_ph_ik_blending", &ph_dbg_draw_mask1, phDbgDrawIKBlending);
    CMD1(CCC_DBGDrawCashedClear, "dbg_ph_cashed_clear");
    extern BOOL dbg_draw_character_bones;
    extern BOOL dbg_draw_character_physics;
    extern BOOL dbg_draw_character_binds;
    extern BOOL dbg_draw_character_physics_pones;
    extern BOOL ik_cam_shift;
    CMD4(CCC_Integer, "dbg_draw_character_bones", &dbg_draw_character_bones, FALSE, TRUE);
    CMD4(CCC_Integer, "dbg_draw_character_physics", &dbg_draw_character_physics, FALSE, TRUE);
    CMD4(CCC_Integer, "dbg_draw_character_binds", &dbg_draw_character_binds, FALSE, TRUE);
    CMD4(CCC_Integer, "dbg_draw_character_physics_pones", &dbg_draw_character_physics_pones, FALSE, TRUE);

    CMD4(CCC_Integer, "ik_cam_shift", &ik_cam_shift, FALSE, TRUE);

    extern float ik_cam_shift_tolerance;
    CMD4(CCC_Float, "ik_cam_shift_tolerance", &ik_cam_shift_tolerance, 0.f, 2.f);
    extern float ik_cam_shift_speed;
    CMD4(CCC_Float, "ik_cam_shift_speed", &ik_cam_shift_speed, 0.f, 1.f);
    extern float ik_cam_shift_interpolation;
    CMD4(CCC_Float, "ik_cam_shift_interpolation", &ik_cam_shift_interpolation, 1.f, 10.f);
    extern BOOL dbg_draw_doors;
    CMD4(CCC_Integer, "dbg_draw_doors", &dbg_draw_doors, FALSE, TRUE);

    /*
    extern int ik_allign_free_foot;
    extern int ik_local_blending;
    extern int ik_blend_free_foot;
    extern int ik_collide_blend;
        CMD4(CCC_Integer,	"ik_allign_free_foot"			,&ik_allign_free_foot,	0,	1);
        CMD4(CCC_Integer,	"ik_local_blending"				,&ik_local_blending,	0,	1);
        CMD4(CCC_Integer,	"ik_blend_free_foot"			,&ik_blend_free_foot,	0,	1);
        CMD4(CCC_Integer,	"ik_collide_blend"				,&ik_collide_blend,	0,	1);
    */
    extern BOOL dbg_draw_ragdoll_spawn;
    CMD4(CCC_Integer, "dbg_draw_ragdoll_spawn", &dbg_draw_ragdoll_spawn, FALSE, TRUE);
    extern BOOL debug_step_info;
    extern BOOL debug_step_info_load;
    CMD4(CCC_Integer, "debug_step_info", &debug_step_info, FALSE, TRUE);
    CMD4(CCC_Integer, "debug_step_info_load", &debug_step_info_load, FALSE, TRUE);
    extern BOOL debug_character_material_load;
    CMD4(CCC_Integer, "debug_character_material_load", &debug_character_material_load, FALSE, TRUE);
    extern XRPHYSICS_API BOOL dbg_draw_camera_collision;
    CMD4(CCC_Integer, "dbg_draw_camera_collision", &dbg_draw_camera_collision, FALSE, TRUE);
    extern XRPHYSICS_API float camera_collision_character_skin_depth;
    extern XRPHYSICS_API float camera_collision_character_shift_z;
    CMD4(CCC_FloatBlock, "camera_collision_character_shift_z", &camera_collision_character_shift_z, 0.f, 1.f);
    CMD4(CCC_FloatBlock, "camera_collision_character_skin_depth", &camera_collision_character_skin_depth, 0.f, 1.f);
    extern BOOL dbg_draw_animation_movement_controller;
    CMD4(CCC_Integer, "dbg_draw_animation_movement_controller", &dbg_draw_animation_movement_controller, FALSE, TRUE);

    /*
    enum
    {
        dbg_track_obj_blends_bp_0			= 1<< 0,
        dbg_track_obj_blends_bp_1			= 1<< 1,
        dbg_track_obj_blends_bp_2			= 1<< 2,
        dbg_track_obj_blends_bp_3			= 1<< 3,
        dbg_track_obj_blends_motion_name	= 1<< 4,
        dbg_track_obj_blends_time			= 1<< 5,
        dbg_track_obj_blends_ammount		= 1<< 6,
        dbg_track_obj_blends_mix_params		= 1<< 7,
        dbg_track_obj_blends_flags			= 1<< 8,
        dbg_track_obj_blends_state			= 1<< 9,
        dbg_track_obj_blends_dump			= 1<< 10
    };
    */
    extern Flags32 dbg_track_obj_flags;
    CMD3(CCC_Mask, "dbg_track_obj_blends_bp_0", &dbg_track_obj_flags, dbg_track_obj_blends_bp_0);
    CMD3(CCC_Mask, "dbg_track_obj_blends_bp_1", &dbg_track_obj_flags, dbg_track_obj_blends_bp_1);
    CMD3(CCC_Mask, "dbg_track_obj_blends_bp_2", &dbg_track_obj_flags, dbg_track_obj_blends_bp_2);
    CMD3(CCC_Mask, "dbg_track_obj_blends_bp_3", &dbg_track_obj_flags, dbg_track_obj_blends_bp_3);
    CMD3(CCC_Mask, "dbg_track_obj_blends_motion_name", &dbg_track_obj_flags, dbg_track_obj_blends_motion_name);
    CMD3(CCC_Mask, "dbg_track_obj_blends_time", &dbg_track_obj_flags, dbg_track_obj_blends_time);

    CMD3(CCC_Mask, "dbg_track_obj_blends_ammount", &dbg_track_obj_flags, dbg_track_obj_blends_ammount);
    CMD3(CCC_Mask, "dbg_track_obj_blends_mix_params", &dbg_track_obj_flags, dbg_track_obj_blends_mix_params);
    CMD3(CCC_Mask, "dbg_track_obj_blends_flags", &dbg_track_obj_flags, dbg_track_obj_blends_flags);
    CMD3(CCC_Mask, "dbg_track_obj_blends_state", &dbg_track_obj_flags, dbg_track_obj_blends_state);
    CMD3(CCC_Mask, "dbg_track_obj_blends_dump", &dbg_track_obj_flags, dbg_track_obj_blends_dump);

    CMD1(CCC_DbgVar, "dbg_var");

    extern float dbg_text_height_scale;
    CMD4(CCC_FloatBlock, "dbg_text_height_scale", &dbg_text_height_scale, 0.2f, 5.f);
#endif

#ifdef DEBUG
    CMD1(CCC_DumpInfos, "dump_infos");
    CMD1(CCC_DumpTasks, "dump_tasks");
    CMD1(CCC_DumpMap, "dump_map");
    CMD1(CCC_DumpCreatures, "dump_creatures");

#endif

    CMD3(CCC_Mask, "cl_dynamiccrosshair", &psHUD_Flags, HUD_CROSSHAIR_DYNAMIC);
    CMD1(CCC_MainMenu, "main_menu");
    // [DA_PORT] Registered outside the DEBUG block on purpose: we need it in the Release build we ship
    // and test with (the dump_* commands above are debug-only, which is why the first attempt at this
    // came back as "Unknown command").
    CMD1(CCC_DumpUIXml, "da_dump_ui_xml");
    CMD1(CCC_DumpHud, "da_dump_hud");
    CMD1(CCC_DumpBelt, "da_dump_belt");
    CMD1(CCC_DumpShaders, "da_dump_shaders");

#ifndef MASTER_GOLD
    CMD1(CCC_StartTimeSingle, "start_time_single");
    CMD4(CCC_TimeFactorSingle, "time_factor_single", &g_fTimeFactor, 0.f, 1000.0f);
    CMD4(CCC_Integer, "da_time_log", &g_da_time_log, 0, 1);
    CMD4(CCC_Float, "da_torch_hand_delay", &ps_da_torch_hand_delay, 0.f, 5.f);
    CMD4(CCC_Integer, "da_cell_bar_debug", &g_da_cell_bar_debug, 0, 1);
#endif // MASTER_GOLD

    g_uCommonFlags.zero();
    g_uCommonFlags.set(flAiUseTorchDynamicLights, TRUE);

    CMD3(CCC_Mask, "ai_use_torch_dynamic_lights", &g_uCommonFlags, flAiUseTorchDynamicLights);

#ifndef MASTER_GOLD
    CMD4(CCC_Vector3, "psp_cam_offset", &CCameraLook2::m_cam_offset, Fvector().set(-1000, -1000, -1000),
        Fvector().set(1000, 1000, 1000));
#endif // MASTER_GOLD

    CMD1(CCC_GSCheckForUpdates, "check_for_updates");

    // [DA_PORT] Намеренная авария для проверки отчёта о вылете.
    //
    // Штатная "crash" есть только в отладочной сборке, а проверять надо ИМЕННО ту, что уходит
    // игрокам: карту модулей, стек, запись лога. Иначе связка «движок пишет - инструмент читает»
    // остаётся непроверенной до первого настоящего вылета, а он одноразовый.
    //
    // Имя нарочно длинное и с "test": случайно не наберут. В лог перед падением идёт заметная
    // строка - по ней такой отчёт сразу отличается от настоящего и не уходит в расследование.
    CMD1(CCC_DaCrashTest, "da_crash_test");
    CMD1(CCC_DaAfterLoad, "da_after_load");

#ifdef DEBUG
    CMD1(CCC_Crash, "crash");
    CMD1(CCC_DumpObjects, "dump_all_objects");
    CMD3(CCC_String, "stalker_death_anim", dbg_stalker_death_anim, 32);
    CMD4(CCC_Integer, "death_anim_debug", &death_anim_debug, FALSE, TRUE);
    CMD4(CCC_Integer, "death_anim_velocity", &b_death_anim_velocity, FALSE, TRUE);
    CMD4(CCC_Integer, "dbg_imotion_draw_velocity", &dbg_imotion_draw_velocity, FALSE, TRUE);
    CMD4(CCC_Integer, "dbg_imotion_collide_debug", &dbg_imotion_collide_debug, FALSE, TRUE);

    CMD4(CCC_Integer, "dbg_imotion_draw_skeleton", &dbg_imotion_draw_skeleton, FALSE, TRUE);
    CMD4(CCC_Float, "dbg_imotion_draw_velocity_scale", &dbg_imotion_draw_velocity_scale, 0.0001f, 100.0f);

    CMD4(CCC_Integer, "dbg_show_ani_info", &g_ShowAnimationInfo, 0, 1);
    CMD4(CCC_Integer, "dbg_dump_physics_step", &ph_console::g_bDebugDumpPhysicsStep, 0, 1);
    CMD1(CCC_InvUpgradesHierarchy, "inv_upgrades_hierarchy");
    CMD1(CCC_InvUpgradesCurItem, "inv_upgrades_cur_item");
    CMD4(CCC_Integer, "inv_upgrades_log", &g_upgrades_log, 0, 1);
    CMD1(CCC_InvDropAllItems, "inv_drop_all_items");

    extern BOOL dbg_moving_bones_snd_player;
    CMD4(CCC_Integer, "dbg_bones_snd_player", &dbg_moving_bones_snd_player, FALSE, TRUE);
#endif
    CMD4(CCC_Float, "con_sensitive", &g_console_sensitive, 0.01f, 1.0f);
    CMD4(CCC_Integer, "wpn_aim_toggle", &b_toggle_weapon_aim, 0, 1);

// [DA_PORT] Выключатель сборщика мусора Lua — для ПРЯМОГО ОПЫТА, а не для игры.
//
// Зачем. Выбросы в 10-15 мс на объекте: замер тактов потока показал, что процессор их честно
// отработал (4.3 млн тактов на мс и в обычном вызове, и в выбросе), то есть остановки НЕТ - работа
// настоящая, просто её в сорок пять раз больше. В списке худших есть даже провод light_wire, а не
// только сталкеры, значит дело не в логике NPC, а в чём-то общем для любого объекта внутри
// scriptBinder.shedule_Update.
//
// Признак «память Lua упала» гипотезу о сборщике НЕ проверяет: инкрементальный шаг может ничего не
// освободить, а память при этом продолжит расти. Поэтому проверяем опытом: выключить сборщик и
// посмотреть, останутся ли выбросы.
//
// ⛔ Не для игры: с выключенным сборщиком память Lua растёт неограниченно. Только на время замера.
// [DA_PORT] Выключатель компиляции LuaJIT — тоже для опыта, не для игры.
//
// Что уже установлено замерами по выбросам в 10-15 мс внутри scriptBinder.shedule_Update:
//   - поток ВЫПОЛНЯЕТСЯ: 4.3 млн тактов на миллисекунду и в обычном вызове, и в выбросе;
//   - НЕ сборщик мусора: с da_lua_gc 0 выбросов осталось столько же (16 против 19);
//   - НЕ выделения памяти: при выбросе Lua тратит те же ~11 КБ, что и в обычном вызове,
//     а у галогенного светильника - 800 байт при 62 млн тактов;
//   - НЕ логика объекта: в списке худших провод, стекло, дверь, светильник.
//
// Много тактов, мало выделений, объект неважен, один выброс на пятьсот вызовов - это похоже на
// разовую работу внутри самого LuaJIT: компиляцию трассы, когда счётчик горячего участка перевалил
// порог. Платит за это тот вызов, которому не повезло.
//
// Проверка опытом: если с выключенной компиляцией выбросы исчезнут, а средняя цена вырастет -
// вопрос закрыт.
//
// ⛔ Не для игры: без компиляции все скрипты идут интерпретатором и работают заметно медленнее.
class CCC_DaLuaJIT final : public IConsole_Command
{
public:
    CCC_DaLuaJIT(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(pcstr args) override
    {
        lua_State* L = GEnv.ScriptEngine->lua();
        if (!L)
        {
            Msg("! [DA_PORT] компиляция: скриптовая машина ещё не создана");
            return;
        }

        if (!args || !args[0])
        {
            Msg("~ [DA_PORT] da_lua_jit 0 - выключить компиляцию LuaJIT, 1 - вернуть");
            return;
        }

        const bool on = atoi(args) != 0;
        const int mode = LUAJIT_MODE_ENGINE | (on ? LUAJIT_MODE_ON : LUAJIT_MODE_OFF);
        const int rc = luaJIT_setmode(L, 0, mode);
        Msg("~ [DA_PORT] компиляция LuaJIT %s (код %d). Только для замера!", on ? "возвращена" : "ВЫКЛЮЧЕНА", rc);
    }
};

class CCC_DaLuaGC final : public IConsole_Command
{
public:
    CCC_DaLuaGC(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }

    void Execute(pcstr args) override
    {
        lua_State* L = GEnv.ScriptEngine->lua();
        if (!L)
        {
            Msg("! [DA_PORT] сборщик: скриптовая машина ещё не создана");
            return;
        }

        const int kb = lua_gc(L, LUA_GCCOUNT, 0);
        if (!args || !args[0])
        {
            Msg("~ [DA_PORT] память Lua: %d КБ. da_lua_gc 0 - остановить сборщик, 1 - вернуть", kb);
            return;
        }

        if (atoi(args) == 0)
        {
            lua_gc(L, LUA_GCSTOP, 0);
            Msg("~ [DA_PORT] сборщик мусора Lua ОСТАНОВЛЕН (память %d КБ). Только для замера!", kb);
        }
        else
        {
            lua_gc(L, LUA_GCRESTART, 0);
            Msg("~ [DA_PORT] сборщик мусора Lua возвращён (память %d КБ)", kb);
        }
    }
};

    CMD1(CCC_DaLuaGC, "da_lua_gc"); // [DA_PORT] опыт со сборщиком
    CMD1(CCC_DaLuaJIT, "da_lua_jit"); // [DA_PORT] опыт с компиляцией
    CMD1(CCC_UIStyle, "ui_style");
    CMD1(CCC_UIRestart, "ui_restart");
    {
        // [DA_PORT] Брать широкоформатную разметку и на узком экране. Подробности - у самой
        // переменной в xrUICore/ui_base.cpp: у Dead Air настоящий интерфейс лежит только в файлах
        // _16, а файлы без суффикса остались от основы. Ноль возвращает выбор по соотношению сторон
        // и нужен тому, кто соберёт мод с честной парой разметок. Действует со следующего открытия
        // экрана (разметка читается при создании окна), полностью - после ui_restart.
        extern XRUICORE_API int ps_ui_widescreen_layout;
        CMD4(CCC_Integer, "ui_widescreen_layout", &ps_ui_widescreen_layout, 0, 1);

        // [DA_PORT] Раскладка интерфейса на сверхшироких и «с краёв»: pillarbox ужимает UI в
        // центральные 16:9 (подробности — у переменных в xrUICore/ui_base.cpp), safe zone
        // отодвигает его от краёв на заданный процент. Действуют сразу, без перезапуска:
        // пересчёт делает UICore::UpdateLayout из рендера курсора. Крутятся из отдельной
        // вкладки «Интерфейс» экранной подгонки (ui_da_tune).
        extern XRUICORE_API int ps_ui_pillarbox;
        CMD4(CCC_Integer, "ui_pillarbox", &ps_ui_pillarbox, 0, 1);
        extern XRUICORE_API int ps_ui_safe_zone;
        CMD4(CCC_Integer, "ui_safe_zone", &ps_ui_safe_zone, 0, 20);
    }

#ifdef DEBUG
    CMD4(CCC_Float, "ai_smart_cover_animation_speed_factor", &g_smart_cover_animation_speed_factor, .1f, 10.f);
    CMD4(CCC_Float, "air_resistance_epsilon", &air_resistance_epsilon, .0f, 1.f);
#endif // #ifdef DEBUG

    CMD4(CCC_Integer, "g_sleep_time", &psActorSleepTime, 1, 24);

    CMD4(CCC_Integer, "ai_use_old_vision", &g_ai_use_old_vision, 0, 1);

    CMD4(CCC_Integer, "ai_die_in_anomaly", &g_ai_die_in_anomaly, 0, 1); //Alundaio

    CMD4(CCC_Float, "ai_aim_predict_time", &g_aim_predict_time, 0.f, 10.f);

#ifdef DEBUG
    // extern BOOL g_use_new_ballistics;
    // CMD4(CCC_Integer,	"use_new_ballistics",	&g_use_new_ballistics, 0, 1);
    extern float g_bullet_time_factor;
    CMD4(CCC_Float, "g_bullet_time_factor", &g_bullet_time_factor, 0.f, 10.f);
#endif

#ifdef DEBUG
    extern BOOL g_ai_dbg_sight;
    CMD4(CCC_Integer, "ai_dbg_sight", &g_ai_dbg_sight, 0, 1);
#endif // #ifdef DEBUG

    //Alundaio: Scoped outside DEBUG
    extern BOOL g_ai_aim_use_smooth_aim;
    CMD4(CCC_Integer, "ai_aim_use_smooth_aim", &g_ai_aim_use_smooth_aim, 0, 1);

    extern float g_ai_aim_min_speed;
    CMD4(CCC_Float, "ai_aim_min_speed", &g_ai_aim_min_speed, 0.f, 10.f * PI);

    extern float g_ai_aim_min_angle;
    CMD4(CCC_Float, "ai_aim_min_angle", &g_ai_aim_min_angle, 0.f, 10.f * PI);

    extern float g_ai_aim_max_angle;
    CMD4(CCC_Float, "ai_aim_max_angle", &g_ai_aim_max_angle, 0.f, 10.f * PI);

#ifdef DEBUG
    extern BOOL g_debug_doors;
    CMD4(CCC_Integer, "ai_debug_doors", &g_debug_doors, 0, 1);
#endif // #ifdef DEBUG

    *g_last_saved_game = 0;

    CMD3(CCC_String, "slot_0", g_quick_use_slots[0], 32);
    CMD3(CCC_String, "slot_1", g_quick_use_slots[1], 32);
    CMD3(CCC_String, "slot_2", g_quick_use_slots[2], 32);
    CMD3(CCC_String, "slot_3", g_quick_use_slots[3], 32);

    CMD4(CCC_Integer, "keypress_on_start", &g_keypress_on_start, 0, 1);
    CMD1(CCC_UI_Time_Factor, "ui_time_factor");
    CMD2(CCC_UI_Time_Dilation_Mode, "time_dilation_inventory", UITimeDilator::Inventory);
    CMD2(CCC_UI_Time_Dilation_Mode, "time_dilation_pda", UITimeDilator::Pda);

    register_mp_console_commands();
}
