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
    {"st_opt_fps_unlimited", 1000},
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
ENGINE_API bool da_upscaler_active()
{
    return !!ps_r__fsr2 || !!ps_r__fsr3 || !!ps_r__xess || !!ps_r__dlss;
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
    { "ui_mm_upscaler_off", 0 },
    { "ui_mm_upscaler_taa", 1 },
    { "ui_mm_upscaler_fsr1", 2 },
    { "ui_mm_upscaler_fsr2", 3 },
    { "ui_mm_upscaler_fsr3", 4 },
    { "ui_mm_upscaler_xess", 5 },
    { "ui_mm_upscaler_dlss", 6 }, // [DA_PORT]
    { nullptr, 0 },
};

// Five steps, shared by all three backends. XeSS has exactly these five; FSR 2 is given a 1.3x step of
// its own (see da_fsr2::render_size_for); FSR 1.0 is only a scale plus sharpening, so it follows them
// directly. Same wording everywhere, so the choice means the same thing whichever backend is picked.
ENGINE_API u32 ps_r__upscaler_quality = 1; // default "quality" when an upscaler is first switched on
ENGINE_API xr_token qupscaler_quality_token[] = {
    { "ui_mm_upq_ultra_quality", 0 },
    { "ui_mm_upq_quality", 1 },
    { "ui_mm_upq_balanced", 2 },
    { "ui_mm_upq_performance", 3 },
    { "ui_mm_upq_ultra_performance", 4 },
    { nullptr, 0 },
};

// Render scale per quality step, in percent of the output: 1.3x, 1.5x, 1.7x, 2.0x, 3.0x per dimension.
static const int da_upscaler_scale[5] = { 77, 67, 59, 50, 33 };
// FSR 1.0 has no reconstruction to recover detail with, so it leans harder on sharpening the lower the
// source resolution gets. The temporal upscalers do their own and ignore this.
static const int da_upscaler_sharpen[5] = { 35, 40, 45, 55, 60 };

static void da_apply_upscaler()
{
    const u32 q = (ps_r__upscaler_quality < 5) ? ps_r__upscaler_quality : 1;

    // Everything off first, one thing on after - including our own temporal AA, which is now a choice
    // in the same list. Keeping the reset complete is the whole point of the shape: a case that forgets
    // to switch off what it replaces is precisely how two temporal filters came to run at once.
    ps_r__fsr2 = 0;
    ps_r__fsr3 = 0;
    ps_r__xess = 0;
    ps_r__dlss = 0;
    ps_r__taa = 0;
    ps_r__upscale_preset = 0;

    switch (ps_r__upscaler)
    {
    case 1: // Our own temporal AA - no upscaling, so the scene renders at full size
        ps_r__taa = 1;
        ps_r__render_scale = 100;
        ps_r__upscale_sharpness = 0;
        break;
    case 2: // FSR 1.0 - spatial, applied to the finished frame
        ps_r__upscale_preset = q + 1;
        ps_r__render_scale = da_upscaler_scale[q];
        ps_r__upscale_sharpness = da_upscaler_sharpen[q];
        break;
    case 3: // FSR 2 - temporal reconstruction
        ps_r__fsr2 = int(q + 1);
        ps_r__render_scale = da_upscaler_scale[q];
        break;
    case 4: // FSR 3 - temporal reconstruction, community DX11 backend
        ps_r__fsr3 = int(q + 1);
        ps_r__render_scale = da_upscaler_scale[q];
        break;
    case 5: // XeSS - temporal reconstruction, Intel Arc only on D3D11
        ps_r__xess = int(q + 1);
        ps_r__render_scale = da_upscaler_scale[q];
        break;
    case 6: // DLSS - temporal reconstruction, RTX only
        ps_r__dlss = int(q + 1);
        ps_r__render_scale = da_upscaler_scale[q];
        break;
    default:
        ps_r__render_scale = 100;
        ps_r__upscale_sharpness = 0;
        break;
    }

    Device.UpdateRenderResolution();
    if (Device.b_is_Ready)
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
    inline static xr_token vid_window_mode_token[] =
    {
        { "st_opt_windowed",                rsWindowed             },
        { "st_opt_windowed_borderless",     rsWindowedBorderless   },
        { "st_opt_fullscreen",              rsFullscreen           },
        { "st_opt_fullscreen_borderless",   rsFullscreenBorderless },
        { nullptr,                          -1                     },
    };

public:
    CCC_VidWindowMode(pcstr name) : CCC_Token(name, &psDeviceMode.WindowStyle, vid_window_mode_token) {}

    void Execute(pcstr args) override
    {
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
                psDeviceMode.WindowStyle = rsWindowedBorderless;
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
        inherited::Execute(args);
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
// [DA_PORT] How much the upscalers should distrust their history on alpha-tested foliage. Not a switch
// but a weight: 0 accumulates normally (shimmer on thin branches), 1 ignores history entirely (no
// shimmer, but the sway loses its smoothness because nothing is left to accumulate). The useful value
// is somewhere between and is a matter of taste, hence a slider rather than a constant.
ENGINE_API float ps_r__reactive_foliage = 0.5f;

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
ENGINE_API shared_str current_player_hud_sect{};

extern u32 ps_fps_limit;
extern u32 ps_fps_limit_in_menu;

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
    CMD4(CCC_Integer, "r__motion_vectors", &ps_r__motion_vectors, 0, 5); // 5 = eye-space depth
    CMD4(CCC_Float, "r__reactive_foliage", &ps_r__reactive_foliage, 0.f, 1.f);
    CMD4(CCC_Float, "r__reactive_motion", &ps_r__reactive_motion, 0.f, 200.f);
    CMD4(CCC_Float, "r__reactive_object", &ps_r__reactive_object, 0.f, 2000.f);
    CMD4(CCC_Integer, "r__reactive_dilate", &ps_r__reactive_dilate, 0, 16);
    CMD4(CCC_Float, "r__reactive_deadzone", &ps_r__reactive_deadzone, 0.f, 0.02f);
    CMD4(CCC_Integer, "r__reactive_debug", &ps_r__reactive_debug, 0, 4);
    CMD4(CCC_Integer, "r__reactive_selftest", &ps_r__reactive_selftest, 0, 1);
    CMD4(CCC_Integer, "r__reactive_ref_fps", &ps_r__reactive_ref_fps, 30, 300);
    CMD4(CCC_Integer, "r__sky_velocity", &ps_r__sky_velocity, 0, 1);
    CMD4(CCC_Integer, "ai_unstick", &ps_ai_unstick, 0, 1);
    {
        extern ENGINE_API int ps_da_perf_dump;
        CMD4(CCC_Integer, "da_perf_dump", &ps_da_perf_dump, 0, 2000);
        extern ENGINE_API int ps_da_perf_watch;
        CMD4(CCC_Integer, "da_perf_watch", &ps_da_perf_watch, 0, 500);
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
    CMD4(CCC_Integer, "r__detail_debug", &ps_r__detail_debug, 0, 9);
    CMD4(CCC_Float, "r__wind_scale", &ps_r__wind_scale, 0.f, 4.f); // 0 = vegetation frozen
    CMD4(CCC_Integer, "r__wind_shadow", &ps_r__wind_shadow, 0, 1); // 0 = still foliage in shadow map
    CMD4(CCC_Integer, "r__foliage_tandc", &ps_r__foliage_tandc, 0, 1);
    CMD3(CCC_XESS, "r__xess", (u32*)&ps_r__xess, qxess_token);
    CMD3(CCC_DLSS, "r__dlss", (u32*)&ps_r__dlss, qdlss_token); // [DA_PORT]
    CMD4(CCC_Integer, "r__dlss_reactive", &ps_r__dlss_reactive, 0, 1); // [DA_PORT] применяется сразу
    CMD4(CCC_Integer, "r__dlss_selftest", &ps_r__dlss_selftest, 0, 1); // [DA_PORT] разовый замер в лог
    CMD3(CCC_FSR2, "r__fsr2", (u32*)&ps_r__fsr2, qfsr2_token);
    CMD4(CCC_Integer, "r__d3d_debug", &ps_r__d3d_debug, 0, 1); // [DA_PORT] restart to apply
    CMD4(CCC_Integer, "r__fsr3_debug", &ps_r__fsr3_debug, 0, 1); // [DA_PORT] 1 = create but never dispatch
    CMD4(CCC_Integer, "r__fsr3", &ps_r__fsr3, 0, 5); // [DA_PORT] quality step, restart to apply // sets r__render_scale to match; needs a renderer restart
    CMD4(CCC_Integer, "r__upscale_sharpness", &ps_r__upscale_sharpness, 0, 100); // [DA_PORT] FSR-style RCAS
    CMD4(CCC_Integer, "r__taa", &ps_r__taa, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_debug", &ps_r__taa_debug, 0, 3);
    CMD4(CCC_Integer, "r__taa_sky", &ps_r__taa_sky, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_sharp", &ps_r__taa_sharp, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_mipbias", &ps_r__taa_mipbias, 0, 100); // [DA_PORT]
    CMD4(CCC_Integer, "r__taa_jitter", &ps_r__taa_jitter, 0, 1); // [DA_PORT]
    CMD4(CCC_Integer, "r__wallmarks_on_skeleton", &ps_r__WallmarksOnSkeleton, 0, 1);

    CMD1(CCC_Editor, "rs_editor");

    CMD3(CCC_Token, "rs_fps_limit", &ps_fps_limit, fps_limit_token); // [DA_PORT] list, not a raw number
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
