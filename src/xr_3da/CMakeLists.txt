add_executable(xr_3da)

target_sources(xr_3da PRIVATE
    entry_point.cpp
    stdafx.h

    resource.h
    resource.rc

    splash_oxr.bmp
    stalker_cop.ico
    stalker_cs.ico
    stalker_soc.ico
)

set_property(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    PROPERTY
    VS_STARTUP_PROJECT xr_3da
)

target_include_directories(xr_3da
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(xr_3da
    PRIVATE
    xrCore
    xrAPI
    xrEngine
    xrRender_GL
    $<$<BOOL:${WIN32}>:xrRenderPC_R4>
    xrGame
)

set_target_properties(xr_3da PROPERTIES
    PREFIX ""
)

# [DA_PORT] Disable ASLR for the game binary. Non-GC64 LuaJIT on Win64 needs its
# allocations in the low 2GB; high-entropy ASLR maps the image >4GB and randomizes
# layout, which intermittently corrupts luabind/LuaJIT class registration (crash in
# CGameObject::script_register). A fixed low-address load makes the export stable.
if (WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_options(xr_3da PRIVATE
        "-Wl,--disable-dynamicbase"
        "-Wl,--disable-high-entropy-va"
    )
endif()

install(TARGETS xr_3da RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
