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

    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2 | HUD_DRAW_RT2))
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

    if (!psHUD_Flags.is(HUD_WEAPON | HUD_WEAPON_RT | HUD_WEAPON_RT2 | HUD_DRAW_RT2))
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
