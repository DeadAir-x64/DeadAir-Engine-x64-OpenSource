# Binding-gap worklist (static sweep, 2026-06-29)

Источник: статический свип 378 распакованных DA-скриптов (`D:\Dead Air Test\_unpacked\scripts`)
против зарегистрированных luabind-биндингов в `src/`. Скрипт: `scratchpad/binding_sweep.py`.

Метод считается ДЫРОЙ, если вызывается в скриптах (`obj:method(`), НЕ определён в самих
скриптах (Lua-ООП) и НЕ зарегистрирован в движке (`.def("...")`/`.property`/etc).
Все перечисленные ниже подтверждённо отсутствуют в `src/` (grep = 0 файлов).

Это **DA-специфичные расширения движка** — Dead Air добавила их в свой x32-движок.
В стоковом OpenXRay их нет. Чинить батчами по кластеру = одна пересборка на кластер.

ОТФИЛЬТРОВАННЫЙ ШУМ (НЕ дыры — Lua stdlib): write, read, close, gsub, gmatch, match, len, if.

## Кластер A — Фонарик (torch), ~17 методов, объект `torch`
torch_set_range, torch_set_radius, torch_set_animation, torch_set_texture,
torch_switch_spot, torch_set_offset_z, torch_set_offset_y, torch_set_inertion,
torch_set_color_r, torch_set_color_g, torch_set_color_b, torch_set_color_a,
torch2_set_range, torch2_set_radius, torch2_set_offset_x, torch2_set_offset_y,
torch2_set_color_r, torch2_set_color_g, torch2_set_color_b
→ цель: CTorch / каст с CScriptGameObject. Управление светом фонаря.

## Кластер B — Артефакты (get/set immunity+weight), ~25, объект `arte`/`object`
set_artefact_additional_weight, set_artefact_weight,
set_artefact_wound_immunity, set_artefact_strike_immunity, set_artefact_shock_immunity,
set_artefact_radiation_immunity, set_artefact_fire_wound_immunity,
set_artefact_explosion_immunity, set_artefact_chemical_burn_immunity,
set_artefact_burn_immunity, set_artefact_telepatic_immunity,
+ соответствующие get_artefact_* (те же поля), get_artefact_weight
→ цель: CArtefact / CSE_ALifeItemArtefact. Тюнинг свойств артефактов.

## Кластер C — Оружие / 3D-UI, ~7, объект `wpn`/`itm`/`item`
get_weapon_condition_type, set_weapon_condition_type, weapon_get_scope, weapon_set_scope,
get_ammo_name, is_ammo_suitable, get_3d_ui, reset_3d_ui
→ цель: CWeapon / CWeaponMagazined / inventory item.

## Кластер D — AI / уровень, ~6, объект `object`/`squad`/`commander`
set_path_evaluator, set_node_evaluator, level_vertex_light, get_next_action,
is_npc_indoors, enter_smart
→ цель: CScriptGameObject / monster_squad / level.

## Кластер E — Прочее, ~6
pack_level, unpack_level (self),  set_use_callback (door),
free_obj, free_obj_and_reinit (gulag),  open_check (_bp),  visibility_state (story_id)
→ разнородное; смотреть по месту вызова.

## Стратегия
1. Каждый кластер — отдельная пачка `.def` на одном целевом классе → одна пересборка/деплой.
2. Сверить с донором: форк OpenXRay от Anomaly (та же CoC-база). Что есть у Anomaly —
   перенести готовое. НО эти API скорее DA-специфичны; у Anomaly свои имена → пересечение
   может быть малым. Донор лучше закрывает ОБЩИЙ CoC-слой (он в OpenXRay уже есть).
3. Если есть исходники x32-движка самой Dead Air — реализации брать оттуда напрямую.
4. Soft-Lua-errors (pcall+лог+continue под флагом) — чтобы один прогон вываливал все
   рантайм-ошибки разом, а не по одной. (Опция ① — пока отложена по решению юзера.)

ВАЖНО: эти методы НЕ трогают save/net раскладку (это геттеры/сеттеры свойств) — относительно
безопасны. Но проверять каждый: если сеттер пишет в поле, читаемое в STATE_Read/Write —
не менять раскладку (правило #4).
