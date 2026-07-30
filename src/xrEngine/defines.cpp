#include "stdafx.h"

#ifdef DEBUG
ECORE_API bool bDebug = false;

#endif

// Video
DeviceMode psDeviceMode =
{
    .Monitor      = 0,
    // [DA_PORT] По умолчанию — полный экран в окне, а не окно без рамки. Во-первых, это то, чего
    // ждёт игрок от первого запуска; во-вторых, режима без рамки больше нет в списке (см.
    // CCC_VidWindowMode), и значение по умолчанию обязано быть одним из предлагаемых — иначе
    // список настроек открывается ни на чём.
    .WindowStyle  = rsFullscreenBorderless,
    .Width        = 0,
    .Height       = 0,
    .RefreshRate  = 0,
    .BitsPerPixel = 32
};

// release version always has "mt_*" enabled
Flags32 psDeviceFlags =
{
   rsDrawStatic | rsDrawDynamic | rsDrawDetails | rsDrawParticles | mtSound | mtNetwork
};

// textures
int psTextureLOD = 1;
