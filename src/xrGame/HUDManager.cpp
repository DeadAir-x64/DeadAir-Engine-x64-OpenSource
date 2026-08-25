#include "StdAfx.h"
#include "HUDManager.h"
#include "HUDTarget.h"
#include "Actor.h"
#include "CustomOutfit.h" // [DA_PORT] отчёт da_fp_body_debug печатает секцию костюма
#include "Include/xrRender/Kinematics.h" // [DA_PORT] прятать верхние кости в перволичном проходе
#include "xrEngine/IGame_Level.h"
#include "xrEngine/xr_input.h"
#include "GamePersistent.h"
#include "MainMenu.h"
#include "Grenade.h"
#include "Spectator.h"
#include "Car.h"
#include "UIGameCustom.h"
#include "xrUICore/Cursor/UICursor.h"
#include "game_cl_base.h"

// [DA_PORT] Перволичное тело: 1 — модель актёра рисуется в главном проходе, 0 — как в оригинале.
// Команда da_fp_body, см. console_commands.cpp.
int g_da_fp_body = 1;


// [DA_PORT] Экранный отчёт числами для подгонки перволичного тела. Команда da_fp_body_debug.
int g_da_fp_body_debug = 0;

// Размещение ног, определены в Actor.cpp
extern float g_da_legs_fwd;
extern float g_da_legs_y;
extern int g_da_legs_cam;
#ifdef DEBUG
#include "PHDebug.h"
#endif

extern CUIGameCustom* CurrentGameUI() { return HUD().GetGameUI(); }

//--------------------------------------------------------------------
CHUDManager::CHUDManager() : m_pHUDTarget(xr_new<CHUDTarget>()) {}
//--------------------------------------------------------------------
CHUDManager::~CHUDManager()
{
    OnDisconnected();

    if (pUIGame)
        pUIGame->UnLoad();

    xr_delete(pUIGame);
    xr_delete(m_pHUDTarget);
}

//--------------------------------------------------------------------
void CHUDManager::OnFrame()
{
    ZoneScoped;

    if (!psHUD_Flags.is(HUD_DRAW_RT2))
        return;

    if (!b_online)
        return;

    if (pUIGame)
        pUIGame->OnFrame();

    m_pHUDTarget->CursorOnFrame();
}
//--------------------------------------------------------------------

void CHUDManager::Render_First(u32 context_id)
{
    ZoneScoped;

    // [DA_PORT] Тело и его тень НЕ зависят от признаков ОРУЖИЯ.
    //
    // Здесь стояла та же проверка, что и у отрисовки оружия в руках, по четырём признакам сразу.
    // Flags32::is требует ВСЕ, поэтому стоит убрать оружие — один гаснет, и тело с тенью пропадают.
    //
    // Видно это стало на анимациях еды: сцена убирает оружие, и посреди неё игрок переставал видеть
    // собственное тело, а после она возвращала и оружие, и тело. К оружию тело отношения не имеет —
    // оно должно быть видно, пока мы в первом лице и живы.
    if (!psHUD_Flags.test(HUD_DRAW_RT2))
        return;
    if (0 == pUIGame)
        return;
    IGameObject* O = g_pGameLevel->CurrentViewEntity();
    if (0 == O)
        return;
    CActor* A = smart_cast<CActor*>(O);
    if (!A || !A->HUDview())
        return;

    // [DA_PORT] Выключатель перволичного тела. Фича наша, в моде её нет, и вкус у неё спорный —
    // пусть будет чем погасить, не пересобирая.
    if (!g_da_fp_body)
        return;

    // [DA_PORT] Рисуем ОТДЕЛЬНУЮ модель ног, а не визуал актёра.
    //
    // Сначала было проще: снять с актёра флаг невидимости и отрисовать его самого, обрезав верхние
    // кости. Не годится — визуал актёра идёт со своей матрицей, а телу нужна своя: отодвинутая от
    // камеры и привязанная к ней по горизонтали (разбор в CActor::renderable_RenderLegs). Поэтому
    // здесь только вызов, а всё остальное — там, рядом с теневой моделью, по той же технике.
    {
        ScopeLock lock{ &render_lock };
        A->renderable_RenderLegs(context_id, O->H_Root());
    }
}

// [DA_PORT] Тот же отбор, что и в Render_First, но рисуется теневая модель актёра: целое тело с
// головой и руками, тогда как в камере остаются авторские перволичные ноги. Разбор в CustomHUD.h.
void CHUDManager::Render_ActorShadow(u32 context_id)
{
    ZoneScoped;

    // [DA_PORT] Тело и его тень НЕ зависят от признаков ОРУЖИЯ.
    //
    // Здесь стояла та же проверка, что и у отрисовки оружия в руках, по четырём признакам сразу.
    // Flags32::is требует ВСЕ, поэтому стоит убрать оружие — один гаснет, и тело с тенью пропадают.
    //
    // Видно это стало на анимациях еды: сцена убирает оружие, и посреди неё игрок переставал видеть
    // собственное тело, а после она возвращала и оружие, и тело. К оружию тело отношения не имеет —
    // оно должно быть видно, пока мы в первом лице и живы.
    if (!psHUD_Flags.test(HUD_DRAW_RT2))
        return;
    if (0 == pUIGame)
        return;
    IGameObject* O = g_pGameLevel->CurrentViewEntity();
    if (0 == O)
        return;
    CActor* A = smart_cast<CActor*>(O);
    if (!A || !A->HUDview())
        return;

    const auto root = O->H_Root();
    ScopeLock lock{ &render_lock };
    const bool was_invisible = root->renderable_Invisible();
    root->renderable_Invisible(false);
    A->renderable_RenderShadow(context_id, root);
    root->renderable_Invisible(was_invisible);
}

bool need_render_hud()
{
    if (Device.IsAnselActive)
        return false;

    IGameObject* O = g_pGameLevel ? g_pGameLevel->CurrentViewEntity() : NULL;
    if (0 == O)
        return false;

    CActor* A = smart_cast<CActor*>(O);
    if (A && (!A->HUDview() || !A->g_Alive()))
        return false;

    if (smart_cast<CCar*>(O) || smart_cast<CSpectator*>(O))
        return false;

    return true;
}

void CHUDManager::Render_Last(u32 context_id)
{
    ZoneScoped;

    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2 | HUD_DRAW_RT2))
        return;
    if (0 == pUIGame)
        return;

    if (!need_render_hud())
        return;

    IGameObject* O = g_pGameLevel->CurrentViewEntity();
    // hud itself
    {
        const auto root = O->H_Root();
        ScopeLock lock{ &render_lock };
        root->renderable_HUD(true);
        O->OnHUDDraw(context_id, this, root);
        root->renderable_HUD(false);
    }
}

#include "player_hud.h"
bool CHUDManager::RenderActiveItemUIQuery()
{
    if (!psHUD_Flags.is(HUD_DRAW_RT2))
        return false;

    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2))
        return false;

    if (!need_render_hud())
        return false;

    return (g_player_hud && g_player_hud->render_item_ui_query());
}

void CHUDManager::RenderActiveItemUI()
{
    if (!psHUD_Flags.is(HUD_DRAW_RT2))
        return;

    g_player_hud->render_item_ui();
}

// [DA_PORT] Экранный отчёт по перволичному телу — команда da_fp_body_debug 1.
//
// Подгонять модель на глаз бесполезно: не видно ни где стоит камера относительно модели, ни куда
// уехала голова, ни какая модель вообще надета. Здесь всё это числами, в системе координат самой
// модели — то есть ровно в тех величинах, которыми правку и задавать.
// [DA_PORT] Экранный отчёт прибора увода прицела — команда da_aim_debug 1. Разбор, что именно
// меряется и почему, — у объявления переменных в player_hud.cpp.
//
// Читать так: строка «догон» показывает, работает ли выравнивание отстающего вектора. Если она
// говорит СНЯТ и счётчик кадров растёт, а угол при этом уползает вверх — версия подтверждается:
// вектор замер, а камера ушла. Если увод на экране есть, а угол мал — версия НЕВЕРНА, и смещение
// приходит откуда-то ещё; тогда смотреть надо на пару обзоров внизу.
// [DA_PORT] Перекодировка UTF-8 -> cp1251 для ЭКРАННОГО вывода.
//
// Исходники движка в UTF-8, а игровые шрифты рисуют cp1251 — русские подписи на экране выходили
// кашей вида «PSPµC» при совершенно правильных числах. Я сперва решил, что дело в шрифте, и
// подменил его на русский; не помогло, потому что портятся не глифы, а БАЙТЫ.
//
// В лог писать ничего не надо: файл читается как UTF-8 и там всё в порядке. Портится только экран.
static pcstr da_cp1251(pcstr utf8, char* out, size_t out_sz)
{
    size_t o = 0;
    for (const u8* p = (const u8*)utf8; *p && o + 1 < out_sz;)
    {
        if (*p < 0x80) { out[o++] = char(*p++); continue; }
        u32 cp = 0;
        if ((*p & 0xE0) == 0xC0 && p[1]) { cp = ((*p & 0x1Fu) << 6) | (p[1] & 0x3Fu); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && p[1] && p[2])
        { cp = ((*p & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu); p += 3; }
        else { ++p; continue; }

        if (cp >= 0x0410 && cp <= 0x044F) out[o++] = char(cp - 0x0410 + 0xC0); // А..я
        else if (cp == 0x0401) out[o++] = char(0xA8); // Ё
        else if (cp == 0x0451) out[o++] = char(0xB8); // ё
        else if (cp == 0x2014 || cp == 0x2013) out[o++] = '-';
        else if (cp == 0x00AB) out[o++] = '"';
        else if (cp == 0x00BB) out[o++] = '"';
        else if (cp == 0x2192) out[o++] = '>';
        else out[o++] = '?'; // стрелки, значки и прочее в cp1251 не лезут
    }
    out[o] = 0;
    return out;
}

static void DaRenderAimStats()
{
    extern int g_da_aim_debug;
    if (!g_da_aim_debug || !g_pGameLevel)
        return;

    extern bool g_da_aim_allowed;
    extern u32 g_da_aim_frozen_frames;
    extern float g_da_aim_angle_deg, g_da_aim_angle_deg_max;
    extern float g_da_aim_shift, g_da_aim_shift_max;
    extern float g_da_aim_tendto, g_da_aim_power;
    extern u8 g_da_aim_offset_idx;
    extern ENGINE_API float psHUD_FOV;
    extern ENGINE_API float g_hud_fov_current;
    extern ENGINE_API float g_fov;

    // ⚠️ Именно русский шрифт. pFontArial14 кириллицу не знает, и первый же замер пришёл нечитаемым:
    // цифры видны, подписи — каша. Соседний отчёт по телу тем и страдает, но там подписи латиницей.
    CGameFont* pFont = UI().Font().pFontLetterica16Russian;
    if (!pFont)
        return;

    float x = 20.0f, y = 320.0f;
    const float step = 18.0f;
    pFont->SetAligment(CGameFont::alLeft);

    string512 line;
    string512 da_buf;
    pFont->SetColor(0xFFFFE080);
    pFont->Out(x, y, da_cp1251("[DA] УВОД ПРИЦЕЛА   da_aim_debug 1", da_buf, sizeof(da_buf))); y += step;

    // Главное: работает ли догон и сколько кадров подряд он стоит.
    pFont->SetColor(g_da_aim_allowed ? 0xFF80FF80 : 0xFFFF6060);
    xr_sprintf(line, "догон инерции : %s   кадров подряд стоит: %u",
        g_da_aim_allowed ? "ВЗВЕДЁН" : "СНЯТ", g_da_aim_frozen_frames);
    pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

    pFont->SetColor(0xFFFFFFFF);
    xr_sprintf(line, "угол взгляд-вектор: %6.2f град   ПИК за сессию: %6.2f", g_da_aim_angle_deg,
        g_da_aim_angle_deg_max);
    pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

    xr_sprintf(line, "сдвиг оружия      : %6.4f м      ПИК за сессию: %6.4f", g_da_aim_shift,
        g_da_aim_shift_max);
    pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

    xr_sprintf(line, "скорость догона %.3f   сила %.3f   смещение прицела idx %u", g_da_aim_tendto,
        g_da_aim_power, u32(g_da_aim_offset_idx));
    pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

    // Доля прицеливания. Красным — расхождение флагов оружия и актёра либо доля, застрявшая
    // ненулевой при опущенном оружии: тогда прицельное смещение применяется от бедра.
    {
        extern float g_da_zoom_factor;
        extern bool g_da_zoom_weapon, g_da_zoom_actor;
        extern u8 g_da_zoom_idx;
        const bool da_stuck = (!g_da_zoom_weapon && g_da_zoom_factor > EPS) || (g_da_zoom_weapon != g_da_zoom_actor);
        pFont->SetColor(da_stuck ? 0xFFFF6060 : 0xFF80FF80);
        xr_sprintf(line, "доля прицеливания %.3f   оружие в прицеле: %s   актёр целится: %s   idx %u%s",
            g_da_zoom_factor, g_da_zoom_weapon ? "да " : "нет", g_da_zoom_actor ? "да " : "нет",
            u32(g_da_zoom_idx), da_stuck ? "   <-- ЗАСТРЯЛА" : "");
        pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;
    }

    // ⭐ Главная строка при подгонке прицела: куда смотрит дуло относительно центра экрана.
    // Пуля летит ровно в центр (CActor::g_fireParams), поэтому «мушка совпала» = оба числа в нуле.
    {
        extern float g_da_muzzle_yaw, g_da_muzzle_pitch;
        extern Fvector g_da_aim_offset_delta;
        const bool ok = _abs(g_da_muzzle_yaw) < 0.05f && _abs(g_da_muzzle_pitch) < 0.05f;
        pFont->SetColor(ok ? 0xFF80FF80 : 0xFFFFE080);
        xr_sprintf(line, "ДУЛО от центра: гориз %+.3f  верт %+.3f (град)%s", g_da_muzzle_yaw,
            g_da_muzzle_pitch, ok ? "   <-- СОВПАЛО" : "");
        pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

        xr_sprintf(line, "поправка прицела (стрелки в прицеливании): %+.4f %+.4f %+.4f",
            g_da_aim_offset_delta.x, g_da_aim_offset_delta.y, g_da_aim_offset_delta.z);
        pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;
    }

    // ⭐ Куда РЕАЛЬНО ушли пули относительно перекрестия. Среднее по серии: случайный разброс в нём
    // гасится, систематический увод — нет.
    {
        extern float g_da_shot_last_yaw, g_da_shot_last_pitch;
        extern float g_da_shot_sum_yaw, g_da_shot_sum_pitch;
        extern u32 g_da_shot_count;
        extern float g_da_shot_disp_deg, g_da_shot_origin_right, g_da_shot_origin_up;

        if (g_da_shot_count)
        {
            const float my = g_da_shot_sum_yaw / float(g_da_shot_count);
            const float mp = g_da_shot_sum_pitch / float(g_da_shot_count);
            const bool centred = _abs(my) < 0.05f && _abs(mp) < 0.05f;
            pFont->SetColor(centred ? 0xFF80FF80 : 0xFFFF6060);
            xr_sprintf(line, "ВЫСТРЕЛ: последний %+.3f/%+.3f | СРЕДНЕЕ по %u: %+.3f/%+.3f град%s",
                g_da_shot_last_yaw, g_da_shot_last_pitch, g_da_shot_count, my, mp,
                centred ? "   <-- в перекрестие" : "   <-- УВОД");
            pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

            pFont->SetColor(0xFFFFFFFF);
            xr_sprintf(line, "разброс ствола %.3f град | пуля стартует от глаза вправо %+.4f вверх %+.4f м",
                g_da_shot_disp_deg, g_da_shot_origin_right, g_da_shot_origin_up);
            pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;
        }
    }

    // Вторая версия: несогласованная пара обзоров. HUD рисуется своей проекцией, и при узком
    // hud_fov постоянное смещение ствола даёт на экране куда больший увод.
    pFont->SetColor(0xFF90C0FF);
    xr_sprintf(line, "обзор: мир %.1f град | HUD %.3f x %.1f = %.1f град (множитель сейчас %.3f)",
        g_fov, psHUD_FOV, g_fov, g_hud_fov_current * g_fov, g_hud_fov_current);
    pFont->Out(x, y, da_cp1251(line, da_buf, sizeof(da_buf))); y += step;

    // ⛔ Строка про «перевод HUD->мир» убрана намеренно, не забыта.
    //
    // Она мерила отношение проекций в TransformDirFromWorldToHud и горела красным «расхождение
    // -27%». Но эту версию я потом сам же и снял: та функция к стрельбе не относится вовсе, её
    // зовёт только отладочный подгонщик ImGui (player_hud_tune.cpp). Индикатор остался и продолжал
    // уводить разбор не туда — прибор, который врёт про важность, хуже отсутствующего.
}

static void DaRenderFpBodyStats()
{
    if (!g_da_fp_body_debug || !g_pGameLevel)
        return;

    CActor* A = smart_cast<CActor*>(g_pGameLevel->CurrentViewEntity());
    if (!A)
        return;

    CGameFont* pFont = UI().Font().pFontArial14;
    if (!pFont)
        return;

    IKinematics* K = smart_cast<IKinematics*>(A->Visual());
    CCustomOutfit* outfit = A->GetOutfit();

    // Камера в системе координат модели. Ноль по X/Z означает «камера ровно на оси модели»,
    // Y — высота глаз над её началом.
    Fmatrix inv;
    inv.invert(A->XFORM());
    Fvector cam_local;
    inv.transform_tiny(cam_local, Device.vCameraPosition);

    Fvector head_world{ 0.f, 0.f, 0.f };
    float head_dist = -1.f;
    u16 head_id = BI_NONE;
    if (K)
    {
        head_id = K->LL_BoneID("bip01_head");
        if (head_id != BI_NONE)
        {
            Fmatrix world;
            world.mul_43(A->XFORM(), K->LL_GetTransform(head_id));
            head_world = world.c;
            head_dist = head_world.distance_to(Device.vCameraPosition);
        }
    }

    float x = 20.0f;
    float y = 120.0f;
    const float step = 16.0f;
    pFont->SetAligment(CGameFont::alLeft);
    pFont->SetColor(0xFFFFE080);

    string512 line;

    xr_sprintf(line, "[DA] FIRST-PERSON BODY   da_fp_body=%d   fwd=%.2f  y=%.2f  cam=%d", g_da_fp_body,
        g_da_legs_fwd, g_da_legs_y, g_da_legs_cam);
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "visual  : %s", A->cNameVisual().c_str());
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "outfit  : %s%s", outfit ? outfit->cNameSect().c_str() : "NONE",
        outfit ? "" : "  <- base visual from actor config: has head and arms");
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "bones   : %d", K ? int(K->LL_BoneCount()) : -1);
    pFont->Out(x, y, line); y += step;

    y += step * 0.5f;
    pFont->SetColor(0xFF80FFC0);

    xr_sprintf(line, "camera world : %7.3f %7.3f %7.3f", Device.vCameraPosition.x,
        Device.vCameraPosition.y, Device.vCameraPosition.z);
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "model origin : %7.3f %7.3f %7.3f", A->XFORM().c.x, A->XFORM().c.y, A->XFORM().c.z);
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "CAMERA IN MODEL: %7.3f %7.3f %7.3f   (X side, Y height, Z fwd)",
        cam_local.x, cam_local.y, cam_local.z);
    pFont->Out(x, y, line); y += step;

    if (head_dist >= 0.f)
        xr_sprintf(line, "head bone    : %7.3f %7.3f %7.3f   dist to cam %.3f m", head_world.x,
            head_world.y, head_world.z, head_dist);
    else
        xr_sprintf(line, "head bone    : not found (bip01_head)");
    pFont->Out(x, y, line); y += step;

    y += step * 0.5f;
    pFont->SetColor(0xFFC0C0FF);

    xr_sprintf(line, "torso : pitch %6.1f  yaw %6.1f  roll %6.1f", rad2deg(A->cam_Active()->pitch),
        rad2deg(A->cam_Active()->yaw), rad2deg(A->cam_Active()->roll));
    pFont->Out(x, y, line); y += step;

    xr_sprintf(line, "flags : HUDview=%d  visible=%d  alive=%d", A->HUDview() ? 1 : 0,
        A->getVisible() ? 1 : 0, A->g_Alive() ? 1 : 0);
    pFont->Out(x, y, line); y += step;

    pFont->OnRender();
}

extern ENGINE_API bool bShowPauseString;
//отрисовка элементов интерфейса
void CHUDManager::RenderUI()
{
    ZoneScoped;

    if (!psHUD_Flags.is(HUD_DRAW_RT2))
        return;

    if (!b_online)
        return;

    if (true /*|| psHUD_Flags.is(HUD_DRAW | HUD_DRAW_RT)*/)
    {
        HitMarker.Render();
        if (pUIGame)
            pUIGame->Render();

        UI().RenderFont();
    }

    m_pHUDTarget->Render();

    if (Device.Paused() && bShowPauseString)
    {
        CGameFont* pFont = UI().Font().pFontGraffiti50Russian;
        pFont->SetColor(0x80FF0000);
        LPCSTR _str = StringTable().translate("st_game_paused").c_str();

        Fvector2 _pos;
        _pos.set(UI_BASE_WIDTH / 2.0f, UI_BASE_HEIGHT / 2.0f);
        UI().ClientToScreenScaled(_pos);
        pFont->SetAligment(CGameFont::alCenter);
        pFont->Out(_pos.x, _pos.y, _str);
        pFont->OnRender();
    }

    DaRenderFpBodyStats(); // [DA_PORT] отчёт по перволичному телу, da_fp_body_debug
    DaRenderAimStats(); // [DA_PORT] прибор увода прицела, da_aim_debug
}

void CHUDManager::OnEvent(EVENT E, u64 P1, u64 P2) {}
collide::rq_result& CHUDManager::GetCurrentRayQuery() { return m_pHUDTarget->GetRQ(); }
void CHUDManager::SetCrosshairDisp(float dispf, float disps)
{
    m_pHUDTarget->GetHUDCrosshair().SetDispersion(psHUD_Flags.test(HUD_CROSSHAIR_DYNAMIC) ? dispf : disps);
}

#ifdef DEBUG
void CHUDManager::SetFirstBulletCrosshairDisp(float fbdispf)
{
    m_pHUDTarget->GetHUDCrosshair().SetFirstBulletDispertion(fbdispf);
}
#endif

void CHUDManager::ShowCrosshair(bool show) { m_pHUDTarget->ShowCrosshair(show); }
void CHUDManager::HitMarked(const Fvector& dir)
{
    HitMarker.Hit(dir);
}

bool CHUDManager::AddGrenade_ForMark(CGrenade* grn) { return HitMarker.AddGrenade_ForMark(grn); }
void CHUDManager::Update_GrenadeView(Fvector& pos_actor) { HitMarker.Update_GrenadeView(pos_actor); }
void CHUDManager::SetHitmarkType(LPCSTR tex_name) { HitMarker.InitShader(tex_name); }
void CHUDManager::SetGrenadeMarkType(LPCSTR tex_name) { HitMarker.InitShader_Grenade(tex_name); }
// ------------------------------------------------------------------------------------

void CHUDManager::Load()
{
    ZoneScoped;

    if (!pUIGame)
    {
        pUIGame = Game().createGameUI();
    }
    else
    {
        pUIGame->SetClGame(&Game());
    }
}

void CHUDManager::OnUIReset()
{
    ZoneScoped;

    // [DA_PORT] Интерфейса может не быть вовсе, а сброс всё равно приходит.
    //
    // Сюда попадают по перезагрузке интерфейса (ui_restart, смена разрешения, переключение
    // масштабирования), и момент не выбираем мы: в главном меню и между уровнями игрового
    // интерфейса ещё или уже нет. Без этой проверки такой сброс кладёт игру на разыменовании нуля.
    //
    // Взято у Dead Air Refined.
    if (!pUIGame)
        return;

    pUIGame->HideShownDialogs();

    pUIGame->UnLoad();
    pUIGame->Load();

    pUIGame->OnConnected();
}

void CHUDManager::OnDisconnected()
{
    ZoneScoped;

    b_online = false;
    if (pUIGame)
        Device.seqFrame.Remove(pUIGame);
}

void CHUDManager::OnConnected()
{
    if (b_online)
        return;

    ZoneScoped;

    b_online = true;
    if (pUIGame)
        Device.seqFrame.Add(pUIGame, REG_PRIORITY_LOW - 1000);
}

void CHUDManager::net_Relcase(IGameObject* obj)
{
    ZoneScoped;

    HitMarker.net_Relcase(obj);

    VERIFY(m_pHUDTarget);
    m_pHUDTarget->net_Relcase(obj);
#ifdef DEBUG
    DBG_PH_NetRelcase(obj);
#endif
}

CDialogHolder* CurrentDialogHolder()
{
    if (MainMenu()->IsActive())
        return MainMenu();
    else
        return HUD().GetGameUI();
}
