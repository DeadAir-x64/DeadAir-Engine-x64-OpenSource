#pragma once

#include "Common/Common.hpp"

// XXX: Get rid of xrMiscMath
//
// [DA_PORT] ⚠️ Здесь XRCORE_API объявлялся импортом ВСЕГДА, минуя проверку XRCORE_EXPORTS — ту
// самую, что делает xrCore.h. А xrMiscMath линкуется ВНУТРЬ xrCore.dll, то есть её код становится
// частью самой xrCore: помеченный импортом, он просил `__imp_xrDebug::Fail` — символ извне той
// библиотеки, внутри которой уже лежит. Линковка xrCore.dll падала с undefined reference.
//
// В релизе это не всплывало и не потому, что было исправно: VERIFY там разворачивается в пустой
// do-while, ссылка на xrDebug::Fail не возникает вовсе, и искать линковщику нечего. Дефект ждал
// того, кто соберёт конфигурацию Mixed, где VERIFY живой.
//
// 🪤 Одной правки в CMake (добавить XRCORE_EXPORTS цели xrMiscMath) НЕ ХВАТИЛО: определение до
// компилятора доходило, но этот заголовок перебивал его безусловным define. Работают только обе
// правки вместе — CMake задаёт признак, заголовок его учитывает.
#ifdef XRAY_STATIC_BUILD
#   define XRCORE_API
#elif defined(XRCORE_EXPORTS)
#   define XRCORE_API XR_EXPORT
#else
#   define XRCORE_API XR_IMPORT
#endif

#include "xrCommon/math_funcs_inline.h"
#include "xrCore/_std_extensions.h"
