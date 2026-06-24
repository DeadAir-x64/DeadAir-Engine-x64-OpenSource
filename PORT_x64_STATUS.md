# Dead Air 0.98b → OpenXRay x64 Port Status

**Дата:** 2026-06-21  
**Тулчейн:** MinGW GCC 16.1.0 (MSYS2), Windows x64  
**Сборочная директория:** `D:\Dead Air\xray-16\build_mingw`

---

## Цель

Собрать все DLL движка Dead Air (xrGame, xrEngine, xrRender_R4, xrRender_GL, xr_3da и др.) под x64 через MinGW/GCC вместо MSVC. Цель — снять 4GB ограничение 32-битного процесса.

---

## Ключевые особенности MinGW vs MSVC

| Проблема | Причина | Решение |
|---|---|---|
| `XR_PLATFORM_WINDOWS` defined на MinGW | MinGW тоже определяет `_WIN32` | Гварды с `&& defined(XR_COMPILER_MSVC)` где нужен только MSVC |
| `__try/__except` не работает | GCC не поддерживает SEH | `#ifdef XR_COMPILER_MSVC` + fallback WinMain |
| `DWORD` vs `u32` разные типы | `unsigned long` ≠ `unsigned int` в mangling | Заменить `DWORD*` на `u32*` в сигнатурах |
| `ultoa` отсутствует | MSVC-only CRT | `#define ultoa _ultoa` в Compiler.inl |
| ANSEL delay-load | MinGW не имеет `/DELAYLOAD` | Весь тело AnselManager завернуть в `#ifdef XR_COMPILER_MSVC` |
| stub DirectXMath.h | MSYS2 кладёт заглушку в `include/` | Добавить `include/directxmath/` в приоритете в include paths |
| LTO маскирует источник ошибок | linker пишет `ltrans.o:<artificial>` | Искать символ в исходниках, не в error message |

---

> **ОБНОВЛЕНО 2026-06-23:** компиляция ЗАВЕРШЕНА. Полный `cmake --build .` =
> exit 0, все 21 DLL + `xr_3da.exe` линкуются под x64. Блокер `smart_cast_impl1.h`
> решён (через `if constexpr`). Дальнейшая работа — рантайм.
> Актуальный план задач (для GLM 5.2): **`PORT_x64_GLM_TASKS.md`**.

## Что собрано (зелёные DLL)

| Цель | Статус | Размер |
|---|---|---|
| `xrGame.dll` | ✅ BUILT | 76.8 MB |
| `xrEngine.dll` | ✅ BUILT | 3.57 MB |
| `xr_3da.exe` | ✅ BUILT | 1.46 MB |
| `xrRender_GL.dll` | ✅ BUILT | 5.85 MB |
| `xrRenderPC_R4.dll` | ✅ BUILT | 5.62 MB |
| (+ ещё 16 DLL ядра) | ✅ BUILT | — |

---

## Изменённые файлы

### `src/xrGame/CMakeLists.txt`
- Раскомментированы `AnselManager.cpp/.h`
- Добавлен `GameSpy-oxr` в link (фикс MD5Digest)
- Добавлен `shlwapi` для Win32 (фикс StrCmpLogicalW)

### `src/xrEngine/CMakeLists.txt`
- `xrImGui` перенесён из PRIVATE в PUBLIC (propagation include dirs)
- Раскомментированы `tntQAVI.cpp/.h`
- Добавлены `winmm`, `vfw32` для Win32

### `src/Layers/xrRenderPC_R4/CMakeLists.txt`
- Добавлен `${CMAKE_CURRENT_SOURCE_DIR}` в includes (фикс `stdafx.h not found` для `../xrRender/` файлов)
- Добавлен `${CMAKE_SOURCE_DIR}/src/Layers/xrRender_R2` в includes
- Добавлен `${CMAKE_SOURCE_DIR}/Externals` в includes (фикс `renderdoc/renderdoc_app.h`)
- Добавлен `C:/msys64/mingw64/include/directxmath` для MinGW (фикс XMVECTOR)
- Добавлен `RENDER_NAMESPACE=render_r4` в definitions (фикс friend-доступа к protected/private через дружественные классы)
- Добавлен `DirectXTex` в link
- **Добавлены все 188 недостающих .cpp файлов** из `../xrRender/`, `../xrRender_R2/`, `../xrRenderDX11/`

### `src/Common/Compiler.inl`
```cpp
#ifdef _WIN32
#define xr_unlink _unlink
#define ultoa _ultoa   // ← добавлено
```

### `src/xrNetServer/NET_Server.h`
- `GetClientAddress`: `DWORD*` → `u32*` (фикс mangling mismatch)

### `src/xrGame/xrServer.cpp` + `console_commands_mp.cpp`
- Локальные `DWORD dwPort` → `u32 dwPort`

### `src/xrGame/AnselManager.cpp`
- `ProcessCam()` и `Init()` завёрнуты в `#ifdef XR_COMPILER_MSVC` с заглушкой для GCC

### `src/xr_3da/entry_point.cpp`
- `WinMain` с `__try/__except` завёрнут в `#if defined(XR_PLATFORM_WINDOWS) && defined(XR_COMPILER_MSVC)`
- Добавлен обычный `WinMain` для MinGW (без SEH)

---

## Текущий блокер (xrRenderPC_R4)

**Файл:** `src/xrServerEntities/smart_cast_impl1.h:533`

```
error: explicit specialization in non-namespace scope 'struct SmartDynamicCast::CHelper2<T2>'
```

MSVC разрешает explicit template specialization внутри класса как расширение. GCC (стандарт C++) — нет. Структура `SmartDynamicCast` содержит вложенный `template <>` специализацию внутри своего тела.

**Варианты фикса:**
1. Вынести специализацию из класса в namespace scope (хирургично, безопасно)
2. Добавить `-fpermissive` в GCC флаги (грязно, может скрыть другие ошибки)
3. Обернуть в `#ifdef XR_COMPILER_MSVC` и переписать для GCC

---

## Следующие шаги (в порядке приоритета)

1. **Исправить `smart_cast_impl1.h`** — вынести template specialization в namespace scope
2. Собрать `xrRenderPC_R4` до успешного линка
3. Проверить `xrRender_GL` (должен линковаться после пересборки xrEngine с tntQAVI)
4. Запустить `xr_3da.exe` и проверить старт до главного меню
5. **Долгосрочно:** поднять `MAX_BLENDED` с 16 до 32+ в `KinematicAnimatedDefs.h:6` + расширить switch в `AnimationKeyCalculate.h:187` — фикс краша "Too many blended motions" у NPC (это не x64-проблема, константа одинакова в 32 и 64 бит)

---

## Команда сборки (всегда через MSYS2 bash)

```powershell
& "C:\msys64\usr\bin\bash.exe" -l -c "export PATH='/mingw64/bin:/usr/bin:`$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . --target xrRenderPC_R4 xrRender_GL xr_3da -j8 2>&1 | tail -100"
```

> **Важно:** cmake нельзя запускать напрямую из PowerShell — LuaJIT arch detection ломается. Только через MSYS2 bash.
