# FSR 3 upscaler, DirectX 11, built for MinGW

`lib/libffx_fsr3_dx11_x64.a` — the FSR 3 upscaler and its DX11 backend, compiled with the same GCC the
engine uses. Written down because the equivalent note for FSR 2 was lost, and rebuilding it meant
rediscovering every step.

## Where it comes from

AMD does not ship a DirectX 11 backend for FSR 3 — the FidelityFX SDK is DX12 and Vulkan only, and the
documentation says so plainly. This is built from the community port
[optiscaler/FidelityFX-SDK-DX11](https://github.com/optiscaler/FidelityFX-SDK-DX11) (MIT, itself a fork
of metarutaiga's), which adds the DX11 backend. Version is FSR 3.0.4.

Why bother, given FSR 2 already works: the FSR 3 upscaler has passes FSR 2 does not —
`shading_change` and `luma_instability`. Those address the case where a surface's shading changes while
its motion vectors say it did not move, which is exactly the artefact that breaks metal in this mod
(see `docs/10_DEBUG_DETAIL_WEAVE.md`).

## Rebuilding

Working copy of the SDK lives outside the repository at `D:\Dead Air\_ffx3` (342 MB, not worth
committing). Two stages, because the shader blobs must be generated before anything can compile.

**1. Generate the shader permutations.** Needs MSVC — the shader compiler hard-fails on
`MSVC_TOOLSET_VERSION < 142`. Visual Studio 2022 Community is enough; a prebuilt `FidelityFX_SC.exe`
already sits in `sdk/tools/binary_store`, so the compiler itself does not have to be built.

```
cmake -S sdk -B sdk/build -G "Visual Studio 17 2022" -A x64 \
      -DFFX_API_CUSTOM=OFF -DFFX_API_VK=OFF -DFFX_API_DX12=OFF -DFFX_ALL=OFF \
      -DFFX_API_DX11=ON -DFFX_FSR=ON -DFFX_FSR1=OFF -DFFX_FSR2=OFF -DFFX_FI=OFF -DFFX_OF=OFF \
      -DFFX_AUTO_COMPILE_SHADERS=1 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build sdk/build --config Release --parallel 4
```

Produces 40 permutation index headers and 200 blob headers under
`sdk/build/src/backends/shaders/dx11`.

**2. Compile with GCC**, since an MSVC static library cannot be linked into this engine. Seven
translation units:

```
src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
src/backends/dx11/ffx_dx11.cpp
src/backends/dx11/DXBCChecksum.c
src/backends/dx11/md5.c
src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
src/shared/ffx_assert.cpp
src/shared/ffx_object_management.cpp
```

Include paths: `include`, `include/FidelityFX/host`, `src/components`, `src/shared`,
`src/backends/shared`, `build/src/backends/shaders/dx11`. Then `ar rcs`.

**Compile with `-DFFX_FSR -DFFX_FSR3`. Both.** The blob tables are behind `#if defined(FFX_FSR)`, but
the FSR 3 cases inside the dispatcher sit behind a further, separate `#if defined(FFX_FSR3)`. Build
with only the first and every FSR 3 lookup falls through to `default`, which returns **an empty blob
together with FFX_OK** - so nothing reports a problem until `CreateComputeShader` rejects the empty
bytecode, and the caller sees a bare `FFX_ERROR_BACKEND_API_ERROR` with no clue as to the cause.
`FFX_FSR` also pulls in the FSR 1, FSR 2, frame-interpolation and optical-flow accessors, so those four
translation units have to be compiled too even though we only use the upscaler.

Eight translation units in total — `src/backends/shared/ffx_shader_blobs.cpp` is easy to miss and is
what defines `ffxGetPermutationBlobByIndex`.

## The three source fixes needed

All in `src/backends/dx11/ffx_dx11.cpp`, all must be reapplied after any update of the upstream port.
The third one is not a GCC quirk but a genuine bug in the port:

3. **`ffxGetResourceDX11_Fsr31` was defined without the `const` its declaration carries.** The header
   declares it inside `extern "C"`, so the mismatched definition becomes an ordinary C++ overload and
   the C symbol every caller links against is never defined. Invisible until link time, where it looks
   like a missing library rather than a signature mismatch.

4. **`fp16Supported` was derived from `AllOtherShaderStagesMinPrecision`.** That flag reports D3D11
   *minimum precision* support - a hint to the driver - not the native half type of Shader Model 6.2.
   The DX11 shaders are built with FXC at SM 5.0, which cannot compile the fp16 variants at all, so
   their permutation tables are empty. On any card reporting min-precision (most of them) the runtime
   asked for a permutation with `ALLOW_FP16` set and got an empty blob. Forced to `false`.

The two GCC ones:

1. **`WKPDID_D3DDebugObjectNameW` is undeclared.** MinGW's `dxguid` ships only the ANSI debug-name GUID.
   Declared locally under `#if defined(__MINGW32__)` with the documented value. Used solely to label
   objects for graphics debuggers.
2. **`std::wstring_convert` no longer exists.** Deprecated in C++17 and removed from current libstdc++.
   Replaced with a two-line struct around `MultiByteToWideChar`; the strings are ASCII shader binding
   names, so the substitution is exact.

## Traps met on the way, so they are not met twice

- **Do not build under `%TEMP%`.** MSVC warns (MSB8029) and the shader generation step fails with no
  usable diagnostic. Moving the tree to `D:` fixed it outright.
- **GCC must have `msys64/mingw64/bin` on PATH**, not merely be invoked by absolute path. Without it
  `g++.exe` cannot load its own DLLs and exits 1 printing absolutely nothing — no error, no object
  file. Easily mistaken for a silent compile failure.
- **CMake from msys rejects `cmake_minimum_required` below 3.5**; pass
  `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`. Pass it from bash, not PowerShell — PowerShell drops the
  fractional part and CMake then rejects the value "3".
- Building the shader compiler from source additionally wants ATL (`atlcomcli.h`), which is a separate
  Visual Studio component. Only `CComPtr` is used, so an 80-line stand-in works — but the prebuilt
  binary in `binary_store` makes this unnecessary.
