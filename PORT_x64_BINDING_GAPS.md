# Binding-gap worklist (static sweep, 2026-06-29)

Источник: статический свип 378 распакованных DA-скриптов (`D:\Dead Air Test\_unpacked\scripts`)
против зарегистрированных luabind-биндингов в `src/`. Скрипт: `scratchpad/binding_sweep.py`.

Метод считается ДЫРОЙ, если вызывается в скриптах (`obj:method(`), НЕ определён в самих
скриптах (Lua-ООП) и НЕ зарегистрирован в движке (`.def("...")`/`.property`/etc).
Все перечисленные ниже подтверждённо отсутствуют в `src/` (grep = 0 файлов).

Это **DA-специфичные расширения движка** — Dead Air добавила их в свой x32-движок.
В стоковом OpenXRay их нет. Чинить батчами по кластеру = одна пересборка на кластер.

ОТФИЛЬТРОВАННЫЙ ШУМ (НЕ дыры — Lua stdlib): write, read, close, gsub, gmatch, match, len, if.

> **СТАТУС 2026-07-22: ВСЕ КЛАСТЕРЫ A–E ЗАКРЫТЫ.** Донор xray-monolith прогрепан по всем
> 19 оставшимся именам — НИ ОДНОГО совпадения (это DA-unique слой, донор закрывает только
> общий CoC-слой, уже присутствующий в OpenXRay). Реализация — по семантике call-sites из
> `_unpacked\scripts`. Итог: 4 реальных биндинга (weapon_get/set_scope, get_ammo_name,
> is_ammo_suitable), 3 стаба (get_3d_ui, reset_3d_ui, get_next_action), остальное — мёртвый
> код/шум свипа (не фильтровал Lua-комментарии). Детали в кластерах ниже.

## Кластер A — Фонарик (torch), ~17 методов, объект `torch`
torch_set_range, torch_set_radius, torch_set_animation, torch_set_texture,
torch_switch_spot, torch_set_offset_z, torch_set_offset_y, torch_set_inertion,
torch_set_color_r, torch_set_color_g, torch_set_color_b, torch_set_color_a,
torch2_set_range, torch2_set_radius, torch2_set_offset_x, torch2_set_offset_y,
torch2_set_color_r, torch2_set_color_g, torch2_set_color_b
→ цель: CTorch / каст с CScriptGameObject. Управление светом фонаря.

### Статус (2026-07-16)
- **enable_torch2** — реализован: `CScriptGameObject::EnableTorch2` → `CTorch::Switch2`
  (см. `script_game_object_inventory_owner.cpp:2257`, `Torch.cpp:192`).
- **enable_torch / torch_enabled** — реализован: `enable_torch` → `CTorch::Switch` (torch/omni),
  `torch_enabled` → `CTorch::torch_active` (`m_switched_on`). DA-скрипт
  `itms_manager.actor_on_update` держит `m_switched_on=true` (фоновый омни-свет).
- **CActor::SwitchTorch()** — ИСПРАВЛЕНО: теперь вызывает `Switch2()` (torch2/beam, реальный
  луч), а не `Switch()` (torch/omni, который скрипт возвращает обратно каждый кадр).
  Файл: `ActorInput.cpp:888`.
- **torch_set_*/torch2_set_*/torch_switch_spot** — STUB (no-op lambda в
  `script_game_object_script3.cpp:263+`). DA-уникальные сеттеры параметров света; в
  xray-monolith их НЕТ (monolith имеет только `enable_torch/torch_enabled/update_torch`).
  Требуют расширения `CTorch` (новые поля + setters) — отложено до необходимости.

## Кластер B — Артефакты (get/set immunity+weight), ~25, объект `arte`/`object`
set_artefact_additional_weight, set_artefact_weight,
set_artefact_wound_immunity, set_artefact_strike_immunity, set_artefact_shock_immunity,
set_artefact_radiation_immunity, set_artefact_fire_wound_immunity,
set_artefact_explosion_immunity, set_artefact_chemical_burn_immunity,
set_artefact_burn_immunity, set_artefact_telepatic_immunity,
+ соответствующие get_artefact_* (те же поля), get_artefact_weight
→ цель: CArtefact / CSE_ALifeItemArtefact. Тюнинг свойств артефактов.

## Кластер C — Оружие / 3D-UI, ~7, объект `wpn`/`itm`/`item` — ЗАКРЫТ (2026-07-22)
- **get/set_weapon_condition_type** — реализованы ранее.
- **weapon_get_scope / weapon_set_scope** — РЕАЛИЗОВАНЫ: индекс текущего прицела в
  `m_scopes` (255 = прицел не надет; сентинел bind_item.script для "нечего сохранять").
  `script_game_object.cpp` рядом с Get/SetAmmoType; set → `m_cur_scope` +
  `UpdateAddonsVisibility()` + `InitAddons()`. Регистрация: `script_game_object_script2.cpp`.
- **get_ammo_name** — РЕАЛИЗОВАН: секция текущего типа патрона `m_ammoTypes[GetAmmoType()]`
  (itms_manager.script: конверсия патронов при drag-n-drop).
- **is_ammo_suitable(section)** — РЕАЛИЗОВАН: поиск секции в `m_ammoTypes`.
- **get_3d_ui / reset_3d_ui** — STUB (no-op, `script_game_object_script3.cpp`): DA-шная
  3D-UI подсистема (экраны на девайсах) в OpenXRay отсутствует; единственный вызов
  get_3d_ui в DA-скриптах ЗАКОММЕНТИРОВАН (xr_actor.script:387, внутри --[[ ]]),
  reset_3d_ui — только из дебаг-меню. Void-возврат читается Lua как nil.

## Кластер D — AI / уровень, ~6 — ЗАКРЫТ (2026-07-22): 5 из 6 — мёртвый код
Проверка call-sites показала: **set_path_evaluator, set_node_evaluator (xr_abuse,
xr_combat_monolith, xr_combat_zombied, xr_sleeper), level_vertex_light (bind_heli:71),
enter_smart (alun_utils:849, xr_effects:3574), is_npc_indoors (surge_manager:1677 внутри
--[[ ]])** — ВСЕ вызовы закомментированы. Свип 2026-06-29 не фильтровал комментарии.
Ничего не реализовано — нечему падать.
- **get_next_action(bool)** — единственный живой (alun_utils.assign_squad_to_smart:852,
  вызывается только из debug_cmd_list:900). STUB no-op на `CSE_ALifeOnlineOfflineGroup`
  (`xrServer_Objects_ALife_Monsters_script3.cpp`): скрипт перед вызовом делает
  `current_action = nil`, и `sim_squad_scripted:update()` сам подберёт действие на
  следующем тике — no-op семантически корректен.

## Кластер E — Прочее, ~6 — ЗАКРЫТ (2026-07-22): целиком шум/мёртвый код, кода не требует
- **pack_level / unpack_level** — закомментированы (level_weathers:641,661).
- **open_check** — только в комментарии (ph_door:200).
- **visibility_state** — шум свипа из строк-комментариев xr_conditions:1804/xr_effects:4394;
  реальные вызовы — `get_visibility_state`/`force_visibility_state` — УЖЕ забинжены
  (`script_game_object_script2.cpp`, кровосос).
- **set_use_callback** — вызывается только из `utils.door_init`, у которого НОЛЬ вызовов.
- **free_obj / free_obj_and_reinit** — только в `xr_gulag.resetJob`/`free_object`,
  у обоих НОЛЬ вызовов (SoC-наследие).

## Свип v2 (2026-07-22, `scratchpad/binding_sweep2.py`)
Новый свип: вырезает Lua-комментарии, покрывает и `obj:method(`, и `module.func(`.
Результаты по 378 скриптам _unpacked:
- **burer_get_force_anti_aim** — РЕАЛИЗОВАН (2026-07-22): DA-переименование CoC-шного
  `get_force_anti_aim`; у нас `CBaseMonster::get_force_anti_aim()` уже был, добавлена
  обёртка+регистрация (`script_game_object_use2.cpp`, `script_game_object_script2.cpp`).
- **actor_stats.remove_from_ranking** — НЕ реализован СОЗНАТЕЛЬНО: единственный вызов
  (bind_stalker:75) сам охраняется `if(...~=nil)` — отсутствие безопасно. Нет ни в
  CoC-Xray, ни в monolith (DA-unique рейтинг). Худший случай — мёртвые NPC остаются
  в списке рейтинга ПДА. Реализовать только если тестер заметит.
- **exit_smart** (alun_utils:846) — БАГ САМОЙ DA: вызов на чистом Lua-классе
  `simulation_board` (sim_board.script), метод нигде не определён — в x32 падал бы
  так же. Движок не при чём. Не чиним (данные DA не трогаем).
- **OnButton_multiplayer_clicked** (ui_main_menu:357) — мёртвый MP-путь (Dispatch cmd==2
  в SP не приходит). Не чиним.
- **marshal / lfs / lua_pack** — ПРОВЕРЕНО: живы в x64 (Externals/xrLuaFix, собирается,
  `luaopen_xrluafix` в `script_engine.cpp:845`). `USE_MARSHAL=true` → персистентность
  alife_storage_manager работает.
- **lootmoney** — вызовы охраняются `if lootmoney then` — безопасно при любом раскладе.

### Свип v3 (2026-07-22, `scratchpad/binding_sweep3.py`) — ПОЛНАЯ поверхность 378 файлов
Расширенный аудит: методы + модульные вызовы + `callback.*` + `clsid.*` + конструкторы
классов + голые глобалы. **Вердикт: НОЛЬ новых реальных дыр движка.**
- callback.*: 4 находки = поля ЛОКАЛЬНОЙ таблицы state_mgr (не движковый enum) — шум.
- clsid.*: ~100 находок = clsid-таблица наполняется РАНТАЙМОМ через object_factory /
  class_registrator.script — статически невидима; игра работает → clsids на месте. Шум.
- Конструкторы: совпадения внутри строковых литералов ("Weapons (Ammo)" и т.п.). Шум.
- Глобалы: multi-var locals (`local a,b,mrand = ...`), строки-паттерны ("quicksave(%d+)"),
  мёртвые функции без вызовов (sim_objects.register_object, npc_in_cover x2). Шум.
- game.CTime — зарегистрирован (`level_script.cpp`, module "game", class_<xrTime>("CTime")).
Достоверный остаток по 378: только известное (см. свип v2) + недостающие скрипты ниже.

### КРИТИЧНО: _unpacked НЕПОЛНЫЙ
`falout_manager.actor_on_save` зовётся БЕЗ охраны на каждом сейве (bind_stalker_ext:221),
сейвы у тестера работают → `falout_manager.script` в игровом VFS ЕСТЬ, а в _unpacked —
НЕТ. Аналогично под подозрением: rx_ai, sr_danger, pda_dialog, level_tasks, xr_position.
→ Добавлен one-shot bulk-export ВСЕХ скриптов из VFS в `appdata\logs\vfs_scripts\`
(`UIGameCustom.cpp`, триггер — первый запрос "hud_borders"). После следующего запуска
игры: пересвипнуть по полному набору.

### Разведка по бартеру и перезарядке с пояса (2026-07-22)
Обе фичи тестера (#3 обмен с NPC, #4 перезарядка только с пояса), похоже, СКРИПТОВЫЕ и
живут в НЕДОСТАЮЩИХ скриптах:
- "barter" не упоминается ни в одном из 378 _unpacked-скриптов вообще.
- Движковая цепочка обмена ЦЕЛА: `CUIActorMenu::SetMenuMode` → `CurModeToScript()`
  (`UIActorMenu.cpp:142`) → `actor_menu.actor_menu_mode(2)` → `trade_wnd_opened()` →
  `SendScriptCallback("TrdWndOpened")` → mob_trade. Режимы 0-4 совпадают с enum,
  10/11 идут в `pda.actor_menu_mode` — ровно как в CoC-Xray (канон).
- `weapon_no_ammo` (= `eWeaponNoAmmoAvailable`) движок стреляет в `TryReload`
  (`WeaponMagazined.cpp:207`) — как и ждёт bind_stalker:177. Но подписчиков
  `RegisterScriptCallback("actor_on_weapon_reload")` среди 378 файлов НЕТ →
  логика пояса в недостающем скрипте.
→ НЕ реализовывать бартер/пояс движком, пока нет полного VFS-экспорта — рискуем
продублировать/сломать скриптовую реализацию DA.

### РЕАЛИЗОВАНО В НОЧНОЙ СЕССИИ 2026-07-22 (по данным реверса строк x32 + полного VFS-набора)
1. **Полный набор скриптов получен**: `-da_export_scripts` → 385 файлов. Дифф с _unpacked:
   +7 loose-скриптов Weapons Evolution (enhanced_animations, unjam_motion_mark, wpn_effects,
   item_animations, sound_on_belt_item_use, dfz_hide_wpn, ayykyu_fp_death). ЯДРО DA = 378,
   _unpacked был ПОЛНЫМ. «Отсутствующие» модули (falout_manager, rx_ai, sr_danger, pda_dialog,
   lootmoney, level_tasks, xr_position) не существуют и в оригинале — спящие баги самой DA
   (falout_manager в actor_on_save/load падает ПОСЛЕ полезной работы симметрично в обеих
   ветках — самонейтрализуется; в x32 идентично).
2. **SetMisfire/IsMisfire на классе CWeapon** (Weapon.h SetMisfireScript + WeaponScript.cpp) —
   для unjam_motion_mark.script (WE) через cast_Weapon. Донор: monolith WeaponAK74.cpp:198.
3. **Перезарядка только с пояса (фишка DA)** — движковая: `FindAmmoForReload` в
   WeaponMagazined (актёр → `CInventory::Get(sect,false)` = только пояс; NPC → GetAny),
   заменены все 6 точек поиска патронов в TryReload/IsAmmoAvailable/ReloadMagazine +
   HUD-счётчик GetAmmoCount_forType не считает рюкзак для актёра.
4. **Бартер (фишка DA)** — движковой: строки x32 показали `barter_mode` в кластере
   CSpecificCharacter::load_shared (флаг профиля персонажа) и `trade_barter_button` в
   кластере XML-имён actor_menu (третья кнопка торговли). Реализовано: чтение
   `<barter_mode>` в specific_character.*, кнопка trade_barter_button (опциональная,
   UIActorMenuInitialize), обработчик OnBtnPerformTradeBarter (обмен вещь-на-вещь без денег,
   сделка при actor_price >= partner_price, перенос через TransferItem(...,bFree=true)),
   гейтинг в InitTradeMode: barter_mode-партнёр → кнопка бартера, скрыть Купить/Продать.
   ⚠ ПРОВЕРИТЬ по экспорту конфигов: есть ли trade_barter_button в actor_menu.xml DA
   (если нет — нужен loose-оверрайд XML, СПРОСИТЬ у юзера) и точное правило сделки.
5. **`-da_export_configs`** — второй флаг экспорта: весь $game_config$ → appdata\logs\vfs_configs\.

### Разгадка старого краша lua_extensions
`xrLuaFix` (`luaopen_xrluafix`) на старте создаёт ПУСТУЮ таблицу `lua_extensions`
("Anomaly compatibility") → авто-загрузчик неймспейсов xray видит существующий глобал и
НИКОГДА не грузит `lua_extensions.script` из VFS (который, судя по _unpacked, существует
и реализует recurse_subdirectories_and_execute через lfs). Наша C++-реализация заполнила
таблицу — работает; но это объясняет механизм и предостерегает: ЛЮБОЙ модуль, который
xrLuaFix/luabind регистрирует заранее, затеняет одноимённый .script в VFS.

### КОРНЕВАЯ ПРИЧИНА НЕСТАБИЛЬНОГО СТАРТА (найдена 2026-07-22, ночная сессия)
Все 4 тестовых запуска x64 зависали/падали ДО создания лога. Раскопано по WER-дампам
(`%LOCALAPPDATA%\CrashDumps\xr_3da.exe.*.dmp`, python-minidump + nm):
- Модуль краша xrAICore.dll, offset 0x3384d = `__report_error` (MinGW CRT).
- На стеке: `__rt_psrelocs_end`, `__fu0_lua_pushboolean` → рантайм-релокатор
  `_pei386_runtime_relocator` упал на auto-import фиксапе `lua_pushboolean`.
- Причина: ld `--enable-auto-image-base` раскидал preferred base наших DLL по ~8ГБ
  (0x1F3A9_0000..0x38BD1_0000, ASLR у модулей ВЫКЛЮЧЕН — dllchar 0x100). 32-битные
  pseudo-relocs требуют |дистанции| < 2ГБ; xrAICore→xrLuaJIT = ~4.2ГБ → abort при
  загрузке DLL, процесс остаётся спинить в кернеле, лог не создаётся, kill не берёт
  (только WMI Terminate). Это же почти наверняка объясняет исторические капризы
  запуска/"работает-не-работает" между машинами (у тестера layout другой).
- ФИКС: явные упакованные image base для всех 20 DLL в окне 0x180000000..0x1A8000000
  (32МБ слоты, xrGame 64МБ; до exe 0x140000000 — 1.6ГБ) — блок в корневом
  `CMakeLists.txt` (`LINKER:--image-base`). Полная пересборка + деплой ВСЕГО комплекта
  (не только xrGame!) во все установки. ПРАВИЛО ДЕПЛОЯ: всегда копировать весь набор
  xr*.dll + xr_3da.exe — частичный деплой смешивает ABI/лейауты.

## Доноры (обновлено 2026-07-22)
1. **OpenXRay** — база порта (`D:\Dead Air\xray-16\`).
2. **xray-monolith** (`scratchpad/xray-monolith/`, themrdemonized, all-in-one-vs2022-wpo) —
   общий CoC/Anomaly-слой. DA-unique имён НЕ содержит (прогрепано: 0/19).
3. **CoC-Xray** (`scratchpad/CoC-Xray/`, github.com/revolucas/CoC-Xray, shallow, read-only) —
   **прямой предок движка DA** (X-Ray 1.6.02 под Call of Chernobyl; DA 0.98 построена на CoC).
   Уже дал референс `weapon_get/set_scope` (`script_game_object_inventory_owner.cpp:1703+`,
   255-сентинел подтверждён). Проверять его ПЕРВЫМ при вопросах "как это делала DA":
   всё, что DA унаследовала от CoC, тут в исходниках.
Исходники самой DA x32 — НЕ опубликованы (перепроверено поиском 2026-07-22).
Оставшийся путь к DA-unique: таргетный реверс `_x32_backup\xrGame.dll` по luabind-строкам.

## Стратегия
1. Каждый кластер — отдельная пачка `.def` на одном целевом классе → одна пересборка/деплой.
2. Сверить с донором: **`themrdemonized/xray-monolith`** (ветка
   `all-in-one-vs2022-wpo`, локально `scratchpad/xray-monolith/`). Форк OpenXRay+CoC с
   DA-подобными расширениями. grep'ать реализации по имени метода, переносить нужную
   функцию в наш `src/` по 3-touch схеме. НЕ линковать в билд, НЕ копировать целиком.
   Что есть у Anomaly — перенести готовое. НО эти API скорее DA-специфичны; у Anomaly
   свои имена → пересечение может быть малым. Донор лучше закрывает ОБЩИЙ CoC-слой
   (он в OpenXRay уже есть).
3. Если есть исходники x32-движка самой Dead Air — реализации брать оттуда напрямую.
   (Проверено 2026-07-16: исходники DA x32 в открытом доступе ОТСУТСТВУЮТ.)
4. Soft-Lua-errors (pcall+лог+continue под флагом) — чтобы один прогон вываливал все
   рантайм-ошибки разом, а не по одной. (Опция ① — пока отложена по решению юзера.)

ВАЖНО: эти методы НЕ трогают save/net раскладку (это геттеры/сеттеры свойств) — относительно
безопасны. Но проверять каждый: если сеттер пишет в поле, читаемое в STATE_Read/Write —
не менять раскладку (правило #4).
