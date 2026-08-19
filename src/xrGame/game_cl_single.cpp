#include "pch_script.h"

#include "game_cl_single.h"

extern int g_da_time_log;
#include "UIGameSP.h"
#include "Actor.h"
#include "clsid_game.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_time_manager.h"
#include "xrCore/xr_token.h"

ESingleGameDifficulty g_SingleGameDifficulty = egdStalker;

extern const  xr_token difficulty_type_token[] = {
    {"gd_novice", egdNovice}, {"gd_stalker", egdStalker}, {"gd_veteran", egdVeteran}, {"gd_master", egdMaster}, {0, 0}};

game_cl_Single::game_cl_Single() {}
CUIGameCustom* game_cl_Single::createGameUI()
{
    CLASS_ID clsid = CLSID_GAME_UI_SINGLE;
    CUIGameSP* pUIGame = smart_cast<CUIGameSP*>(NEW_INSTANCE(clsid));
    R_ASSERT(pUIGame);
    pUIGame->Load();
    pUIGame->SetClGame(this);
    pUIGame->Init(0);
    pUIGame->Init(1);
    pUIGame->Init(2);
    return pUIGame;
}

pcstr game_cl_Single::getTeamSection(int Team) { return NULL; };
void game_cl_Single::OnDifficultyChanged() { Actor()->OnDifficultyChanged(); }
ALife::_TIME_ID game_cl_Single::GetGameTime()
{
    ALife::_TIME_ID result;
    if (ai().get_alife() && ai().alife().initialized())
        result = ai().alife().time_manager().game_time();
    else
        result = inherited::GetGameTime();

    // [DA_PORT] Измеряет ФАКТИЧЕСКИЙ ход времени, а не заявленный, — на случай, если что-то
    // применяет коэффициент дважды или в других единицах.
    //
    // 🪤 Первая версия прибора стояла в game_GameState::GetGameTime, и он молчал: в одиночной
    // игре этот метод ПЕРЕОПРЕДЕЛЁН здесь и в базовый класс управление не заходит вовсе.
    // Время берётся из менеджера ALife, поэтому и мерить надо здесь.
    if (g_da_time_log)
    {
        static u32 last_real = 0;
        static ALife::_TIME_ID last_game = 0;
        if (Device.dwTimeGlobal - last_real > 5000)
        {
            if (last_real != 0 && result > last_game)
            {
                const double real_sec = (Device.dwTimeGlobal - last_real) / 1000.0;
                const double game_sec = (result - last_game) / 1000.0;
                if (real_sec > 0.0)
                {
                    const double ratio = game_sec / real_sec;
                    Msg("* [DA_TIME] ход времени: %.2fx (задано %.2fx), игровая минута = %.1f реальных секунд",
                        ratio, GetGameTimeFactor(), ratio > 0.0 ? 60.0 / ratio : 0.0);
                    FlushLog();
                }
            }
            last_real = Device.dwTimeGlobal;
            last_game = result;
        }
    }

    return result;
}

ALife::_TIME_ID game_cl_Single::GetStartGameTime()
{
    if (ai().get_alife() && ai().alife().initialized())
        return (ai().alife().time_manager().start_game_time());
    else
        return (inherited::GetStartGameTime());
}

float game_cl_Single::GetGameTimeFactor()
{
    if (ai().get_alife() && ai().alife().initialized())
        return (ai().alife().time_manager().time_factor());
    else
        return (inherited::GetGameTimeFactor());
}

void game_cl_Single::SetGameTimeFactor(const float fTimeFactor)
{
    Level().Server->GetGameState()->SetGameTimeFactor(fTimeFactor);
}

ALife::_TIME_ID game_cl_Single::GetEnvironmentGameTime()
{
    if (ai().get_alife() && ai().alife().initialized())
        return (ai().alife().time_manager().game_time());
    else
        return (inherited::GetEnvironmentGameTime());
}

float game_cl_Single::GetEnvironmentGameTimeFactor()
{
    if (ai().get_alife() && ai().alife().initialized())
        return (ai().alife().time_manager().time_factor());
    else
        return (inherited::GetEnvironmentGameTimeFactor());
}

void game_cl_Single::SetEnvironmentGameTimeFactor(const float fTimeFactor)
{
    if (ai().get_alife() && ai().alife().initialized())
        Level().Server->GetGameState()->SetGameTimeFactor(fTimeFactor);
    else
        inherited::SetEnvironmentGameTimeFactor(fTimeFactor);
}

void game_cl_Single::SetEnvironmentGameTimeFactor(ALife::_TIME_ID GameTime, const float fTimeFactor)
{
    if (ai().get_alife() && ai().alife().initialized())
        Level().Server->GetGameState()->SetGameTimeFactor(GameTime, fTimeFactor);
    else
        inherited::SetEnvironmentGameTimeFactor(GameTime, fTimeFactor);
}
