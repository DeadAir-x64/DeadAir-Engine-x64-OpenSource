include_guard()

if (APPLE)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        if ($ENV{MACOSX_DEPLOYMENT_TARGET})
            set(CMAKE_OSX_DEPLOYMENT_TARGET $ENV{MACOSX_DEPLOYMENT_TARGET})
        else()
            message(NOTICE "CMAKE_OSX_DEPLOYMENT_TARGET is not set, defaulting it to your system's version: ${CMAKE_SYSTEM_VERSION}")
            set(CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_SYSTEM_VERSION})
        endif()
    endif()
    message(STATUS "CMAKE_OSX_DEPLOYMENT_TARGET: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# Redirecting the default installation path /usr/local to /usr no need to use -DCMAKE_INSTALL_PREFIX =/usr
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr")
endif()

include(GNUInstallDirs)

if (DISABLE_PORTABLE_MODE)
    add_compile_definitions(DISABLE_PORTABLE_MODE)
endif()

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
set(CMAKE_MACOSX_RPATH TRUE)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
    set(ENV{CCACHE_SLOPPINESS} pch_defines,time_macros)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0)
        message(FATAL_ERROR "Building with a gcc version less than 8.0 is not supported.")
    endif()
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # XXX: Remove -fdelayed-template-parsing
    add_compile_options(
        -fdelayed-template-parsing
        -Wno-unused-command-line-argument
        -Wno-inconsistent-missing-override
    )
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT XRAY_USE_DEFAULT_CXX_LIB)
    if (NOT XRAY_CXX_LIB)
        include(CheckCXXCompilerFlag)
        CHECK_CXX_COMPILER_FLAG("-stdlib=libc++" LIBCPP_AVAILABLE)
        CHECK_CXX_COMPILER_FLAG("-stdlib=libstdc++" LIBSTDCPP_AVAILABLE)

        if (LIBCPP_AVAILABLE)
            set(XRAY_CXX_LIB "libc++" CACHE STRING "" FORCE)
        elseif (LIBSTDCPP_AVAILABLE)
            set(XRAY_CXX_LIB "libstdc++" CACHE STRING "" FORCE)
        else()
            message("Neither libstdc++ nor libc++ are available. Hopefully, system has another custom stdlib?")
        endif()
    endif()

    if (XRAY_CXX_LIB STREQUAL "libstdc++")
        add_compile_options(-stdlib=libstdc++)
        add_link_options(-stdlib=libstdc++)
    elseif (XRAY_CXX_LIB STREQUAL "libc++")
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++)
        if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
            add_compile_options(-lcxxrt)
            add_link_options(-lcxxrt)
        else()
            add_compile_options(-lc++abi)
            add_link_options(-lc++abi)
        endif()
    endif()
endif()

add_compile_options(-Wno-attributes)
if (APPLE)
    add_compile_options(-Wl,-undefined,error)
else()
    add_compile_options(-Wl,--no-undefined)
endif()

# TODO test
if (XRAY_USE_ASAN)
    add_compile_options(
        -fsanitize=address
        -fsanitize=leak
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -fno-sanitize=vptr
    )

    add_link_options(
        $<$<CXX_COMPILER_ID:Clang>:-shared-libasan>
        -fsanitize=address
        -fsanitize=leak
        -fsanitize=undefined
    )
endif()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(PROJECT_PLATFORM_ARM64 TRUE)
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "armv*")
    set(PROJECT_PLATFORM_ARM TRUE)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "e2k")
    set(PROJECT_PLATFORM_E2K TRUE)
elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "ppc64le")
    set(PROJECT_PLATFORM_PPC TRUE)
endif()

if (PROJECT_PLATFORM_ARM)
    add_compile_options(-mfpu=neon)
elseif (PROJECT_PLATFORM_ARM64)
    #add_compile_options()
elseif (PROJECT_PLATFORM_E2K)
    add_compile_options(-Wno-unknown-pragmas)
elseif (PROJECT_PLATFORM_PPC)
    add_compile_options(
        -maltivec
        -mabi=altivec
    )
    add_compile_definitions(NO_WARN_X86_INTRINSICS)
else()
    add_compile_options(
        -mfpmath=sse
        -msse3
    )
endif()

if (XRAY_LINKER)
    add_link_options(-fuse-ld=${XRAY_LINKER})
endif()

# [DA_PORT] Disable ASLR/high-entropy-VA for ALL targets (DLLs + exe). MinGW's 32-bit
# pseudo-relocation scheme cannot resolve cross-DLL references when images are mapped
# at high-entropy addresses (>4GB); this manifests as "32 bit pseudo relocation out of
# range" runtime failures at DLL load. Without --disable-dynamicbase the loader randomizes
# image bases and pseudo-relocations overflow. LuaJIT (non-GC64) also needs low 2GB.
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_link_options(
        "-Wl,--disable-dynamicbase"
        "-Wl,--disable-high-entropy-va"
    )
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-Og)
endif()

# [DA_PORT] ⭐ ИГРАБЕЛЬНАЯ сборка с ЖИВЫМИ VERIFY.
#
# Конфигурация Mixed определяет DEBUG (см. XRay.Build.cmake), а значит все ~5700 вызовов VERIFY в
# дереве перестают быть пустышкой. Это единственный способ узнать, какие из них РЕАЛЬНО срабатывают:
# переписывать их вручную нельзя — часть нарушается штатно и в релизе безвредна ровно потому, что
# вырезается (проверено на своей шкуре в задаче #46, где живой R_ASSERT ронял загрузку сохранения).
#
# ⛔ Но флаги для Mixed не задавал НИКТО — ни здесь, ни в upstream. Пустая строка означает для GCC
# отсутствие -O, то есть -O0: сборка получалась такой же неиграбельной, как полный Debug, и смысл
# конфигурации терялся. У Monolith та же беда решена не была: их VerifiedDX11 честно описан как
# «до 20 ГБ ОЗУ и около половины производительности релиза».
#
# -O2, а не -O3: нужна играбельность при читаемом стеке, а не рекорд. -g даёт привязку адреса к
# строке — без неё сработавший VERIFY назовёт файл и строку самой проверки, но не путь к ней.
# Межмодульная оптимизация (LTO) для Mixed намеренно НЕ включается: она стирает эту привязку.
#
# 🪤 Пустую запись в кэше CMake заводит сам, и обычный `set(... CACHE ...)` её НЕ перезаписывает —
# первая попытка молча ничего не сделала, флаги так и остались пустыми. Поэтому FORCE, но только
# когда значение пустое: заданное вручную (например, для разовой сборки с -O0) остаётся в силе.
if (NOT CMAKE_CXX_FLAGS_MIXED)
    set(CMAKE_CXX_FLAGS_MIXED "-O2 -g" CACHE STRING "Флаги C++ для Mixed: релизная скорость, живые VERIFY" FORCE)
endif()
if (NOT CMAKE_C_FLAGS_MIXED)
    set(CMAKE_C_FLAGS_MIXED "-O2 -g" CACHE STRING "Флаги C для Mixed: релизная скорость, живые VERIFY" FORCE)
endif()

# [DA_PORT] ⚠️ ТОЛЬКО для Mixed: разрешить дубликат символа из ДВУХ прекомпилированных библиотек AMD.
#
# ffx-fsr2-api и ffx-fsr3 обе содержат ffxGetDX11BindFlags. Линкер тянет .o из архива лениво —
# только под неразрешённый символ. В релизе ffx_dx11.o из FSR3 не вытягивается вовсе, и дубля нет;
# в Mixed добавочный отладочный код (живые VERIFY, блоки #ifdef DEBUG) создаёт ссылку, которая
# втягивает весь этот объектник, и всплывает второе определение той же функции.
#
# Это НЕ маскирует наших дефектов: символ внешний, из готовых .a от AMD, обе реализации идентичны —
# линкер берёт первую. Наши три дефекта сборки (xrMiscMath, dx11StateCache, da_vs_needs_uv) уже
# найдены и починены по-настоящему, ДО этого флага. Он относится к сторонним SDK и только к
# отладочной конфигурации, которую игроку не отдают.
add_link_options($<$<CONFIG:Mixed>:-Wl,--allow-multiple-definition>)

if (NOT WIN32 OR MINGW)
    find_package(SDL2 2.0.18 REQUIRED)
    find_package(OpenAL REQUIRED)
    find_package(JPEG)
    find_package(Ogg REQUIRED)
    find_package(Vorbis REQUIRED)
    find_package(Theora REQUIRED)
    find_package(LZO REQUIRED)
    find_package(mimalloc NAMES mimalloc2 mimalloc2.0 mimalloc)
endif()

# Memory allocator option
if (mimalloc_FOUND)
    set(MEMORY_ALLOCATOR "mimalloc" CACHE STRING "Use specific memory allocator (mimalloc/standard)")
else()
    set(MEMORY_ALLOCATOR "standard" CACHE STRING "Use specific memory allocator (mimalloc/standard)")
endif()
set_property(CACHE MEMORY_ALLOCATOR PROPERTY STRINGS "mimalloc" "standard")

if (MEMORY_ALLOCATOR STREQUAL "mimalloc" AND NOT mimalloc_FOUND)
    message(FATAL_ERROR "mimalloc allocator requested but not found. Please, install mimalloc package or select standard allocator.")
endif()

message(STATUS "Using ${MEMORY_ALLOCATOR} memory allocator")

get_property(LIB64 GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS)

if ("${LIB64}" STREQUAL "TRUE")
    set(LIBSUFFIX 64)
else()
    set(LIBSUFFIX "")
endif()

set(XRAY_ENABLE_WARNINGS
    -Wall
    #-Werror
    -Wextra
    #-pedantic
    -Wno-unknown-pragmas
    -Wno-strict-aliasing
    -Wno-parentheses
    -Wno-unused-label
    -Wno-unused-parameter
    -Wno-switch
    -Wno-trigraphs
    #-Wno-padded
    #-Wno-c++98-compat
    #-Wno-c++98-compat-pedantic
    #-Wno-c++11-compat
    #-Wno-c++11-compat-pedantic
    #-Wno-c++14-compat
    #-Wno-c++14-compat-pedantic
    #-Wno-newline-eof
    $<$<CXX_COMPILER_ID:GNU>:$<$<COMPILE_LANGUAGE:CXX>:-Wno-class-memaccess>>
    $<$<CXX_COMPILER_ID:GNU>:$<$<COMPILE_LANGUAGE:CXX>:-Wno-interference-size>>
)

set(XRAY_DISABLE_WARNINGS "-w")
