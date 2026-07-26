add_library(xrRenderPC_R4)

target_sources(xrRenderPC_R4 PRIVATE
    packages.config
    r2_test_hw.cpp
    r4_rendertarget.h
    r4_rendertarget_accum_direct.cpp
    r4_rendertarget_build_textures.cpp
    r4_rendertarget_phase_combine.cpp
    r4_rendertarget_phase_hdao.cpp
    r4_rendertarget_phase_taa.cpp
    da_fsr2.cpp
    da_fsr2.h
    da_fsr3.cpp
    da_fsr3.h
    da_fsr3_api.h
    da_xess.cpp
    da_xess.h
    da_upscaler.h
    r4_rendertarget_phase_fsr2.cpp
    r4_rendertarget_phase_fsr3.cpp
    r4_rendertarget_phase_velocity_guard.cpp
    r4_rendertarget_phase_reactive.cpp
    r4_rendertarget_phase_xess.cpp
    r4_rendertarget_u_set_rt.cpp
    r4_shaders.cpp
    R_Backend_LOD.cpp
    R_Backend_LOD.h
    stdafx.cpp
    stdafx.h
    xrRender_R4.cpp
    # --- xrRender (common render backend) ---
    "../xrRender/Animation.cpp"
    "../xrRender/Blender.cpp"
    "../xrRender/Blender_Recorder.cpp"
    "../xrRender/Blender_Recorder_R2.cpp"
    "../xrRender/Blender_Recorder_StandartBinding.cpp"
    "../xrRender/blenders/blender_bloom_build.cpp"
    "../xrRender/blenders/Blender_BmmD_deferred.cpp"
    "../xrRender/blenders/blender_combine.cpp"
    "../xrRender/blenders/blender_deffer_aref.cpp"
    "../xrRender/blenders/blender_deffer_flat.cpp"
    "../xrRender/blenders/blender_deffer_model.cpp"
    "../xrRender/blenders/Blender_detail_still_deferred.cpp"
    "../xrRender/blenders/Blender_Editor_Selection.cpp"
    "../xrRender/blenders/Blender_Editor_Wire.cpp"
    "../xrRender/blenders/blender_light_direct.cpp"
    "../xrRender/blenders/blender_light_mask.cpp"
    "../xrRender/blenders/blender_light_occq.cpp"
    "../xrRender/blenders/blender_light_point.cpp"
    "../xrRender/blenders/blender_light_reflected.cpp"
    "../xrRender/blenders/blender_light_spot.cpp"
    "../xrRender/blenders/Blender_Lm(EbB).cpp"
    "../xrRender/blenders/blender_luminance.cpp"
    "../xrRender/blenders/Blender_Model_EbB_deferred.cpp"
    "../xrRender/blenders/Blender_Particle_deferred.cpp"
    "../xrRender/blenders/Blender_Screen_SET.cpp"
    "../xrRender/blenders/blender_ssao.cpp"
    "../xrRender/blenders/blender_taa.cpp"
    "../xrRender/blenders/Blender_tree_deferred.cpp"
    "../xrRender/blenders/dx11HDAOCSBlender.cpp"
    "../xrRender/blenders/dx11MinMaxSMBlender.cpp"
    "../xrRender/blenders/dx11MSAABlender.cpp"
    "../xrRender/blenders/dx11RainBlender.cpp"
    "../xrRender/blenders/uber_deffer.cpp"
    "../xrRender/ColorMapManager.cpp"
    "../xrRender/D3DUtils.cpp"
    "../xrRender/D3DXRenderBase.cpp"
    "../xrRender/DetailManager.cpp"
    "../xrRender/DetailManager_CACHE.cpp"
    "../xrRender/DetailManager_Decompress.cpp"
    "../xrRender/DetailManager_soft.cpp"
    "../xrRender/DetailManager_VS.cpp"
    "../xrRender/DetailModel.cpp"
    "../xrRender/du_box.cpp"
    "../xrRender/du_cone.cpp"
    "../xrRender/du_cylinder.cpp"
    "../xrRender/du_sphere.cpp"
    "../xrRender/du_sphere_part.cpp"
    "../xrRender/dxDebugRender.cpp"
    "../xrRender/dxEnvironmentRender.cpp"
    "../xrRender/dxFontRender.cpp"
    "../xrRender/dxImGuiRender.cpp"
    "../xrRender/dxLensFlareRender.cpp"
    "../xrRender/dxObjectSpaceRender.cpp"
    "../xrRender/dxParticleCustom.cpp"
    "../xrRender/dxRainRender.cpp"
    "../xrRender/dxRenderFactory.cpp"
    "../xrRender/dxStatGraphRender.cpp"
    "../xrRender/dxThunderboltDescRender.cpp"
    "../xrRender/dxThunderboltRender.cpp"
    "../xrRender/dxUIRender.cpp"
    "../xrRender/dxUISequenceVideoItem.cpp"
    "../xrRender/dxUIShader.cpp"
    "../xrRender/dxWallMarkArray.cpp"
    "../xrRender/ETextureParams.cpp"
    "../xrRender/FBasicVisual.cpp"
    "../xrRender/FHierrarhyVisual.cpp"
    "../xrRender/FLOD.cpp"
    "../xrRender/FProgressive.cpp"
    "../xrRender/FSkinned.cpp"
    "../xrRender/FTreeVisual.cpp"
    "../xrRender/FVisual.cpp"
    "../xrRender/HOM.cpp"
    "../xrRender/light.cpp"
    "../xrRender/Light_DB.cpp"
    "../xrRender/light_GI.cpp"
    "../xrRender/Light_Package.cpp"
    "../xrRender/Light_Render_Direct.cpp"
    "../xrRender/Light_Render_Direct_ComputeXFS.cpp"
    "../xrRender/light_smapvis.cpp"
    "../xrRender/light_vis.cpp"
    "../xrRender/LightTrack.cpp"
    "../xrRender/ModelPool.cpp"
    "../xrRender/NvTriStrip.cpp"
    "../xrRender/NvTriStripObjects.cpp"
    "../xrRender/occRasterizer.cpp"
    "../xrRender/occRasterizer_core.cpp"
    "../xrRender/ParticleEffect.cpp"
    "../xrRender/ParticleEffectDef.cpp"
    "../xrRender/ParticleGroup.cpp"
    "../xrRender/PSLibrary.cpp"
    "../xrRender/r__dsgraph_build.cpp"
    "../xrRender/r__dsgraph_render.cpp"
    "../xrRender/r__dsgraph_render_lods.cpp"
    "../xrRender/r__occlusion.cpp"
    "../xrRender/r__pixel_calculator.cpp"
    "../xrRender/r__sector.cpp"
    "../xrRender/r__sector_detect.cpp"
    "../xrRender/r__sector_traversal.cpp"
    "../xrRender/r__sync_point.cpp"
    "../xrRender/R_Backend.cpp"
    "../xrRender/R_Backend_DBG.cpp"
    "../xrRender/R_Backend_hemi.cpp"
    "../xrRender/R_Backend_Runtime.cpp"
    "../xrRender/R_Backend_tree.cpp"
    "../xrRender/R_Backend_xform.cpp"
    "../xrRender/r_constants.cpp"
    "../xrRender/R_DStreams.cpp"
    "../xrRender/ResourceManager.cpp"
    "../xrRender/ResourceManager_Loader.cpp"
    "../xrRender/ResourceManager_Reset.cpp"
    "../xrRender/ResourceManager_Resources.cpp"
    "../xrRender/SH_Atomic.cpp"
    "../xrRender/SH_Constant.cpp"
    "../xrRender/SH_Matrix.cpp"
    "../xrRender/Shader.cpp"
    "../xrRender/SkeletonAnimated.cpp"
    "../xrRender/SkeletonCustom.cpp"
    "../xrRender/SkeletonRigid.cpp"
    "../xrRender/SkeletonX.cpp"
    "../xrRender/SkeletonXSkinXW_CPP.cpp"
    "../xrRender/SkeletonXSkinXW_SSE.cpp"
    "../xrRender/stats_manager.cpp"
    "../xrRender/TextureDescrManager.cpp"
    "../xrRender/tss_def.cpp"
    "../xrRender/Utils/dxHashHelper.cpp"
    "../xrRender/VertexCache.cpp"
    "../xrRender/WallmarksEngine.cpp"
    "../xrRender/xr_effgamma.cpp"
    "../xrRender/xrRender_console.cpp"
    "../xrRender/xrStripify.cpp"
    # --- xrRender_R2 (R2/R3 render targets and phases) ---
    "../xrRender_R2/r2.cpp"
    "../xrRender_R2/r2_blenders.cpp"
    "../xrRender_R2/r2_loader.cpp"
    "../xrRender_R2/r2_R_calculate.cpp"
    "../xrRender_R2/r2_R_lights.cpp"
    "../xrRender_R2/r2_R_render.cpp"
    "../xrRender_R2/r2_rendertarget.cpp"
    "../xrRender_R2/r2_rendertarget_accum_omnipart_geom.cpp"
    "../xrRender_R2/r2_rendertarget_accum_point_geom.cpp"
    "../xrRender_R2/r2_rendertarget_accum_reflected.cpp"
    "../xrRender_R2/r2_rendertarget_accum_spot_geom.cpp"
    "../xrRender_R2/r2_rendertarget_draw_volume.cpp"
    "../xrRender_R2/r2_rendertarget_enable_scissor.cpp"
    "../xrRender_R2/r2_rendertarget_phase_accumulator.cpp"
    "../xrRender_R2/r2_rendertarget_phase_bloom.cpp"
    "../xrRender_R2/r2_rendertarget_phase_luminance.cpp"
    "../xrRender_R2/r2_rendertarget_phase_PP.cpp"
    "../xrRender_R2/r2_rendertarget_phase_smap_S.cpp"
    "../xrRender_R2/r3_R_rain.cpp"
    "../xrRender_R2/r3_rendertarget_accum_point.cpp"
    "../xrRender_R2/r3_rendertarget_accum_spot.cpp"
    "../xrRender_R2/r3_rendertarget_create_minmaxSM.cpp"
    "../xrRender_R2/r3_rendertarget_draw_rain.cpp"
    "../xrRender_R2/r3_rendertarget_mark_msaa_edges.cpp"
    "../xrRender_R2/r3_rendertarget_phase_occq.cpp"
    "../xrRender_R2/r3_rendertarget_phase_scene.cpp"
    "../xrRender_R2/r3_rendertarget_phase_smap_D.cpp"
    "../xrRender_R2/r3_rendertarget_phase_ssao.cpp"
    "../xrRender_R2/render_phase_sun.cpp"
    "../xrRender_R2/render_phase_sun_old.cpp"
    # --- imgui DX11 backend (not in xrImGui target, compile directly) ---
    "${CMAKE_SOURCE_DIR}/Externals/imgui/backends/imgui_impl_dx11.cpp"
    "${CMAKE_SOURCE_DIR}/Externals/imgui/backends/imgui_impl_dx11.h"
    # --- xrRenderDX11 (DX11 backend) ---
    "../xrRenderDX11/3DFluid/dx113DFluidBlenders.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidData.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidEmitters.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidGrid.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidManager.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidObstacles.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidRenderer.cpp"
    "../xrRenderDX11/3DFluid/dx113DFluidVolume.cpp"
    "../xrRenderDX11/Blender_Recorder_R3.cpp"
    "../xrRenderDX11/dx11BufferUtils.cpp"
    "../xrRenderDX11/dx11ConstantBuffer.cpp"
    "../xrRenderDX11/dx11DetailManager_VS.cpp"
    "../xrRenderDX11/dx11HW.cpp"
    "../xrRenderDX11/dx11HWCaps.cpp"
    "../xrRenderDX11/dx11r_constants.cpp"
    "../xrRenderDX11/dx11r_constants_cache.cpp"
    "../xrRenderDX11/dx11r_screenshot.cpp"
    "../xrRenderDX11/dx11ResourceManager_Resources.cpp"
    "../xrRenderDX11/dx11ResourceManager_Scripting.cpp"
    "../xrRenderDX11/dx11SH_RT.cpp"
    "../xrRenderDX11/dx11SH_Texture.cpp"
    "../xrRenderDX11/dx11StateUtils.cpp"
    "../xrRenderDX11/dx11Texture.cpp"
    "../xrRenderDX11/dx11TextureUtils.cpp"
    "../xrRenderDX11/StateManager/dx11SamplerStateCache.cpp"
    "../xrRenderDX11/StateManager/dx11ShaderResourceStateCache.cpp"
    "../xrRenderDX11/StateManager/dx11State.cpp"
    "../xrRenderDX11/StateManager/dx11StateCache.cpp"
    "../xrRenderDX11/StateManager/dx11StateManager.cpp"
)

target_include_directories(xrRenderPC_R4
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Layers/xrRender_R2"
    "${CMAKE_SOURCE_DIR}/sdk/include"
    "${CMAKE_SOURCE_DIR}/sdk/include/DirectXMesh"
    "${CMAKE_SOURCE_DIR}/sdk/include/nvapi"
    "${CMAKE_SOURCE_DIR}/Externals"
    "${CMAKE_SOURCE_DIR}/Externals/AGS_SDK"
    # MinGW has a stub DirectXMath.h at include/ root missing XMVECTOR;
    # the real implementation is in include/directxmath/ — prioritize it.
    "$<$<BOOL:${MINGW}>:C:/msys64/mingw64/include/directxmath>"
)

target_link_libraries(xrRenderPC_R4
    PRIVATE
    xrCore
    xrCDB
    xrEngine
    xrParticles
    xrScriptEngine
    xrAPI
    xrMiscMath
    $<$<BOOL:${WIN32}>:DirectXTex>
    $<$<BOOL:${WIN32}>:d3dcompiler>
    $<$<BOOL:${WIN32}>:dxguid>
    xrMaterialSystem
    $<$<BOOL:${MINGW}>:gomp>
)

# [DA_PORT] AMD FidelityFX Super Resolution 2, with the DX11 backend.
#
# Built from source rather than taken from the NuGet package the reference integrations use: that
# package ships MSVC .lib files, and this project is built with MinGW, whose C++ ABI is incompatible.
# The sources compile with GCC after three small fixes (see Externals/ffx-fsr2-api/README.md).
target_include_directories(xrRenderPC_R4 PRIVATE
    "${CMAKE_SOURCE_DIR}/Externals/ffx-fsr2-api/include"
    "${CMAKE_SOURCE_DIR}/Externals/ffx-fsr3/include"
)
target_link_libraries(xrRenderPC_R4 PRIVATE
    "${CMAKE_SOURCE_DIR}/Externals/ffx-fsr2-api/lib/libffx_fsr2_api_dx11_x64.a"
    "${CMAKE_SOURCE_DIR}/Externals/ffx-fsr2-api/lib/libffx_fsr2_api_x64.a"
    # [DA_PORT] FSR 3 upscaler, DX11 - see Externals/ffx-fsr3/README.md
    "${CMAKE_SOURCE_DIR}/Externals/ffx-fsr3/lib/libffx_fsr3_dx11_x64.a"
)

# [DA_PORT] Intel XeSS, second temporal upscaler. Unlike FSR 2 this one needs no building: Intel ships
# a DX11 path with prebuilt binaries. Their import library is MSVC-produced, but the whole XeSS API is
# extern "C", so there is no C++ ABI to clash with and MinGW links it directly.
target_include_directories(xrRenderPC_R4 PRIVATE
    "${CMAKE_SOURCE_DIR}/Externals/xess/inc"
)
target_link_libraries(xrRenderPC_R4 PRIVATE
    "${CMAKE_SOURCE_DIR}/Externals/xess/lib/libxess_dx11.lib"
)

target_compile_definitions(xrRenderPC_R4
    PRIVATE
    XRRENDER_R4_EXPORTS
    USE_DX11
    RENDER_NAMESPACE=render_r4
    PURE_DYNAMIC_CAST
)

set_target_properties(xrRenderPC_R4 PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrRenderPC_R4
    PRIVATE
    stdafx.h
)

if (BUILD_SHARED_LIBS)
    install(TARGETS xrRenderPC_R4 LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
