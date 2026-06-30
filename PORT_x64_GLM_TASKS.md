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

## PHASE A RESULT (2026-06-28) — BOOT REACHED, HANG ISOLATED

The x64 build **boots and runs**. Using `-force_flushlog` (mandatory — the log is
buffered; without it the last visible line lies about where execution stopped) we
traced the entire startup. The following all WORK on x64 R4 DX11:

- Filesystem, GPU (RTX 5070 Ti), OpenAL sound — full init.
- **Entire R4 DX11 renderer**: `CRender::create()`, CRenderTarget, FluidManager,
  ImGui — `CRenderDevice::Create()` returns cleanly.
- "Version conflict in shader" (9x) and "Failed to find compiled constant buffer"
  (20x) are **benign noise**, NOT the hang. (The constant-buffer error is the
  `~dx11ConstantBuffer` dtor reclaiming all R__NUM_CONTEXTS contexts though the
  buffer lives in one — dx11ConstantBuffer.cpp:10-13.)

**The hang is in the Lua script engine init.** Exact call chain:

```
x_ray.cpp  CApplication ctor
  -> xrGame.cpp:92         create_persistent()
     -> object_factory()   (object_factory_inline.h)  -> g_object_factory->init()
        -> object_factory.cpp:26  register_script_classes()
           -> ai()  -> CAI_Space::init()  (ai_space.cpp:53-55)
              GEnv.ScriptEngine = xr_new<CScriptEngine>(false,true);
              RestartScriptEngine();        <-- HANG IS BELOW HERE
                -> CAI_Space::SetupScriptEngine() (ai_space.cpp:143)
                   -> CScriptEngine::init(node::export_all, true)  (script_engine.cpp:785)
                      -> exporter(lua())  ==  script_export::node::export_all()
                         -> node::sort()  (ScriptExporter.cpp)   topological sort of
                            ALL luabind binding nodes by dependency, then runs each
                            node->m_export_func(luaState).
```

`node::sort()` is the prime suspect: it does a recursive DFS over every `SCRIPT_EXPORT`
node using static-init-order-dependent linked list + `m_deps_getter()`. Under GCC/x64
the static init order across DLLs differs from MSVC, so a dependency can be **null** or
form a **cycle** -> infinite recursion / hang.

### Instrumentation already in place (built, NOT yet deployed/run)

A diagnostic harness was added specifically to name the hang point:

- `ScriptExporter.hpp` — added `m_name` (`__FILE__:__LINE__`) to every export node via
  the `SCRIPT_EXPORT` macro.
- `ScriptExporter.cpp` — `node::sort()` now logs map build, **cycle detection**
  (`LOOP DETECTED` if >500 nodes / runaway DFS), and `export_all()` logs each node's
  **name before and after** its `export_func`.
- `script_engine.cpp` `CScriptEngine::init()` — step traces (reinit, luabind::open,
  setup_callbacks, exporter, lua libs, `_G` load).
- `ai_space.cpp` — step traces through `init()` and `SetupScriptEngine()`.

> NOTE FOR ARCHITECTS: `bin/.../xrScriptEngine.dll` (built 27.06) is NEWER than the
> copy deployed in `D:\Dead Air Test` (23.06). **These traces have never actually run.**
> Deploy the fresh `xrScriptEngine.dll` + rebuilt `xrGame.dll`, launch with
> `-force_flushlog`, and read `appdata\logs\openxray_*.log`. That output decides the
> Phase B-0 task below.

---

## FORWARD PLAN FOR GLM (do these in order)

### B-0 — RESOLVED (2026-06-28): script engine init works

The harness was run. Result: **the "hang in RestartScriptEngine" was an ABI-mismatch
artifact, not a real hang.** Deploying only `xrScriptEngine.dll` after changing the
exported `node::node` ctor signature (added a 3rd param) made other DLLs import the old
2-arg symbol -> `0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND` before `main` (confirmed via gdb).

After a FULL rebuild + deploy of ALL dlls, the script engine initializes cleanly:
`export_all: DONE, exported 248 nodes` -> `ScriptEngine::init: DONE` ->
`create_persistent` -> `OnAppStart`. No binding cycle, no node hang. Log 174 -> 2327 lines.

> HARD RULE learned: any change to an exported symbol's signature (`XRSCRIPTENGINE_API`,
> `XRCORE_API`, etc.) requires a FULL rebuild and deploy of ALL 21 dlls — never a partial
> deploy, or you get entrypoint-not-found at load.

The boot now reaches a real, named **FATAL ERROR**. First concrete Phase B task below.

### B-1 — CEnvAmbient empty section assert (DO THIS FIRST)

**Symptom (from log):**
```
FATAL ERROR
Expression : !m_sound_channels.empty() || !m_effects.empty()
Function   : load
File       : src/xrEngine/Environment_misc.cpp
Line       : 246
```

**Cause:** Dead Air defines a weather `env_ambient` section that has neither sound
channels nor effects. Vanilla OpenXRay `CEnvAmbient::load()` hard-asserts that at least
one is present (line 246). DA expects the engine to tolerate an empty ambient.

**Task:** In `src/xrEngine/Environment_misc.cpp`, function `CEnvAmbient::load()`, replace
the hard `R_ASSERT(!m_sound_channels.empty() || !m_effects.empty());` at line 246 with a
tolerant warning that names the offending section and continues:

```cpp
if (m_sound_channels.empty() && m_effects.empty())
    Msg("! [DA_PORT] CEnvAmbient '%s': no sound channels and no effects, skipping", sect);
```

(`sect` is the section name in scope in `load()`. If `sect` is not the right identifier in
this overload, use `m_load_section.c_str()`.) Do NOT touch any DA data files. Output only
the changed block with line context.

**Acceptance:** rebuild xrEngine, redeploy, launch with `-force_flushlog`; the FATAL is
gone, the log shows at most a `[DA_PORT] CEnvAmbient ... skipping` warning, and boot
advances past environment load (further into `OnAppStart` / main menu).

### B (continuing) — next FATALs surface one at a time

After B-1, relaunch. The architects capture the next `FATAL ERROR` / `[LUA] ... nil
value` and hand GLM the file:line. Keep applying the same shape of fix:
- engine assert too strict for DA data -> relax to a named warning (like B-1);
- missing Lua method -> 3-touch binding (see Phase B main, below);
- MSVC-ism in a touched function -> rule-3 rewrite.

---

## AUTONOMOUS PLAYBOOK FOR GLM (work solo; architects verify each batch)

You (GLM) own the FIX step end-to-end. The architects own only LAUNCH + LOG capture
(they run the game and paste you the next error). For every error they hand you, work the
loop below ALONE — do not wait for hand-holding between sub-steps.

### The per-error loop

1. **Read the error.** You are given a block like:
   ```
   FATAL ERROR
   Expression : <cond>
   Function   : <fn>
   File       : src/.../X.cpp
   Line       : <n>
   Description : assertion failed | <lua error> | <message>
   ```
   or a Lua error `attempt to call method 'NAME' (a nil value)` plus the script snippet.

2. **Open the named file:line and read ~40 lines around it.** Understand WHY it fires
   under DA data / x64, not just what. Classify into one of the four buckets in the table
   below.

3. **Apply the minimal fix** for that bucket (table). Touch only the named function.

4. **Self-check before handing back** (you cannot launch the game, but you CAN reason):
   - Does the change compile in your head? (types, namespaces, headers already included?)
   - Did you preserve behavior for the NON-DA / normal case? (e.g. assert still meaningful
     when data IS valid; binding returns the value the script expects.)
   - Did you avoid the forbidden actions (no silent stub, no save/net packet change, no DA
     data edit, no exported-symbol signature change)?

5. **Hand back** ONLY the changed block(s) with `file:line` context + one sentence on why.
   The architects rebuild the single affected target, redeploy, relaunch, and send you the
   NEXT error. Repeat.

### Fix-bucket table

| Bucket | Signature in the log | Fix shape |
|---|---|---|
| **1. Over-strict engine assert on DA data** | `R_ASSERT...assertion failed` in an engine `load()`/parse fn, where DA data legitimately differs (empty section, extra field, out-of-range enum) | Replace hard `R_ASSERT` with a tolerant path: `if (bad) { Msg("! [DA_PORT] <ctx> '%s': <what>, skipping", name); <safe continue>; }`. Keep the assert's intent as a warning. Name the offending data. |
| **2. Missing DA->engine Lua binding** | `attempt to call method 'X' (a nil value)` | 3-touch: declare in `script_game_object.h`; implement in a `script_game_object_*.cpp`; register in `script_game_object_script3.cpp` `.def("X", &CScriptGameObject::Cpp)`. Return what the DA script reads. Copy `get_addon_flags` exactly. |
| **3. Missing free function / class binding** | `attempt to call global 'Y'` or `'Z' not found` | Find the vanilla export node for that subsystem (search `SCRIPT_EXPORT` near related code), add the missing `.def`/`function`. If the engine capability is truly absent, `[DA_PORT_STUB]` Msg + safe default — NEVER for save/net/load. |
| **4. MSVC-ism in a touched function** | compile error OR wrong runtime value from a fn you just edited | Apply rule 3 of the system prompt: SEH -> `#ifdef XR_COMPILER_MSVC` + fallback; in-class explicit template spec -> `if constexpr`; `DWORD*` in exported sig -> `u32*`. |

### HARD limits (never cross — these cause silent save corruption or load failures)

- **Never** change the signature of an exported symbol (`*_API`) without telling the
  architects it needs a FULL rebuild. Prefer fixes that don't touch exported signatures.
- **Never** stub or approximate a `net_export` / `net_import` / `load` / `save` /
  `STsaveGameState` byte layout. If unsure of the exact field order/size, STOP and ask.
- **Never** edit Dead Air data files (`gamedata\...`). The fix is always engine-side.
- **Never** add a stub without the `Msg("! [DA_PORT_STUB] ... %s", __FUNCTION__)` line.

### When to STOP and ask the architects (do not guess)

- The error is in a save/load/network packet path.
- The fix would require changing an exported symbol's signature.
- The same error recurs after your fix (your fix was wrong — say so, don't pile on).
- The crash has NO file:line (raw access violation) — ask for a gdb backtrace instead.

### What "done for now" looks like

A level loads and is playable with no FATAL and no unresolved `[LUA]` error in the log.
Then: strip all `[DA_PORT]` trace `Msg` scaffolding (keep behavioral fixes), and the
architects do a final full rebuild + playtest.

---

### Earlier decision tree (kept for reference — sort/export now known-good)

**Case A — log shows `! [DA_PORT] sort: LOOP DETECTED ...` or DFS count runs away**
There is a dependency cycle (or self-dependency) among the luabind export nodes under
GCC static-init order. Task:
1. In `ScriptExporter.cpp` `node::sort()`, make the DFS cycle-tolerant: a node already in
   `state::visiting` must be SKIPPED (treat back-edge as already-handled), not recursed
   into. Replace the recursion guard so revisiting a `visiting` node returns immediately
   instead of looping. Keep the existing `not_visited -> visiting -> visited` coloring;
   only the back-edge handling changes.
2. Do NOT change export order semantics for the acyclic case.
3. Acceptance: `sort:` logs reach `dfs done` and `export_all` starts iterating nodes.

**Case B — log stops after `export_all: node[N] before export_func name=FILE:LINE`**
A specific binding's `script_register` hangs/crashes. The `name=FILE:LINE` IS the culprit
`SCRIPT_EXPORT` (e.g. `script_game_object_script3.cpp:NNN`). Task:
1. Open that file:line, read the `SCRIPT_EXPORT(...)` / `script_register` body.
2. The usual GCC/x64 fault is a `.def(...)` binding a function whose signature uses a
   MSVC-ism (DWORD vs u32, an explicit template specialization, or a smart_cast that
   resolved wrong). Apply the rule-3 fixes from the system prompt. Output only the changed
   `.def`/function block.
3. Acceptance: `export_all: node[N] after export_func` appears; trace advances to the next
   node.

**Case C — log reaches `export_all: DONE` then stops in `process_file_if_exists(_G)` or
`LoadCommonScripts`**
Bindings are fine; a DA Lua script hangs/errors at load. Architects supply the Lua error
line; GLM switches to the Phase B binding loop below.

### B (main) — DA->engine Lua API surface (iterative, 3-touch pattern)

Once script init completes, the game will hit `attempt to call method 'X' (a nil value)`
errors one at a time as menus/levels load. For each, GLM implements the missing binding:

1. **Declare** in `src/xrGame/script_game_object.h`.
2. **Implement** in a `script_game_object_*.cpp` (match the return the DA script expects —
   architects supply the calling snippet).
3. **Register** in `src/xrGame/script_game_object_script3.cpp` with
   `.def("lua_name", &CScriptGameObject::CppName)`.

Reference already in tree: `get_addon_flags` (script_game_object.h + ..._inventory_owner.cpp
+ ..._script3.cpp). Copy that exact style. If the engine capability truly doesn't exist,
emit `Msg("! [DA_PORT_STUB] ...")` and return a safe default — but NEVER stub a
net/save/load packet function (rule 2).

### C / D — as they surface (lower priority)

- **C2** (`dx11HWCaps.cpp` GPU-count default = 1 on GCC) — verify when renderer is exercised.
- **D1** `MAX_BLENDED 16->32` — already applied (KinematicAnimatedDefs.h:6). Verify no other
  fixed-16 array assumes the literal.
- **C1** stack-overflow probe, **C3** Ansel, **C4** crash-report richness — polish, last.

### Cleanup before any "release" build

All `[DA_PORT]` trace `Msg(...)+FlushLog()` lines (in ScriptExporter.cpp, script_engine.cpp,
ai_space.cpp, r2.cpp, Device_create.cpp, x_ray.cpp, xrGame.cpp) are debug scaffolding —
strip them once boot is stable. The behavioral fixes (smart_cast if-constexpr, MAX_BLENDED,
GCC stubs, the binding fixes) STAY.

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
