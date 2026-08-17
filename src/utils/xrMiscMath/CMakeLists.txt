add_library(xrMiscMath STATIC)

target_sources(xrMiscMath PRIVATE
    matrix.cpp
    quaternion.cpp
    vector.cpp
    vector3d_ext.cpp
    xrMiscMath.cpp
    pch.hpp
    pch.cpp
)

target_include_directories(xrMiscMath
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

# [DA_PORT] xrMiscMath — СТАТИЧЕСКАЯ библиотека, которая линкуется ВНУТРЬ xrCore.dll, то есть её
# код становится частью самой xrCore. Но XRCORE_EXPORTS задан только для цели xrCore, поэтому здесь
# XRCORE_API разворачивался в dllIMPORT: код просил `__imp_xrDebug::Fail` — символ ИЗВНЕ той самой
# библиотеки, внутри которой уже находится.
#
# ⚠️ В релизе это не всплывало, и не потому, что было исправно: VERIFY там разворачивается в пустой
# do-while, ссылка на xrDebug::Fail не возникает вовсе, и линковщику нечего искать. Стоило собрать
# конфигурацию Mixed, где VERIFY живой, — и линковка xrCore.dll упала сразу.
#
# Дефект достался от upstream и ждал ровно того, кто соберёт Mixed под MinGW.
target_compile_definitions(xrMiscMath PRIVATE XRCORE_EXPORTS)

set_target_properties(xrMiscMath PROPERTIES
    POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS}
)

target_precompile_headers(xrMiscMath
    PRIVATE
    pch.hpp
)
