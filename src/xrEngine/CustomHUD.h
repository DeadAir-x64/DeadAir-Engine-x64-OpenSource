#pragma once
#include "xrEngine/EngineAPI.h"
#include "xrEngine/EventAPI.h"
#include "xrEngine/pure.h"

ENGINE_API extern Flags32 psHUD_Flags;
#define HUD_CROSSHAIR (1 << 0)
#define HUD_CROSSHAIR_DIST (1 << 1)
#define HUD_WEAPON (1 << 2)
#define HUD_INFO (1 << 3)
#define HUD_DRAW (1 << 4)
#define HUD_CROSSHAIR_RT (1 << 5)
#define HUD_WEAPON_RT (1 << 6)
#define HUD_CROSSHAIR_DYNAMIC (1 << 7)
#define HUD_CROSSHAIR_RT2 (1 << 9)
#define HUD_DRAW_RT (1 << 10)
#define HUD_WEAPON_RT2 (1 << 11)
#define HUD_DRAW_RT2 (1 << 12)
#define HUD_LEFT_HANDED (1 << 13)
// [DA_PORT] dedicated bit for the "hud_draw_map" compat alias (see console_commands.cpp) - it used
// to be mapped onto HUD_DRAW itself, so toggling it off (as Dead Air's map/PDA scripts do) silently
// killed the entire main indicators HUD (health/boosts) forever, since the off state got persisted
// to user.ltx. Give it its own bit so it can't affect anything else.
#define HUD_DRAW_MAP (1 << 14)
// [DA_PORT] Dead Air's own flag, gating the bottom-left readout: health bar, stamina bar, ammo counts,
// fire mode, grenade count and the weapon icon. Its gameplay options menu exposes it as a checkbox
// ("hud_draw_info") next to the map one. The alias used to be pointed at HUD_INFO, which is the
// look-at target info and nothing else, so the option toggled the wrong thing and the readout was
// never gated at all. The author used bit 14 for this and 13 for the map; both are taken here (13 by
// upstream's left-handed flag, 14 by the map alias above), and the bit number is private to the
// engine anyway - only the command name has to match.
#define HUD_DRAW_INFO (1 << 15)

class IGameObject;

class ENGINE_API XR_NOVTABLE CCustomHUD
    : public IEventReceiver,
      public CUIResetNotifier
{
public:
    virtual void Render_First(u32 context_id) = 0;
    // [DA_PORT] Отдельный вход для ТЕНЕВОГО прохода. Раньше и камера, и построение теневого
    // подпространства звали один и тот же Render_First, поэтому в обоих случаях рисовалась одна и та
    // же модель актёра — и выбрать между «ноги от первого лица» и «целое тело для тени» было нельзя.
    // Теперь проходы разведены: Render_First рисует то, что видит игрок, Render_ActorShadow — то,
    // что отбрасывает тень. См. CHUDManager и CActor::renderable_RenderShadow.
    virtual void Render_ActorShadow(u32 context_id) = 0;
    virtual void Render_Last(u32 context_id) = 0;

    virtual void OnFrame() = 0;
    virtual void Load() = 0;
    virtual void OnDisconnected() = 0;
    virtual void OnConnected() = 0;
    virtual void RenderActiveItemUI() = 0;
    virtual bool RenderActiveItemUIQuery() = 0;
    virtual void net_Relcase(IGameObject* object) = 0;
};
