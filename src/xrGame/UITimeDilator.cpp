#include "StdAfx.h"

#include "UITimeDilator.h"

// [DA_PORT] Ход времени, выбранный игроком (console_commands.cpp). См. stopTimeDilation ниже.
extern float g_da_time_factor_user;

UITimeDilator* time_dilator;

UITimeDilator* TimeDilator()
{
    if (!time_dilator)
    {
        time_dilator = xr_new<UITimeDilator>();
    }

    return time_dilator;
}

void CloseTimeDilator()
{
    if (time_dilator)
    {
        xr_delete(time_dilator);
    }
}

void UITimeDilator::SetUiTimeFactor(float timeFactor)
{
    uiTimeFactor = timeFactor;
    startTimeDilation();
}

float UITimeDilator::GetUiTimeFactor() const
{
    return uiTimeFactor;
}

void UITimeDilator::SetModeEnability(UIMode mode, bool status)
{
    enabledModes.set(mode, status);

    if (status)
    {
        startTimeDilation();
    }
    else if (!status && mode == currMode)
    {
        stopTimeDilation();
    }
}

bool UITimeDilator::GetModeEnability(UIMode mode) const
{
    return enabledModes.is(mode);
}

void UITimeDilator::SetCurrentMode(UIMode mode)
{
    currMode = mode;
    if (mode != None)
    {
        startTimeDilation();
    }
    else
    {
        stopTimeDilation();
    }
}

void UITimeDilator::startTimeDilation()
{
    if (enabledModes.is(currMode) && currMode != None)
        Device.time_factor(uiTimeFactor);
}

void UITimeDilator::stopTimeDilation()
{
    // [DA_PORT] Возвращаем ход времени, выбранный игроком, а не жёсткую единицу.
    //
    // Здесь стояло `Device.time_factor(1.0f)`, и это гасило команду `time_factor` при каждом закрытии
    // инвентаря или КПК — со стороны игрока «ускорение времени иногда работает, иногда нет». Хуже
    // того, строка не проверяла, включено ли само замедление: время сбрасывалось даже там, где
    // интерфейс его вовсе не трогал.
    Device.time_factor(g_da_time_factor_user);
}
