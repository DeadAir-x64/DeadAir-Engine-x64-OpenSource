#ifndef _LOCAL_RAND
#define _LOCAL_RAND
#include "xr_types.h"
#include "xrCore/xrDebug.h"

/*
u32 dwRandSeed;
IC u32 dwfRandom(u32 dwRange)
{
u32 dwResult;
__asm {
mov eax,dwRange _eax = dwRange
imul edx,dwRandSeed,08088405H
inc edx
mov dwRandSeed,edx dwRandSeed = (dwRandSeed * 08088405H)+1
mul edx return (u64(dwRange) * u64(dwRandSeed)) >> 32
mov dwResult,edx
}
return(dwResult);
}
*/

class XRCORE_API CRandom
{
    volatile s32 holdrand;

public:
    CRandom() : holdrand(1){};
    CRandom(s32 _seed) : holdrand(_seed){};

    void seed(s32 val) { holdrand = val; }
    s32 maxI() { return 32767; }
    ICN s32 randI() noexcept { return (holdrand = holdrand * 214013L + 2531011L) >> 16 & 0x7fff; }
    // [DA_PORT] Пустой диапазон больше не роняет игру делением на ноль.
    //
    // `VERIFY(max)` в релизе не существует, а `randI() % 0` — это аппаратное исключение, то есть
    // мгновенный вылет без внятного сообщения. Диапазон схлопывается штатно: границы почти всегда
    // приходят из данных (`randI(min, max)` при `min == max`) или из размера контейнера, который
    // может оказаться пустым. Вызовов у этой пары по дереву **217**, проверить каждый вручную
    // нельзя — поэтому проверка стоит в корне.
    //
    // Ноль — единственный осмысленный ответ: из пустого набора выбрать нечего, а вызывающий
    // получает нижнюю границу своего диапазона. Взято из журнала Monolith («Fix possible crash in
    // randI when getting random value with min == max in range»), причина проверена по своему коду.
    s32 randI(s32 max)
    {
        VERIFY(max);
        return max > 0 ? randI() % max : 0;
    };
    s32 randI(s32 min, s32 max) { return min + randI(max - min); }
    s32 randIs(s32 range) { return randI(-range, range); }
    s32 randIs(s32 range, s32 offs) { return offs + randIs(range); }
    float maxF() { return 32767.f; }
    float randF() { return float(randI()) / maxF(); }
    float randF(float max) { return randF() * max; }
    float randF(float min, float max) { return min + randF(max - min); }
    float randFs(float range) { return randF(-range, range); }
    float randFs(float range, float offs) { return offs + randFs(range); }
};

XRCORE_API extern CRandom Random;

#endif
