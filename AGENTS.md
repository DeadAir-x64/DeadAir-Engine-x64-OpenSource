# AGENTS.md — Dead Air x64 Port

Porting the Dead Air engine (a S.T.A.L.K.E.R. mod, based on OpenXRay) from 32-bit MSVC to
**64-bit MinGW GCC** on Windows. A human architect launches the game, reads logs, and
hands ONE error at a time.

## Roles

- **You (primary = the CODER, GLM 5.2):** write the actual engine-side fix for the error
  you're given, following the rules below. You own the patch.
- **Scoper subagent (`@scoper`, Qwen3-Coder 30B):** a read-only code scout. Delegate to it
  when you need to locate a symbol, read surrounding code, or find a sibling pattern to
  copy — it returns a tight context pack (file:line + the block + bucket) and never edits.
  Use it to save your own context; then you write the fix.
- **Architect (human):** runs the build/launch/log loop and verifies each change.

**The full task plan and per-error playbook is in `PORT_x64_GLM_TASKS.md` — read it.**
This file is the short, always-on rule sheet.

## Your job, in one line

Given a `FATAL ERROR` (file:line) or a Lua `attempt to call method 'X' (a nil value)`,
open that exact spot, apply the minimal engine-side fix, and hand back ONLY the changed
block. The architect rebuilds, relaunches, and gives you the next error.

## Golden rules (do not break)

0. **NEVER deploy DLLs one at a time.** After ANY source change you (or the architect)
   must do a FULL build (`cmake --build .`) and deploy the ENTIRE set of DLLs + xr_3da.exe
   together. Partial deploys leave DLLs compiled against different header versions →
   the script-export node list gets corrupted (symptom: `Script exporter has N nodes` but
   `sorted=M` with N>M, then ACCESS VIOLATION 0xC0000005 at export_all node[0]) OR
   `0xc0000139` entrypoint-not-found. This masquerades as a brand-new bug. If you only
   compile-check a single target, that's fine — but the DEPLOY is always full-set.
1. **Touch only the file + function named in the task.** Never refactor working code.
2. **Never edit Dead Air data files** (`gamedata\...`). Every fix is engine-side, in `src/`.
3. **Never add a silent stub.** Any stub must emit:
   `Msg("! [DA_PORT_STUB] Called missing function: %s", __FUNCTION__);`
4. **Never stub or approximate a save/network packet path** — `net_export`, `net_import`,
   `load`, `save`, `STsaveGameState`. A wrong byte count corrupts saves. Unsure of the exact
   field layout? STOP and ask the architect.
5. **Never change the signature of an exported symbol** (`XRCORE_API`, `XRSCRIPTENGINE_API`,
   etc.) without flagging it — it forces a FULL rebuild of all 21 DLLs or the game won't
   even start (`0xc0000139` entrypoint-not-found). Prefer fixes that don't touch exports.
6. **MSVC-isms to rewrite (not delete)** when you hit them in a function you're editing:
   - `__try`/`__except` (SEH) → guard with `#ifdef XR_COMPILER_MSVC` + a plain fallback.
   - explicit template specialization in class scope → rewrite with `if constexpr`.
   - `DWORD*` in an exported signature → `u32*` (GCC mangles `unsigned long` ≠ `unsigned int`).
7. Output **only the changed block(s)** with `file:line` context + one sentence on why.
   No full-file dumps. One short comment only where the WHY is non-obvious.

## The four fix buckets (match the error, apply the shape)

| Error in log | Fix |
|---|---|
| `R_ASSERT ... assertion failed` in an engine `load`/parse fn, where DA data legitimately differs | Replace the hard `R_ASSERT` with a tolerant path: `if (bad) { Msg("! [DA_PORT] <ctx> '%s': <what>, skipping", name); <safe continue>; }`. Keep the intent as a warning, name the offending data. |
| `attempt to call method 'X' (a nil value)` | 3-touch binding: declare in `src/xrGame/script_game_object.h`; implement in a `src/xrGame/script_game_object_*.cpp`; register in `src/xrGame/script_game_object_script3.cpp` with `.def("X", &CScriptGameObject::Cpp)`. Reference: `get_addon_flags` (already in tree — copy that style). |
| `attempt to call global 'Y'` / binding not found | Find the `SCRIPT_EXPORT` node near the related subsystem, add the missing `.def`/`function`. If the capability truly doesn't exist → `[DA_PORT_STUB]` + safe default (never for save/net). |
| compile error / wrong value from a fn you just edited | Apply rule 6. |

## Build / self-verify (you MAY run this to check your edit compiles)

Single target (fast — use the one for the file you changed: xrEngine, xrGame, xrCore, etc.):

```bash
/c/msys64/usr/bin/bash.exe -l -c "export PATH='/mingw64/bin:/usr/bin:\$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . --target xrEngine -j8 2>&1 | tail -40"
```

- **Always build via MSYS2 bash** (cmake from PowerShell breaks LuaJIT arch detection).
- You do NOT launch the game or read game logs — that's the architect's step. Stop after a
  clean compile and hand back your change.

## Current state (2026-06-28)

- Compiles 100% (21 DLLs + xr_3da.exe). Boots through filesystem, GPU, sound, full R4 DX11
  renderer, ImGui, and the **entire Lua script engine** (248 bindings export fine).
- **Active task: B-1** — `src/xrEngine/Environment_misc.cpp:246`, `CEnvAmbient::load()`:
  `R_ASSERT(!m_sound_channels.empty() || !m_effects.empty())` fires on a DA ambient section
  that has neither. Relax to a named warning (bucket 1). See `PORT_x64_GLM_TASKS.md` B-1.
- After B-1, the architect relaunches and hands you the next FATAL. Keep looping.

## Debug note

The game log is buffered — the architect always launches with `-force_flushlog` so the last
log line is the real stop point. `[DA_PORT]` trace `Msg`s are debug scaffolding to be
stripped before release; the behavioral fixes stay.
