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

# [DA_PORT] ASLR/high-entropy-VA disable is now applied GLOBALLY in
# XRay.Compiler.GNULike.cmake (all DLLs + exe), since the same 32-bit pseudo-relocation
# overflow also breaks DLL loading under high-entropy VA. The per-target block is no
# longer needed.

install(TARGETS xr_3da RUNTIME
    DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
