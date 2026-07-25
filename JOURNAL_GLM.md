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

### ASLR/High-Entropy-VA — глобальный фикс (2026-07-16)
- **Файлы:** `cmake/XRay.Compiler.GNULike.cmake`, `src/xr_3da/CMakeLists.txt`
- **Проблема:** После полной чистой пересборки (`ninja -t clean` + rebuild) все DLL
  получили `HIGH_ENTROPY_VA` (DllCharacteristics=0x160), что вызывало
  `Mingw-w64 runtime failure: 32 bit pseudo relocation out of range` при загрузке DLL.
  Ранее (до clean) работало, потому что CMake не пересобирал неизменившиеся DLL.
- **Фикс:** `--disable-dynamicbase --disable-high-entropy-va` перенесён из per-target
  `xr_3da/CMakeLists.txt` в глобальный `XRay.Compiler.GNULike.cmake` → применяется ко
  всем DLL + exe. Проверено: DllCharacteristics=0x100 для всех (HIGH_ENTROPY_VA сброшен).
- **Результат:** Полная пересборка (2108/2108) + деплой + запуск — игра работает 40+ секунд
  без краша, без pseudo-relocation error.

### Фонарик (flashlight)
- **Файлы:** `src/xrGame/Torch.cpp`, `Torch.h`, `ActorInput.cpp`, `script_game_object_inventory_owner.cpp`
- **Что сделано (полная картина):**
  1. Двухфонарная система (torch/torch2): `m_switched_on`/`light_omni` = "torch" (фоновый
     омни-свет, DA-скрипт `itms_manager.actor_on_update` держит его ON каждый кадр);
     `m_switched_on2`/`light_render` = "torch2" (реальный луч со спотом/тенью, который
     игрок переключает клавишей L). Реализовано: `Switch()`/`Switch2()`, `torch_active()`/
     `torch2_active()`, `net_Export`/`net_Import` с `eTorch2Active`, guard'ы в `net_Spawn`/
     `net_Import` для актёра (не принимать серверный state torch/torch2).
  2. `EnableTorch2` binding (НЕ stub): `CScriptGameObject::EnableTorch2` → `CTorch::Switch2`
     (`script_game_object_inventory_owner.cpp:2257`).
  3. `CActor::SwitchTorch()` (`ActorInput.cpp:888`) — ИСПРАВЛЕНО: теперь вызывает
     `Switch2()` (torch2/beam), а не `Switch()` (torch/omni). Раньше движок дёргал `Switch()`
     (который `actor_on_update` тут же возвращал обратно), а `Switch2()` (реальный луч) не
     переключался вообще → фонарик "всегда включён".
- **Статус:** Фикс применён, полная пересборка + деплой + запуск — игра работает.

### xray-monolith как донор
- **Файлы:** `scratchpad/xray-monolith/` (read-only референс, в .gitignore).
- **Что:** Клон `themrdemonized/xray-monolith` (ветка `all-in-one-vs2022-wpo`, shallow).
  Используется для grep'а реализаций DA-подобных методов. НЕ линковать в билд, НЕ копировать
  целиком — только точечный перенос функций по 3-touch схеме.
- **Аудит кластера A:** monolith НЕ имеет `enable_torch2` (DA-unique), но имеет
  `enable_torch`/`torch_enabled`/`update_torch`. Torch setters (`torch_set_*`/`torch2_set_*`)
  отсутствуют в monolith — это DA-unique, потребуют расширения `CTorch`.

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

1. **Фонарик** — фикс `CActor::SwitchTorch()` → `Switch2()` применён, ждёт деплоя+теста.
   Torch setters (`torch_set_*`/`torch2_set_*`) — stub'ы, DA-unique, отложены.
2. **R4 чёрный экран** — несовместимость шейдеров. Отложено.
3. **alife_human_brain.cpp warning** — `Wstringop-overflow` на `m_cpEquipmentPreferences[5]` / `m_cpMainWeaponPreferences[4]` (FixedVector bounds). Не критично.
4. **[DA_PORT] trace Msg** — отладочный scaffolding в ScriptExporter.cpp, script_engine.cpp, ai_space.cpp, r2.cpp, Device_create.cpp, x_ray.cpp, xrGame.cpp — нужно вычистить перед релизом.