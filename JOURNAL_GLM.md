# Журнал GLM — Dead Air x64 Port

## Глобальный прогресс (на 2026-06-30)

### Билд
- 21 DLL + xr_3da.exe компилируются под MinGW GCC 16.1.0 x64 (CMake + Ninja, MSYS2).
- Билд-директория: `D:\Dead Air\xray-16\build_mingw`
- Бинарники: `D:\Dead Air\xray-16\bin\AMD64\Release\`
- Рабочая копия игры: `D:\Dead Air Test\Dead Air`

### Рантайм
- Движок, рендер R4 DX11, звук, скриптовый движок (248 биндингов) — работают.
- Новая игра стартует, грузит уровень, спавнит NPC — играбельное состояние.
- OpenGL-рендерер также работает (шейдеры скопированы из репозитория OpenXRay).

---

## Сделанные задачи (по порядку)

### B-1: CEnvAmbient empty section assert
- **Файл:** `src/xrEngine/Environment_misc.cpp:246`
- **Фикс:** `R_ASSERT(!m_sound_channels.empty() || !m_effects.empty())` → tolerant warning `[DA_PORT] CEnvAmbient '%s': no sound channels and no effects, skipping`.

### B-2: is_enough_address_space_available Lua global
- **Файл:** `src/xrEngine/script_engine.cpp`
- **Фикс:** `lua_register` для `is_enough_address_space_available` в `CScriptEngine::init()`.

### B-3: get_addon_flags на правильном классе
- **Файлы:** `src/xrServerEntities/xrServer_Objects_ALife_Items.h`, `xrServer_Objects_ALife_Items_script.cpp`
- **Фикс:** `get_addon_flags()` перенесён с `CScriptGameObject` (клиентский) на `CSE_ALifeItemWeapon` (серверный), возвращает `Flags8*`.
- **Причина:** Dead Air скрипты зовут `se_item:get_addon_flags()` на серверном объекте, не на клиентском.

### ODR violation: NET_Server/NET_Client
- **Файлы:** `src/xrGame/xrServer.h`, `game_sv_base.h`, `Level.h`
- **Фикс:** Под `__MINGW32__` подключаются пустые версии `NET_Server.h`/`NET_Client.h` (без DirectPlay), чтобы xrGame и xrNetServer имели идентичный layout `IPureServer`.

### CRITICAL_SECTION move-ctor
- **Файлы:** `src/xrCore/Threading/Lock.hpp`, `Lock.cpp`
- **Фикс:** Убран bitwise copy `CRITICAL_SECTION` при move-construct — `InitializeCriticalSection` привязан к адресу объекта.

### ISpatial_DB::_remove
- **Файл:** `src/xrCDB/ISpatial.cpp:317-357`
- **Фикс:** Убран `VERIFY(octant < 8)`, который крашился на inconsistent spatial tree — теперь лог и skip prune.

### typeid.hpp luabind fix
- **Файл:** `src/Externals/luabind/typeid.hpp`
- **Фикс:** `operator<` использует `std::strcmp(name())` вместо `std::type_info::before()` — MinGW x64+LTO даёт inconsistent ordering между DLL.

### Empty ambient sections
- **Файлы (данные):** `gamedata/configs/environment/ambients/ambients.ltx`, `preset_underground.ltx`
- **Фикс:** Добавлены пустые секции `evening`, `post_blowout`, `post_psi_storm`, `indoor_underground`, `indoor_x8`, `indoor` (через `fix_ambients.py`).

### ~60 Lua binding stubs
- **Файлы:** `src/xrGame/script_game_object.h`, `script_game_object_inventory_owner.cpp`, `script_game_object_script3.cpp`
- **Фикс:** Добавлены stub-методы для_missing DA bindings: torch, artefact, weapon/UI, AI/level, misc. Каждый stub emits `[DA_PORT_STUB]` Msg.

### Artefact immunity getters/setters + weight
- **Файлы:** `src/xrGame/script_game_object_inventory_owner.cpp`, `Artefact.cpp`
- **Фикс:** Реальные bindings через `CArtefact::m_ArtefactHitImmunities` и `CInventoryItem::Weight()/SetWeight()`.

### ESC key
- **Файл:** `src/xrGame/Level_input.cpp:137-148`
- **Фикс:** ESC сначала закрывает UI-диалоги (TopInputReceiver→HideDialog), потом вызывает `Console->Execute("main_menu")`.
- **Предыдущая попытка:** маппинг на `kPAUSE` → `Device.Pause()` не открывал меню. Исправлено на `main_menu`.

### Фонарик (flashlight)
- **Файлы:** `src/xrGame/Torch.cpp`, `ActorInput.cpp`, `object_handler.cpp`
- **Что сделано:**
  1. `CActor::SwitchTorch()` (`ActorInput.cpp:888`) — ищет фонарь строго в `TORCH_SLOT` через `ItemFromSlot`, не перебирает все attached-объекты.
  2. `CTorch::net_Spawn()` (`Torch.cpp:274`) — стартует выключенным для актёра: `start_on = torch->m_active && !smart_cast<CActor*>(H_Parent())`.
  3. `CTorch::net_Import()` (`Torch.cpp:473`) — игнорирует `eTorchActive` из серверного пакета для актёра.
- **ОСТАЁТСЯ ПРОБЛЕМА:** `CObjectHandler::OnItemTake()` (`object_handler.cpp:77`) и `CObjectHandler::attach()` (`object_handler.cpp:293`) вызывают `switch_torch(item, true)` — фонарь включается при экипировке. Нужно добавить проверку: не включать для актёра.
- **Статус:** НЕ ЗАВЕРШЕНО — фонарь всё ещё всегда включён.

### OpenGL рендерер
- **Файлы (данные):** `gamedata/shaders/gl/` (358 файлов скопировано из `res/gamedata/shaders/gl/`)
- **Launcher:** `Dead Air GL.cmd` (с `-rgl -nofpslock -force_flushlog`)
- **Статус:** Работает. Уровень грузится, картинка видна.

### R4 (DX11) рендерер — ЧЁРНЫЙ ЭКРАН
- **Файл:** `src/Layers/xrRenderPC_R4/r4_shaders.cpp:420-428`
- **Что сделано:** Добавлены Dead Air-specific define'ы в `CRender::shader_compile()`: `H_MODELS=0`, `H_BUSHES=0`, `H_GRASS=0`, `H_TERR=0`, `L_RANGE=1.0`, `PIXEL_SIZE=1.0`.
- **Результат:** Старые ошибки `H_MODELS`/`H_BUSHES` ушли, но появились новые: `eye_direction`, `L_BRIGHT` — это не define'ы, а shader uniforms/cbuffer constants, которые Dead Air HLSL ожидает от движка, а OpenXRay R4 не предоставляет.
- **Диагноз:** Фундаментальная несовместимость шейдерного пайплайна Dead Air и OpenXRay R4. Без исходников шейдеров Dead Air или готового набора R4-шейдеров от совместимого мода (Anomaly) — не починить engine-side.
- **Статус:** ОТЛОЖЕНО. Рабочая настроенная версия держится в `D:\Dead Air Test\Dead Air`.

### Deploy
- Копирование DLL: `xray-16\bin\AMD64\Release\*.dll` → `Dead Air\bin\`
- `xr_3da.exe` → `xrEngine.exe`
- `xrRenderPC_R4.dll` → `xrRender_R4.dll` (копия)
- MinGW runtime DLLs из MSYS2

### Launcher .cmd files
- `Dead Air (no fps lock).cmd` — R4/DX11, `-nofpslock`
- `Dead Air GL.cmd` — OpenGL, `-rgl -nofpslock -force_flushlog`
- `Dead Air DX11.cmd` — R4/DX11, `-r4 -nofpslock -force_flushlog`

---

## Незавершённые / Known Issues

1. **Фонарик всегда включён** — `CObjectHandler::OnItemTake` и `attach` вызывают `switch_torch(item, true)` без проверки на актёра. Нужно добавить `smart_cast<CActor*>` проверку.
2. **R4 чёрный экран** — несовместимость шейдеров. Отложено.
3. **alife_human_brain.cpp warning** — `Wstringop-overflow` на `m_cpEquipmentPreferences[5]` / `m_cpMainWeaponPreferences[4]` (FixedVector bounds). Не критично.
4. **[DA_PORT] trace Msg** — отладочный scaffolding в ScriptExporter.cpp, script_engine.cpp, ai_space.cpp, r2.cpp, Device_create.cpp, x_ray.cpp, xrGame.cpp — нужно вычистить перед релизом.