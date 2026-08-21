#include "stdafx.h"
#include "IGame_Level.h"

#include "XR_IOConsole.h"
#include "xr_ioc_cmd.h"

#include "CameraManager.h"
#include "Environment.h"
#include "xr_input.h"
#include "CustomHUD.h"

#include "xr_object.h"
#include "xr_object_list.h"

xr_vector<xr_token> VidQualityToken;

extern xr_vector<xr_token> vid_monitor_token;
extern xr_map<u32, xr_vector<xr_token>> vid_mode_token;

const xr_token vid_bpp_token[] = {{"16", 16}, {"32", 32}, {0, 0}};

// [DA_PORT] Frame-rate cap offered as a list in the video options. The numeric token names are
// their own labels - no string table entry needed; only the "unlimited" row has one.
const xr_token fps_limit_token[] = {
    {"st_opt_fps_unlimited", ps_fps_limit_unlimited},
    {"30", 30}, {"60", 60}, {"75", 75}, {"90", 90}, {"120", 120}, {"150", 150},
    {"165", 165}, {"180", 180}, {"200", 200}, {"240", 240}, {"260", 260}, {"300", 300},
    {nullptr, 0},
};

void IConsole_Command::InvalidSyntax()
{
    TInfo I;
    Info(I);
    Msg("~ Invalid syntax in call to '%s'", cName);
    Msg("~ Valid arguments: %s", I);
}

//-----------------------------------------------------------------------

void IConsole_Command::add_to_LRU(shared_str const& arg)
{
    if (arg.size() == 0 || bEmptyArgsHandled)
    {
        return;
    }

    bool dup = (std::find(m_LRU.begin(), m_LRU.end(), arg) != m_LRU.end());
    if (!dup)
    {
        m_LRU.push_back(arg);
        if (m_LRU.size() > LRU_MAX_COUNT)
        {
            m_LRU.erase(m_LRU.begin());
        }
    }
}

void IConsole_Command::add_LRU_to_tips(vecTips& tips)
{
    vecLRU::reverse_iterator it_rb = m_LRU.rbegin();
    vecLRU::reverse_iterator it_re = m_LRU.rend();
    for (; it_rb != it_re; ++it_rb)
    {
        tips.push_back((*it_rb));
    }
}

// =======================================================

class CCC_Quit : public IConsole_Command
{
public:
    CCC_Quit(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args)
    {
        // TerminateProcess(GetCurrentProcess(),0);
        Console->Hide();
        Engine.Event.Defer("KERNEL:disconnect");
        Engine.Event.Defer("KERNEL:quit");
    }
};
//-----------------------------------------------------------------------
class CCC_DbgStrCheck : public IConsole_Command
{
public:
    CCC_DbgStrCheck(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args) { g_pStringContainer->verify(); }
};

class CCC_DbgStrDump : public IConsole_Command
{
public:
    CCC_DbgStrDump(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args) { g_pStringContainer->dump(); }
};
//-----------------------------------------------------------------------
class CCC_E_Dump : public IConsole_Command
{
public:
    CCC_E_Dump(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args) { Engine.Event.Dump(); }
};
class CCC_E_Signal : public IConsole_Command
{
public:
    CCC_E_Signal(pcstr N) : IConsole_Command(N){};
    virtual void Execute(pcstr args)
    {
        char Event[128], Param[128];
        Event[0] = 0;
        Param[0] = 0;
        sscanf(args, "%[^,],%s", Event, Param);
        Engine.Event.Signal(Event, (u64)Param);
    }
};

//-----------------------------------------------------------------------
class CCC_Help : public IConsole_Command
{
public:
    CCC_Help(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args)
    {
        Log("- --- Command listing: start ---");
        for (const auto [name, command] : Console->Commands)
        {
            IConsole_Command& C = *command;
            TStatus status;
            C.GetStatus(status);
            TInfo info;
            C.Info(info);

            Msg("%-20s (%-10s) --- %s", C.Name(), status, info);
        }
        Log("Key: Ctrl + A         === Select all ");
        Log("Key: Ctrl + C         === Copy to clipboard ");
        Log("Key: Ctrl + V         === Paste from clipboard ");
        Log("Key: Ctrl + X         === Cut to clipboard ");
        Log("Key: Ctrl + Z         === Undo ");
        Log("Key: Ctrl + Insert    === Copy to clipboard ");
        Log("Key: Shift + Insert   === Paste from clipboard ");
        Log("Key: Shift + Delete   === Cut to clipboard ");
        Log("Key: Insert           === Toggle mode <Insert> ");
        Log("Key: Back / Delete          === Delete symbol left / right ");

        Log("Key: Up   / Down            === Prev / Next command in tips list ");
        Log("Key: Ctrl + Up / Ctrl + Down === Prev / Next executing command ");
        Log("Key: Left, Right, Home, End {+Shift/+Ctrl}       === Navigation in text ");
        Log("Key: PageUp / PageDown      === Scrolling history ");
        Log("Key: Tab  / Shift + Tab     === Next / Prev possible command from list");
        Log("Key: Enter  / NumEnter      === Execute current command ");

        Log("- --- Command listing: end ----");
    }
};

XRCORE_API void _dump_open_files(int mode);
class CCC_DumpOpenFiles : public IConsole_Command
{
public:
    CCC_DumpOpenFiles(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = false; };
    virtual void Execute(pcstr args)
    {
        int _mode = atoi(args);
        _dump_open_files(_mode);
    }
};

//-----------------------------------------------------------------------
class CCC_SaveCFG : public IConsole_Command
{
public:
    CCC_SaveCFG(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args)
    {
        string_path cfg_full_name;
        xr_strcpy(cfg_full_name, (xr_strlen(args) > 0) ? args : Console->ConfigFile);

        bool b_abs_name = xr_strlen(cfg_full_name) > 2 && cfg_full_name[1] == ':';

        if (!b_abs_name)
            FS.update_path(cfg_full_name, "$app_data_root$", cfg_full_name);

        if (strext(cfg_full_name))
            *strext(cfg_full_name) = 0;
        xr_strcat(cfg_full_name, ".ltx");

        bool b_allow = true;
#if defined(XR_PLATFORM_WINDOWS)
        if (FS.exist(cfg_full_name))
            b_allow = SetFileAttributes(cfg_full_name, FILE_ATTRIBUTE_NORMAL);
#endif
        if (b_allow)
        {
            IWriter* F = FS.w_open(cfg_full_name);
            for (const auto [name, command] : Console->Commands)
                command->Save(F);
            FS.w_close(F);
            Msg("Config-file [%s] saved successfully", cfg_full_name);
        }
        else
            Msg("! Cannot store config file [%s]", cfg_full_name);
    }
};
CCC_LoadCFG::CCC_LoadCFG(pcstr N) : IConsole_Command(N){};

void CCC_LoadCFG::Execute(pcstr args)
{
    Msg("Executing config-script \"%s\"...", args);
    string_path cfg_name;

    xr_strcpy(cfg_name, args);
    if (strext(cfg_name))
        *strext(cfg_name) = 0;
    xr_strcat(cfg_name, ".ltx");

    string_path cfg_full_name;

    FS.update_path(cfg_full_name, "$app_data_root$", cfg_name);

    if (!FS.exist(cfg_full_name))
        FS.update_path(cfg_full_name, "$fs_root$", cfg_name);

    if (!FS.exist(cfg_full_name))
        xr_strcpy(cfg_full_name, cfg_name);

    IReader* F = FS.r_open(cfg_full_name);

    string1024 str;
    if (F != nullptr)
    {
        while (!F->eof())
        {
            F->r_string(str, sizeof(str));

            // [DA_PORT] Skip comment lines. Every other .ltx in the game marks comments with ';' and the
            // LTX parser strips them - but this loop hands each line straight to the console, which has
            // never heard of comments. A documented config script therefore reported one
            // "! Unknown command:  ;" per comment line: twenty-one on every startup from the quality
            // presets alone, plus more from the ready-made sets in gamedata/configs.
            //
            // Known noise in a log is not harmless. It is exactly what hides the lines that are not
            // noise, and this file is read before anything interesting has had a chance to go wrong.
            //
            // Leading ';' only. A trailing one would have to be told apart from a legitimate argument -
            // console commands take the whole rest of the line - and no shipped script needs that.
            pcstr line = str;
            while (*line == ' ' || *line == '\t')
                ++line;
            if (*line == ';')
                continue;

            if (allow(str))
                Console->Execute(str);
        }
        FS.r_close(F);
        Msg("[%s] successfully loaded.", cfg_full_name);
    }
    else
    {
        Msg("! Cannot open script file [%s]", cfg_full_name);
    }
}

CCC_LoadCFG_custom::CCC_LoadCFG_custom(pcstr cmd) : CCC_LoadCFG(cmd) { xr_strcpy(m_cmd, cmd); };
bool CCC_LoadCFG_custom::allow(pcstr cmd) { return (cmd == strstr(cmd, m_cmd)); };
//-----------------------------------------------------------------------
class CCC_Start : public IConsole_Command
{
    void parse(pstr dest, pcstr args, pcstr name)
    {
        dest[0] = 0;
        if (strstr(args, name))
            sscanf(strstr(args, name) + xr_strlen(name), "(%[^)])", dest);
    }

    void protect_Name_strlwr(pstr str)
    {
        string4096 out;
        xr_strcpy(out, sizeof(out), str);
        xr_strlwr(str);

        pcstr name_str = "name=";
        pcstr name1 = strstr(str, name_str);
        if (!name1 || !xr_strlen(name1))
        {
            return;
        }
        int begin_p = xr_strlen(str) - xr_strlen(name1) + xr_strlen(name_str);
        if (begin_p < 1)
        {
            return;
        }

        pcstr name2 = strchr(name1, '/');
        int end_p = xr_strlen(str) - ((name2) ? xr_strlen(name2) : 0);
        if (begin_p >= end_p)
        {
            return;
        }
        for (int i = begin_p; i < end_p; ++i)
        {
            str[i] = out[i];
        }
    }

public:
    CCC_Start(pcstr N) : IConsole_Command(N) { bLowerCaseArgs = false; };
    virtual void Execute(pcstr args)
    {
        /* if (g_pGameLevel) {
         Log ("! Please disconnect/unload first");
         return;
         }
         */
        string4096 op_server, op_client, op_demo;
        op_server[0] = 0;
        op_client[0] = 0;

        parse(op_server, args, "server"); // 1. server
        parse(op_client, args, "client"); // 2. client
        parse(op_demo, args, "demo"); // 3. demo

        xr_strlwr(op_server);
        protect_Name_strlwr(op_client);

        if (!op_client[0] && strstr(op_server, "single"))
            xr_strcpy(op_client, "localhost");

        if ((0 == xr_strlen(op_client)) && (0 == xr_strlen(op_demo)))
        {
            Log("! Can't start game without client. Arguments: '%s'.", args);
            return;
        }
        if (g_pGameLevel)
            Engine.Event.Defer("KERNEL:disconnect");

        if (xr_strlen(op_demo))
        {
            Engine.Event.Defer("KERNEL:start_mp_demo", u64(xr_strdup(op_demo)), 0);
        }
        else
        {
            Engine.Event.Defer(
                "KERNEL:start", u64(xr_strlen(op_server) ? xr_strdup(op_server) : 0), u64(xr_strdup(op_client)));
        }
    }
};

class CCC_Disconnect : public IConsole_Command
{
public:
    CCC_Disconnect(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args) { Engine.Event.Defer("KERNEL:disconnect"); }
};
//-----------------------------------------------------------------------
class CCC_VID_Reset : public IConsole_Command
{
public:
    CCC_VID_Reset(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args)
    {
        if (Device.b_is_Ready)
        {
            Device.Reset();
        }
    }
};
//-----------------------------------------------------------------------
// [DA_PORT] Internal render resolution, as a percentage of the output. Every scene render target is
// built from it, so applying the change means recreating them — hence the device reset, the same thing
// a resolution change does. The UI keeps drawing at the output resolution either way.
extern ENGINE_API int ps_r__render_scale; // declared further down too; needed here

// [DA_PORT] FSR 2 quality mode. Setting it also sets the render scale, because the two are not free
// to disagree: each quality mode is defined by a fixed ratio the reconstruction was tuned for, and the
// scene must be rendered at exactly that size. Leaving them as two independent numbers meant every
// change had to be made twice, and any mismatch degraded the picture silently rather than complaining.
//
// AMD's ratios per dimension: quality 1.5x, balanced 1.7x, performance 2.0x, ultra performance 3.0x.
xr_token qxess_token[] = {
    { "ui_mm_xess_off", 0 },
    { "ui_mm_xess_ultra_quality", 1 },
    { "ui_mm_xess_quality", 2 },
    { "ui_mm_xess_balanced", 3 },
    { "ui_mm_xess_performance", 4 },
    { "ui_mm_xess_ultra_performance", 5 },
    { nullptr, 0 },
};

// [DA_PORT] ---- Upscaler registry: only one may be on ------------------------------------------
// A frame can be reconstructed by exactly one upscaler. Leaving two switched on does not merely waste
// work - they share the jitter and the velocity buffer, so the picture shakes, and the cause is nearly
// impossible to guess from the symptom. It cost several measurements during development, and it would
// reach players too, because the options menu offers each upscaler as its own separate setting.
//
// Adding a new upscaler is one line in the table below; everything else follows from it.
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__xess;
extern ENGINE_API int ps_r__dlss; // [DA_PORT]
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API u32 ps_r__upscale_preset;
// Defined further down this file; needed up here now that our own temporal AA is one of the choices.
extern ENGINE_API int ps_r__taa;

struct da_upscaler_entry
{
    u32* value;
    pcstr name;
};

static const da_upscaler_entry da_upscalers[] = {
    { (u32*)&ps_r__fsr2, "r__fsr2" },
    { (u32*)&ps_r__fsr3, "r__fsr3" },
    { (u32*)&ps_r__xess, "r__xess" },
    { (u32*)&ps_r__dlss, "r__dlss" },
    { &ps_r__upscale_preset, "r__upscale_preset" }, // FSR 1.0 - spatial, but it owns the render scale
    // [DA_PORT] Our own temporal AA belongs here too: it owns the frame's history exactly as the
    // reconstructing upscalers do, and two of those at once is what softened every moving figure until
    // it was found. Setting any upscaler from the console now switches it off, the same as the menu.
    { (u32*)&ps_r__taa, "r__taa" },
};

// [DA_PORT] Is a TEMPORAL upscaler reconstructing this frame?
//
// One function, deliberately, because the alternative kept failing. The same question was asked in
// half a dozen places as "is FSR 2 on", each written when FSR 2 was the only one, and none of them
// updated when FSR 3 and XeSS were added beside it. Every miss was silent and every symptom pointed
// somewhere else: models drew in a T-pose (the velocity buffer's consumer list), moving figures went
// soft (temporal AA ran on top of the upscaler), and distant detail never resolved at all (the camera
// generated no sub-pixel jitter, so there was nothing to reconstruct from). Three separate hunts for
// one omission. Anything asking this question should now call this and add its upscaler here once.
//
// FSR 1.0 is deliberately absent: it is a spatial filter with no history and no jitter of its own.
// [DA_PORT] Апскейлер выбран, но НЕ РАБОТАЕТ на этой машине — взводится самим бэкендом после
// нескольких неудачных диспетчей подряд. См. da_upscaler_report_failure ниже.
ENGINE_API bool g_da_upscaler_failed = false;

ENGINE_API bool da_upscaler_active()
{
    // Отказавший апскейлер обязан выключить ВСЁ, что работает ради него: джиттер, маску
    // реактивности, защиту векторов. Иначе получается худшее из двух миров — сцена дрожит на
    // субпиксельный сдвиг, а собрать её обратно некому.
    if (g_da_upscaler_failed)
        return false;

    return !!ps_r__fsr2 || !!ps_r__fsr3 || !!ps_r__xess || !!ps_r__dlss;
}

// [DA_PORT] На кадре работает ЛЮБОЙ временной фильтр - апскейлер или наша собственная TAA.
//
// Отдельный признак нужен потому, что подготовка кадра делится надвое. Часть её служит именно
// масштабированию (векторы движения, защита векторов) - это про апскейлеры. Часть служит НАКОПЛЕНИЮ
// по кадрам, и она одинаково нужна обоим: наша TAA точно так же тянет историю и точно так же читает
// маску реактивности, просто репроецирует по глубине, а не по векторам.
//
// Раньше такого различия не было, и всё висело на da_upscaler_active(). Из-за этого под TAA в маску
// реактивности не попадали вода, стекло и свечение: проходы, которые их помечают, не запускались.
// Симптом - призраки и дрожь ровно на тех поверхностях, которые под апскейлерами уже вылечены.
ENGINE_API bool da_temporal_active()
{
    return da_upscaler_active() || !!ps_r__taa;
}


// [DA_PORT] ---- Отказ апскейлера: гасим тихо и говорим громко ------------------------------------
//
// Зачем. У тестера на R9 290 (GCN 2013 года) FSR 3 «тряс весь экран». Механизм известен: диспетч не
// прошёл, отметки кадра нет, постобработка растягивает УМЕНЬШЕННУЮ сцену обычным фильтром — а сцена
// сдвинута джиттером, и растянутый кадр ездит туда-сюда ровно по нему. Со стороны это выглядит не
// как «апскейлер не поддерживается», а как «игра сломалась».
//
// Что делаем. После трёх неудач подряд перестаём джиттерить: картинка становится просто мягче
// (ровно как на FSR 1.0), тряска исчезает, а в лог уходит внятное объяснение. Выбор игрока при этом
// НЕ меняем: подменять настройку за спиной хуже, чем честно сказать, что она не работает.
//
// Три, а не одна: единичный провал бывает на пересоздании устройства и сам проходит.
ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed)
{
    static int in_a_row = 0;

    // [DA_PORT] Срывы ВРАЗБИВКУ считаются отдельно, и вот почему.
    //
    // Сторож ниже ждёт трёх провалов подряд — он лечит машину, где апскейлер не работает вовсе.
    // Но кадр, срывающийся через раз, до трёх подряд не доходит НИКОГДА, и потому молчит по
    // построению. А видно глазом именно его: на сорвавшемся кадре постобработка растягивает сырую
    // сцену вместе со сдвигом джиттера, на соседнем берёт собранную апскейлером, и картинка
    // дёргается между двумя состояниями. Симптом при этом выглядит как дрожание мира, а в логе
    // нет ни строки — то есть худший из возможных видов отказа.
    //
    // Считаем окном, а не на каждом кадре: строка в лог каждый кадр сама по себе стоила бы кадров.
    {
        static u32 frames = 0, misses = 0;
        ++frames;
        if (failed)
            ++misses;
        if (frames >= 600) // около секунды на любой разумной частоте
        {
            if (misses && !g_da_upscaler_failed)
                Msg("! [DA_PORT] %s: сорвалось кадров %u из %u. На них постобработка растягивает "
                    "сырую сцену вместе со сдвигом джиттера — это видно как дрожание картинки.",
                    who, misses, frames);
            frames = 0;
            misses = 0;
        }
    }

    if (!failed)
    {
        in_a_row = 0;
        return;
    }


    if (g_da_upscaler_failed)
        return; // уже погашен, молчим

    if (++in_a_row < 3)
        return;

    g_da_upscaler_failed = true;
    Msg("! [DA_PORT] %s не работает на этой видеокарте: три неудачных кадра подряд.", who);
    Msg("! [DA_PORT] Субпиксельный сдвиг выключен, иначе картинка тряслась бы: сцена сдвигается, а");
    Msg("! [DA_PORT] собрать её обратно некому. Сейчас кадр просто растягивается — будет мягче.");
    Msg("! [DA_PORT] Выберите другой апскейлер в настройках видео: FSR 2.0 работает на любой карте.");
}

// [DA_PORT] Мгновенный отказ: контекст библиотеки вообще не создался.
//
// Ждать три кадра тут незачем — если библиотека не поднялась, она уже не поднимется. Именно так и
// вышло у тестера на R9 290: "context creation failed (FFX_ERROR_BACKEND_API_ERROR)", после чего
// весь экран трясло, потому что субпиксельный сдвиг продолжал работать в пустоту.
ENGINE_API void da_upscaler_mark_failed(pcstr who)
{
    if (g_da_upscaler_failed)
        return;

    g_da_upscaler_failed = true;
    Msg("! [DA_PORT] %s не запустился на этой видеокарте.", who ? who : "апскейлер");
    Msg("! [DA_PORT] Субпиксельный сдвиг выключен, иначе картинка тряслась бы: сцена сдвигается, а");
    Msg("! [DA_PORT] собрать её обратно некому. Кадр просто растягивается - будет мягче, но ровно.");
    Msg("! [DA_PORT] Выберите другой апскейлер в настройках видео: FSR 2.0 работает на любой карте.");
}

// Выбор сменили — даём новому бэкенду чистый лист.
ENGINE_API void da_upscaler_clear_failure()
{
    if (g_da_upscaler_failed)
        Msg("* [DA_PORT] отметка «апскейлер не работает» снята: выбран другой режим");
    g_da_upscaler_failed = false;
    da_upscaler_report_failure(nullptr, false);
}

// [DA_PORT] ---- Сброс истории временных фильтров -------------------------------------------------
//
// Любой реконструирующий фильтр копит историю кадров и переносит её вперёд по векторам движения. Есть
// моменты, когда переносить нечего: загрузка уровня, телепорт, склейка камеры. Прошлый кадр тогда
// показывает совершенно другое место, вектора его не описывают, и фильтр несколько кадров тащит
// поверх новой картинки куски старой.
//
// Раньше здесь стояло `Device.dwFrame < 3` в каждом из четырёх проходов. Это покрывает только запуск
// сессии: dwFrame обнуляется при создании устройства и больше нигде, так что при переходе на другой
// уровень история не сбрасывалась НИ РАЗУ. Симптом смазанный - грязь в первых кадрах после загрузки,
// которую легко списать на прогрев кэшей.
static u32 g_da_history_reset_frame = 0;

ENGINE_API void da_upscaler_reset_history(pcstr why)
{
    g_da_history_reset_frame = Device.dwFrame;
    if (why)
        Msg("* [DA_PORT] temporal history discarded: %s", why);
}

ENGINE_API bool da_upscaler_history_reset()
{
    if (Device.dwFrame < 3)
        return true; // истории ещё нет

    // Телепорт уровень не перезагружает, поэтому ловим его по камере. Порог намеренно грубый: 25
    // метров за кадр - это 1500 м/с при 60 к/с, столько в игре не двигается ничто, включая транспорт.
    // Ложное срабатывание стоит одного мутного кадра, пропуск - нескольких кадров с чужой геометрией.
    //
    // По повороту камеры НЕ срабатываем сознательно: резкий разворот история переживает штатно, её
    // репроецируют по матрице, а сброс на каждом быстром развороте был бы виден постоянно.
    static u32 seen_frame = 0;
    static Fvector last_pos{};
    if (seen_frame != Device.dwFrame)
    {
        const Fvector now = Device.vCameraPosition;
        if (seen_frame && now.distance_to(last_pos) > 25.f)
            da_upscaler_reset_history("the camera jumped - teleport or a cut");
        last_pos = now;
        seen_frame = Device.dwFrame;
    }

    // Два кадра, а не один: фильтру нужен кадр, чтобы начать копить заново.
    return Device.dwFrame <= g_da_history_reset_frame + 1;
}

// Switches off every upscaler except the one being selected. Call it BEFORE the caller applies its own
// render scale, so that the winner's ratio is the one left standing.
static void da_upscaler_make_exclusive(const void* chosen)
{
    for (const auto& e : da_upscalers)
    {
        if (e.value == chosen || *e.value == 0)
            continue;
        Msg("! [DA_PORT] %s switched off - only one upscaler may reconstruct a frame, and two at once "
            "make the image shake.", e.name);
        *e.value = 0;
    }
}

// [DA_PORT] ---- What the menu actually shows: which upscaler, and how hard -----------------------
// Two controls instead of one per vendor. The old layout exposed FSR 1.0 and FSR 2 as separate lists
// plus a raw render-scale percentage, which asked the player to know that the three interact, that
// only one may be on, and which percentage belongs to which mode. These two say what a player wants
// to say: what to upscale with, and how much quality to trade.
//
// The per-vendor variables below stay exactly as they were and remain usable from the console - this
// layer only writes them, so nothing that already works had to be rewritten.
extern ENGINE_API int ps_r__upscale_sharpness;

ENGINE_API u32 ps_r__upscaler = 0;
// [DA_PORT] Our own temporal AA belongs in this list, not beside it.
//
// It is the same kind of thing as the others - a filter that owns the frame's history - and only one
// of them may. The engine already enforces that (phase_taa stands down under any upscaler), but with
// two separate controls in the menu a player could still ask for both and get an image blended twice,
// which is exactly the softness on moving figures that took an afternoon to trace. One list, one
// choice, and the question cannot be asked.
//
// Values renumbered to put it in a sensible order. Safe: the config stores the token NAME, not the
// number, so existing settings keep meaning what they meant.
ENGINE_API xr_token qupscaler_token[] = {
    // [DA_PORT] Порядок строк — это порядок пунктов в меню, и он выбран, а не унаследован: сначала то,
    // что стоит попробовать первым. «Выкл» и TAA — для тех, кто не масштабирует вовсе; дальше DLSS как
    // лучший из реконструирующих там, где он вообще доступен; затем семейство FSR по возрастанию версии;
    // XeSS последним, потому что на D3D11 он работает только на Intel Arc.
    //
    // ⚠ ЗНАЧЕНИЯ ПРИ ПЕРЕСТАНОВКЕ НЕ МЕНЯЮТСЯ, и это обязательное условие. Значение токена — это то, что
    // ложится в user.ltx, по нему же ветвится da_apply_upscaler, и по нему меню решает, показывать ли
    // ряд «Качество» (id > 1) и ряд MSAA (id <= 1): CUIComboBox кладёт в элемент списка именно
    // tok->id, а CurrentID() возвращает его, а НЕ порядковый номер строки. Поменяйте значения местами —
    // и сохранённые настройки игроков станут означать другой апскейлер.
    { "ui_mm_upscaler_off", 0 },
    { "ui_mm_upscaler_taa", 1 },
    { "ui_mm_upscaler_dlss", 6 },
    { "ui_mm_upscaler_fsr1", 2 },
    { "ui_mm_upscaler_fsr2", 3 },
    { "ui_mm_upscaler_fsr3", 4 },
    { "ui_mm_upscaler_xess", 5 },
    { nullptr, 0 },
};

// [DA_PORT] ---- Список апскейлеров подрезается под ЖЕЛЕЗО --------------------------------------
//
// Меню строит список прямо из этой таблицы токенов, поэтому вычеркнуть строку здесь - значит убрать
// пункт из меню, без правки разметки и скриптов.
//
// Зачем. Пункт, который на этой видеокарте заведомо не заработает, - это гарантированный отчёт «у
// меня трясёт» от игрока, у которого мы этого не воспроизведём. Так и вышло 01.08: FSR 3 не поднялся
// на R9 290 (карта 2013 года), а выглядело это как поломка игры. DLSS и XeSS в том же положении:
// первый требует RTX, второй на D3D11 работает только на Intel Arc - и оба всё это время честно
// показывались всем.
//
// Что НЕ прячем никогда: «Выкл», TAA, FSR 1.0 и FSR 2.0 - они работают на любой карте.
// [DA_PORT] Взводится только на время подмены апскейлера ВНУТРИ создания устройства — см. пояснение
// у сброса в конце da_apply_upscaler.
static bool s_upscaler_switch_during_create = false;

ENGINE_API void da_upscaler_set_available(u32 mask, pcstr why_hidden)
{
    // Токены с этими значениями остаются всегда.
    constexpr u32 always = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
    mask |= always;

    xr_token* src = qupscaler_token;
    xr_token* dst = qupscaler_token;
    int hidden = 0;

    for (; src->name; ++src)
    {
        if (mask & (1u << src->id))
        {
            *dst++ = *src;
            continue;
        }
        Msg("* [DA_PORT] апскейлер «%s» скрыт из меню: %s", src->name, why_hidden ? why_hidden : "не поддерживается");
        ++hidden;
    }
    dst->name = nullptr;
    dst->id = 0;

    if (!hidden)
        return;

    // Выбранный ранее мог оказаться среди скрытых - например, пакет переехал на другую машину.
    // Молча оставлять нельзя: движок бы понизил разрешение рендера под апскейлер, которого нет.
    if (0 == (mask & (1u << ps_r__upscaler)))
    {
        Msg("! [DA_PORT] выбранный апскейлер на этой видеокарте недоступен - переключаюсь на FSR 2.0.");

        // Зовёмся из построения целей рендера, то есть В СЕРЕДИНЕ создания устройства. Пометка
        // запрещает сброс устройства внутри обработчика команды - иначе игра падает ещё до меню.
        s_upscaler_switch_during_create = true;
        Console->Execute("r__upscaler ui_mm_upscaler_fsr2");
        s_upscaler_switch_during_create = false;
    }
}


// Five steps, shared by all three backends. XeSS has exactly these five; FSR 2 is given a 1.3x step of
// its own (see da_fsr2::render_size_for); FSR 1.0 is only a scale plus sharpening, so it follows them
// directly. Same wording everywhere, so the choice means the same thing whichever backend is picked.
ENGINE_API u32 ps_r__upscaler_quality = 1; // default "quality" when an upscaler is first switched on
ENGINE_API xr_token qupscaler_quality_token[] = {
    // [DA_PORT] DLAA — реконструкция в РОДНОМ разрешении, без масштабирования. Стоит первой как
    // самая качественная ступень. Разбор — у da_upscaler_scale ниже.
    { "ui_mm_upq_dlaa", 5 },
    { "ui_mm_upq_ultra_quality", 0 },
    { "ui_mm_upq_quality", 1 },
    { "ui_mm_upq_balanced", 2 },
    { "ui_mm_upq_performance", 3 },
    { "ui_mm_upq_ultra_performance", 4 },
    { nullptr, 0 },
};

// Render scale per quality step, in percent of the output: 1.3x, 1.5x, 1.7x, 2.0x, 3.0x per dimension.
//
// [DA_PORT] Шестая ступень — 100%, то есть DLAA: сглаживание без масштабирования.
//
// ЗАЧЕМ. Замер показал, что кадр НЕ упирается в закраску: снятие четверти пикселей (77% -> 67%)
// не сдвинуло gbuffer на видеокарте вовсе (1.81 -> 1.84), а всю видеокарту — на 2%. Узкое место —
// дробность подачи (2016 вызовов по 567 треугольников), и от числа пикселей она не зависит.
// Значит родное разрешение здесь почти бесплатно, а реконструкция при нём даёт то же сглаживание
// по честным пикселям, без домысливания из уменьшенного кадра.
//
// ⚠️ Ступень добавлена ПОСЛЕДНЕЙ (значение 5), а не первой, хотя в меню стоит сверху: значения
// 0..4 уже лежат в user.ltx у игроков, и сдвиг индексов молча переключил бы им качество.
//
// ⛔ ТОЛЬКО DLSS. У XeSS родного режима нет вовсе, у FSR он появился лишь в 3.1 (наш backend
// старее), FSR 1.0 — вообще не реконструкция. Остальным ступень подменяется лучшей доступной,
// см. da_apply_upscaler.
static const int da_upscaler_scale[6] = { 77, 67, 59, 50, 33, 100 };
// FSR 1.0 has no reconstruction to recover detail with, so it leans harder on sharpening the lower the
// source resolution gets. The temporal upscalers do their own and ignore this.
static const int da_upscaler_sharpen[6] = { 35, 40, 45, 55, 60, 20 };

static void da_apply_upscaler()
{
    const u32 q = (ps_r__upscaler_quality < 6) ? ps_r__upscaler_quality : 1;

    // [DA_PORT] Ступень DLAA есть только у DLSS — остальным отдаём их лучшую, а не молча худшую.
    //
    // 🪤 Просто обрезать индекс сверху (`min(q, 4)`) было бы миной: четвёрка это УЛЬТРА-СКОРОСТЬ,
    // 33% масштаба. Игрок, выбравший высшее качество и переключивший апскейлер, получил бы самое
    // мыльное изображение из возможных и не понял бы, почему.
    const u32 q_scaled = (q > 4) ? 0 : q;
    if (q != q_scaled && ps_r__upscaler != 6 && ps_r__upscaler != 1)
        Msg("! [DA_PORT] DLAA доступен только с DLSS; для этого апскейлера взято высшее качество");

    // Everything off first, one thing on after - including our own temporal AA, which is now a choice
    // in the same list. Keeping the reset complete is the whole point of the shape: a case that forgets
    // to switch off what it replaces is precisely how two temporal filters came to run at once.
    ps_r__fsr2 = 0;
    ps_r__fsr3 = 0;
    ps_r__xess = 0;
    ps_r__dlss = 0;
    ps_r__taa = 0;
    ps_r__upscale_preset = 0;

    // [DA_PORT] Новый выбор — новая попытка: прошлый отказ к нему отношения не имеет.
    da_upscaler_clear_failure();

    switch (ps_r__upscaler)
    {
    case 1: // Our own temporal AA - no upscaling, so the scene renders at full size
        ps_r__taa = 1;
        ps_r__render_scale = 100;
        ps_r__upscale_sharpness = 0;
        break;
    case 2: // FSR 1.0 - spatial, applied to the finished frame
        ps_r__upscale_preset = q_scaled + 1;
        ps_r__render_scale = da_upscaler_scale[q_scaled];
        ps_r__upscale_sharpness = da_upscaler_sharpen[q_scaled];
        break;
    // [DA_PORT] ⚠️ Резкость задаёт КАЖДАЯ ветка, и это не украшательство.
    //
    // Раньше её выставляли только три случая: наша темпоралка (в ноль), FSR 1 (из таблицы) и
    // «выключено» (в ноль). Четыре реконструирующих апскейлера не трогали её вовсе — то есть
    // наследовали то, что осталось от прошлого выбора. А «выключено» оставляет ноль, поэтому
    // достаточно было один раз выключить апскейлер и включить обратно, чтобы кадр навсегда пошёл на
    // экран без единого прохода резкости.
    //
    // Выглядит это как «разрешение будто ниже, чем 1920»: сцена честно реконструируется, но результат
    // не точат. Причину по картинке не угадать, а в настройках виден ноль, который игрок туда не
    // ставил. Нашли ровно так — по скриншоту Кладбища техники.
    case 3: // FSR 2 - temporal reconstruction
        ps_r__fsr2 = int(q_scaled + 1);
        ps_r__render_scale = da_upscaler_scale[q_scaled];
        // FSR сам гоняет RCAS от этого же ползунка, поэтому берём значение по ступени качества:
        // чем сильнее масштабирование, тем больше нужно.
        ps_r__upscale_sharpness = da_upscaler_sharpen[q_scaled];
        break;
    case 4: // FSR 3 - temporal reconstruction, community DX11 backend
        ps_r__fsr3 = int(q_scaled + 1);
        ps_r__render_scale = da_upscaler_scale[q_scaled];
        ps_r__upscale_sharpness = da_upscaler_sharpen[q_scaled];
        break;
    case 5: // XeSS - temporal reconstruction, Intel Arc only on D3D11
        ps_r__xess = int(q_scaled + 1);
        ps_r__render_scale = da_upscaler_scale[q_scaled];
        // XeSS и DLSS своей резкости НЕ имеют: у Intel её нет, NVIDIA свою объявила устаревшей и в
        // DLSS 4 игнорирует. Точит их наш проход в постобработке (r2_rendertarget_phase_PP.cpp), и
        // без внятного значения они смотрятся мягче FSR при равной реконструкции.
        //
        // 35 - выбрано по картинке в игре. Пробовали 50, оказалось избыточно: реконструкция у обоих
        // и так держит детали, и лишняя резкость лезет ореолом по кромкам.
        ps_r__upscale_sharpness = 35;
        break;
    case 6: // DLSS - temporal reconstruction, RTX only
        // [DA_PORT] q == 5 даёт ps_r__dlss == 6, а шесть уходит в ветку `default` моста NGX,
        // то есть ровно в NVSDK_NGX_PerfQuality_Value_DLAA. Ветка была написана и проверена, но
        // недостижима: ступени давали только 1..5.
        ps_r__dlss = int(q + 1);
        ps_r__render_scale = da_upscaler_scale[q];
        // При родном разрешении домысливать нечего, и прежние 35 дают ореол по кромкам.
        ps_r__upscale_sharpness = (q > 4) ? 20 : 35;
        break;
    default:
        ps_r__render_scale = 100;
        ps_r__upscale_sharpness = 0;
        break;
    }

    // [DA_PORT] MSAA cannot run next to a RECONSTRUCTING upscaler, and loses to it.
    //
    // Two reasons, the second one hard. Multisampling resolves geometry edges inside a single frame,
    // and those resolved edges are exactly the sub-pixel information FSR/XeSS/DLSS reconstruct from
    // across frames - handing them a pre-resolved image throws away what they exist to use, and the
    // extra samples are paid for anyway. And: rt_Velocity and rt_Reactive are created with one sample
    // (r2_rendertarget.cpp) while the rest of the G-buffer follows the MSAA count, and with vectors on
    // they are bound alongside it. D3D11 requires every bound target and the depth buffer to agree on
    // sample count - a mismatch there is refused silently, the worst kind of failure we have.
    //
    // ⚠ The test is `> 1`, not `!= 0`, and the difference matters: entry 1 is our own temporal AA,
    // which is NOT in da_upscaler_active() and therefore does not switch the velocity buffer on. With
    // TAA the scene pass binds no extra targets, so MSAA is free to run beside it - the two cover
    // different things, geometry edges against shading and specular. Only the reconstructing entries
    // above it are exclusive with MSAA.
    //
    // Switched off through the console rather than by poking ps_r3_msaa: the variable lives in the render
    // DLL, and its command is what makes the value stick. The reverse direction is enforced in
    // CCC_MSAA (xrRender_console.cpp); the two cannot loop, because each only acts when ITS OWN setting
    // is set and both hand the other a zero.
    if (ps_r__upscaler > 1)
        Console->Execute("r3_msaa st_opt_off");

    Device.UpdateRenderResolution();

    // ⚠️ [DA_PORT] `b_is_Ready` НЕ означает «создание устройства закончено».
    //
    // Оно выставляется сразу после Render->Create (Device_create.cpp), а цели рендера строятся
    // ПОСЛЕ, в OnDeviceCreate. Мы же вызываемся как раз оттуда - через da_upscaler_set_available,
    // когда сохранённый апскейлер на этой видеокарте недоступен. Сброс на полпути сборки уронил
    // игру у тестера на R9 290: `CRenderDevice::Reset` читал по нулевому адресу, C0000005, ещё до
    // главного меню (лог openxray_alex_001). Условие на b_is_Ready там стояло и не помогло.
    //
    // Пересобирать на этом этапе и незачем: остаток создания и так возьмёт уже исправленные
    // значения.
    if (Device.b_is_Ready && !s_upscaler_switch_during_create)
        Device.Reset();

    Msg("* [DA_PORT] upscaler %d, quality step %d: scene renders at %d%% of the output", ps_r__upscaler,
        q, ps_r__render_scale);
}

class CCC_Upscaler : public CCC_Token
{
public:
    CCC_Upscaler(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        const u32 was = *value;
        CCC_Token::Execute(args);
        if (*value == was)
            return;
        da_apply_upscaler();
    }
};

// [DA_PORT] Same idea as CCC_FSR2 below: the quality mode also sets the render scale, because Intel's
// ratios are what the reconstruction was tuned for and the two must not disagree.
class CCC_XESS : public CCC_Token
{
public:
    CCC_XESS(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        const u32 was = *value;
        CCC_Token::Execute(args);
        if (*value == was)
            return;

        if (*value)
            da_upscaler_make_exclusive(value);

        switch (*value)
        {
        case 1: ps_r__render_scale = 77; break; // ultra quality, 1.3x
        case 2: ps_r__render_scale = 67; break; // quality, 1.5x
        case 3: ps_r__render_scale = 59; break; // balanced, 1.7x
        case 4: ps_r__render_scale = 50; break; // performance, 2.0x
        case 5: ps_r__render_scale = 33; break; // ultra performance, 3.0x
        default: ps_r__render_scale = 100; break;
        }
        Device.UpdateRenderResolution();
        Msg("* [DA_PORT] XeSS mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
    }
};

// [DA_PORT] DLSS. Устроен как XeSS выше, но коэффициенты свои: они замерены у самой NGX через
// da_ngx_optimal_size, а не взяты из документации.
//
// ⚠️ Ступени 1 и 2 дают одинаковый масштаб, и это не опечатка: NGX не считает «ультра-качество»
// отдельным режимом и на запрос отвечает тем же разрешением, что и на «качество». Оставлены обе,
// чтобы номера ступеней совпадали с остальными апскейлерами — иначе одно и то же число в меню
// означало бы разное качество в зависимости от выбранного бэкенда.
class CCC_DLSS : public CCC_Token
{
public:
    CCC_DLSS(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        const u32 was = *value;
        CCC_Token::Execute(args);
        if (*value == was)
            return;

        if (*value)
            da_upscaler_make_exclusive(value);

        switch (*value)
        {
        case 1: ps_r__render_scale = 67; break; // ультра-качество -> тот же 1.5x, см. выше
        case 2: ps_r__render_scale = 67; break; // качество, 1.5x
        case 3: ps_r__render_scale = 58; break; // сбалансированно, 1.72x
        case 4: ps_r__render_scale = 50; break; // производительность, 2.0x
        case 5: ps_r__render_scale = 33; break; // ультра-производительность, 3.0x
        default: ps_r__render_scale = 100; break;
        }
        Device.UpdateRenderResolution();
        Msg("* [DA_PORT] DLSS mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
    }
};

xr_token qdlss_token[] = {
    { "ui_mm_dlss_off", 0 },
    { "ui_mm_dlss_ultra_quality", 1 },
    { "ui_mm_dlss_quality", 2 },
    { "ui_mm_dlss_balanced", 3 },
    { "ui_mm_dlss_performance", 4 },
    { "ui_mm_dlss_ultra_performance", 5 },
    { nullptr, 0 },
};

// Token names double as the localisation ids the options menu shows (st_da_port_ui.xml).
xr_token qfsr2_token[] = {
    { "ui_mm_fsr2_off", 0 },
    { "ui_mm_fsr2_quality", 1 },
    { "ui_mm_fsr2_balanced", 2 },
    { "ui_mm_fsr2_performance", 3 },
    { "ui_mm_fsr2_ultra", 4 },
    { nullptr, 0 },
};

class CCC_FSR2 : public CCC_Token
{
public:
    CCC_FSR2(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        const u32 was = *value;
        CCC_Token::Execute(args);
        if (*value == was)
            return;

        if (*value)
            da_upscaler_make_exclusive(value);

        switch (*value)
        {
        case 1: ps_r__render_scale = 67; break; // quality
        case 2: ps_r__render_scale = 59; break; // balanced
        case 3: ps_r__render_scale = 50; break; // performance
        case 4: ps_r__render_scale = 33; break; // ultra performance
        default: ps_r__render_scale = 100; break; // off - render at native size again
        }
        Device.UpdateRenderResolution();
        Msg("* [DA_PORT] FSR 2 mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
    }
};

class CCC_RenderScale : public CCC_Integer
{
public:
    CCC_RenderScale(pcstr N, int* V, int _min, int _max) : CCC_Integer(N, V, _min, _max) {}

    void Execute(pcstr args) override
    {
        const int was = *value;
        CCC_Integer::Execute(args);
        if (*value == was)
            return;

        Device.UpdateRenderResolution();
        if (Device.b_is_Ready)
            Device.Reset();
    }
};
//-----------------------------------------------------------------------
// [DA_PORT] Upscaling presets (FSR 1.0). Scales follow the usual naming: quality is a barely visible
// step down, performance is half resolution per axis. Sharpening rises with the preset because the
// lower the source, the more detail the reconstruction has to put back — but only to a point, past
// which it stops recovering detail and starts amplifying noise in grass and gravel.
// Defined further down this file, alongside the other render variables.
extern ENGINE_API int ps_r__render_scale;
extern ENGINE_API int ps_r__upscale_sharpness;

class CCC_UpscalePreset : public CCC_Token
{
public:
    CCC_UpscalePreset(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);

        if (*value)
            da_upscaler_make_exclusive(value);

        const int was = ps_r__render_scale;
        switch (*value)
        {
        case 1: ps_r__render_scale = 77; ps_r__upscale_sharpness = 40; break;
        case 2: ps_r__render_scale = 67; ps_r__upscale_sharpness = 40; break;
        case 3: ps_r__render_scale = 50; ps_r__upscale_sharpness = 55; break;
        default: ps_r__render_scale = 100; ps_r__upscale_sharpness = 0; break;
        }

        if (ps_r__render_scale == was)
            return;

        // Same sequence CCC_RenderScale uses — the scene targets are recreated at the new size.
        Device.UpdateRenderResolution();
        if (Device.b_is_Ready)
            Device.Reset();
    }
};
//-----------------------------------------------------------------------
class CCC_VidMonitor : public CCC_Token
{
public:
    CCC_VidMonitor(pcstr name) : CCC_Token(name, &psDeviceMode.Monitor, nullptr) {}

    const xr_token* GetToken() noexcept override
    {
        return vid_monitor_token.data();
    }
};
//-----------------------------------------------------------------------
class CCC_VidMode : public CCC_Token
{
    u32 _dummy = 0;

public:
    CCC_VidMode(pcstr name) : CCC_Token(name, &_dummy, nullptr) {}

    void Execute(pcstr args) override
    {
        u32 w, h, r = 0;
        const int cnt = sscanf(args, "%ux%u (%uHz)", &w, &h, &r);
        if (cnt >= 2)
        {
            psDeviceMode.Width = w;
            psDeviceMode.Height = h;

            if (cnt == 3)
            {
                psDeviceMode.RefreshRate = r;
                m_Refresh60hz.set(fl_Refresh60hz, psDeviceMode.RefreshRate == 60);
            }

            // [DA_PORT] Частоту приводим к той, что есть в списке для этого разрешения.
            //
            // Список теперь показывает каждое разрешение один раз, с наибольшей частотой
            // (FillResolutionsForMonitor). Значит сохранённое «1920x1080 (60Hz)» из прежних
            // конфигов в списке не найдётся, и строка настроек откроется пустой — выбор есть, а
            // показать его нечем. Подгоняем молчаливое несоответствие здесь, один раз, со строкой
            // в лог: пусть настройки показывают то, что реально будет включено.
            //
            // Точное совпадение не трогаем, поэтому осознанный выбор из консоли переживает эту
            // правку, если такой режим в списке есть.
            if (!vid_mode_token[psDeviceMode.Monitor].empty())
            {
                string64 exact;
                xr_sprintf(exact, "%ux%u (%uHz)", psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.RefreshRate);

                string64 prefix;
                xr_sprintf(prefix, "%ux%u (", psDeviceMode.Width, psDeviceMode.Height);
                const size_t prefix_len = xr_strlen(prefix);

                bool found_exact = false;
                pcstr listed = nullptr;
                for (const xr_token& token : vid_mode_token[psDeviceMode.Monitor])
                {
                    if (!token.name)
                        continue;
                    if (0 == xr_strcmp(token.name, exact))
                    {
                        found_exact = true;
                        break;
                    }
                    if (!listed && 0 == strncmp(token.name, prefix, prefix_len))
                        listed = token.name;
                }

                if (!found_exact && listed)
                {
                    u32 lw, lh, lr = 0;
                    if (sscanf(listed, "%ux%u (%uHz)", &lw, &lh, &lr) == 3 && lr != psDeviceMode.RefreshRate)
                    {
                        Msg("~ [DA_PORT] режим %s: в списке для этого разрешения только %u Гц, беру её",
                            exact, lr);
                        psDeviceMode.RefreshRate = lr;
                        m_Refresh60hz.set(fl_Refresh60hz, psDeviceMode.RefreshRate == 60);
                    }
                }
            }
        }
        else
        {
            Msg("! Wrong video mode [%s]", args);
        }
    }

    const xr_token* GetToken() noexcept override
    {
        return vid_mode_token[psDeviceMode.Monitor].data();
    }

    void GetStatus(TStatus& S) override
    {
        xr_sprintf(S, "%ux%u (%uHz)", psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.RefreshRate);
    }

    void Info(TInfo& I) override
    {
        xr_strcpy(I, sizeof(I), "change screen resolution WxH (RHz)");
    }

    void fill_tips(vecTips& tips, u32 /*mode*/) override
    {
        TStatus buf;
        xr_sprintf(buf, "%ux%u (%dHz) (current)", psDeviceMode.Width, psDeviceMode.Height, psDeviceMode.RefreshRate);
        tips.push_back(buf);

        const xr_token* tok = GetToken();
        while (tok->name)
        {
            tips.push_back(tok->name);
            tok++;
        }
    }

private:
    enum { fl_Refresh60hz = 1u << 0u };
    inline static Flags32 m_Refresh60hz; // for rs_refresh_60hz backwards compatibility

public:
    class CCC_Refresh60hz final : public CCC_Mask
    {
    public:
        CCC_Refresh60hz(pcstr name) : CCC_Mask(name, &m_Refresh60hz, fl_Refresh60hz)
        {
            m_Refresh60hz.set(fl_Refresh60hz, psDeviceMode.RefreshRate == 60);
        }

        void Execute(pcstr args) override
        {
            CCC_Mask::Execute(args);
            if (GetValue())
                psDeviceMode.RefreshRate = 60;
            else
                psDeviceMode.RefreshRate = 0; // Device will adjust
        }
    };
};
using CCC_Refresh60hz = CCC_VidMode::CCC_Refresh60hz;
//-----------------------------------------------------------------------
class CCC_VidWindowMode final : public CCC_Token
{
    // [DA_PORT] Режима три, а не четыре, и порядок в списке — от оконного к полноэкранному.
    //
    // Убран `rsWindowedBorderless` — окно заданного размера без рамки. От обычного окна игрок его
    // не отличает, а в списке настроек два почти одинаковых пункта заставляют выбирать вслепую.
    // Остались те три, между которыми есть настоящая разница: окно, полный экран в окне (растянут
    // на рабочий стол, мгновенный Alt+Tab) и монопольный полный экран (смена режима монитора).
    //
    // Значение из enum'а не выброшено — на нём стоит логика окна в Device_mode.cpp, — просто больше
    // не предлагается. Старое имя по-прежнему принимается, см. Execute: сохранённый в user.ltx
    // выбор не должен превращаться в «Invalid syntax» после обновления.
    inline static xr_token vid_window_mode_token[] =
    {
        { "st_opt_windowed",                rsWindowed             },
        { "st_opt_fullscreen_borderless",   rsFullscreenBorderless },
        { "st_opt_fullscreen",              rsFullscreen           },
        { nullptr,                          -1                     },
    };

public:
    CCC_VidWindowMode(pcstr name) : CCC_Token(name, &psDeviceMode.WindowStyle, vid_window_mode_token) {}

    void Execute(pcstr args) override
    {
        // [DA_PORT] Старое имя режима из прежних конфигов приводим к ближайшему из оставшихся.
        // Молча подменять нельзя — человек должен понимать, почему в меню стоит не то, что он
        // выбирал когда-то, — поэтому строка в лог.
        if (args && 0 == xr_strcmp(args, "st_opt_windowed_borderless"))
        {
            Msg("~ [DA_PORT] режим окна без рамки больше не предлагается, включён оконный");
            CCC_Token::Execute("st_opt_windowed");
        }
        else
            CCC_Token::Execute(args);
        m_fullscreen.set(fl_fullscreen, psDeviceMode.WindowStyle == rsFullscreen);
    }

private:
    enum { fl_fullscreen = 1u << 0u };
    inline static Flags32 m_fullscreen; // for rs_fullscreen backwards compatibility

public:
    class CCC_Fullscreen final : public CCC_Mask
    {
    public:
        CCC_Fullscreen(pcstr name) : CCC_Mask(name, &m_fullscreen, fl_fullscreen)
        {
            m_fullscreen.set(fl_fullscreen, psDeviceMode.WindowStyle == rsFullscreen);
        }

        void Execute(pcstr args) override
        {
            CCC_Mask::Execute(args);
            if (GetValue())
                psDeviceMode.WindowStyle = rsFullscreen;
            else
                // [DA_PORT] Выключение полного экрана даёт полный экран В ОКНЕ, а не окно без рамки:
                // из трёх оставшихся режимов это ближайший к «не монопольный», и он же по умолчанию.
                psDeviceMode.WindowStyle = rsFullscreenBorderless;
        }
    };
};
using CCC_Fullscreen = CCC_VidWindowMode::CCC_Fullscreen;
//-----------------------------------------------------------------------
class CCC_SND_Restart : public IConsole_Command
{
public:
    CCC_SND_Restart(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
    virtual void Execute(pcstr args)
    {
        GEnv.Sound->_restart();
    }
};

//-----------------------------------------------------------------------
// [DA_PORT] Exported: the renderer now applies these itself when the hardware gamma ramp is
// unavailable (see xr_effgamma.cpp) instead of leaving the sliders doing nothing.
ENGINE_API float ps_gamma = 1.f, ps_brightness = 1.f, ps_contrast = 1.f;
class CCC_Gamma : public CCC_Float
{
public:
    CCC_Gamma(pcstr N, float* V) : CCC_Float(N, V, 0.5f, 1.5f) {}
    virtual void Execute(pcstr args)
    {
        CCC_Float::Execute(args);
        GEnv.Render->setGamma(ps_gamma);
        GEnv.Render->setBrightness(ps_brightness);
        GEnv.Render->setContrast(ps_contrast);
        GEnv.Render->updateGamma();
    }
};

//-----------------------------------------------------------------------
/*
#ifdef DEBUG
extern int g_bDR_LM_UsePointsBBox;
extern int g_bDR_LM_4Steps;
extern int g_iDR_LM_Step;
extern Fvector g_DR_LM_Min, g_DR_LM_Max;

class CCC_DR_ClearPoint : public IConsole_Command
{
public:
CCC_DR_ClearPoint(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
virtual void Execute(pcstr args) {
g_DR_LM_Min.x = 1000000.0f;
g_DR_LM_Min.z = 1000000.0f;

g_DR_LM_Max.x = -1000000.0f;
g_DR_LM_Max.z = -1000000.0f;

Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x, g_DR_LM_Max.z);
}
};

class CCC_DR_TakePoint : public IConsole_Command
{
public:
CCC_DR_TakePoint(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; };
virtual void Execute(pcstr args) {
Fvector CamPos = Device.vCameraPosition;

if (g_DR_LM_Min.x > CamPos.x) g_DR_LM_Min.x = CamPos.x;
if (g_DR_LM_Min.z > CamPos.z) g_DR_LM_Min.z = CamPos.z;

if (g_DR_LM_Max.x < CamPos.x) g_DR_LM_Max.x = CamPos.x;
if (g_DR_LM_Max.z < CamPos.z) g_DR_LM_Max.z = CamPos.z;

Msg("Local BBox (%f, %f) - (%f, %f)", g_DR_LM_Min.x, g_DR_LM_Min.z, g_DR_LM_Max.x, g_DR_LM_Max.z);
}
};

class CCC_DR_UsePoints : public CCC_Integer
{
public:
CCC_DR_UsePoints(pcstr N, int* V, int _min=0, int _max=999) : CCC_Integer(N, V, _min, _max) {};
virtual void Save (IWriter *F) {};
};
#endif
*/

ENGINE_API bool renderer_allow_override = false;

class CCC_renderer : public CCC_Token
{
    typedef CCC_Token inherited;

    u32 renderer_value = 0;
    static bool cmd_lock;

public:
    CCC_renderer(pcstr N) : inherited(N, &renderer_value, NULL) {};
    ~CCC_renderer() override {}
    void Execute(pcstr args) override
    {
        if ((renderer_allow_override == false) && (cmd_lock == true))
        {
            /*
             * It is a case when the renderer type was specified as
             * an application command line argument. This setting should
             * have the highest priority over other command invocations
             * (e.g. user config loading).
             * Since the Engine doesn't support switches between renderers
             * in runtime, it's safe to disable this command until restart.
             */
            Msg("Renderer is overrided by command line argument");
            return;
        }

        inherited::Execute(args);

        cmd_lock = true;
    }

    void Save(IWriter* F) override
    {
        if (renderer_allow_override == false)
        {   // Do not save forced value
            return;
        }

        tokens = VidQualityToken.data();
        inherited::Save(F);
    }

    const xr_token* GetToken() noexcept override
    {
        tokens = VidQualityToken.data();
        return inherited::GetToken();
    }
};
bool CCC_renderer::cmd_lock = false;

class CCC_soundDevice : public CCC_Token
{
    typedef CCC_Token inherited;

public:
    CCC_soundDevice(pcstr N) : inherited(N, &snd_device_id, NULL){};
    virtual ~CCC_soundDevice() {}
    virtual void Execute(pcstr args)
    {
        GetToken();
        if (!tokens)
            return;

        // [DA_PORT] Устройства с таким именем больше нет — не ругаемся «Invalid syntax» в пустоту, а
        // возвращаемся к системному. Строка в user.ltx переживает и смену наушников, и переезд на
        // другую машину, и тогда единственная понятная реакция — играть туда, куда играет система.
        pcstr name = args;
        const xr_token* found = tokens;
        while (found->name && xr_stricmp(found->name, name) != 0)
            ++found;

        // Старые конфиги хранят имя целиком — со служебным префиксом бэкенда и со скобкой адаптера
        // («OpenAL Soft on Наушники (JBL TUNE770NC)»), а список теперь показывает только имя. Это
        // ровно то же устройство, и терять выбор игрока из-за нашей же косметики нельзя: сравниваем
        // ещё раз, приведя обе стороны к тому виду, в каком имя стоит в меню.
        if (!found->name)
        {
            string512 wanted;
            snd_device_display_name(wanted, sizeof(wanted), args);

            for (const xr_token* tok = tokens; tok->name; ++tok)
            {
                string512 candidate;
                snd_device_display_name(candidate, sizeof(candidate), tok->name);
                if (0 == xr_stricmp(candidate, wanted))
                {
                    found = tok;
                    name = tok->name;
                    break;
                }
            }
        }

        if (!found->name)
        {
            Msg("~ SOUND: устройство [%s] не найдено — звук идёт в системное по умолчанию", args);
            snd_device_id = snd_device_auto;
            return;
        }

        inherited::Execute(name);
    }

    void GetStatus(TStatus& S) override
    {
        GetToken();
        if (!tokens)
            return;
        inherited::GetStatus(S);
    }

    const xr_token* GetToken() noexcept override
    {
        tokens = Engine.Sound.GetDevicesList().data();
        return tokens;
    }

    virtual void Save(IWriter* F)
    {
        GetToken();
        if (!tokens)
            return;
        inherited::Save(F);
    }
};

//-----------------------------------------------------------------------

class CCC_ExclusiveMode : public IConsole_Command
{
private:
    typedef IConsole_Command inherited;

public:
    CCC_ExclusiveMode(pcstr N) : inherited(N) {}
    virtual void Execute(pcstr args)
    {
        bool value = false;
        if (!xr_strcmp(args, "on"))
            value = true;
        else if (!xr_strcmp(args, "off"))
            value = false;
        else if (!xr_strcmp(args, "true"))
            value = true;
        else if (!xr_strcmp(args, "false"))
            value = false;
        else if (!xr_strcmp(args, "1"))
            value = true;
        else if (!xr_strcmp(args, "0"))
            value = false;
        else
            InvalidSyntax();

        pInput->ExclusiveMode(value);
    }

    virtual void Save(IWriter* F) {}
};

class ENGINE_API CCC_HideConsole : public IConsole_Command
{
public:
    CCC_HideConsole(pcstr N) : IConsole_Command(N) { bEmptyArgsHandled = true; }
    virtual void Execute(pcstr args) { Console->Hide(); }
    void GetStatus(TStatus& S) override { S[0] = 0; }
    virtual void Info(TInfo& I) { xr_sprintf(I, sizeof(I), "hide console"); }
};

class CCC_Editor : public IConsole_Command
{
public:
    CCC_Editor(pcstr name) : IConsole_Command(name) { bEmptyArgsHandled = true; }
    void Execute(pcstr args) override
    {
        Device.editor().SetState(xray::editor::ide::visible_state::full);
    }
};

ENGINE_API float g_fov = 67.5f;
ENGINE_API float psHUD_FOV = 0.45f;
// [DA_PORT] Effective per-frame HUD FOV. Equals psHUD_FOV unless the opt-in "nearwall" weapon
// collision feature modulates it (see CHudItem::UpdateCL). All HUD projection sites read this
// instead of psHUD_FOV so the weapon/particles/UI stay consistent. Off by default => == psHUD_FOV.
ENGINE_API float g_hud_fov_current = 0.45f;

// extern int psSkeletonUpdate;
extern int rsDVB_Size;
extern int rsDIB_Size;

extern int psNET_DedicatedSleep;

extern Flags32 psEnvFlags;
// extern float r__dtex_range;

extern int g_ErrorLineCount;

ENGINE_API int ps_r__Supersample = 1;
ENGINE_API int ps_r__WallmarksOnSkeleton = 0;
// [DA_PORT] Percentage of the output resolution the 3D scene is rendered at (see
// CRenderDevice::UpdateRenderResolution). 100 = native, i.e. exactly the stock behaviour.
ENGINE_API int ps_r__render_scale = 100;

// [DA_PORT] Temporal anti-aliasing. Lives in the engine rather than the renderer because the camera
// code needs it too: the projection jitter TAA feeds on is applied in CCameraManager::ApplyDevice.
// Off by default — at 0 the resolve pass returns immediately and no jitter is applied, so the frame is
// exactly what it was before TAA existed.
ENGINE_API int ps_r__taa = 0;

// [DA_PORT] Sharpening applied after the render-scale upscale (the RCAS half of FSR 1.0), in percent.
// Only does anything when r__render_scale is below 100 - at native resolution there is nothing to
// recover. 0 disables it.
ENGINE_API int ps_r__upscale_sharpness = 40;

// [DA_PORT] Motion vectors: an extra G-buffer target recording where each pixel was last frame. This is
// the one input FSR 2, XeSS and DLSS all require and the engine has never had. Off by default and read
// only when the renderer starts — changing it needs a renderer restart, because it decides both the
// number of targets the scene pass binds and the shader option the G-buffer shaders are compiled with.
ENGINE_API int ps_r__motion_vectors = 0;

// [DA_PORT] AMD FidelityFX Super Resolution 2: 0 off, 1 quality, 2 balanced, 3 performance,
// 4 ultra performance. Unlike the spatial upscale (r__render_scale), this one reconstructs detail
// from the history of previous frames, which is why it needs the motion vector buffer — and why
// enabling it turns that buffer on. Read when the renderer starts.
ENGINE_API int ps_r__fsr2 = 0;
// [DA_PORT] Intel XeSS. A separate variable rather than one shared "upscaler" enum: each builds its
// context at renderer start for a fixed pair of resolutions, so they are chosen before that point.
ENGINE_API int ps_r__xess = 0;
ENGINE_API int ps_r__dlss = 0; // [DA_PORT] NVIDIA DLSS: 0 выкл, 1-5 ступень качества

// [DA_PORT] Отдавать ли DLSS нашу реактивную маску.
//
// Маска строилась под FSR 2, где она значит «этому пикселю не верь по истории». У NVIDIA параметр
// называется иначе и значит не совсем то же: pInBiasCurrentColorMask смещает пиксель к текущему
// кадру, и NVIDIA советует применять её скупо. На листве, которой в этом моде везде много, слишком
// смелая маска даёт дрожание и шум там, где реконструкция справилась бы сама.
//
// Ручка, а не решение в коде, потому что на глаз это различают только сравнением: включить, выключить,
// посмотреть на кусты в покое. Значение по умолчанию сохраняет прежнее поведение.
ENGINE_API int ps_r__dlss_reactive = 1;

// [DA_PORT] Разовый замер векторов движения: числа в лог вместо перебора знаков глазами.
// Читает буфер обратно, поэтому останавливает конвейер — только на один кадр и только по команде.
// [DA_PORT] Показать ВХОД апскейлера вместо его результата.
//
// Апскейлер продолжает работать (джиттер, векторы, накопление — всё на месте), но на экран идёт не его
// выход, а та самая картинка в разрешении рендера, которую ему скармливают, растянутая обычным
// фильтром. Ровно один вопрос: дефект уже во входе или его делает реконструкция.
//
// Сравнение «выключил апскейлер — стало чисто» на этот вопрос НЕ отвечает: вместе с апскейлером
// выключается и подпиксельный сдвиг, то есть меняются сразу две вещи. Здесь меняется одна.
ENGINE_API int ps_r__upscale_show_input = 0;

// [DA_PORT] Разовый снимок среза G-буфера по строке через прицел (см. CRenderTarget::
// da_dump_gbuffer_row). Наводим на дефект, выполняем — в логе встают рядом глубина, цвет, вектор и
// маска реактивности ОДНОГО и того же пикселя. Средние по кадру такое не ловят: дефект живёт в
// десятке пикселей, а среднее по миллиону их не видит.
ENGINE_API int ps_r__gbuffer_probe = 0;

// [DA_PORT] Покадровая запись накопленного света в пикселе под перекрестьем, N кадров подряд.
// Против дефектов, которые мерцают сами по себе: срез отвечает «что в этом кадре», а здесь нужна
// последовательность, чтобы увидеть размах и период.
ENGINE_API int ps_r__light_watch = 0;
// [DA_PORT] Карта света по всему экрану, N кадров подряд. Разбор — у CRenderTarget::da_light_map.
// Полноэкранное чтение с видеокарты синхронизирует с ней кадр, поэтому это прибор, а не режим:
// включать на десяток кадров, а не держать постоянно.
ENGINE_API int ps_r__light_map = 0;

// [DA_PORT] Разбор слагаемых сборки кадра, см. da_dbg в r3\combine_1.ps. Ноль — обычный кадр.
ENGINE_API int ps_r__combine_dbg = 0;

// [DA_PORT] Нижняя граница полусферической засветки, долей от цвета неба. Лечит чёрные пятна
// вечером и ночью; разбор — в r3\combine_1.ps. 0 возвращает прежнее поведение точь-в-точь.
ENGINE_API float ps_r__hemi_floor = 0.0f; // ОТКАТ: граница поднимала засветку во всех тенях

// [DA_PORT] Сколько кадров подряд мерить, ЕЗДИТ ЛИ ГОТОВЫЙ КАДР относительно предыдущего.
//
// Отдельно от da_light_watch, потому что отвечает на другой вопрос. Тот смотрит на накопленный свет
// ДО апскейлера, где кадр обязан меняться от джиттера — там движение и есть смысл происходящего.
// Этот смотрит на то, что выходит ПОСЛЕ, где движения быть не должно вовсе: снять сдвиг — работа
// апскейлера. См. CRenderTarget::da_shift_watch.
ENGINE_API int ps_r__shift_watch = 0;

// [DA_PORT] Сколько кадров подряд разбирать ОБНОВЛЕНИЕ ОБЪЕКТОВ по секциям конфига.
//
// Отвечает на вопрос, который da_perf_dump поставил, но сам не решает: игровая логика занимает
// две трети кадра, из них GOAP — 14%, поиск пути — ноль, а остальные 81% не разложены ни на что.
// Здесь они и раскладываются: сколько стоят все сталкеры вместе, сколько предметы, сколько
// физика в объектах. Разбор в CObjectList::da_move_probe.
ENGINE_API int ps_da_move_dump = 0;

// [DA_PORT] Сколько кадров подряд разбирать ОБНОВЛЕНИЕ СТАЛКЕРА по фазам.
//
// Следующий уровень после da_move_dump. Тот показал, что 55 сталкеров стоят 1.63 мс на кадр —
// четверть кадра, — но не сказал, что внутри. Здесь фазы: обработчик действий, наследуемое
// обновление (зрение, память, движение, анимация), физика, прицеливание, шаги, отдача.
// Разбор в CAI_Stalker::UpdateCL.
ENGINE_API int ps_da_stalker_dump = 0;

// [DA_PORT] Сколько кадров подряд разбирать ПЛАНИРОВЩИК поимённо. Разбор в xrSheduler.cpp.
ENGINE_API int ps_da_sched_dump = 0;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ, выше которого печатать разбор обновления ОТРЯДА по фазам.
//
// Следующий спуск после da_sched_dump. Тот назвал виновника поимённо - обновление одного отряда
// изредка стоит 6-7 мс при общей нагрузке планировщика 0.2 мс на кадр, то есть выброс делает
// ОДИН неделимый вызов. Здесь видно, какая его фаза столько стоит.
//
// Порогом, а не числом кадров: выброс редкий, и печатать все обновления подряд - это залить лог
// тысячами строк, среди которых нужных три. 0 выключает.
//
// Читает эту ручку СКРИПТ (sim_squad_scripted.script) через get_console():get_float, поэтому в
// движке у неё читателя нет и быть не должно.
ENGINE_API float ps_da_squad_dump = 0.f;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ для разбора обновления ОФФЛАЙНОВОГО NPC по фазам.
//
// da_sched_dump называет виновниками выбросов объекты вида sim_default_duty_123356 — это НЕ
// отряды, а отдельные сталкеры: sim_default_* объявлены профилями персонажей в creatures/
// profiles.ltx, тогда как отряды наследуют online_offline_group в squad_descr*.ltx. Разбор
// отрядного кода из-за этой путаницы ушёл впустую.
//
// Здесь меряется то, что планировщик и вызывает: CALifeMonsterBrain::update — выбор задачи,
// её исполнение и передвижение по графу. Печатается только превышение порога, 0 выключает.
ENGINE_API float ps_da_alife_dump = 0.f;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ для разбора CEntityAlive::shedule_Update по шагам.
//
// Куда пришли: da_perf_dump -> планировщик -> da_sched_dump -> объекты sim_default_* ->
// da_stalker_dump -> фаза «[мозги] наследуемое (существо)». В живой игре у неё средняя цена
// 0.073 мс на кадр, а ХУДШИЙ вызов 6.52 мс - единственная фаза, чей пик дотягивается до
// выбросов, которые видно в разборе кадра. Здесь она раскладывается на шаги.
ENGINE_API float ps_da_entity_dump = 0.f;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ для разбора CGameObject::shedule_Update по шагам.
//
// Дно спуска: da_entity_dump показал, что 98.7% времени существа уходит в вызов базового класса,
// а собственный код CEntityAlive не стоит ничего. Здесь этот базовый вызов и раскладывается.
ENGINE_API float ps_da_object_dump = 0.f;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ для разбора скриптового обновления NPC (xr_motivator).
//
// Конец спуска в движке: da_object_dump показал, что 99.5% базового обновления объекта уходит
// в scriptBinder.shedule_Update, то есть в Lua мода. Дальше меряет уже сам скрипт, а эта ручка
// служит ему выключателем - читается через get_console():get_float.
ENGINE_API float ps_da_npc_dump = 0.f;

// [DA_PORT] Порог в МИЛЛИСЕКУНДАХ для разбора перехода Lua -> движок.
//
// Последняя ступень спуска. Выше был вывод «весь выброс сидит в scriptBinder.shedule_Update», а
// внутри обрамлять уже нечего: там вызов Lua мода. Но и он не сам по себе дорог — у лампы выброс
// 12.578 мс дался при изменении памяти Lua на минус семь байт, то есть работа была в движке.
//
// Эта ручка включает счёт по КАЖДОМУ вызову связанной функции (луабинд, см. da_call_probe.hpp).
// На выходе — самые дорогие функции обновления и отдельной строкой доля, которую съел сам Lua,
// без движка. Это и есть развилка «виноват скрипт или виноват движок».
ENGINE_API float ps_da_lua_call_dump = 0.f;

// [DA_PORT] Период отчёта о памяти, В СЕКУНДАХ. 0 - молчит.
//
// Заведено под проверку смены стратегии сборки мусора Lua на gc_timeout: она перестаёт запускать
// сборку из выделений памяти внутри скриптов, и надо убедиться, что память при этом не растёт.
// Одним числом такое не проверяется - решает ФОРМА кривой на длинном прогоне.
ENGINE_API float ps_da_mem_log = 0.f;

// [DA_PORT] Сколько миллисекунд отдать прогреву планировщика в конце загрузки уровня.
//
// Первые обновления всех объектов локации иначе достаются первому игровому кадру: на Юпитере это
// 367 мс в одном кадре. Здесь они делаются под экраном загрузки. 0 выключает прогрев.
// Разбор — в Device.cpp, у конца предзагрузки.
ENGINE_API int ps_da_sched_warmup_ms = 500;

// [DA_PORT] Бюджет на разбор очереди игровых событий за кадр, миллисекунды.
//
// Массовый спавн давал выброс 9.71 мс в одном кадре. Остаток переносится на следующий кадр —
// события помечены временем и не теряются. 0 возвращает прежнее поведение. Разбор — в
// CLevel::ProcessGameEvents.
ENGINE_API float ps_da_events_budget_ms = 3.f;

// [DA_PORT] Сколько миллисекунд за кадр предзагрузки тратить на прогрев визуалов. Разбор — в
// CLevel::da_warmup_visuals. 0 выключает.
// [DA_PORT] Потолок прогрева визуалов на кадр, миллисекунды. 0 -- без потолка, весь список за
// один заход; отрицательное -- прогрев выключен.
//
// По умолчанию ВЫКЛЮЧЕН -- замер сказал, что он не окупается, и вот числа.
//
// Сначала прогрев не успевал: с потолком в 5 мс он растягивался на десятки кадров, а весь залп
// спавна приходит в ОДНОМ кадре и приходит раньше. Порядок в логе был именно такой -- начало
// прогрева, залп на 2.3 с, конец прогрева. Убрал потолок, прогрев встал перед залпом, и стало видно
// настоящее соотношение: прогрев 1465 мс, спавн 2174 -> 1308. Экономия 866 при цене 1465, то есть
// загрузка стала ДЛИННЕЕ на 600 мс.
//
// Причина в устройстве пула моделей (CModelPool::Create): готовый экземпляр берётся из пула, иначе
// клонируется базовая модель, и только если базы нет -- читается .ogf. Прогрев снимает лишь третий,
// одноразовый шаг; клонирование платится за каждый объект в любом случае. А греем мы визуалы ВСЕГО
// уровня, тогда как онлайн выходит заметно меньше -- лишнее и ушло в минус.
//
// Ручка оставлена: на слабом диске соотношение может быть другим, там чтение .ogf дороже.
ENGINE_API float ps_da_visual_warmup_ms = -1.f;

// [DA_PORT] Замер кэша теневых карт. Значение = число кадров: первая половина меряет ВРЕМЯ КАДРА без
// единого чтения буфера, вторая читает весь экран и ищет дрожание теней. Разделено потому, что
// полноэкранное чтение само стоит кадров и испортило бы измерение времени.
ENGINE_API int ps_r__shadow_test = 0;

ENGINE_API int ps_r__dlss_selftest = 0;
// [DA_PORT] FSR 3 upscaler. A separate variable rather than a mode of r__fsr2: the two build
// their contexts independently at renderer start, and having both live means comparing them
// costs one restart rather than two. Only whichever is selected ever dispatches.
ENGINE_API int ps_r__fsr3 = 0;

// [DA_PORT] D3D11 validation layer, plus draining its messages into the engine log. Off by
// default and needs a restart: the flag is passed at device creation. Requires the 'Graphics
// Tools' Windows feature; without it the device falls back to creating without the layer.
ENGINE_API int ps_r__d3d_debug = 0;

// [DA_PORT] Halves the FSR 3 path so the damage can be attributed without a validation layer.
// 0 = normal. 1 = context is created and its resources exist, but nothing is dispatched and the
// upscaled frame is never claimed, so the engine falls back to the plain stretch. If models break
// at 1 as well, the fault is in creation or in the resources FSR 3 holds; if they survive, it is
// the dispatch that disturbs the pipeline.
ENGINE_API int ps_r__fsr3_debug = 0;
// [DA_PORT] Насколько апскейлеры должны не доверять своей истории на альфа-тестовой растительности.
// Не выключатель, а вес: 0 — накопление как обычно, 1 — история игнорируется полностью.
//
// ⚠️ Прежнее значение 0.5 (а в конфигах вообще стояла 1.0) оказалось СЛИШКОМ БОЛЬШИМ и давало
// заметную дрожь травы, кустов и листьев на FSR 2 и FSR 3. Механизм: alpha-test шейдеры
// (deffer_base_aref_bump/flat.ps) пишут это значение реактивности КАЖДОМУ своему пикселю, а не
// только там, где история действительно ненадёжна. При высокой реактивности апскейлер собирает
// такой пиксель из одного джиттернутого кадра, и подпиксельный сдвиг проступает наружу как дрожь —
// тем заметнее, чем ниже частота кадров, потому что глаз усредняет меньше состояний.
//
// Почему у DLSS этого не было: он вообще НЕ получает маску (см. ps_r__dlss_reactive и
// p.reactive в r4_rendertarget_phase_dlss.cpp). Именно это сравнение и указало на причину.
//
// 0.05 — значение, выбранное по картинке: дрожи уже нет, а немного реактивности остаётся против
// смаза за качающимися ветками. Совсем ноль ставить не стоит, маска добавлялась не просто так.
ENGINE_API float ps_r__reactive_foliage = 0.05f;

// [DA_PORT] Reactivity from screen-space motion, against ghosting behind moving objects. The
// scale is in reactive units per NDC unit of travel: at 8 a pixel crossing a hundredth of the
// screen in one frame is already fully reactive. 0 disables it.
//
// Superseded by r__reactive_object below and off by default because it cannot tell the two kinds of
// motion apart: a camera turn moves every pixel on screen, so this marked the WHOLE frame reactive
// whenever the player looked around - precisely when an upscaler most needs its history. Kept as a
// cvar so the old behaviour can still be compared against directly.
ENGINE_API float ps_r__reactive_motion = 0.f;

// [DA_PORT] Reactivity from motion THROUGH THE WORLD, against the ghost trailing an NPC.
//
// The distinction from r__reactive_motion above is the whole point. What a pixel's motion vector
// records is the camera's movement and the object's added together, and the first of those is not a
// reason to distrust history at all. The pass subtracts the camera's share analytically - depth plus
// the previous camera matrix give exactly where a STATIC surface would have been - and what remains
// is what the object itself did. A static world under a moving camera then yields zero everywhere.
//
// Scale is in reactive units per NDC unit of that residual travel. Set from measurement rather than
// guessed: a readback of a frame with someone walking through it put the largest residual at 0.0016,
// so 250 could only ever have reached a fifth of full reactivity. 700 measured 0.67 on that figure,
// which is where this sits.
//
// It was briefly raised to 1000 to chase softness on moving figures. That softness turned out to be
// two temporal filters running one after the other (see phase_taa), and nothing to do with this at
// all - so the extra strength was only ever buying shimmer on glossy surfaces, which is what too
// little accumulation looks like on a narrow specular highlight.
ENGINE_API float ps_r__reactive_object = 700.f;

// [DA_PORT] Метка самосветящейся геометрии - лампочка, экран телевизора, светящаяся палочка в руке.
//
// Свечение рисуется ОТДЕЛЬНЫМ проходом после G-буфера, и цель у него одна - накопитель освещения.
// Ни вектора движения, ни реактивность эти пиксели не пишут: там остаётся то, что записала
// непрозрачная геометрия под ними, то есть чаще всего фон. Апскейлер честно тянет для яркого пятна
// историю по чужому вектору - отсюда пила по кромке свечения и мерцание ламп в помещениях.
//
// Это ровно тот случай, для которого маска реактивности и заведена: и FSR, и DLSS требуют помечать
// в ней всё, что композируется поверх кадра. Ноль выключает проход целиком - тогда поведение
// становится прежним, и это единственный честный способ сравнить в игре.
//
// ⚠️ ПО УМОЛЧАНИЮ НОЛЬ - проверено в игре 01.08. Настоящей причиной пилы по кромке свечения оказался
// не пропуск в маске, а джиттер: проход свечения рисовался вершинным шейдером КАРТЫ ТЕНЕЙ, в котором
// сдвига нет (см. da_emissive_model.vs). После починки сдвига метка не даёт видимой разницы ни на
// палочке, ни на лампах, а стоит прохода и запрета копить историю. Оставлена как ручка: механизм
// верный по документации FSR/DLSS и может пригодиться на другом дефекте.
ENGINE_API float ps_r__reactive_emissive = 0.f;
// [DA_PORT] Насколько маска реактивности гасит накопление в НАШЕЙ темпоралке (r__taa).
//
// Сама маска пишется из G-буфера: листве - r__reactive_foliage (0.05), движущимся пикселям -
// da_motion_reactive. FSR 2 читает её и потому не рябит на листве; наша TAA до 01.08 не читала
// вовсе - отсюда «на FSR 2 чисто, на TAA листва дрожит» при одинаковых настройках.
//
// Усиление отдельной ручкой, потому что 0.05 подбиралось под формулу FSR, а у нас другая: там это
// доля недоверия, здесь - множитель к коэффициенту накопления 0.93. При усилении 8 листва теряет
// примерно 40% накопления, что и требуется. Ноль возвращает прежнее поведение точь-в-точь.
ENGINE_API float ps_r__taa_reactive = 8.f;

// [DA_PORT] То же для прозрачной геометрии - стекло, вода, частицы. По умолчанию НОЛЬ, и это не
// осторожность ради осторожности: свечение занимает в кадре считанные пиксели, а прозрачного бывает
// пол-экрана. Метка означает "не копить историю", и на большой поверхности воды это меняет картинку
// заметно - лечить надо то, что действительно мерцает, а не всё сразу.
ENGINE_API float ps_r__reactive_transparent = 0.f;

// [DA_PORT] Метка реактивности ТОЛЬКО для воды, в отличие от r__reactive_transparent, который метит
// весь прямой проход разом — вместе с дождём, стёклами и частицами.
//
// Зачем вообще метить. Вода не пишет ни глубины, ни векторов движения: она рисуется после G-буфера
// и с выключенной записью глубины. Апскейлер поэтому восстанавливает её историю по векторам ДНА,
// то есть берёт прошлый кадр не оттуда, где вода была, — отсюда мерцание, которое видно только при
// включённом апскейлере и не лечится никакой настройкой самой воды.
//
// Отметку ставит сама вода, битом трафарета (см. effects_water.s), поэтому в меню и на локациях без
// воды проход не делает ничего.
//
// [DA_PORT] Умолчание снижено 0.6 -> 0.0 после того, как заработал проход векторов воды.
//
// Раньше метка была вынужденной: своих векторов у воды не было, история бралась по векторам дна, и
// запрет усреднять был меньшим злом. Ценой шло то, что апскейлер вообще не сглаживал воду — блики
// на ряби вспыхивали каждый кадр в новом пикселе, «как огоньки на ёлке».
//
// Теперь вектора считаются своим проходом (phase_water_velocity), история корректна, и запрет стал
// не нужен. Проверено в игре: на 0 мерцание падает в разы, шлейфов за движущимся по воде не
// появилось. Ручка остаётся — если у кого-то на другом железе вылезет шлейф, поднять до 0.2.
//
// 🪤 Долго это не проявлялось потому, что в dev-ярлыке стоял -nodistort: проход векторов исполнялся
// вхолостую, список искажения был пуст. Прежде чем настраивать, стоит убедиться, что настраиваемое
// вообще работает — прибор r__water_velocity_log отвечает на это одной строкой.
ENGINE_API float ps_r__reactive_water = 0.f;

// [DA_PORT] Проход векторов движения для воды: 1 - вода пишет СВОИ вектора, 0 - проход выключен и
// в буфере скоростей на воде остаётся вектор ДНА (поведение до появления прохода).
//
// Ручка нужна для честного сравнения двух половин. У пикселя мелкой воды содержимое - смесь дна и
// поверхности, а вектор в буфере может быть только ОДИН. С вектором воды дрожит дно, видимое
// сквозь неё; с вектором дна дрожит сама вода. Какая из двух дрожей слабее - вопрос к глазу, а не
// к рассуждению, и решается только переключением на одной сцене.
ENGINE_API int ps_da_water_velocity = 1;

// [DA_PORT] Показать, работает ли проход векторов движения для воды: сколько объектов он видит.
ENGINE_API int ps_da_water_velocity_log = 0;

// [DA_PORT] Пошаговый след освобождения объектов ALife. Нужен для падений БЕЗ стека: виртуальный
// вызов по освобождённой памяти даёт прыжок по нулевому адресу, разматывать нечего, и последняя
// строка лога — единственное, что указывает на виновника.
ENGINE_API int ps_da_alife_release_log = 0;

// [DA_PORT] Не загружать константный буфер, если его содержимое не отличается от уже загруженного.
//
// Движок помечает буфер изменённым на ЛЮБУЮ запись (в Access стоит авторское "TODO: проверять, меняет
// ли set что-нибудь"), поэтому каждый кадр заново отображается и копируется даже то, что не менялось.
// Сравнение дешевле отображения, буферы небольшие.
//
// Пропуск безопасен ровно потому, что экземпляр буфера СВОЙ у каждого контекста: dx11r_constants.cpp
// создаёт их циклом по R__NUM_CONTEXTS и раскладывает в m_CBTable[id]. Один экземпляр - один контекст,
// значит наша теневая копия действительно описывает то, что в нём лежит.
//
// Ручка оставлена, чтобы можно было замерить А/Б, а не поверить на слово: выигрыш не измерен.
ENGINE_API int ps_r__cb_skip_redundant = 1;

// [DA_PORT] Куда целится срез G-буфера (da_dump_gbuffer_row, r__reactive_selftest).
//
// 0 — самый яркий пиксель кадра: так проба делалась под кромку свечения, там яркое пятно и было
// предметом разбора. 1 — перекрестье, то есть то, на что смотрит игрок. Для растительности нужен
// именно второй: самый яркий пиксель в лесу — это небо, а не куст.
ENGINE_API int ps_r__probe_center = 0;

// [DA_PORT] Знак векторов движения, отдаваемых XeSS. См. da_xess_mv.s.
//
// 0 — как в буфере (прежнее поведение), 1 — перевернуть обе оси, 2 — только X, 3 — только Y.
//
// ✅ ПРОВЕРЕНО В ИГРЕ на Intel Arc B580: верное значение — 1. При 2 и 3 (одна ось) размазывает,
// при 1 картинка чистая. Ручка оставлена только для отладки, менять её незачем.
//
// Почему именно так: у FSR и DLSS буфер читается как (x*-W/2, y*+H/2), тогда как честный перевод
// NDC->пиксели требует (x*+W/2, y*-H/2). Значит в буфере лежит NDC с обратным знаком по ОБЕИМ осям,
// а XeSS с флагом USE_NDC_VELOCITY ждёт прямой — и параметра масштаба, которым это можно было бы
// поправить на месте, у него нет вовсе. Отсюда отдельный проход, см. da_xess_mv.s.
ENGINE_API int ps_r__xess_mv_sign = 1;

// [DA_PORT] Сколько объектов лежит в очередях, которые рисуются ПОСЛЕ G-буфера, и сколько света в
// кадре - СТРОКА НА КАДР, заданное число кадров подряд.
//
// Отвечает на два разных вопроса. Первый: "в какой очереди наш предмет" - хватает одного кадра.
// Второй: "что именно мигает" - а вот тут один снимок бесполезен, мигание живёт в разнице МЕЖДУ
// кадрами. Пропал объект из очереди свечения - виновата отрисовка; скачет число видимых источников -
// виноват отбор света; не меняется ничего - значит мигает то, что внутри прохода.
ENGINE_API int ps_r__emissive_probe = 0;

// The band, in pixels, that the mark is widened by. This is what addresses the ghost rather than the
// figure: the trail sits on the ground the figure has just uncovered, and those pixels are static -
// they have no motion of their own to be marked by, only a moving neighbour. Their history is the
// unreliable one, because it still holds the figure.
// Stated at the reference frame rate and widened from there as frames get slower, since the trail is
// as wide as whatever made it travels between two frames. Kept modest even so: every pixel it marks
// is a pixel the upscaler stops accumulating on, and that is paid for in shimmer.
ENGINE_API int ps_r__reactive_dilate = 3;

// Residual motion below this is ignored. Vegetation also moves through the world, and marking every
// swaying leaf reactive costs the accumulation that keeps the sway smooth - the same trade
// r__reactive_foliage already governs deliberately.
//
// Also measured rather than guessed. A static world under a still camera leaves a residual of about
// 5e-6 - half-float storage and the sub-pixel jitter, not real movement - so the floor only has to
// clear that. The first value, 0.0008, was set before there was anything to measure and sat halfway
// up the useful range, discarding most of the signal along with the noise.
ENGINE_API float ps_r__reactive_deadzone = 0.00025f;

// [DA_PORT] Which ingredient of the pass to write out instead of the mask, so that "r__motion_vectors 4"
// shows it directly. A pass whose output is zero can have any of its inputs at fault and they all look
// the same from outside, so each one gets shown on its own rather than guessed at.
//   1 = the mask the G-buffer left    2 = the raw motion vector
//   3 = eye-space depth               4 = motion through the world, before the deadzone
ENGINE_API int ps_r__reactive_debug = 0;

// [DA_PORT] One-shot readback of the pass's inputs and output into the log, with figures rather than a
// picture. Clears itself after the frame it runs on - it stalls the pipeline to map the targets.
ENGINE_API int ps_r__reactive_selftest = 0;

// [DA_PORT] The frame rate the three settings above are stated at.
//
// They are all measured in travel PER FRAME, which makes every one of them frame-rate dependent: at
// half the frame rate everything on screen moves twice as far between frames, so the same threshold
// admits twice as much, and the same band covers half of what it should. Tuned on one machine they
// would misbehave on every other - foliage marked reactive here, half the trail left behind there.
//
// So the pass converts them against this reference before use: the numbers keep meaning what they
// meant when they were measured, and the conversion absorbs the difference.
//
// 280 because that is the rate the settings above were actually tuned at, measured rather than
// assumed - the first guess of 120 silently rescaled every one of them and narrowed the band from
// five pixels to two, which read as the trail coming back.
ENGINE_API int ps_r__reactive_ref_fps = 280;

// [DA_PORT] Motion vectors for the sky, which no shader writes - see da_sky_velocity.ps. A switch
// because it is a new pass over the velocity buffer and worth being able to rule out.
ENGINE_API int ps_r__sky_velocity = 1;

// [DA_PORT] Put a stalker that has slid off the navigation mesh back onto it, rather than let it fail
// to build a path a hundred times a second forever. Measured on the swamps: two stalkers doing exactly
// that cost some two thousand wasted path searches a second between them.
//
// It is a snap to the nearest node to the stalker ITSELF, not a teleport to any fixed place - and it
// only happens when that node is within range, so nobody is ever dragged across the map. A switch,
// because moving an NPC is a real change to the world and should be possible to refuse.
ENGINE_API int ps_ai_unstick = 1;
ENGINE_API float ps_ai_unstick_range = 5.f; // metres; beyond this the stalker is left where it is

// [DA_PORT] Velocity guard: damps vegetation motion near glossy standing surfaces, so that FSR's
// velocity dilation stops dragging grass movement onto the metal behind it. Local by design - in
// open country nothing is damped at all. See da_velocity_guard.ps.
ENGINE_API float ps_r__vguard_strength = 1.f;   // 0 = pass skipped entirely
// Gloss above which a surface is worth protecting. Lowered from 0.5: the artefact is plainest on
// barrels and car panels, and half is a lot of gloss to demand before a surface counts at all.
ENGINE_API float ps_r__vguard_gloss = 0.25f;

// Neighbourhood, in pixels - and one is not a timid setting here, it is the right one.
//
// This is not a blur where wider means stronger. It exists to undo FSR's velocity dilation, and that
// dilation looks one pixel out: it takes the motion vector of the nearest-depth neighbour within a
// three-by-three. So the vegetation that steals a metal surface's vector is always immediately
// adjacent to it, and the search only has to reach that far. The nine taps here are SPACED by this
// radius rather than filling it, so the previous value of 2 sampled at plus and minus two and skipped
// the immediately neighbouring pixels entirely - the only ones that could have been at fault.
ENGINE_API int ps_r__vguard_radius = 1;
// How quickly the guard fades out once the protected surface starts moving on screen. At 1000 a
// surface travelling a thousandth of the screen in one frame - roughly one pixel - already
// disables it, which is what keeps camera movement untouched.
ENGINE_API float ps_r__vguard_still = 1000.f;

// [DA_PORT] ---- Detail-bump stability under a temporal upscaler --------------------------------
// Measured 26.07: the iridescent mottling that appears on metal (barrels, cars) and on distant ground
// under FSR 2 comes from the detail bump, and from nothing else - turning it off with r2_detail_bump
// removes it completely, while sharpening, mip bias, render scale, steep parallax, the reactive mask
// and the motion vectors were each ruled out by measurement.
//
// The mechanism: on top of tinting the albedo, the detail bump tilts the SURFACE NORMAL at the detail
// texture's own frequency and modulates gloss. A narrow specular lobe riding a normal that changes
// from one pixel to the next answers differently every time the sampling point moves, and the jitter
// moves it every frame. The upscaler then rectifies its history per colour channel, which is why the
// result is coloured fringing rather than ordinary white sparkle.
//
// These two damp that instability where it exists and leave the rest of the surface alone: the weight
// is driven by how fast the detail normal changes ACROSS THE SCREEN, so a wall seen up close, where the
// detail is properly resolved, keeps the author's look untouched. Two separate controls because the
// detail bump does two things and only measurement can say which one matters here.
// Zero disables each path entirely - the shader is then bit-identical to what it was.
ENGINE_API float ps_r__detail_gloss_fix = 0.f;
ENGINE_API float ps_r__detail_normal_fix = 0.f;
// Albedo is the third path, and on the surfaces that actually show the artefact it is the ONLY one that
// runs: metal props take the branch without a detail bump map, where a coloured detail texture is
// multiplied into the albedo. Verified in game, not assumed - see r__detail_debug 6/7.
ENGINE_API float ps_r__detail_albedo_fix = 0.f;

// [DA_PORT] Фильтрация блика по разбросу нормалей внутри пикселя (specular antialiasing).
//
// Родословная приёма короткая: Kaplanyan 2016 «Stable specular highlights», Tokuyoshi 2017,
// Tokuyoshi & Kaplanyan 2019 «Improved Geometric Specular Antialiasing». Сегодня он встроен в
// Filament, Unity HDRP и Unreal. Замер один и тот же: дисперсия нормали по экрану,
// var = strength * (|ddx N|^2 + |ddy N|^2), с потолком.
//
// ⛔ ПЕРВОИСТОЧНИК ПЕРЕВОДИТ ДИСПЕРСИЮ В ШЕРОХОВАТОСТЬ — У НАС ЭТОГО СДЕЛАТЬ НЕЛЬЗЯ, и это
// установлено по коду, а не предположено. Ширину блика в X-Ray задаёт третья координата выборки
// из s_material, а таблица (r4_rendertarget_build_textures.cpp) состоит из ЧЕТЫРЁХ разных моделей:
// pow(ls,16)*0.5, pow(ls,24), pow(ls,128) и «металл» pow(...,24) с рисунком. Ось немонотонная:
// сдвиг вниз от металла даёт не расширение блика, а сужение в 128-ю степень. Поэтому «двигать
// материал» — путь в никуда, проверено до написания кода.
//
// Остаётся то, к чему сводится приём при НЕИЗМЕННОЙ ширине лепестка, и это исходная формулировка
// Toksvig 2005: если расширить блик нельзя, его надо ровно настолько же приглушить. Для степенной
// модели с показателем n лепесток с дисперсии s^2 равносилен показателю n/(1+n*s^2), а высота пика
// падает во столько же раз — отсюда множитель 1/(1 + power * kernel), который и применяется к
// глянцу. Энергия при этом не выдумывается: пик теряет ровно то, что ушло бы в ширину.
//
// ⚠️ Чем это отличается от прежних r__detail_*_fix, которые сделали металл плоским: там гасился
// вклад ДЕТАЛИ подобранным на глаз весом и без потолка, здесь мера снимается с ИТОГОВОЙ нормали
// (то есть видит и базовый рельеф, и деталь, и наш отрицательный сдвиг мипов разом), а потолок
// не даёт заматовить сцену. Ноль отключает: множитель схлопывается в единицу.
// [DA_PORT] Склейка одинаковых предметов в одну ячейку, как в ящиках.
//
// Почему это ручка, а не починка. Мод ставит dont_stack = true в секции [identity_immunities]
// (defines.ltx), а её наследует почти всё, — то есть запрет склейки задан НАМЕРЕННО и на весь
// инвентарь разом. Прибор da_stack_debug показал ровно это: сорок отказов подряд, все с причиной
// «в конфиге стоит dont_stack». Патроны склеиваются потому, что этой секции не наследуют.
//
// Единица снимает и запрет, и сравнение по состоянию: без второго тушёнка разной свежести всё
// равно осталась бы по ячейкам, и толку от первого не было бы.
//
// ⚠️ Цена честная: из склеенной стопки берётся ПРОИЗВОЛЬНЫЙ предмет, а не самый свежий. Похоже,
// ради этого запрет и стоял. По умолчанию НОЛЬ — замысел мода не меняем, пока игрок не попросит.
ENGINE_API int ps_da_stack_all = 0;

// [DA_PORT] Прибор: при отказе склейки пишет в лог, КАКАЯ проверка её завернула.
// Ноль — молчит. Разбор в UIDragDropListEx.cpp, CUICellContainer::AddSimilar.
ENGINE_API int ps_da_stack_debug = 0;

ENGINE_API float ps_r__spec_aa = 0.f;
// Потолок добавки. 0.15 — значение по умолчанию в Filament, взято оттуда же, а не подобрано.
ENGINE_API float ps_r__spec_aa_max = 0.15f;
// Показатель степени, под который считается потеря пика. 24 — срез «Blinn» и «металл» в нашей
// таблице материалов; менять есть смысл только при замере.
ENGINE_API float ps_r__spec_aa_power = 24.f;
// 1 — красит сам множитель (белое = не трогаем), 2 — красит то, что снято.
ENGINE_API int ps_r__spec_aa_debug = 0;

// [DA_PORT] Крутой параллакс: дальность, глубина, собственная тень. Разбор — в sload.h, UpdateTC.
//
// Все четыре числа были ЗАШИТЫ в шейдер константами и наружу не выходили. Главная беда — дальность:
// рельеф начинал гаснуть на 8 метрах и пропадал полностью на 12. Для сравнения, тень солнца у нас
// держится до 140. То есть глубина исчезала на расстоянии вытянутой руки, и стена превращалась в
// плоскую картинку прямо на глазах у идущего мимо игрока — ровно тот дефект, ради которого
// параллакс и включают.
//
// Замер по архивам 20.08: параллакс просят 112 текстур из 5985, но карту высот несут 2020 — то есть
// подготовленных поверхностей в восемнадцать раз больше, чем помеченных. Расширять покрытие имеет
// смысл только после того, как сам приём начнёт выглядеть прилично на тех, что уже помечены.
// ⚠️ 8/12 — это СТОКОВЫЕ значения, и они возвращены СОЗНАТЕЛЬНО, после замера и просмотра.
//
// Промежуточно стояло 30/45: рассуждение было такое, что рельеф не должен пропадать на расстоянии
// вытянутой руки, раз тень солнца держится до 140. Рассуждение оказалось неверным — глазом дальше
// восьми метров рельеф всё равно не читается, кирпич на такой дальности занимает единицы пикселей.
//
// А платили за это дорого: замер da_frame 20.08 дал 299 -> 282 кадра, то есть около 0.2 мс, и
// почти всё — на дальнем плане, где эффекта не видно. Площадь экрана под рельефом растёт с
// дальностью быстрее, чем линейно, поэтому расширение границы стоит непропорционально много.
ENGINE_API float ps_r__parallax_start = 8.f; // метры: докуда рельеф в полную силу
ENGINE_API float ps_r__parallax_stop = 12.f; // метры: где он пропал совсем
// Глубина продавливания в единицах текстурных координат. В шейдере стояло 0.013 (со знаком минус,
// знак учтён на месте). Это «насколько далеко уезжает текстура» — не метры и не тексели.
// 0.0105 — принято глазом 20.08 после перебора. Стоковые 0.013 давали «пьяные», желеобразные
// кромки кирпича на остром угле: чем глубже продавливание, тем длиннее промах луча на обрыве и
// тем сильнее растягивается боковая грань, которой в текстуре нет вовсе. Минус 19% убрали
// искажение, не потеряв читаемой глубины швов.
ENGINE_API float ps_r__parallax_depth = 0.0105f;
// Плотность собственной тени. 0 — поведение до правки (тени нет вовсе).
//
// Это ПРЯМОЙ множитель перекрытия, а не доля: перекрытие живёт в долях высоты рельефа и редко
// переваливает за 0.15, поэтому осмысленные значения — единицы, а не доли единицы.
//
// Четвёрка принята глазом 20.08 — уже ПОСЛЕ того, как множитель стали накладывать и на блеск.
// (Промежуточно стояла единица: тогда тень действовала на одно альбедо и поверхность отдавала
// зеркалом, так что то значение к нынешнему поведению отношения не имеет.)
ENGINE_API float ps_r__parallax_shadow = 4.f;
// Шаги поиска пересечения: в упор — max, на скользящем угле — min. Было зашито 25 и 5.
ENGINE_API int ps_r__parallax_samples = 32;
// Двойка, а не восьмёрка — принято глазом 20.08. Это шаги при взгляде В УПОР, где смещение почти
// нулевое по определению, а недостающую точность добирает двоичное уточнение (см. sload.h). Разбор
// цены при этом настоящий: на стену, в которую упёрся игрок, приходится большая часть экрана.
ENGINE_API int ps_r__parallax_samples_min = 2;
// Шаги луча к солнцу. Считаются для КАЖДОГО пикселя рельефа, поэтому дешевле их держать вдвое
// меньше, чем шагов основного поиска: собственная тень мягкая, ступеньки в ней глаз не ловит.
ENGINE_API int ps_r__parallax_shadow_samples = 8;
// Параллакс ВСЕМ, у кого есть карта высот, а не только 112 помеченным вручную. Разбор — в
// TextureDescrManager.cpp, UseSteepParallax. ⚠️ Нужна перезагрузка уровня: имя шейдера собирается
// один раз, при постройке блендера.
//
// Включено по умолчанию: 2020 поверхностей вместо 112, принято глазом 20.08. Пометку в редакторе
// GSC ставили вручную, и до подавляющего большинства подходящих текстур просто не дошли руки —
// признак «есть пара _bump/_bump#» у них ровно тот же самый.
ENGINE_API int ps_r__parallax_force = 1;
// [DA_PORT] Сила затенения полостей. Применяется в combine_1.ps, там же разбор.
//
// ЕДИНИЦА — как было, картинка не меняется. Ноль убирает затенение целиком, больше единицы —
// усиливает в углах и примыканиях.
//
// Зачем ручка нужна отдельно от r2_ssao: та величина уходит в ДЕФАЙНЫ шейдера и в имя кэша, то есть
// на лету не переключается — нужен перезапуск игры (в меню это помечено верно, «restart»). Судить
// по такому переключению о вкладе затенения почти невозможно: между двумя запусками меняется всё
// сразу — погода, время, положение камеры. Здесь величина приходит константой и меняется мгновенно.
ENGINE_API float ps_r__ssao_power = 1.f;
// 1 — красит САМ множитель затенения вместо картинки. Белое не затенено, чёрное затенено полностью.
// Единственный способ отличить «затенение слабое» от «его тут нет вовсе», а на глаз это неразличимо:
// и то, и другое выглядит как плоская картинка.
ENGINE_API int ps_r__ssao_debug = 0;

// [DA_PORT] Разрыв повторов текстуры шестиугольной решёткой (Mikkelsen, JCGT 2022). Разбор — в
// da_hextile.h. Каждый шестиугольник берёт текстуру со своим случайным сдвигом, точка смешивается
// из трёх ближайших, узор рассыпается.
//
// ⚠️ Выключено по умолчанию, и не только из-за цены (три выборки вместо одной у цвета, рельефа и
// высоты). Приём рассчитан на текстуры БЕЗ выраженной структуры — грунт, бетон, ржавчина,
// штукатурка. На правильном узоре вроде кирпичной кладки случайный сдвиг разрывает ряды: швы
// соседних плиток не сходятся, и это видно. Отбор по материалам ещё не сделан.
ENGINE_API int ps_r__hex_tiling = 0;
// Размер шестиугольника: сколько их приходится на единицу текстурных координат. Меньше — крупнее
// куски и реже стыки, больше — мельче перемешивание.
ENGINE_API float ps_r__hex_scale = 1.f;
// Сила поворота плиток. НОЛЬ по умолчанию: поворот добивает повторяемость там, где у текстуры есть
// направление (галька в статье), но у любого правильного узора он же её и ломает.
ENGINE_API float ps_r__hex_rot = 0.f;
// Контраст весов: 0.5 — как есть, больше — переход между плитками резче, меньше — мягче.
ENGINE_API float ps_r__hex_contrast = 0.5f;

// 1 — красит саму тень, 2 — карту высот, 3 — все пиксели, куда параллакс вообще дошёл.
//
// Третий режим существует по той же причине, что и у детальных текстур: он отделяет «поправка мала»
// от «этот код здесь не выполняется вовсе», а перепутать их без прибора очень легко.
ENGINE_API int ps_r__parallax_debug = 0;

// [DA_PORT] Paints the damping weight instead of the surface: 1 = the weight the normal path applies,
// 2 = the weight the gloss path applies. Bright means "fully damped here", black means "untouched".
// This exists because the weight is a rate of change measured in screen space, and there was no way to
// guess its magnitude in advance - a slider whose useful range is unknown is indistinguishable from a
// slider that is not connected at all. With this the calibration takes one look instead of a sweep.
ENGINE_API int ps_r__detail_debug = 0;

// [DA_PORT] Scales the sway amplitude of trees and grass. 0 freezes the vegetation completely while
// leaving everything else - time, weather, lighting - running, which is the only way to ask whether a
// temporal artefact is being driven by movement in the scene or lives entirely in the shading of the
// surface it appears on. Amplitude rather than speed, so the previous-frame copies the motion vectors
// are built from stay consistent with the current ones.
ENGINE_API float ps_r__wind_scale = 1.f;

// [DA_PORT] ---- Glossy surfaces opt out of temporal accumulation -------------------------------
// Measured 26.07: metal breaks up under FSR 2 not because of anything on the metal, but because of
// what moves AROUND it - freezing the vegetation with r__wind_scale 0 makes the surface clean and
// sharp with nothing else changed. Swaying foliage keeps disturbing the light reaching the surface,
// and a narrow specular lobe turns that into a large change in every pixel every frame, which the
// reconstruction then blends into iridescent mush.
//
// So the surface is told to stop accumulating: the reactive mask already exists as an input to both
// FSR 2 and XeSS, and it means exactly "do not trust the history here". Alpha-tested foliage uses it
// already; this extends it to gloss. The trade is deliberate - a reactive pixel shows the current
// frame instead of a blend, so metal becomes sharper and noisier rather than smeared.
//
// weight: how strongly glossy pixels reject history, 0 disables the whole thing.
// threshold: gloss below this is left alone entirely, so wood, brick and ground never qualify.
// [DA_PORT] 0 = vegetation stands still in the shadow map while still swaying on screen. See
// FTreeVisual::Render for the measurement behind it.
ENGINE_API int ps_r__wind_shadow = 1;

// [DA_PORT] Качание ТРАВЫ на сильном ветру: 0 - как в исходном движке, 1 - как в Dead Air.
//
// В [details] у мода стоит swing_fast_amp1 = 0.65 при 0.25 у основы, amp2 = .50 при 0.15 и
// speed = 20 при 1 - прежние значения автор не удалил, а оставил рядом в комментариях. Разгон
// сделан сознательно, но в двадцать раз более частое качание читается уже не как ветер, а как
// дрожь, и вдобавок дорого обходится временной реконструкции.
//
// ⚠️ По умолчанию 0, то есть исходное поведение. Это ЕДИНСТВЕННОЕ место, где порт сознательно
// расходится с данными мода по картинке; вернуть авторский вид - `r__grass_sway 1`.
//
// Тихая погода не затронута: swing_normal_* у мода и у основы совпадают, автор их не трогал.
// Промежуточные значения осмысленны - это плавный переход между двумя наборами.
ENGINE_API float ps_r__grass_sway = 0.f;

// [DA_PORT] Частота качания травы, множителем к тому, что получилось выше. Единица - не вмешиваться.
//
// Отдельная ручка не для симметрии: ощущение «трясётся» даёт именно частота, а не размах, и она же
// дороже всего обходится временной реконструкции - чем быстрее мечется стебель, тем меньше смысла в
// накопленной истории (см. r__reactive_foliage). Уменьшить частоту, оставив размах, - и спокойнее
// на глаз, и чище в движении.
ENGINE_API float ps_r__grass_sway_speed = 1.f;

// [DA_PORT] ---- Лужи в дождь ------------------------------------------------------------------
// Шейдерам нужны два числа: СКОЛЬКО льёт прямо сейчас и НАСКОЛЬКО земля успела намокнуть. Первое —
// rain_density текущей погоды, оно уже есть в движке и правит частицами дождя. Второе движок не
// считал никогда, а без него луж не бывает: они не появляются в тот же кадр, когда пошёл дождь, и
// не исчезают, когда он кончился. Накопитель ниже и есть вся разница между «мокрым выключателем» и
// погодой — вода набирается за минуты и высыхает дольше, чем набралась.
//
// Обе величины уезжают в шейдеры одной константой rain_params (см. Blender_Recorder_StandartBinding).
ENGINE_API int ps_r__puddles = 1;

// Во сколько раз быстрее набирается влага, чем высыхает. Время набора при сплошном дожде —
// r__puddles_buildup секунд; высыхание идёт в r__puddles_dry раз дольше.
ENGINE_API float ps_r__puddles_buildup = 90.f;
ENGINE_API float ps_r__puddles_dry = 4.f;

// Доля поверхности, которую занимают лужи при полной влажности: 0 — только блеск мокрого асфальта,
// 1 — сплошная вода. Уходит в шейдер тем же вектором, чтобы подбирать вид без пересборки шейдеров.
ENGINE_API float ps_r__puddles_size = 0.80f;

// Принудительная сырость для проверки: 0 — как в погоде, больше нуля — считать, что льёт именно так,
// и влага уже набралась. Иначе подбор вида упирается в ожидание дождя, а он в Зоне не по расписанию.
ENGINE_API float ps_r__puddles_force = 0.f;

// [DA_PORT] Вид воды — отдельной константой, чтобы правился В ИГРЕ, а не пересборкой шейдера.
// Первые две пробы ушли в молоко именно на этом: каждое число стоило правки, компиляции, снятия
// кэша шейдеров и перезапуска, а решается всё равно на глаз.
//
// gloss — насколько лужа зеркальна. Главная ручка: солнце в мокром асфальте бликует очень сильно, и
// значения выше 0.4 в этом движке дают выбеленное пятно вместо воды.
ENGINE_API float ps_r__puddles_gloss = 1.00f;

// Во сколько раз лужа темнее сухой земли. ⚠️ Единица (то есть «не темнее») — НЕ забытая заглушка, а
// утверждённый в игре вид: вода читается чистым глянцем, без потемнения. Все значения ниже подобраны
// на экране и приняты; менять их «по смыслу» не надо, вид складывается из всех четырёх сразу.
ENGINE_API float ps_r__puddles_dark = 1.00f;	// утверждено в игре: вода читается глянцем, без потемнения

// Глянец просто мокрой земли, без лужи. Держать заметно ниже gloss, иначе блестит вообще всё.
ENGINE_API float ps_r__puddles_damp = 0.10f;

// Множитель ряби от капель.
ENGINE_API float ps_r__puddles_ripple = 1.f;

// [DA_PORT] --- Сезон -------------------------------------------------------------------------------
//
// Лето — это архив `xtra_green.xdb0` с зелёными текстурами; осень — та же игра без него. Раньше сезон
// переключался перекладыванием 204 МБ между каталогами при закрытой игре; теперь архив всегда на месте,
// а подключать его или нет решает признак в файле `database\da_season.txt` (см. `CLocatorAPI::
// ProcessArchive`). Зима не участвует: у неё четыре архива и два гигабайта, это отдельная работа.
//
// ⚠️ Применяется со следующего запуска, и иначе быть не может: архивы монтируются на старте движка, а
// уровень держит ссылки на уже загруженные текстуры. Поэтому подпись в меню обязана про это говорить —
// молчаливая настройка «которая не работает» хуже её отсутствия.
ENGINE_API u32 ps_da_season = 0;

ENGINE_API xr_token qda_season_token[] = {
    { "ui_mm_season_autumn", 0 },
    { "ui_mm_season_summer", 1 },
    { nullptr, 0 },
};

// [DA_PORT] Лежит ли летний архив там, где его ищет файловая система.
//
// Проверяем именно файлом на диске: сам архив в виртуальной файловой системе не числится — он ею и
// является, поэтому FS.exist по нему ничего не знает.
//
// ⚠️ Без этой проверки выбор «лето» был МОЛЧАЛИВО бесполезен у части игроков. В оригинальной
// установке Dead Air летний архив лежит не в database, а в отдельной папке «Летняя растительность
// (опционально)» — автор оставил перенос на усмотрение игрока. Кто не переносил, тот выбирал лето в
// меню, признак послушно записывался, игра перезапускалась — и ничего не менялось, без единого слова.
// Пришло с закрытого теста именно так: «при смене осень→лето не заработало лето».
static bool da_season_summer_archive_present()
{
    string_path summer_archive;
    FS.update_path(summer_archive, "$arch_dir$", "xtra_green.xdb0");

    if (FILE* archive = fopen(summer_archive, "rb"))
    {
        fclose(archive);
        return true;
    }

    return false;
}

// [DA_PORT] ⚠️ Хранилище у сезона РОВНО ОДНО — файл-признак. В user.ltx команда не пишется намеренно.
//
// Пока писалась, хранилищ было два, и они разъезжались. Порядок на старте такой: файловая система
// монтирует архивы по признаку, и лишь ПОТОМ исполняется user.ltx — а он выполняет `da_season` и
// переписывает признак своим значением. То есть игра шла с одним сезоном, а признак уже говорил про
// другой, и настоящее переключение случалось только со следующего запуска. Наблюдалось живьём: в
// user.ltx лежало лето (список в меню сохранил позицию сам), признак был осенним — и запуск молча
// перевёл игру на лето.
//
// Теперь user.ltx о сезоне не знает вовсе, а значение для меню и консоли поднимается из признака при
// регистрации команды — см. da_season_read_marker ниже.
class CCC_DaSeason : public CCC_Token
{
public:
    CCC_DaSeason(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Save(IWriter*) override {}

    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);

        // Пишем признак рядом с архивами — там его и читает файловая система на следующем старте.
        string_path marker;
        FS.update_path(marker, "$arch_dir$", "da_season.txt");

        if (FILE* f = fopen(marker, "wb"))
        {
            const pcstr value = (1 == ps_da_season) ? "summer" : "autumn";
            fwrite(value, 1, xr_strlen(value), f);
            fclose(f);
            Msg("* [DA_PORT] сезон: %s (применится после перезапуска игры)", value);
        }
        else
            Msg("! [DA_PORT] сезон: не удалось записать %s", marker);

        if (1 == ps_da_season && !da_season_summer_archive_present())
        {
            Msg("! [DA_PORT] сезон: выбрано лето, но архива xtra_green.xdb0 в database НЕТ - "
                "трава останется осенней");
            Msg("! [DA_PORT] сезон: перенесите его туда из папки 'Летняя растительность (опционально)'");
        }
    }
};

// [DA_PORT] Поднять текущий сезон из того же признака, по которому файловая система уже смонтировала
// (или не смонтировала) летний архив. Вызывается при регистрации команды, то есть ДО user.ltx.
//
// Признака может не быть — например, у того, кто переключал сезон старым способом, перекладыванием
// архива. Тогда правду говорит наличие самого архива: лежит `xtra_green.xdb0` — значит игра уже
// запустилась летней (см. da_seasonal_archive_enabled: без признака архив подключается).
static void da_season_read_marker()
{
    string_path marker;
    FS.update_path(marker, "$arch_dir$", "da_season.txt");

    if (FILE* f = fopen(marker, "rb"))
    {
        char buf[32] = {};
        const size_t got = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[got] = 0;
        ps_da_season = (nullptr != strstr(buf, "summer")) ? 1 : 0;
        // Сезон невидим, пока не посмотришь на деревья, а вопрос «почему у меня зелень» задают по
        // логу. Одна строка на запуск — и состояние больше не приходится угадывать.
        Msg("* [DA_PORT] сезон: %s", (1 == ps_da_season) ? "лето" : "осень");

        // Признак говорит «лето», а архива нет — игрок УЖЕ выбрал лето и уже его не получил.
        // Сказать об этом надо здесь, а не только в момент выбора: между выбором и запуском
        // проходит перезапуск, и до этой строки лог доживает, а прошлый - нет.
        if (1 == ps_da_season && !da_season_summer_archive_present())
        {
            Msg("! [DA_PORT] сезон: выбрано лето, но архива xtra_green.xdb0 в database НЕТ - "
                "игра идёт осенней");
            Msg("! [DA_PORT] сезон: перенесите его туда из папки 'Летняя растительность (опционально)'");
        }
        return;
    }

    if (da_season_summer_archive_present())
    {
        ps_da_season = 1;
        Msg("* [DA_PORT] сезон: лето (признака нет, летний архив на месте)");
        return;
    }

    ps_da_season = 0;
    Msg("* [DA_PORT] сезон: осень (признака нет, летнего архива тоже)");
}

// [DA_PORT] Яркость луча фонарей. Множитель к цвету лампы, а не к дальности: цвет фонарей задаёт
// xr_actor.script (налобному — каждый тик), поэтому любое значение, выставленное в движке напрямую,
// затирается через кадр. Множитель переживает это, потому что применяется уже ПОСЛЕ скрипта.
// Раздельно у налобного и ручного: у них разные дальность и конус, и одинаковый множитель даёт
// разный результат на экране.
ENGINE_API float ps_r__torch_bright = 3.0f;      // налобный (torch2: дальность 12, конус 95)
ENGINE_API float ps_r__torch_bright_item = 3.0f; // ручной фонарик (дальность 60, конус 50)

// [DA_PORT] ---- Дождь -------------------------------------------------------------------------
// Все размеры капель были зашиты числами в двух файлах сразу (Rain.cpp и dxRainRender.cpp), причём
// половина — закомментированными дублями. Значения родом из 2007 года и рассчитаны на 800×600: капля
// длиной ПЯТЬ МЕТРОВ и шириной ТРИДЦАТЬ САНТИМЕТРОВ. На современном разрешении это не капли, а
// полосы поперёк экрана.
//
// Правильная длина считается из выдержки: капля летит 40–80 м/с, кадр 1/60 с, значит след 0.7–1.3 м.
// Ширина у настоящей капли миллиметры; для видимости оставлен запас, но не в сто раз.
ENGINE_API float ps_r__rain_len = 2.0f;
ENGINE_API float ps_r__rain_width = 0.20f;	// 0.08 не читалось на экране: при рендере 77% полоска в доли пикселя

// Яркость капель множителем к rain_color погоды. Нужна потому, что в конфигах ливня цвет капли —
// (0.34, 0.31, 0.26), тёмно-серо-бурый: при исходной ширине в 30 см такую каплю было видно просто за
// счёт размера, а тонкую — уже нет, особенно на фоне тёмного грозового неба. Настоящий дождь ловит
// свет неба и читается СВЕТЛЕЕ фона, а не темнее.
ENGINE_API float ps_r__rain_bright = 2.2f;

// Яркость ВСПЛЕСКОВ на земле — отдельно от капель. В основе они делили один цвет, и поднятая яркость
// капли превращала всплески в белую крупу на тёмной земле: «будто град падает». Летящая капля должна
// читаться светлее фона, лежащий на земле всплеск — нет, он в тени и мокрый.
ENGINE_API float ps_r__rain_splash_bright = 0.9f;



// Сколько капель в воздухе вокруг игрока и в каком радиусе. Капли тоньше — значит их нужно больше,
// иначе дождь редеет. ⚠️ Число капель стоит ДОРОГО: на каждое рождение делается луч в геометрию,
// чтобы найти, где капля разобьётся. При 6000 это около трёхсот лучей на кадр.
ENGINE_API int ps_r__rain_drops = 6000;
ENGINE_API float ps_r__rain_radius = 14.0f;

// Всплески на земле. В основе стоял отказ на КАЖДЫЙ ВТОРОЙ удар (`if (0 != Random.randI(2)) return`)
// — то есть половина капель падала беззвучно и бесследно. Теперь это доля, и по умолчанию всплеск
// даёт каждая капля: именно всплески, а не сами капли, показывают, что дождь идёт по земле.
ENGINE_API float ps_r__rain_splash = 1.0f;
ENGINE_API float ps_r__rain_splash_time = 0.30f;

// [DA_PORT] ---- «Стоим ли мы в луже» для остальной игры -------------------------------------------
//
// Накопленную влажность считает биндер рендера (там она и нужна каждый кадр), а сюда только
// складывает: игровому коду негде взять её самому, а звук шагов по воде — это уже игровой код.
ENGINE_API float g_da_rain_wetness = 0.f;

// Тот же шум, что в da_puddles.h, слово в слово. Иначе звук и картинка разойдутся: игрок будет
// слышать плеск, стоя на сухом, и молчание — стоя в воде. Совпадать обязаны и константы, и порядок
// действий.
static float da_hash21(float px, float py)
{
    px = px * 127.1f;
    py = py * 311.7f;
    px -= floorf(px);
    py -= floorf(py);
    const float d = px * (px + 34.23f) + py * (py + 34.23f);
    px += d;
    py += d;
    const float r = px * py;
    return r - floorf(r);
}

static float da_vnoise(float px, float py)
{
    const float ix = floorf(px), iy = floorf(py);
    float fx = px - ix, fy = py - iy;
    fx = fx * fx * (3.f - 2.f * fx);
    fy = fy * fy * (3.f - 2.f * fy);
    const float a = da_hash21(ix, iy);
    const float b = da_hash21(ix + 1.f, iy);
    const float c = da_hash21(ix, iy + 1.f);
    const float d = da_hash21(ix + 1.f, iy + 1.f);
    const float top = a + (b - a) * fx;
    const float bot = c + (d - c) * fx;
    return top + (bot - top) * fy;
}

// Есть ли лужа в этой точке мира. Порог взят с запасом относительно шейдерного: у самой кромки
// картинка показывает лужу в полсилы, а звук — величина «да/нет», и на границе он бы дребезжал.
ENGINE_API bool da_puddle_at(const Fvector& p)
{
    if (!ps_r__puddles || g_da_rain_wetness < 0.15f)
        return false;

    const float n = da_vnoise(p.x * 0.33f, p.z * 0.33f) * 0.62f + da_vnoise(p.x * 1.10f, p.z * 1.10f) * 0.38f;
    const float size = (ps_r__puddles_size < 0.f) ? 0.f : (ps_r__puddles_size > 1.f ? 1.f : ps_r__puddles_size);
    const float thr = (0.86f + (0.30f - 0.86f) * size) + (1.f - g_da_rain_wetness) * 0.15f;
    return (n - thr) > 0.06f;
}

// [DA_PORT] ---- Пункты меню «Дождь» ------------------------------------------------------------
//
// Дистанция луж. Значение токена — это САМИ МЕТРЫ, а не порядковый номер: так значение из user.ltx
// читается человеком и не «переезжает», если однажды добавить четвёртую ступень между этими.
// [DA_PORT] Отражения в лужах: отдельный полноэкранный проход (см. r4_rendertarget_phase_da_puddle_refl).
// Отдельной ручкой от самих луж потому, что стоит она заметно дороже: это ray-march по глубине, тот
// же, которым отражает вода.
ENGINE_API int ps_r__puddles_refl = 1;
ENGINE_API float ps_r__puddles_refl_power = 1.5f;	// подобрано в игре и утверждено

// [DA_PORT] Нижняя граница френеля у луж: сколько отражения остаётся, когда смотришь В УПОР.
//
// Физически честное значение около 0.05 — вода в упор почти не зеркалит, и лужа под ногами
// пропадает. Это верно, но в кадре читается как «отражений нет». Ручка задаёт пол этой кривой:
// вблизи горизонта отражение по-прежнему полное, меняется только взгляд сверху вниз.
ENGINE_API float ps_r__puddles_facing = 0.10f;

// [DA_PORT] Доля отражения НЕБА там, где луч-марш не нашёл геометрии. Без неё лужа при взгляде
// сверху вниз пропадала: луч уходит в небо, а неба в буфере глубины нет.
ENGINE_API float ps_r__puddles_sky = 0.15f;

// [DA_PORT] 1 — лужи меняют G-буфер (нормаль, глянец, потемнение, кайма) плюс проход отражений;
// 0 — только проход отражений поверх кадра. Обводка родится исключительно в первом варианте: у края
// маски встречаются бугристая земля и плоская вода. Второй свободен от неё по построению, но теряет
// отклик воды на лампы и костры. Ручка — чтобы сравнить в игре, а не спорить.
ENGINE_API int ps_r__puddles_gbuf = 1;

// [DA_PORT] Жёсткость края лужи, 0..1. Единица — чистая ступень без смешивания: именно она убирает
// зернистую обводку по контуру. Меньше единицы — переход мягче, зерно возвращается. Умолчание 1.
ENGINE_API float ps_r__puddles_edge = 0.3f; // подобрано в игре 03.08 вместе с нормалью-«вверх»

// [DA_PORT] Кайма промокшего грунта вокруг воды: сила потемнения и ширина полосы. Значения
// утверждены в игре 31.07 вместе с остальным видом луж.
ENGINE_API float ps_r__puddles_rim = 0.72f;
ENGINE_API float ps_r__puddles_rim_width = 0.22f;

// Качество луж одной ступенью. Низкое — только влажная земля, без самих луж: это дёшево и всё равно
// читается как дождь. Среднее — лужи как они есть. Высокое — они же с отражениями, а отражения это
// отдельный полноэкранный проход с ray-march, то есть заметно дороже остального.
ENGINE_API u32 ps_r__puddles_quality = 2;
ENGINE_API xr_token qpuddles_quality_token[] = {
    { "ui_mm_puddles_q_low", 0 },
    { "ui_mm_puddles_q_med", 1 },
    { "ui_mm_puddles_q_high", 2 },
    { nullptr, 0 },
};

ENGINE_API void da_apply_puddles_quality()
{
    switch (ps_r__puddles_quality)
    {
    case 0: // низкое — только влажная земля
        ps_r__puddles_size = 0.f;
        ps_r__puddles_refl = 0;
        break;
    case 1: // среднее — лужи без отражений
        ps_r__puddles_size = 0.80f;
        ps_r__puddles_refl = 0;
        break;
    default: // высокое — лужи с отражениями
        ps_r__puddles_size = 0.80f;
        ps_r__puddles_refl = 1;
        break;
    }
}

class CCC_PuddlesQuality : public CCC_Token
{
public:
    CCC_PuddlesQuality(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}
    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        da_apply_puddles_quality();
    }
};

// Тёмная кайма промокшей земли вокруг воды. У настоящей лужи она есть всегда: грунт у кромки
// напитан водой и темнее сухого, а к краю сходит на нет. Заодно это лучшее лекарство от «обводки» —
// светлая линия перехода тонет в тёмной пелене, вместо того чтобы бороться с ней в лоб.
//
// _rim — во сколько раз кайма темнее сухой земли (1 = нет каймы), _rim_width — её ширина в долях
// шума: 0.1 узкая полоска, 0.4 широкий разлив мокроты.

ENGINE_API u32 ps_r__puddles_dist = 20;
ENGINE_API xr_token qpuddles_dist_token[] = {
    { "ui_mm_puddles_dist_low", 14 },
    { "ui_mm_puddles_dist_med", 20 },
    { "ui_mm_puddles_dist_high", 30 },
    { nullptr, 0 },
};

// Качество дождя — одна ступень вместо шести ручек. Ручки остаются для тонкой настройки, но игроку
// в меню нужен выбор «дешевле / красивее», а не шесть чисел, из которых дорого ровно одно.
ENGINE_API u32 ps_r__rain_quality = 1;
ENGINE_API xr_token qrain_quality_token[] = {
    { "ui_mm_rain_quality_low", 0 },
    { "ui_mm_rain_quality_med", 1 },
    { "ui_mm_rain_quality_high", 2 },
    { nullptr, 0 },
};

// Пресеты качества дождя. Дорого только число капель: на каждое рождение делается луч в геометрию,
// поэтому от ступени к ступени растёт в первую очередь оно, а размеры почти не меняются.
ENGINE_API void da_apply_rain_quality()
{
    switch (ps_r__rain_quality)
    {
    case 0: // низкое
        ps_r__rain_drops = 2500;
        ps_r__rain_radius = 10.f;
        ps_r__rain_splash = 0.5f;
        ps_r__rain_splash_time = 0.25f;
        break;
    default: // среднее
        ps_r__rain_drops = 6000;
        ps_r__rain_radius = 14.f;
        ps_r__rain_splash = 1.0f;
        ps_r__rain_splash_time = 0.30f;
        break;
    case 2: // высокое
        ps_r__rain_drops = 12000;
        ps_r__rain_radius = 20.f;
        ps_r__rain_splash = 1.0f;
        ps_r__rain_splash_time = 0.40f;
        break;
    }
}

class CCC_RainQuality : public CCC_Token
{
public:
    CCC_RainQuality(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}
    void Execute(pcstr args) override
    {
        CCC_Token::Execute(args);
        da_apply_rain_quality();
    }
};

// Отладочная раскраска: красный — маска луж, зелёный — общая мокрота, синий — «видно небо», плюс
// клетка по метру из восстановленных мировых координат. Нужна потому, что «воды не видно» — это
// сразу пять возможных причин, и перебирать их по одной дороже, чем один раз посмотреть на маску.
ENGINE_API int ps_r__puddles_debug = 0;

// [DA_PORT] 0 = vegetation reports no motion at all to the upscaler. FSR 2 dilates velocity from the
// nearest-depth neighbour, so grass and branches standing in front of a static surface can push their
// own motion onto it; the surface then has its history fetched as though it had moved with them.
// This is the only remaining path by which wind reaches metal, all the shading routes having been
// ruled out by measurement.
// [DA_PORT] Feeds the foliage mask to FSR 2's transparency-and-composition input as well as its
// reactive one. The two ask different questions of the same pixels, and alpha-tested vegetation
// arguably answers yes to both.
ENGINE_API int ps_r__foliage_tandc = 0;

// Trees and grass separately: the contamination comes overwhelmingly from grass, which grows right
// against objects and is always the nearer surface, while trees usually stand clear of anything.
// So grass can be silenced without paying for it on the canopy.
ENGINE_API float ps_r__foliage_velocity = 1.f;   // trees
ENGINE_API float ps_r__grass_velocity = 1.f;     // grass and bushes

ENGINE_API float ps_r__reactive_gloss = 0.f;
ENGINE_API float ps_r__reactive_gloss_min = 0.5f;

// [DA_PORT] Which way round the jitter is handed to FSR 2: 0 = (+x,-y), 1 = (-x,+y), 2 = (+x,+y),
// 3 = (-x,-y). The engine stores the jitter in clip space (y up), the library wants pixels (y down),
// and which axis ends up needing the flip depends on conventions on both sides. Getting it wrong
// leaves part of the jitter uncompensated and the whole picture shakes, so this is switchable rather
// than guessed: one command instead of a rebuild per attempt.

// [DA_PORT] Sign of the motion vectors handed to FSR 2: 0 = (-x,+y), 1 = (-x,-y), 2 = (+x,+y),
// 3 = (+x,-y). Our buffer stores "current minus previous", while FSR 2 wants the vector pointing back
// to where the pixel was — an opposite sign — and on top of that the two disagree about which way the
// vertical axis runs. Getting this wrong makes the upscaler pull history the wrong way, which looks
// exactly like the picture shaking, and is indistinguishable by eye from a jitter problem.

// [DA_PORT] One control instead of two. The pair that actually drives the upscale — render scale and
// sharpening — means nothing to a player, and the whole point of this feature is the people running the
// mod on old hardware. The preset writes both; the individual console variables still work for tuning.
ENGINE_API u32 ps_r__upscale_preset = 0;
ENGINE_API xr_token qupscale_preset_token[] = {
    { "st_da_upscale_off", 0 },
    { "st_da_upscale_quality", 1 },
    { "st_da_upscale_balanced", 2 },
    { "st_da_upscale_performance", 3 },
    { nullptr, 0 },
};

// [DA_PORT] Post-resolve sharpening, in percent. Temporal accumulation is inherently softening — every
// frame the history is resampled and averaged — so a little high-frequency restoration afterwards is
// part of the technique, not a cosmetic extra.
ENGINE_API int ps_r__taa_sharp = 20;

// [DA_PORT] Show the temporal resolve where it fetches history from - see da_taa.ps.
// 1 = reprojection offset, 2 = the sky mask this pass actually sees, 3 = how far the fetched history
// is from the current pixel. The last two exist because the sky question was asked with tools that
// measure a DIFFERENT pass at a DIFFERENT point in the frame, and agreement there proves nothing here.
ENGINE_API int ps_r__taa_debug = 0;

// [DA_PORT] How much of the normal history weight the SKY keeps, in percent. 100 is the behaviour
// everything else has.
//
// Zero by default, which is a SUPPRESSION and not a diagnosis - worth being honest about, because the
// difference decides whether anyone looks again.
//
// The sky trailing behind the camera survived every check of the reprojection: the maths in da_taa.ps
// is the same analytic rotation-only reprojection that da_sky_velocity.ps uses, and that one
// demonstrably fixed the identical symptom under the upscalers. So this started as a falsification
// switch - at 0 the sky is the raw current frame and nothing else - and it came back positive: the
// trail is made HERE, in the accumulation, and not upstream. Why a reprojection that checks out
// analytically still fetches a wrong history on sky is still unanswered; r__taa_debug 3 is the tool
// that would answer it.
//
// What zero costs: the sky is still jittered along with everything else (under our own temporal AA the
// offset sits in Device.mProject, so the skybox does move sub-pixel every frame), and with no history
// there is nothing left to average that jitter out. On the smooth gradient that is invisible. On the
// sun, on cloud edges and along the horizon it is the shimmer this pass was extended to cover the sky
// for in the first place. If that comes back, the middle ground is a partial value rather than 100:
// the trail lasts about 1/(1-feedback) frames, so around 40 keeps two frames of averaging while
// cutting the fourteen-frame smear that made this visible.
ENGINE_API int ps_r__taa_sky = 0;

// [DA_PORT] Negative mip bias to pair with TAA, in hundredths of a level. Off by default: it sharpens
// distant textures, but it also hands the temporal filter more aliasing than it can absorb on foliage.
ENGINE_API int ps_r__taa_mipbias = 0;

// [DA_PORT] Projection jitter, separately switchable. TAA has two halves that fail in different ways —
// jitter turns aliasing into sub-pixel samples, the resolve averages them — and being able to run the
// resolve without the jitter is what tells the two apart when something shimmers.
ENGINE_API int ps_r__taa_jitter = 1;

// [DA_PORT] Временное подавление джиттера на время замеров. ОТДЕЛЬНО от ps_r__taa_jitter, и вот
// почему: прибор гасил саму настройку, а она СОХРАНЯЕМАЯ. Прогон прерван (закрыл игру, снял отчёт не
// до конца) - ноль уезжает в user.ltx навсегда. Дальше апскейлер лишается субпиксельного сдвига, то
// есть реконструировать ему уже не из чего, и он просто растягивает кадр: картинка "пиксельная", а
// кадров подозрительно много. Причину по виду не угадать - в настройках стоит ноль, которого игрок
// туда не ставил.
//
// ⇒ Правило: диагностика НИКОГДА не пишет в переменную, которая уходит в конфиг.
ENGINE_API bool g_da_jitter_suppress = false;
ENGINE_API shared_str current_player_hud_sect{};

extern u32 ps_fps_limit;
extern u32 ps_fps_limit_in_menu;

// [DA_PORT] Переходник со сборки, где синхронизация была ПУНКТОМ этого списка, а не галочкой.
//
// У того, кто её тогда выбрал, в user.ltx осталась строка `rs_fps_limit st_opt_fps_vsync`. Такого
// значения в списке больше нет, и без этой ветки строка дала бы «Invalid syntax» в лог, а настройка
// молча вернулась бы к значению по умолчанию: игрок включал синхронизацию, а получил её отсутствие
// и ни слова о том, почему.
//
// Переводим в то же самое, но по-новому: галочка включена, потолка нет.
class CCC_FpsLimit final : public CCC_Token
{
public:
    CCC_FpsLimit(pcstr N, u32* V, const xr_token* T) : CCC_Token(N, V, T) {}

    void Execute(pcstr args) override
    {
        if (args && 0 == xr_strcmp(args, "st_opt_fps_vsync"))
        {
            psDeviceFlags.set(rsVSync, TRUE);
            *value = ps_fps_limit_unlimited;
            Msg("* [DA_PORT] синхронизация переехала из списка в галочку - настройка перенесена");
            return;
        }

        CCC_Token::Execute(args);
    }
};

void CCC_Register()
{
    // General
    CMD1(CCC_Help, "help");
    CMD1(CCC_Quit, "quit");
    CMD1(CCC_Start, "start");
    CMD1(CCC_Disconnect, "disconnect");
    CMD1(CCC_SaveCFG, "cfg_save");
    CMD1(CCC_LoadCFG, "cfg_load");

#ifdef DEBUG
    CMD3(CCC_Mask, "mt_particles", &psDeviceFlags, mtParticles);

    CMD1(CCC_DbgStrCheck, "dbg_str_check");
    CMD1(CCC_DbgStrDump, "dbg_str_dump");

    CMD3(CCC_Mask, "mt_sound", &psDeviceFlags, mtSound);
    CMD3(CCC_Mask, "mt_physics", &psDeviceFlags, mtPhysics);
    CMD3(CCC_Mask, "mt_network", &psDeviceFlags, mtNetwork);

    // Events
    CMD1(CCC_E_Dump, "e_list");
    CMD1(CCC_E_Signal, "e_signal");

    CMD3(CCC_Mask, "rs_clear_bb", &psDeviceFlags, rsClearBB);

    // CMD4(CCC_Float, "r__dtex_range", &r__dtex_range, 5, 175 );
    // CMD3(CCC_Mask, "rs_constant_fps", &psDeviceFlags, rsConstantFPS );
#endif // DEBUG

#ifndef MASTER_GOLD
    CMD3(CCC_Mask, "rs_detail", &psDeviceFlags, rsDrawDetails);
    CMD3(CCC_Mask, "rs_render_statics", &psDeviceFlags, rsDrawStatic);
    CMD3(CCC_Mask, "rs_render_dynamics", &psDeviceFlags, rsDrawDynamic);
    CMD3(CCC_Mask, "rs_render_particles", &psDeviceFlags, rsDrawParticles);
    CMD3(CCC_Mask, "rs_wireframe", &psDeviceFlags, rsWireframe);
#endif

    // Render device states
    CMD4(CCC_Integer, "r__supersample", &ps_r__Supersample, 1, 4);
    // [DA_PORT] Above 100 the scene is rendered LARGER than the window and downsampled on the way out,
    // i.e. supersampling — worth having, because this engine is heavily CPU-bound and a modern GPU sits
    // idle at 1080p, so spare GPU time is better spent on image quality than left unused.
    CMD4(CCC_RenderScale, "r__render_scale", &ps_r__render_scale, 25, 200);
    CMD3(CCC_UpscalePreset, "r__upscale_preset", &ps_r__upscale_preset, qupscale_preset_token);
    // [DA_PORT] What the options menu shows. The three per-vendor variables above stay for the console.
    CMD3(CCC_Upscaler, "r__upscaler", &ps_r__upscaler, qupscaler_token);
    CMD3(CCC_Upscaler, "r__upscaler_quality", &ps_r__upscaler_quality, qupscaler_quality_token);
    // 2 = show the velocity buffer, 3 = map which shader drew what, 4 = show the reactive mask
    // [DA_PORT] Диагностика, не настройка: ноль — рабочее состояние (буфер скоростей включает сам
    // апскейлер), а любое другое значение показывает служебную картинку или подменяет содержимое
    // буфера. Оставленное в user.ltx, оно тихо портит апскейлер до конца жизни установки.
    CMD4(CCC_DaDebugInteger, "r__motion_vectors", &ps_r__motion_vectors, 0, 5); // 5 = eye-space depth
    CMD4(CCC_Float, "r__reactive_foliage", &ps_r__reactive_foliage, 0.f, 1.f);
    CMD4(CCC_Float, "r__reactive_motion", &ps_r__reactive_motion, 0.f, 200.f);
    CMD4(CCC_Float, "r__reactive_object", &ps_r__reactive_object, 0.f, 2000.f);
    CMD4(CCC_Float, "r__reactive_emissive", &ps_r__reactive_emissive, 0.f, 1.f);
    CMD4(CCC_Float, "r__taa_reactive", &ps_r__taa_reactive, 0.f, 32.f);
    CMD4(CCC_Float, "r__reactive_transparent", &ps_r__reactive_transparent, 0.f, 1.f);
    CMD4(CCC_Float, "r__reactive_water", &ps_r__reactive_water, 0.f, 1.f);
    {
        extern ENGINE_API int ps_da_water_velocity_log;
        CMD4(CCC_DaDebugInteger, "r__water_velocity_log", &ps_da_water_velocity_log, 0, 1);

        // [DA_PORT] Переключатель самого прохода — см. ps_da_water_velocity.
        extern ENGINE_API int ps_da_water_velocity;
        CMD4(CCC_DaDebugInteger, "r__water_velocity", &ps_da_water_velocity, 0, 1);
    }
    {
        extern ENGINE_API int ps_da_alife_release_log;
        CMD4(CCC_DaDebugInteger, "da_alife_release_log", &ps_da_alife_release_log, 0, 1);
    }
    CMD4(CCC_DaDebugInteger, "r__emissive_probe", &ps_r__emissive_probe, 0, 2000);
    CMD4(CCC_DaDebugInteger, "r__cb_skip_redundant", &ps_r__cb_skip_redundant, 0, 1);
    CMD4(CCC_DaDebugInteger, "r__probe_center", &ps_r__probe_center, 0, 1);
    CMD4(CCC_DaDebugInteger, "r__xess_mv_sign", &ps_r__xess_mv_sign, 0, 3);
    CMD4(CCC_Integer, "r__reactive_dilate", &ps_r__reactive_dilate, 0, 16);
    CMD4(CCC_Float, "r__reactive_deadzone", &ps_r__reactive_deadzone, 0.f, 0.02f);
    CMD4(CCC_DaDebugInteger, "r__reactive_debug", &ps_r__reactive_debug, 0, 4);
    CMD4(CCC_DaDebugInteger, "r__reactive_selftest", &ps_r__reactive_selftest, 0, 1);
    CMD4(CCC_Integer, "r__reactive_ref_fps", &ps_r__reactive_ref_fps, 30, 300);
    CMD4(CCC_Integer, "r__sky_velocity", &ps_r__sky_velocity, 0, 1);
    CMD4(CCC_Integer, "ai_unstick", &ps_ai_unstick, 0, 1);
    {
        // [DA_PORT] Обе — диагностика (см. CCC_DaDebug в xr_ioc_cmd.h): da_perf_dump печатает в лог
        // разбор кадра и на больших N успевает написать гигабайты, если про него забыть.
        // [DA_PORT] Покадровый замер джиттера: сколько кадров печатать. Разбор в CameraManager.cpp.
        extern ENGINE_API int ps_da_jitter_log;
        CMD4(CCC_DaDebugInteger, "da_jitter_log", &ps_da_jitter_log, 0, 2000);
        extern ENGINE_API int ps_da_perf_dump;
        CMD4(CCC_DaDebugInteger, "da_perf_dump", &ps_da_perf_dump, 0, 2000);
        extern ENGINE_API int ps_da_perf_watch;
        CMD4(CCC_DaDebugInteger, "da_perf_watch", &ps_da_perf_watch, 0, 500);

        // [DA_PORT] Ловушка на выброс в отложенных задачах кадра (поле `seq` у da_perf_dump).
        //
        // Порог в миллисекундах, 0 — выключено. Когда цикл seqParallel за кадр перевалил порог, в
        // лог уходит разбор: сколько всего, сколько задач, какая из них худшая и на сколько. Плюс
        // сами задачи, помеченные пробой, называют себя по имени — см. da_seq_probe в Device.h.
        //
        // Зачем: обычный дамп ловит выброс только если снять его ровно в тот кадр, а выброс редкий.
        extern ENGINE_API float ps_da_seq_trap;
        CMD4(CCC_DaDebugFloat, "da_seq_trap", &ps_da_seq_trap, 0.f, 100.f);
        // Сколько отчётов о выбросе напечатать и замолчать. Ноль — без предела; поставлен предел
        // после случая, когда сломанный замер выдал 11928 строк за один прогон.
        extern ENGINE_API int ps_da_seq_trap_max;
        CMD4(CCC_DaDebugInteger, "da_seq_trap_max", &ps_da_seq_trap_max, 0, 10000);
        // Итог за сессию: 1 — напечатать, 2 — напечатать и обнулить. Сбрасывается сам после печати.
        extern ENGINE_API int ps_da_seq_stats;
        CMD4(CCC_DaDebugInteger, "da_seq_stats", &ps_da_seq_stats, 0, 2);
    }
    CMD4(CCC_Float, "ai_unstick_range", &ps_ai_unstick_range, 0.5f, 20.f);
    CMD4(CCC_Float, "r__vguard_strength", &ps_r__vguard_strength, 0.f, 1.f);
    CMD4(CCC_Float, "r__vguard_gloss", &ps_r__vguard_gloss, 0.f, 1.f);
    CMD4(CCC_Integer, "r__vguard_radius", &ps_r__vguard_radius, 1, 8);
    CMD4(CCC_Float, "r__vguard_still", &ps_r__vguard_still, 0.f, 20000.f);
    // [DA_PORT] Detail-bump damping, see the declarations. Sensitivity and strength in one number:
    // the weight is saturate(rate_of_change * value), so 0 is off and larger bites sooner.
    // Ceiling deliberately far above any sane setting: the units are a screen-space rate of change and
    // nobody knows the scale until it is measured, so the slider must be able to overshoot obviously.
    CMD4(CCC_Float, "r__detail_gloss_fix", &ps_r__detail_gloss_fix, 0.f, 4096.f);
    CMD4(CCC_Float, "r__detail_normal_fix", &ps_r__detail_normal_fix, 0.f, 4096.f);
    CMD4(CCC_Float, "r__detail_albedo_fix", &ps_r__detail_albedo_fix, 0.f, 4096.f);
    // 1/2 paint the damping weight, 3 paints every detail-bump pixel red, 4/5 drop the detail's
    // contribution to the normal / to the gloss outright - see the shader for why 3..5 exist.
    CMD4(CCC_DaDebugInteger, "r__detail_debug", &ps_r__detail_debug, 0, 9);
    // [DA_PORT] Фильтрация блика, см. объявления. Сила — ноль по умолчанию, картинка не меняется.
    CMD4(CCC_Integer, "da_stack_all", &ps_da_stack_all, 0, 1);
    CMD4(CCC_DaDebugInteger, "da_stack_debug", &ps_da_stack_debug, 0, 1);
    CMD4(CCC_Float, "r__spec_aa", &ps_r__spec_aa, 0.f, 64.f);
    CMD4(CCC_Float, "r__spec_aa_max", &ps_r__spec_aa_max, 0.f, 1.f);
    CMD4(CCC_Float, "r__spec_aa_power", &ps_r__spec_aa_power, 1.f, 256.f);
    CMD4(CCC_DaDebugInteger, "r__spec_aa_debug", &ps_r__spec_aa_debug, 0, 2);
    // [DA_PORT] Крутой параллакс, см. объявления. Потолок дальности взят с запасом: 300 заведомо
    // больше любой открытой сцены, а упирается всё равно не в него, а в цену шагов.
    CMD4(CCC_Float, "r__parallax_start", &ps_r__parallax_start, 0.f, 300.f);
    CMD4(CCC_Float, "r__parallax_stop", &ps_r__parallax_stop, 0.f, 300.f);
    CMD4(CCC_Float, "r__parallax_depth", &ps_r__parallax_depth, 0.f, 0.2f);
    CMD4(CCC_Float, "r__parallax_shadow", &ps_r__parallax_shadow, 0.f, 16.f);
    CMD4(CCC_Integer, "r__parallax_samples", &ps_r__parallax_samples, 4, 128);
    CMD4(CCC_Integer, "r__parallax_samples_min", &ps_r__parallax_samples_min, 2, 64);
    CMD4(CCC_Integer, "r__parallax_shadow_samples", &ps_r__parallax_shadow_samples, 2, 32);
    CMD4(CCC_Integer, "r__parallax_force", &ps_r__parallax_force, 0, 1); // нужна перезагрузка уровня
    // [DA_PORT] Сила затенения полостей, см. объявления. Живьём.
    CMD4(CCC_Float, "r__ssao_power", &ps_r__ssao_power, 0.f, 8.f);
    CMD4(CCC_DaDebugInteger, "r__ssao_debug", &ps_r__ssao_debug, 0, 1);
    // [DA_PORT] Разрыв повторов, см. объявления. Всё живьём, перезагрузка не нужна.
    CMD4(CCC_Integer, "r__hex_tiling", &ps_r__hex_tiling, 0, 1);
    CMD4(CCC_Float, "r__hex_scale", &ps_r__hex_scale, 0.05f, 16.f);
    CMD4(CCC_Float, "r__hex_rot", &ps_r__hex_rot, 0.f, 1.f);
    CMD4(CCC_Float, "r__hex_contrast", &ps_r__hex_contrast, 0.1f, 0.9f);
    CMD4(CCC_DaDebugInteger, "r__parallax_debug", &ps_r__parallax_debug, 0, 3);
    CMD4(CCC_Float, "r__wind_scale", &ps_r__wind_scale, 0.f, 4.f); // 0 = vegetation frozen
    CMD4(CCC_Integer, "r__wind_shadow", &ps_r__wind_shadow, 0, 1); // 0 = still foliage in shadow map
    // [DA_PORT] Качание травы: 0 = как в исходном движке (по умолчанию), 1 = как в моде.
    CMD4(CCC_Float, "r__grass_sway", &ps_r__grass_sway, 0.f, 1.f);
    CMD4(CCC_Float, "r__grass_sway_speed", &ps_r__grass_sway_speed, 0.f, 2.f);

    // [DA_PORT] Лужи в дождь. Действуют сразу, без перезапуска: все четыре числа читаются на кадр.
    CMD4(CCC_Integer, "r__puddles", &ps_r__puddles, 0, 1);
    CMD4(CCC_Float, "r__puddles_buildup", &ps_r__puddles_buildup, 5.f, 600.f);
    CMD4(CCC_Float, "r__puddles_dry", &ps_r__puddles_dry, 1.f, 20.f);
    CMD4(CCC_Float, "r__puddles_size", &ps_r__puddles_size, 0.f, 1.f);
    // [DA_PORT] Принудительная сырость — отладочная: держит мокрый асфальт в ясную погоду, поэтому
    // в user.ltx ей делать нечего (см. CCC_DaDebug в xr_ioc_cmd.h).
    CMD4(CCC_DaDebugFloat, "r__puddles_force", &ps_r__puddles_force, 0.f, 1.f);
    CMD4(CCC_Float, "r__puddles_gloss", &ps_r__puddles_gloss, 0.f, 1.f);
    CMD4(CCC_Float, "r__puddles_dark", &ps_r__puddles_dark, 0.1f, 1.f);
    CMD4(CCC_Float, "r__puddles_damp", &ps_r__puddles_damp, 0.f, 1.f);
    CMD4(CCC_Float, "r__puddles_ripple", &ps_r__puddles_ripple, 0.f, 3.f);
    CMD4(CCC_DaDebugInteger, "r__puddles_debug", &ps_r__puddles_debug, 0, 3);

    // [DA_PORT] Яркость фонарей. Действуют сразу: цвет ламп пересчитывается каждый кадр.
    CMD4(CCC_Float, "r__torch_bright", &ps_r__torch_bright, 0.2f, 12.f);
    CMD4(CCC_Float, "r__torch_bright_item", &ps_r__torch_bright_item, 0.2f, 12.f);

    // [DA_PORT] Сезон: осень / лето. Применяется со следующего запуска игры.
    // Значение берём из файла-признака (единственное хранилище), а не из user.ltx.
    da_season_read_marker();
    CMD3(CCC_DaSeason, "da_season", &ps_da_season, qda_season_token);

    // [DA_PORT] Дождь. Действуют сразу, без перезапуска.
    CMD4(CCC_Float, "r__rain_len", &ps_r__rain_len, 0.3f, 8.f);
    CMD4(CCC_Float, "r__rain_width", &ps_r__rain_width, 0.01f, 0.5f);
    CMD4(CCC_Float, "r__rain_bright", &ps_r__rain_bright, 0.2f, 6.f);
    CMD4(CCC_Float, "r__rain_splash_bright", &ps_r__rain_splash_bright, 0.1f, 4.f);

    // [DA_PORT] Пункты меню «Дождь»: две ступени вместо восьми чисел.
    CMD3(CCC_PuddlesQuality, "r__puddles_quality", &ps_r__puddles_quality, qpuddles_quality_token);
    CMD3(CCC_Token, "r__puddles_dist", &ps_r__puddles_dist, qpuddles_dist_token);
    CMD4(CCC_Integer, "r__puddles_refl", &ps_r__puddles_refl, 0, 1);
    CMD4(CCC_Float, "r__puddles_refl_power", &ps_r__puddles_refl_power, 0.f, 2.f);
    CMD4(CCC_Float, "r__puddles_facing", &ps_r__puddles_facing, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__puddles_sky", &ps_r__puddles_sky, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Integer, "r__puddles_gbuf", &ps_r__puddles_gbuf, 0, 1); // [DA_PORT]
    CMD4(CCC_Float, "r__puddles_edge", &ps_r__puddles_edge, 0.f, 1.f); // [DA_PORT]
    CMD4(CCC_Float, "r__puddles_rim", &ps_r__puddles_rim, 0.2f, 1.f);
    CMD4(CCC_Float, "r__puddles_rim_width", &ps_r__puddles_rim_width, 0.02f, 0.5f);
    CMD3(CCC_RainQuality, "r__rain_quality", &ps_r__rain_quality, qrain_quality_token);
    CMD4(CCC_Integer, "r__rain_drops", &ps_r__rain_drops, 500, 40000);
    CMD4(CCC_Float, "r__rain_radius", &ps_r__rain_radius, 5.f, 40.f);
    CMD4(CCC_Float, "r__rain_splash", &ps_r__rain_splash, 0.f, 1.f);
    CMD4(CCC_Float, "r__rain_splash_time", &ps_r__rain_splash_time, 0.1f, 2.f);
    CMD4(CCC_Integer, "r__foliage_tandc", &ps_r__foliage_tandc, 0, 1);
    CMD3(CCC_XESS, "r__xess", (u32*)&ps_r__xess, qxess_token);
    CMD3(CCC_DLSS, "r__dlss", (u32*)&ps_r__dlss, qdlss_token); // [DA_PORT]
    CMD4(CCC_Integer, "r__dlss_reactive", &ps_r__dlss_reactive, 0, 1); // [DA_PORT] применяется сразу
    CMD4(CCC_DaDebugInteger, "r__dlss_selftest", &ps_r__dlss_selftest, 0, 1); // [DA_PORT] разовый замер в лог
    CMD4(CCC_DaDebugInteger, "r__upscale_show_input", &ps_r__upscale_show_input, 0, 1); // [DA_PORT] вход вместо выхода
    // [DA_PORT] Срез по строке в лог. Значение — сколько ПОДВИЖНЫХ кадров пропустить: отсчёт идёт
    // только пока камера действительно движется, стоящая ждёт сколько угодно. Меткость не нужна:
    // набрал, закрыл консоль, повёл камерой — снимок возьмётся сам.
    CMD4(CCC_DaDebugInteger, "da_gbuffer_probe", &ps_r__gbuffer_probe, 0, 600);
    // [DA_PORT] Свет под перекрестьем, N кадров подряд — для мерцания при неподвижной камере.
    CMD4(CCC_DaDebugInteger, "da_light_watch", &ps_r__light_watch, 0, 600);
    CMD4(CCC_DaDebugInteger, "da_light_map", &ps_r__light_map, 0, 600);
    CMD4(CCC_DaDebugInteger, "da_combine_dbg", &ps_r__combine_dbg, 0, 12);
    CMD4(CCC_Float, "da_hemi_floor", &ps_r__hemi_floor, 0.f, 0.5f);
    CMD4(CCC_DaDebugInteger, "da_shift_watch", &ps_r__shift_watch, 0, 600);
    CMD4(CCC_DaDebugInteger, "da_move_dump", &ps_da_move_dump, 0, 2000);
    CMD4(CCC_DaDebugFloat, "da_squad_dump", &ps_da_squad_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_alife_dump", &ps_da_alife_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_entity_dump", &ps_da_entity_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_object_dump", &ps_da_object_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_npc_dump", &ps_da_npc_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_lua_call_dump", &ps_da_lua_call_dump, 0.f, 1000.f); // [DA_PORT] порог, мс
    CMD4(CCC_DaDebugFloat, "da_mem_log", &ps_da_mem_log, 0.f, 600.f); // [DA_PORT] период, с
    CMD4(CCC_DaDebugInteger, "da_stalker_dump", &ps_da_stalker_dump, 0, 2000);
    CMD4(CCC_DaDebugInteger, "da_sched_dump", &ps_da_sched_dump, 0, 2000);
    CMD4(CCC_DaDebugInteger, "da_sched_warmup_ms", &ps_da_sched_warmup_ms, 0, 5000);
    {
        extern ENGINE_API float ps_da_sched_budget_ms;
        CMD4(CCC_DaDebugFloat, "da_sched_budget_ms", &ps_da_sched_budget_ms, 0.f, 20.f);
        CMD4(CCC_DaDebugFloat, "da_events_budget_ms", &ps_da_events_budget_ms, 0.f, 50.f);
        extern ENGINE_API float ps_da_visual_warmup_ms;
        CMD4(CCC_DaDebugFloat, "da_visual_warmup_ms", &ps_da_visual_warmup_ms, -1.f, 50.f);
    }
    {
        extern ENGINE_API int ps_da_discord_interval_ms;
        CMD4(CCC_DaDebugInteger, "da_discord_interval_ms", &ps_da_discord_interval_ms, 0, 5000);
    }
    CMD4(CCC_DaDebugInteger, "da_goap_dump", &ps_da_goap_dump, 0, 2000); // [DA_PORT] живёт в xrCore
    // [DA_PORT] Полный замер кэша теней: время кадра + дрожание по всему экрану, одной командой.
    // [DA_PORT] Включаемый: 1 — начать сбор, 0 — закончить и выдать отчёт. Не отсчёт кадров.
    CMD4(CCC_DaDebugInteger, "da_shadow_test", &ps_r__shadow_test, 0, 1);
    CMD3(CCC_FSR2, "r__fsr2", (u32*)&ps_r__fsr2, qfsr2_token);
    // [DA_PORT] restart to apply. 1 — слой проверки DirectX, 2 — он же плюс перепись живых объектов
    // при выходе (она дважды роняла выход, поэтому вынесена отдельным уровнем, см. dx11HW.cpp).
    // ⚠️ Слой проверки стоит кадров, а забыть его включённым легко — поэтому он тоже живёт один запуск.
    CMD4(CCC_DaDebugInteger, "r__d3d_debug", &ps_r__d3d_debug, 0, 2);
    CMD4(CCC_DaDebugInteger, "r__fsr3_debug", &ps_r__fsr3_debug, 0, 1); // [DA_PORT] 1 = create but never dispatch
    CMD4(CCC_Integer, "r__fsr3", &ps_r__fsr3, 0, 5); // [DA_PORT] quality step, restart to apply // sets r__render_scale to match; needs a renderer restart
    CMD4(CCC_Integer, "r__upscale_sharpness", &ps_r__upscale_sharpness, 0, 100); // [DA_PORT] FSR-style RCAS
    CMD4(CCC_Integer, "r__taa", &ps_r__taa, 0, 1); // [DA_PORT]
    CMD4(CCC_DaDebugInteger, "r__taa_debug", &ps_r__taa_debug, 0, 3);
    CMD4(CCC_Integer, "r__taa_sky", &ps_r__taa_sky, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_sharp", &ps_r__taa_sharp, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_mipbias", &ps_r__taa_mipbias, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_jitter", &ps_r__taa_jitter, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__wallmarks_on_skeleton", &ps_r__WallmarksOnSkeleton, 0, 1);

    CMD1(CCC_Editor, "rs_editor");

    CMD3(CCC_FpsLimit, "rs_fps_limit", &ps_fps_limit, fps_limit_token); // [DA_PORT] список, не число
    CMD4(CCC_Integer, "rs_fps_limit_in_menu", (int*)&ps_fps_limit_in_menu, 30, 501);
    CMD3(CCC_Mask, "rs_always_active", &psDeviceFlags, rsAlwaysActive);
    CMD3(CCC_Mask, "rs_v_sync", &psDeviceFlags, rsVSync);
    // CMD3(CCC_Mask, "rs_disable_objects_as_crows",&psDeviceFlags, rsDisableObjectsAsCrows );
    CMD1(CCC_Fullscreen, "rs_fullscreen");
    CMD1(CCC_Refresh60hz, "rs_refresh_60hz");
    CMD3(CCC_Mask, "rs_stats", &psDeviceFlags, rsStatistic);
    CMD3(CCC_Mask, "rs_fps", &psDeviceFlags, rsShowFPS);
    CMD3(CCC_Mask, "rs_fps_graph", &psDeviceFlags, rsShowFPSGraph);
    CMD4(CCC_Float, "rs_vis_distance", &psVisDistance, 0.4f, 1.5f);

    CMD3(CCC_Mask, "rs_cam_pos", &psDeviceFlags, rsCameraPos);
#ifdef DEBUG
    CMD3(CCC_Mask, "rs_occ_draw", &psDeviceFlags, rsOcclusionDraw);
// CMD4(CCC_Integer, "rs_skeleton_update", &psSkeletonUpdate, 2, 128 );
#endif // DEBUG

    CMD2(CCC_Gamma, "rs_c_gamma", &ps_gamma);
    CMD2(CCC_Gamma, "rs_c_brightness", &ps_brightness);
    CMD2(CCC_Gamma, "rs_c_contrast", &ps_contrast);
    // CMD4(CCC_Integer, "rs_vb_size", &rsDVB_Size, 32, 4096);
    // CMD4(CCC_Integer, "rs_ib_size", &rsDIB_Size, 32, 4096);

    // Texture manager
    CMD4(CCC_Integer, "texture_lod", &psTextureLOD, 0, 4);
    CMD4(CCC_Integer, "net_dedicated_sleep", &psNET_DedicatedSleep, 0, 64);

    // General video control
    CMD1(CCC_VidMonitor, "vid_monitor");
    CMD1(CCC_VidMode, "vid_mode");
    CMD1(CCC_VidWindowMode, "vid_window_mode");

#ifdef DEBUG
    CMD3(CCC_Token, "vid_bpp", &psDeviceMode.BitsPerPixel, vid_bpp_token);
#endif // DEBUG

    CMD1(CCC_VID_Reset, "vid_restart");

    // Sound
    CMD2(CCC_Float, "snd_volume_eff", &psSoundVEffects);
    CMD2(CCC_Float, "snd_volume_music", &psSoundVMusic);
    CMD1(CCC_SND_Restart, "snd_restart");
    CMD3(CCC_Mask, "snd_acceleration", &psSoundFlags, ss_Hardware);
    CMD3(CCC_Mask, "snd_efx", &psSoundFlags, ss_EFX);
    CMD3(CCC_Mask, "snd_use_float32", &psSoundFlags, ss_UseFloat32);
    CMD4(CCC_Integer, "snd_targets", &psSoundTargets, 4, 256);
    CMD4(CCC_Integer, "snd_cache_size", &psSoundCacheSizeMB, 4, 64);

#ifdef DEBUG
    CMD3(CCC_Mask, "snd_stats", &g_stats_flags, st_sound);
    CMD3(CCC_Mask, "snd_stats_min_dist", &g_stats_flags, st_sound_min_dist);
    CMD3(CCC_Mask, "snd_stats_max_dist", &g_stats_flags, st_sound_max_dist);
    CMD3(CCC_Mask, "snd_stats_ai_dist", &g_stats_flags, st_sound_ai_dist);
    CMD3(CCC_Mask, "snd_stats_info_name", &g_stats_flags, st_sound_info_name);
    CMD3(CCC_Mask, "snd_stats_info_object", &g_stats_flags, st_sound_info_object);

    CMD4(CCC_Integer, "error_line_count", &g_ErrorLineCount, 6, 1024);
#endif // DEBUG

    // Mouse
    CMD3(CCC_Mask, "mouse_invert", &psMouseInvert, 1);
    psMouseSens = 0.12f;
    CMD4(CCC_Float, "mouse_sens", &psMouseSens, 0.001f, 0.6f);

    // Gamepad
    CMD3(CCC_Mask, "gamepad_invert_x", &psControllerFlags, ControllerInvertX);
    CMD3(CCC_Mask, "gamepad_invert_y", &psControllerFlags, ControllerInvertY);
    CMD4(CCC_Float, "gamepad_stick_sens_x", &psControllerStickSensX, 0.00001f, 2.0f);
    CMD4(CCC_Float, "gamepad_stick_sens_y", &psControllerStickSensY, 0.00001f, 2.0f);
    CMD4(CCC_Float, "gamepad_stick_inner_deadzone", &psControllerStickInnerDeadZone, 0.0f, 1.0f);
    CMD4(CCC_Float, "gamepad_stick_outer_deadzone", &psControllerStickOuterDeadZone, 0.0f, 1.0f);
    //CMD4(CCC_Float, "gamepad_stick_angular_deadzone", &psControllerStickAngularDeadZone, 0.0f, 1.0f);
    CMD4(CCC_Float, "gamepad_sensor_sens", &psControllerSensorSens, 0.01f, 3.f);
    CMD4(CCC_Float, "gamepad_sensor_deadzone", &psControllerSensorDeadZone, 0.001f, 1.f);
    CMD3(CCC_Mask,  "gamepad_sensors_enable", &psControllerFlags, ControllerEnableSensors);
    CMD4(CCC_Float, "gamepad_cursor_autohide_time", &psControllerCursorAutohideTime, 0.5f, 3.f);

    // Camera
    CMD2(CCC_Float, "cam_inert", &psCamInert);
    CMD2(CCC_Float, "cam_slide_inert", &psCamSlideInert);

    CMD1(CCC_renderer, "renderer");

    if (!GEnv.isDedicatedServer)
        CMD1(CCC_soundDevice, "snd_device");

    // psSoundRolloff = pSettings->r_float ("sound","rolloff"); clamp(psSoundRolloff, EPS_S, 2.f);
    psSoundOcclusionScale = pSettings->r_float("sound", "occlusion_scale");
    clamp(psSoundOcclusionScale, 0.1f, .5f);

    extern int g_Dump_Export_Obj;
    extern int g_Dump_Import_Obj;
    CMD4(CCC_Integer, "net_dbg_dump_export_obj", &g_Dump_Export_Obj, 0, 1);
    CMD4(CCC_Integer, "net_dbg_dump_import_obj", &g_Dump_Import_Obj, 0, 1);

#ifdef DEBUG
    CMD1(CCC_DumpOpenFiles, "dump_open_files");
#endif

    CMD1(CCC_ExclusiveMode, "input_exclusive_mode");
#if defined(XR_PLATFORM_WINDOWS) // XXX: enable (remove ifdef) when text console will be available on Linux
    extern int g_svTextConsoleUpdateRate;
    CMD4(CCC_Integer, "sv_console_update_rate", &g_svTextConsoleUpdateRate, 1, 100);
#endif
    extern int g_svDedicateServerUpdateReate;
    CMD4(CCC_Integer, "sv_dedicated_server_update_rate", &g_svDedicateServerUpdateReate, 1, 1000);

    CMD1(CCC_HideConsole, "hide");

#ifdef DEBUG
    extern BOOL debug_destroy;
    CMD4(CCC_Integer, "debug_destroy", &debug_destroy, 0, 1);

    extern int g_bShowRedText;
    CMD4(CCC_Integer, "debug_show_red_text", &g_bShowRedText, 0, 1);
#endif

    extern int ps_disable_lens_flare;
    CMD4(CCC_Integer, "disable_lens_flare", &ps_disable_lens_flare, 0, 1);
};
