# Журнал проекта Dead Air

Проект: моддинг S.T.A.L.K.E.R.: Dead Air 0.98b
Движок: OpenXRay (build 9951, dev) + оригинальный X-Ray (build 7090)
Дата начала: 2026-06-26

---

## 2026-06-26 — Начало работ

### Изучение проекта
- Проект представляет собой рабочее окружение для моддинга Dead Air.
- Распаковщик архивов .xdb0/.db на pure Python (LZHUF + LZO1X) — `deadair_xdb.py`, `lzo1x.py`.
- Установщик Community Bugfixes с бэкапом — `install_bugfixes.py` (бэкап от 20.06.2026 уже есть).
- Диагностика: `find_empty_ambient.py`, `fix_ambients.py`, `scan_fps.py`.
- Сезонный свитчер на WinForms — `season_switcher.ps1`.
- Сборка OpenXRay из исходников — `xray-16/`, `xray-16-build/`.
- ReShade извлечён из установщика — `reshade_extract/`.
- 24 файла-оверрайда скриптов в `Dead Air/gamedata/scripts/`.

### Найден критический вылет OpenXRay
**Лог:** `Dead Air/appdata/logs/openxray_cap3347.log:1330-1342`
```
! [LUA] SCRIPT RUNTIME ERROR
! ai_stalker.script:242: attempt to call method 'get_addon_flags' (a nil value)
!   0 : [C  ] get_addon_flags
!   1 : [Lua] ai_stalker.script(242) : function <ai_stalker.script:147>
FATAL ERROR  CScriptEngine::lua_pcall_failed (script_engine.cpp:686)
```
Скрипт `ai_stalker.script` лежит внутри .xdb0 архивов, в `gamedata/scripts/` его нет.
Метод `get_addon_flags` — C++-метод движка для получения флагов аддонов оружия.
В OpenXRay он недоступен (переименован/удалён) → краш при создании новой игры.

### Предупреждения из лога OpenXRay (нужно проверить)
- 15 duplicate string table id
- 82 `id for info_portion don't set` (info_alife_switch.xml, info_yantar.xml)
- `Unknown command: r__actor_body`
- `[ActionNameToPtr] cant find 'wpn_7'`
- 9 `Version conflict in shader` (models\helicopter, models\xglass, и др.)
- `Renderer doesn't support blender 'effects\shadow_world'`

### План работ
1. [КРИТИЧНО] Извлечь ai_stalker.script, найти замену get_addon_flags в OpenXRay, пропатчить
2. [ВЫСОКО] Проверить пустые ambients во всех архивах, исправить
3. [СРЕДНЕ] Проверить остальные Lua-несовместимости
4. [НИЗКО] Инициализировать git, удалить мусор

---

## Контекст проекта (уточнён)

Проект — **портирование Dead Air 0.98b с x32 на x64** через OpenXRay + MinGW GCC 16.1.
Автор: DanesCrai1 + Claude (коммиты 329fdcc, 1aeb24e в xray-16/).

### Этапы (по PORT_x64_STATUS.md, PORT_x64_GLM_TASKS.md)
- **ЗАВЕРШЕНО:** Компиляция всех 21 DLL + xr_3da.exe под x64 MinGW (23.06.2026).
- **ТЕКУЩИЙ ЭТАП:** Phase A — boot x64 build, Phase B — Lua API stubs.
- `get_addon_flags` stub уже добавлен в исходники (script_game_object.h:405, script_game_object_inventory_owner.cpp:2247, script_game_object_script3.cpp:258).

### Найдена причина краха openxray_cap3347.log
В `Dead Air/bin/` стоят бинарники официального OpenXRay (май 2026, 14 MB) — **БЕЗ** `get_addon_flags`.
Свежая x64-сборка в `xray-16/bin/AMD64/Release/` (23 июня, 80 MB) — **С** stub.
Лог краша от запуска через `Dead Air/bin/` (без stub) → метод nil → fatal.

### Архитектура бинарников (проверено)
| Путь | Arch | Размер | Дата | get_addon_flags |
|------|------|--------|------|-----------------|
| `Dead Air/bin/xrGame.dll` | x64 | 14 MB | 07.05.2026 | NO (офиц. OpenXRay) |
| `Dead Air/xrGame.dll` (корень) | x86 | 7 MB | — | YES (ориг. DA x32) |
| `xray-16/bin/AMD64/Release/xrGame.dll` | x64 | 80 MB | 23.06.2026 | YES (наша сборка) |
| `OpenXRay_stage/bin/xrGame.dll` | x64 | 14 MB | 07.05.2026 | NO |

### План исправления
1. Заменить бинарники в `Dead Air/bin/` на сборку из `xray-16/bin/AMD64/Release/`.
2. Убедиться что `xrEngine.exe` в `Dead Air/` — x64 (сейчас x86 в корне!).
3. Запустить игру, проверить лог.

---

## Хронология изменений

### 2026-06-26 (продолжение)
- **Бинарники развёрнуты:** x64-сборка из xray-16/bin/AMD64/Release в Dead Air/bin/.
- **MinGW runtime:** скопированы 13 DLL из MSYS2 (libstdc++-6, libgcc_s_seh-1, libgomp-1, libwinpthread-1, libjpeg-8, liblzo2-2, libogg-0, libtheora-1, libvorbis-0, libvorbisfile-3, libopenal-1, libpng16-16, libbz2-1, zlib1→libz-1).
- **Рендер:** xrRenderPC_R4.dll → xrRender_R4.dll; user.ltx: renderer_r3 → renderer_r4.
- **Лаунчеры:** .cmd обновлены запускают bin\xrEngine.exe из корня (где fsgame.ltx).
- **РЕЗУЛЬТАТ:** Игра запускается! Build 9998, R4 DX11, GPU RTX 5070 Ti. Краш get_addon_flags ИСЧЕЗ.
- **НОВЫЙ БЛОКЕР:** Вечная загрузка на `create_persistent: before object_factory`.
  - `object_factory()` → `register_script_classes()` → `ai()` → CAI_Space::init → RestartScriptEngine (Lua/luabind).
  - Это известный блокер из PORT_x64_STATUS.md (коммит 329fdcc): "Hang isolated to RestartScriptEngine".
  - Требует отладки инициализации Lua/luabind под x64 MinGW.

### Phase B — отладка Lua/luabind зависания (в работе)
- Добавлены [DA_PORT] trace-точки в:
  - `src/xrServerEntities/object_factory_inline.h` — object_factory() создание/init
  - `src/xrGame/ai_space.cpp` — CAI_Space::init, SetupScriptEngine (по шагам)
  - `src/xrScriptEngine/script_engine.cpp` — ScriptEngine::init (по шагам: reinit, luabind::open, setup_callbacks, exporter, open_lib, process_file_if_exists)
- Пересобраны xrScriptEngine.dll и xrGame.dll, скопированы в Dead Air/bin/.
- **Локализация 1:** зависание в `ScriptEngine::init` → `exporter(lua())` → `node::export_all` → `sort()`.
- Добавлены trace в `ScriptExporter.cpp` sort() и export_all().
- **Локализация 2:** sort() прошла (303 nodes, 437 dfs calls, 248 sorted). Краш на export node[2].
  - node[0], node[1] — OK. node[2] addr=00007ff8ef3937e0 → access violation 0xC0000005.
  - Адрес в xrGame.dll — нужно определить какой класс экспортируется.
- Добавлено поле `m_name` в `class node` (через `__FILE__:__LINE__` в макросе DECLARE_SCRIPT_REGISTER_FUNCTION).
- **Полная пересборка запущена** (макрос в header изменился → все 303 nodes нужно перекомпилировать). PID 2280, лог в `_build_all_log.txt`.
- **Полная пересборка завершена** (BUILD_EXIT=0, 1926 targets). Все DLL + xr_3da.exe пересобраны.
- **Локализация 3 (ФИНАЛЬНАЯ):** Краш на `export_all: node[1]` = `CGameObject::script_register` (GameObject.h:395).
  - `base_client_classes_script.cpp:22` — luabind регистрация `class_<CGameObject, bases<IFactoryObject, ISheduled, ICollidable, IRenderable>, ..., CGameObjectWrapper>`.
  - Это **множественное наследование** с `luabind::wrap_base` через цепочку:
    `CGameObjectWrapper` → `CGameObjectIRenderable` → `ISheduledWrapper<CGameObjectIFactoryObject>` → `FactoryObjectWrapperTpl<CGameObject>` → `CGameObject` + `wrap_base`.
  - Access violation 0xC0000005 — классическая проблема luabind ABI под x64: неверное вычисление смещений (this-offset) для multiple inheritance с wrappers.
  - **Требует правки luabind** (Externals/luabind/) под x64 MinGW — большая задача.

### Phase B — фикс luabind ABI (продолжение, в работе)
- Добавлен trace в luabind: `class.cpp` (register_ по шагам), `class_registry.cpp` (add_class/find_class), `class_rep.cpp` (shared_init).
- Логи: `da_port_luabind.log`, `da_port_registry.log`, `da_port_classrep.log`.
- **Локализация 4:** Краш на регистрации `CBlend` → `IRenderable` после `add_class` в `m_classes[info] = crep` (map insert).
- **Корневая гипотеза:** `type_id::operator<` через `std::type_info::before()` — inconsistent ordering под MinGW x64.
- **Результат:** После добавления trace в `add_class` (с fflush) краш luabind **исчез**! Возможно UB / timing / map rehash.
- **ПРОГРЕСС:** Lua/luabind инициализация прошла ПОЛНОСТЬЮ. Лог вырос до 116 KB, дошли до загрузки level.
- **Новый краш (НЕ Lua):** `Environment_misc.cpp:246` — assertion `!m_sound_channels.empty() || !m_effects.empty()`.
  - Это проблема **пустых ambient-секций** (sound_channels/effects пустые) — известный баг, под который уже есть `find_empty_ambient.py`/`fix_ambients.py`.

### Phase B — фикс ambients (завершён)
- Запущен `find_empty_ambient.py` — найдено 6 пустых секций:
  - `ambients.ltx`: `[evening]`, `[post_blowout]`, `[post_psi_storm]`
  - `preset_underground.ltx`: `[indoor_underground]`, `[indoor_x8]`, `[indoor]`
- Запущен `fix_ambients.py` — пропатчено 6 секций (effects = effect_1), файлы в `gamedata/configs/environment/`.
- **Результат:** Assertion ambients исчез! Игра дошла до `App: after OnAppStart` (главный цикл).
- **Текущий краш:** Silent crash (без FATAL) после `App: after OnAppStart`. Лог обрывается. Возможно display/window или рендер проблема.
- **Прогресс сессии:** get_addon_flags краш → luabind ABI → ambients — все преодолены. Игра доходит до главного цикла рендера.

### Phase B - FIX luabind typeid comparator (FINAL FIX LUABIND)
- Root cause found and fixed: luabind::type_id::operator< used std::type_info::before().
  - Under MinGW x64 with LTO before() gives inconsistent ordering between DLLs -> std::map insert crashes on rebalance.
- Fix: Externals/luabind/luabind/typeid.hpp - operator< rewritten to string compare id->name() via std::strcmp (stable, deterministic).
- Rebuilt xrLuabind.dll.
- RESULT: Lua/luabind init passes fully, main render loop runs 400+ frames!
- Current crash: Silent crash (access violation, no FATAL) after ~400 frames. Possibly render/level loading.


---

## ПЛАН ОСТАВШЕЙСЯ РАБОТЫ

### Текущий статус (конец Phase B, 2026-06-26)
- Игра запускается на x64 MinGW-сборке OpenXRay, доходит до главного цикла рендера (400+ кадров).
- Lua/luabind полностью инициализируется (303 export nodes, 248 sorted).
- Загружаются скрипты (axr_main, xrs_dyn_music, closecaption, xrs_debug_tools).
- **Текущий блокер:** Silent crash (access violation, 0xC0000005) после ~400 кадров главного цикла, без FATAL ERROR в логе.

### Файлы изменённые в этой сессии (продакшн-код)
1. `Externals/luabind/luabind/typeid.hpp` — **КРИТИЧЕСКИЙ ФИКС**: `operator<` переписан на `std::strcmp(name())` вместо `type_info::before()`.
2. `src/xrScriptEngine/ScriptExporter.hpp` — добавлено поле `m_name` в `class node` + макрос `XRAY_STRINGIFY`.
3. `src/xrScriptEngine/ScriptExporter.cpp` — trace в `sort()` и `export_all()`.
4. `src/xrScriptEngine/script_engine.cpp` — trace в `init()` по шагам.
5. `src/xrServerEntities/object_factory_inline.h` — trace в `object_factory()`.
6. `src/xrGame/ai_space.cpp` — trace в `CAI_Space::init` и `SetupScriptEngine`.
7. `src/xrGame/base_client_classes_script.cpp` — trace в `CGameObject::script_register`.
8. `src/xrEngine/x_ray.cpp` — trace в `CApplication::Run` (frame counter).
9. `Externals/luabind/src/class.cpp` — trace в `class_registration::register_`.
10. `Externals/luabind/src/class_registry.cpp` — trace в `add_class`/`find_class`.
11. `Externals/luabind/src/class_rep.cpp` — trace в `shared_init`.
12. `Dead Air/gamedata/configs/environment/ambients.ltx` — фикс 3 пустых ambient-секций.
13. `Dead Air/gamedata/configs/environment/ambients/preset_underground.ltx` — фикс 3 пустых ambient-секций.
14. `Dead Air/appdata/user.ltx` — renderer_r3 → renderer_r4.
15. `Dead Air/bin/` — развёрнуты 21 DLL + xrEngine.exe из x64-сборки + 13 MinGW runtime DLL.
16. `Dead Air/Dead Air (no fps lock).cmd`, `Dead Air/debug.cmd` — запуск через bin/xrEngine.exe.

### Временные файлы (можно удалить)
- `_*.py`, `_*.sh`, `_*.txt`, `_find_out.txt`, `_dump_*.txt`, `_build_*.sh`, `_build_all_log.txt` — вспомогательные скрипты сессии.
- `da_port_luabind.log`, `da_port_registry.log`, `da_port_classrep.log` — trace-логи luabind.

---

## Phase B — оставшаяся работа (высокий приоритет)

### B-fix-1. Отладка silent crash после frame 400 (КРИТИЧНО)
**Проблема:** Access violation после ~400 кадров, без FATAL. Скрипты загружаются (frame 1), потом рендер (frame 2-400), потом краш.
**План:**
1. Добавить trace в `Device.ProcessFrame()` — `on_frame` / `BeforeFrame` / `AfterFrame` в `src/xrEngine/Device.cpp`.
2. Добавить trace в рендер `src/Layers/xrRenderPC_R4/r2.cpp` — `BeforeRender` / `AfterRender`.
3. Определить на каком кадре и в какой подсистеме краш (render? script? physics?).
4. Возможные причины:
   - Lua script callback краш (axr_main.script on_game_start / actor_on_update).
   - Рендер constant buffer (в логе 20+ `Failed to find compiled constant buffer`).
   - Shaders version conflict (9 `Version conflict in shader`).
   - `Renderer doesn't support blender 'effects\\shadow_world'`.
5. Попробовать `rspec_low.ltx` вместо `rspec_extreme.ltx` для минимизации рендера.

### B-fix-2. Шейдерные конфликты (ВЫСОКО)
**Проблема:** 9 `Version conflict in shader` + `shadow_world` blender не поддерживается.
**Файлы:** `models\helicopter`, `models\lightplanesself`, `models\model_puh`, `models\model_refl`, `models\xbrainglass`, `models\xdistortcolorlinv`, `models\xglass`, `models\xglass2`, `models\xlens`.
**План:** Проверить `shaders.xr` в `configs.xdb0` — возможно нужен патч shaders для R4 (DX11) вместо R2/R3 (DX9).

### B-fix-3. Constant buffer errors (ВЫСОКО)
**Проблема:** 20+ `Failed to find compiled constant buffer` при инициализации рендера.
**План:** Это связанно с шейдерами R4. Возможно нужен `shaders_cache_oxr` для DX11. Проверить `appdata/shaders_cache_oxr/`.

### B-fix-4. info_portion warnings (СРЕДНЕ)
**Проблема:** 82 `id for info_portion don't set` в `info_alife_switch.xml` (82) + `info_yantar.xml` (3).
**План:** Не критично (warning), но стоит проверить XML на корректность. Возможен конфликт с bugfixes.

### B-fix-5. Duplicate string table id (СРЕДНЕ)
**Проблема:** 15 duplicate string table id (st_unequip, ui_mm_detail_radius, zat_b22_stalker_medic_start_1 и др.).
**План:** Проверить `configs/text/rus/` на дубликаты между оригиналом и bugfixes.

### B-fix-6. Unknown console commands (НИЗКО)
**Проблема:** `r__actor_body`, `hud_draw_info`, `hud_draw_map`, 20+ `r2_*` команд не распознаны.
**План:** Эти команды из Dead Air x32, отсутствуют в OpenXRay. Добавить stub-команды в `console_commands.cpp` или игнорировать.

---

## Phase C — стабилизация (после Phase B)

### C1. Очистка trace-кода
- Убрать `[DA_PORT]` trace из production-кода (или обернуть в `#ifdef DA_PORT_DEBUG`).
- Убрать trace из luabind (`class.cpp`, `class_registry.cpp`, `class_rep.cpp`).
- Оставить `typeid.hpp` фикс (КРИТИЧНО — не trace).

### C2. Harden GCC fallback stubs
- `src/xrCore/string_concatenations.cpp` `check_stack_overflow()` — GCC branch no-op (C1 в PORT_x64_GLM_TASKS.md).
- `src/Layers/xrRenderDX11/dx11HWCaps.cpp` `GetNVGpuNum()` — GPU count default (C2).
- `src/xrGame/AnselManager.cpp` — cosmetic stub (C3).
- `src/xrCore/xrDebug.cpp` — minidump на x64 crash (C4).

### C3. MAX_BLENDED raise (D1 в PORT_x64_GLM_TASKS.md)
- `src/Layers/xrRender/KinematicAnimatedDefs.h:6` — `MAX_BLENDED` 16 → 32.
- Фикс "Too many blended motions" для тяжёлых анимаций.

---

## Phase D — финализация

### D1. Git-коммит
- Закоммитить все изменения: `typeid.hpp` фикс, ambients патч, trace-код (опционально).
- Сообщение: `Phase B: fix luabind typeid comparator for x64 MinGW + ambients patch`.

### D2. Удалить мусор
- `gui_err.txt`, временные `_*.py`/`_*.sh`/`_*.txt`.

### D3. Тестирование
- После B-fix-1: запустить игру, дойти до главного меню → новая игра → загрузка level.
- Проверить: сохранения работают, звук играет, рендер корректный.
- Протестировать `Dead Air (no fps lock).cmd` и `debug.cmd`.

### D4. Документация
- Обновить `PORT_x64_STATUS.md` и `PORT_x64_GLM_TASKS.md` с результатами Phase B.
- Зафиксировать typeid.hpp фикс как ключевой x64-патч.

---

## Будущее (после полного запуска)

### F1. Сезонная система
- `season_switcher.ps1` — проверить работу на x64 (зима/лето).
- Тестировать `xtra_green.xdb0` (лето) и `textures.xdb0` в patches (зима).

### F2. ReShade
- Проверить `dxgi.dll` + ReShade на x64 (ReShade_x64_5157144.dll).
- В логе `ReShade.log` — возможны конфликты с DX11 R4.

### F3. Community Bugfixes
- 11 модов в `user_fixes/_extracted/DeadAir Mods/` — проверить совместимость с x64.
- `DeadAir_0.98b_Community_Bugfixes` — убедиться что бэкап актуален.

### F4. Производительность
- FPS без лимита (`-nofpslock`) — проверить RTSS cap.
- `MAX_BLENDED` 32 — измерить impact на память.

### F5. Мультиплеер
- `xrGameSpy.dll` — GameSpy отключён, но MP-код компилируется. Проверить что не крашит.

### F6. Сохранения
- `appdata/savedgames/cap3347 - quicksave4.scop` — проверить загрузку на x64.
- Возможен краш из-за разницы в net_export/net_import (u32/u64 для ID).


### Phase B - B-fix-6 stub console commands (in progress)
- Added 36 Dead Air x32 console commands as stubs in xrRender_console.cpp/.h (r2_shadow_map_size, r2_sun_shafts_value, r2_fxaa, r2_sss_*, r2_vignette, r__color_*, r1_dynamic_lights, r__actor_body, etc.).
- Added hud_draw_info/hud_draw_map aliases in console_commands.cpp.
- Rebuilt xrGame.dll, xrRenderPC_R4.dll, xrRender_GL.dll.
- Goal: fix UIComboBox crash when entering video settings menu (r2_shadow_map_size token missing).
