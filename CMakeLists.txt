cmake_minimum_required(VERSION 3.23)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PreProjectInit)

calculate_xray_build_id(XRAY_BUILD_ID)
project(OpenXRay
    DESCRIPTION "OpenXRay is an improved version of the X-Ray Engine, \
                the game engine used in the world-famous S.T.A.L.K.E.R. game series by GSC Game World."
    VERSION 1.6.02.${XRAY_BUILD_ID}
    HOMEPAGE_URL "https://github.com/OpenXRay/xray-16"
    LANGUAGES CXX C
)

option(BUILD_SHARED_LIBS "Build shared libraries"     ${BUILD_SHARED_LIBS_DEFAULT_VALUE})

option(XRAY_USE_LUAJIT   "Use LuaJIT"                 ON)
option(XRAY_USE_ASAN     "Use AddressSanitizer"       OFF)
option(XRAY_ENABLE_TRACY "Enable tracy profiler"      OFF)

include(XRay.Build)
include(XRay.Packaging)

add_subdirectory(Externals)
add_subdirectory(src)
add_subdirectory(res)
add_subdirectory(misc)

# [DA_PORT] Explicit clustered image bases for our PE modules (MinGW only).
# ld's default --enable-auto-image-base scatters DLL preferred bases over an ~8GB
# range (hash of the dll name). MinGW auto-imports (functions/data referenced across
# DLLs without __declspec(dllimport), e.g. lua_* from xrLuaJIT used by xrAICore)
# are fixed up at load by 32-bit runtime pseudo-relocations, which silently require
# every importer<->exporter pair to sit within +-2GB. With the scattered bases that
# broke at startup: xrAICore's relocator hit lua_pushboolean (xrLuaJIT ~4.2GB away),
# called __report_error and the process died/hung before even creating the log
# (WER dumps xr_3da.exe.*.dmp, fault offset 0x3384d = __report_error).
# Packing all modules into one <2GB window (and <2GB from the exe at 0x140000000)
# makes every pseudo-reloc reachable, deterministically, on every machine.
# Slots: 32MB per module, 64MB for xrGame (SizeOfImage 44MB, the largest).
if(MINGW)
    set(_da_image_base_pairs
        xrAICore            0x180000000
        xrAPI               0x182000000
        xrCDB               0x184000000
        xrCore              0x186000000
        xrEngine            0x188000000
        xrGame              0x18A000000 # 64MB slot, next starts at 0x18E000000
        xrGameSpy           0x18E000000
        xrLuaJIT            0x190000000
        xrLuabind           0x192000000
        xrMaterialSystem    0x194000000
        xrNetServer         0x196000000
        xrODE               0x198000000
        xrOPCODE            0x19A000000
        xrParticles         0x19C000000
        xrPhysics           0x19E000000
        xrRenderPC_R4       0x1A0000000
        # 0x1A2000000 — освободился после удаления xrRender_GL, слот намеренно не переиспользуется:
        # адреса в старых логах крашей должны и дальше раскладываться по этой таблице.
        xrScriptEngine      0x1A4000000
        xrSound             0x1A6000000
        xrUICore            0x1A8000000
    )
    list(LENGTH _da_image_base_pairs _da_len)
    math(EXPR _da_last "${_da_len} - 2")
    foreach(_da_i RANGE 0 ${_da_last} 2)
        list(GET _da_image_base_pairs ${_da_i} _da_target)
        math(EXPR _da_i1 "${_da_i} + 1")
        list(GET _da_image_base_pairs ${_da_i1} _da_base)
        if(TARGET ${_da_target})
            set_property(TARGET ${_da_target} APPEND PROPERTY
                LINK_OPTIONS "LINKER:--image-base,${_da_base}")
        endif()
    endforeach()
endif()
