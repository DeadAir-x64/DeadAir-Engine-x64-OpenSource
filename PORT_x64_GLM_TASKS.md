# Dead Air x64 Port — Task Plan for GLM 5.2

**Role split:** Claude + user = architects (decisions, test loop, build, review). GLM 5.2 = hands (mechanical implementation of the marked tasks below).

**Toolchain:** MinGW GCC 16.1.0 (MSYS2), Windows x64, CMake + Ninja.
**Source root:** `D:\Dead Air\xray-16` (`D:/Dead Air/xray-16`)
**Build dir:** `D:\Dead Air\xray-16\build_mingw`
**Base engine:** OpenXRay (branch `dev`).

---

## VERIFIED CURRENT STATE (2026-06-23)

The **compilation phase is COMPLETE**. A full `cmake --build .` returns exit code 0.
All 21 DLLs + `xr_3da.exe` build and link clean under x64 MinGW:

```
xr_3da.exe (1.46 MB), xrGame.dll (76.8 MB), xrRenderPC_R4.dll (5.62 MB),
xrRender_GL.dll, xrUICore.dll, xrPhysics.dll, xrAICore.dll, xrParticles.dll,
xrEngine.dll, xrGameSpy.dll, xrSound.dll, xrScriptEngine.dll, xrCDB.dll,
xrOPCODE.dll, xrNetServer.dll, xrMaterialSystem.dll, xrCore.dll, xrAPI.dll,
xrODE.dll, xrLuabind.dll, xrLuaJIT.dll
```

**What this means:** there are no more compile errors to fix. The remaining work is
**runtime** — boot the x64 build, read the crash log, fix the failing site, rebuild, repeat.

32 source files were modified to reach this point (see `git diff --stat HEAD`). The
risky/behavioral ones are catalogued below as tasks.

---

## DIVISION OF LABOR (read this first)

GLM **cannot** drive the boot-test loop alone — that requires launching the game and
reading logs iteratively (human-in-the-loop). The architects do that.

GLM **can** do, given a specific error + the relevant files:
- Implement a missing DA→engine Lua binding (Phase B — the bulk of remaining work).
- Harden a specific marked GCC fallback stub (Phase C).
- Apply a known concrete change (Phase D).

**Loop:** architects boot → capture crash/log → hand GLM the exact error + file paths →
GLM returns the fix → architects apply, rebuild, re-boot.

---

## GLM SYSTEM PROMPT (paste verbatim)

```
You are porting Dead Air (a S.T.A.L.K.E.R. mod) engine code from 32-bit MSVC to
64-bit MinGW GCC on Windows. Base engine: OpenXRay (github.com/OpenXRay/xray-16).
Compiler: GCC 16.1.0, MinGW64, target x86_64-w64-mingw32. Build: CMake + Ninja, MSYS2.

The whole engine already compiles and links under x64. Do NOT refactor working code.
Touch only the file and function named in the task.

MANDATORY RULES:
1. Never add a silent stub. Every stub MUST emit, at the point it is hit:
   Msg("! [DA_PORT_STUB] Called missing function: %s", __FUNCTION__);
2. Any function that reads or writes a save/network packet (net_export / net_import /
   load / save / STsaveGameState) must be fully implemented, never stubbed. A wrong byte
   count corrupts saves or crashes on load. If you are unsure of the exact field layout,
   STOP and ask — do not guess.
3. MSVC-only constructs to replace, not delete:
   - __try/__except (SEH): guard with #ifdef XR_COMPILER_MSVC, provide a plain fallback.
   - explicit template specialization in class scope: rewrite with `if constexpr`.
   - DWORD* in exported signatures: GCC mangles `unsigned long` != `unsigned int`; use u32*.
4. New Lua bindings: declare the method in script_game_object.h, implement in a
   script_game_object_*.cpp file, and register it in script_game_object_script3.cpp with
   .def("lua_name", &CScriptGameObject::CppName). Match the exact lua name the DA script calls.
5. Output ONLY changed blocks, each with file path + line-number context. No full-file dumps.
6. One short comment only where the WHY is non-obvious. Match surrounding code style.
```

---

## REFERENCE: the pattern already in the tree (copy this style)

DA scripts call `obj:get_addon_flags()`, which vanilla OXR lacks. It was added as:

- `src/xrGame/script_game_object.h:405` — `int GetAddonFlags(); // Dead Air compat stub`
- `src/xrGame/script_game_object_inventory_owner.cpp` (end of file) — implementation
- `src/xrGame/script_game_object_script3.cpp:258` — `.def("get_addon_flags", &CScriptGameObject::GetAddonFlags)`

Every Phase-B task follows this exact 3-touch pattern.

---

## TASK BATCHES

### Phase A — Boot the x64 build (ARCHITECTS, not GLM)

- **A1.** Deploy the 21 DLLs + `xr_3da.exe` from `xray-16/bin` into a **COPY** of a Dead Air
  install — NEVER the user's working 32-bit install. Candidate: `D:\Dead Air Test`
  (back up its 32-bit binaries first).
- **A2.** Launch `xr_3da.exe`, reach as far as it goes (window → main menu → load level).
- **A3.** Capture `appdata\logs\xray_*.log`. The last `FATAL ERROR` / failing function is
  the input to the next GLM task. Repeat after every fix.

> Output of A3 is what feeds Phase B. Until we boot once, Phase B tasks are unknown.

### Phase B — DA→engine Lua API surface (GLM, iterative)

Each missing binding surfaces as a Lua error at script load or call time, e.g.
`attempt to call method 'X' (a nil value)` or `function 'Y' not found`.

- **B-template.** Given a missing method `obj:lua_name(args)`:
  1. Find what DA's script expects it to return (architects supply the script snippet).
  2. Implement via the 3-touch pattern above.
  3. If the underlying engine capability genuinely does not exist, emit the
     `[DA_PORT_STUB]` Msg and return a safe default (0 / false / nullptr).
  - Acceptance: game gets past that Lua error; log shows no new error from this binding.

> Concrete B1, B2, … get filled in from Phase-A logs. Known seed: `get_addon_flags` (DONE).

### Phase C — Harden GCC fallback stubs (GLM, each self-contained)

- **C1 — `src/xrCore/string_concatenations.cpp` `check_stack_overflow()`**
  GCC branch is a no-op (`(void)stack_increment;`). Script recursion can overflow the
  stack with no guard → hard crash instead of caught error.
  Task: implement a GCC-safe probe (e.g. compare current SP against
  `NtCurrentTeb()->NtTib.StackLimit`, or a guard-page touch) that mirrors the MSVC
  `_resetstkoflw` intent without SEH. If not feasible, leave the no-op but add a one-line
  comment explaining why. LOW priority — does not block boot.

- **C2 — `src/Layers/xrRenderDX11/dx11HWCaps.cpp`**
  `GetNVGpuNum()` / AMD AGS GPU-count detection is compiled out on GCC
  (`&& defined(XR_COMPILER_MSVC)`). Verify the code path that consumes the GPU count has a
  sane default (single GPU) on GCC and does not divide-by-zero / read uninitialized.
  Task: trace the consumer; ensure default = 1 GPU. MEDIUM — could affect renderer init.

- **C3 — `src/xrGame/AnselManager.cpp:226`**
  Already a clean `[DA_PORT_STUB]` (NVIDIA Ansel screenshots). Cosmetic. LEAVE AS-IS unless
  a DA script hard-depends on Ansel. LOWEST priority.

- **C4 — `src/xrCore/xrDebug.cpp`**
  BugTrap, ReportFault, and `DXGetErrorDescription` are disabled on GCC, so crash reports
  are thinner. The signal-handler minidump path still works. Task (optional): confirm a
  usable minidump/stack is still produced on an x64 crash — we need it for the Phase-A loop.

### Phase D — Known concrete change (GLM, one-shot)

- **D1 — Raise `MAX_BLENDED`** in `src/Layers/xrRender/KinematicAnimatedDefs.h:6`
  from `16` to `32` (fixes "Too many blended motions" crash with heavy anim mods / WE).
  IMPORTANT: this is a **one-line change**. The earlier note about extending a `switch` in
  `AnimationKeyCalculate.h` is WRONG — `MixInterlerp` special-cases only b_count 0/1/2 and
  routes everything else through the `default` loop (lines 238-251), which already handles
  any count up to MAX_BLENDED. No switch edit needed.
  Side effects: `MAX_BLENDED_POOL` and stack arrays (`CKey BK[][MAX_BLENDED]`,
  `ConsistantKey S[MAX_BLENDED]`) scale automatically. Verify no fixed 16-sized array
  elsewhere assumes the literal. NOT an x64 issue (same constant in 32/64-bit) — safe anytime.

---

## BUILD & TEST LOOP (architects)

Build — ALWAYS via MSYS2 bash (cmake from PowerShell breaks LuaJIT arch detection):

```powershell
& "C:\msys64\usr\bin\bash.exe" -l -c "export PATH='/mingw64/bin:/usr/bin:`$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . -j8 2>&1 | tail -60"
```

Single target (faster while iterating xrGame):

```powershell
& "C:\msys64\usr\bin\bash.exe" -l -c "export PATH='/mingw64/bin:/usr/bin:`$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . --target xrGame -j8 2>&1 | tail -60"
```

Then: copy changed DLL(s) → test install → launch → read `appdata\logs\xray_*.log` → next task.

---

## PRIORITY ORDER

1. Phase A (boot once — unblocks everything).
2. Phase B (iterate on each Lua/runtime error until a level loads and is playable).
3. Phase C2 (renderer GPU default), then D1 (anim crash) as they surface.
4. Phase C1 / C3 / C4 — polish, last.
