# Phase B — инструкции для GLM (старт: 2026-06-30)

Полный плейбук и правила — в `AGENTS.md` и `PORT_x64_GLM_TASKS.md`. Этот файл — что делать
завтра, по шагам, с первой конкретной задачей.

## Где мы стоим

Билд **стабилен и запускается** (ASLR-фикс применён в `src/xr_3da/CMakeLists.txt`). Движок,
рендер R4 DX11, звук, скриптовый движок (248 биндингов) — работают. Новая игра стартует,
грузит уровень, спавнит NPC — и падает на **первом сталкере** (задача B-3 ниже).

Уже закрыто: B-1 (ассерт `Environment_misc.cpp:246`), B-2
(`is_enough_address_space_available` — глобал в `script_engine.cpp`).

## Рабочий цикл (как двигаться)

1. Архитектор запускает игру с `-force_flushlog`, ловит FATAL/`[LUA]` и даёт тебе `file:line`.
2. Ты открываешь это место, чинишь по нужному из 4 типов (таблица в `AGENTS.md`), правишь
   ТОЛЬКО названную функцию.
3. Проверяешь компиляцию одним таргетом (команда ниже) — не запускаешь игру сам.
4. Отдаёшь только изменённый блок с `file:line` + одно предложение «почему».
5. Архитектор делает ПОЛНУЮ пересборку + деплой ВСЕХ DLL, перезапускает, даёт следующий FATAL.

## ЖЕЛЕЗНЫЕ правила (нарушение = краш/порча сейвов)

0. **Никогда не деплой DLL по одной** — это работа архитектора, всегда полным набором.
   Ты можешь только compile-check одного таргета.
1. Трогай только названный файл+функцию. Не рефактори рабочий код.
2. Не редактируй данные DA (`gamedata\...`). Все фиксы — в `src/`.
3. Не добавляй молчаливых заглушек: `Msg("! [DA_PORT_STUB] %s", __FUNCTION__)`.
4. НИКОГДА не заглушай и не «прикидывай» save/net пакеты (`net_export`, `net_import`,
   `load`, `save`, `STsaveGameState`). Не уверен в раскладке байт — СТОП, спроси.
5. Не меняй сигнатуру экспортируемого символа (`*_API`) без предупреждения — это требует
   полной пересборки всех DLL (иначе `0xc0000139` до старта).

## Compile-check (одним таргетом, через MSYS2 bash)

```bash
/c/msys64/usr/bin/bash.exe -l -c "export PATH='/mingw64/bin:/usr/bin:\$PATH'; cd '/d/Dead Air/xray-16/build_mingw' && cmake --build . --target xrGame -j8 2>&1 | tail -40"
```
(таргет под изменённый файл: `xrGame`, `xrEngine`, `xrCore`, и т.д.)

---

## ЗАДАЧА B-3 (делать первой) — get_addon_flags на ВЕРНОМ классе

**Симптом:** `ai_stalker.script:242: attempt to call method 'get_addon_flags' (a nil value)`.

**Диагноз (подтверждён распакованным скриптом):**
```lua
local se_item = create(sim, spawn_sec, ...)   -- СЕРВЕРНЫЙ alife-объект
if (IsWeapon(nil, cls) ...) then
    local flags = se_item:get_addon_flags()    -- строка 242
    flags:set(cse_alife_item_weapon.eWeaponAddonScope, true)
```
`se_item` — это **серверный** объект `cse_alife_item_weapon` (класс `CSE_ALifeItemWeapon`),
а НЕ `CScriptGameObject`. Метод `GetAddonFlags` ошибочно добавили в `CScriptGameObject`
(клиентский) — поэтому в рантайме nil. Это надо исправить на правильном классе.

**Что сделать:**

1. **Реализация** — в `CSE_ALifeItemWeapon` добавить метод, возвращающий ссылку на
   `m_addon_flags` (поле уже есть: `Flags8 m_addon_flags;` в
   `src/xrServerEntities/xrServer_Objects_ALife_Items.h:227`):
   ```cpp
   Flags8& get_addon_flags() { return m_addon_flags; }
   ```
   Объяви в `xrServer_Objects_ALife_Items.h` (рядом с классом `CSE_ALifeItemWeapon`),
   определи inline или в `.cpp`.

2. **Регистрация** — класс `CSE_ALifeItemWeapon` регистрируется в
   `src/xrServerEntities/xrServer_Objects_ALife_Items_script.cpp:70`:
   ```cpp
   luabind_class_item1(CSE_ALifeItemWeapon, "cse_alife_item_weapon", CSE_ALifeItem)
   ```
   Добавь в цепочку `.def(...)` этого класса:
   ```cpp
   .def("get_addon_flags", &CSE_ALifeItemWeapon::get_addon_flags)
   ```
   (посмотри как устроен макрос `luabind_class_item1` и куда в него вставляются `.def` —
   попроси scoper найти соседний пример `.def` на серверном классе, скопируй стиль.)

3. **Откат неверного** — в `CScriptGameObject` ранее добавили `GetAddonFlags`
   (`script_game_object.h:405`, `script_game_object_inventory_owner.cpp`,
   `script_game_object_script3.cpp:258`). Он не на том классе — **убери эти три правки**
   (или оставь, но они мёртвые; чисто — убрать).

4. **Проверь возврат by-reference** — luabind должен отдать `Flags8&` так, чтобы Lua мог
   звать `flags:set(enum, bool)`. `Flags8` уже зарегистрирован в luabind с методом `set`
   (используется в скриптах как `flags:set(...)`). Если возврат по значению ломает `:set`,
   вернуть указатель/ссылку. НЕ меняй раскладку `m_addon_flags` (это net/save поле —
   читается/пишется в `STATE_Read/Write`, см. `..._Items.cpp:532,546`).

**Acceptance:** после полной пересборки игра проходит спавн сталкера, FATAL на
`ai_stalker.script:242` исчезает, в логе нет нового `[LUA]`-эрора от get_addon_flags.

---

## После B-3 — продолжай цикл

Архитектор перезапустит и даст следующий FATAL. Применяй тот же подход (4 типа фиксов из
`AGENTS.md`). Подсказка: распакованные скрипты DA лежат в `D:\Dead Air Test\_unpacked` —
если надо понять на КАКОМ объекте/классе зовётся метод, читай реальный скрипт там (как с
B-3), а не гадай.
