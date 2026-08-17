# VERIFY-аудит: отложенные места для второго прохода

Рабочий список кандидатов, которые в кампании «VERIFY исчез в релизе» были **осознанно отложены**, а не
починены: либо строгий refute не построил путь из данных, либо правка требует прогона/структурного
разбора, либо это другой класс. Сгруппировано по типу. Для каждого — где, источник риска, почему отложено,
что проверить на 2-м проходе.

Статус кампании на 17.08.2026: 5391 VERIFY → 39 реальных фиксов. Ниже — что НЕ трогали.

---

## ✅ 1. Навигация — ЗАКРЫТО вторым проходом (17.08)
**Аудит показал: read-путь level-графа УЖЕ безопасен.** `CLevelGraph::is_accessible` валидирует
(`valid_vertex_id(id) && m_access_mask[id]`); документированный краш bug 15983 (`accessible(level_vertex_id())`)
upstream уже прикрыл в `base_monster::useful` через `valid_vertex_id` + ресинк vertex из позиции. Память
была права — «вызывающие валидируют». Незакрытыми были два, оба поправлены:
- `game_graph_inline.h:79` `CGameGraph::accessible(vertex_id)` get/set — добавлен guard-зеркало
  `is_accessible` (невалидная вершина → false / пропуск записи, не OOB `m_enabled[]`). Прямых рантайм-
  вызовов нет — belt-and-suspenders, для валидных id поведение неизменно.
- `graph_abstract_inline.h:54` `CAbstractGraph::add_edge` — РЕАЛЬНЫЙ data-путь: `PhraseDialog.cpp:282`
  (phrase_id из диалогового XML мода), `smart_cover_description.cpp:174`/`smart_cover_loophole.cpp:160`
  (id переходов из конфига smart_cover). Битый id → `vertex()` null → краш; теперь ребро пропускается.
Собрано (xrAICore релиз, xrGame -Og), разложено, ABI ok. Прогон по локациям больше не обязателен для
этого класса — но остаётся полезен как подтверждение (log da_clamp/skip не должен срабатывать в норме).

<details><summary>Исходная запись (до закрытия)</summary>

## 1. Навигация: vertex_id в центральных аксессорах графа (нужен ПРОГОН)
Индекс вершины графа из данных (сейв/сеть/Lua `level.vertex_id`), VERIFY вырезан, дальше индексация.
Отложено, т.к. это горячие центральные аксессоры — слепая правка = поведенческая регрессия; ловить
только живым прогоном по локациям с логом невалидных vertex_id.

- `src/xrAICore/Navigation/game_graph_inline.h:79-86` — `accessible(vertex_id)`, низкоуровневый аксессор.
- `src/xrAICore/Navigation/graph_abstract_inline.h:54` — `_vertex1` из `vertex` (CodeQL deref-null-result).
- + ещё ~5 мест из первичной разведки (см. [[verify-recon-batch]] «nav vertex-id OOB (6 мест)»).
- **2-й проход:** взвести da_heap_guard + лог vertex_id на входе аксессоров, обойти все локации level-tour;
  чинить ТОЛЬКО те, что реально дают OOB на прогоне. Корень скорее у вызывающих (валидируют), не в листе.

</details>

## ✅ 2. Рендер getVB/getIB — ЗАКРЫТО вторым проходом (17.08)
`CRender::getVB/getIB/getVB_Format/getVisual/getSWI` (r2.cpp, компилится в shipping `xrRenderPC_R4`) —
все один шаблон `VERIFY(id<size); return &pool[id]`, id из OGF модели, ВСЕ вызовы на загрузке визуала
(FVisual/FTreeVisual::Load), не в кадре. Добавлен `da_clamp_geom_id`: стрелый id логируется раз и
зажимается в диапазон (модель рендерит зажатую геометрию вместо OOB-краша), пустой пул → null
(недостижимый фатал). Стиль как `GetMaterialByIdx`. Собрано (xrRenderPC_R4 -Og), разложено, ABI ok.

<details><summary>Исходная запись (до закрытия)</summary>

## 2. Рендер: getVB/getIB — индекс вершинного/индексного буфера из .ogf (рассинхрон модели)
`nVB[id]` / `nIB[id]` по id из геометрии модели под вырезаемым VERIFY; при рассинхроне модели — OOB.
Другой класс (не data-taint сейва/сети, а целостность ассета), потому вне основного невода.

- `src/Layers/xrRender/FVisual.cpp:38,51,74,86,106,120,142,154` — `getVB/getIB`.
- `src/Layers/xrRender/FTreeVisual.cpp:44,55` — то же (агент отметил: адрес элемента, не null, но id не проверен).
- `src/Layers/xrRenderPC_R2/r2.cpp:967-990` — `getVB/getIB` (батч 04, латентное наблюдение).
- **2-й проход:** проверить, валидирует ли загрузчик OGF диапазон id при разборе; если да — заведомо
  безопасно (структура ассета), закрыть. Если нет — мягкая отбраковка на загрузке, НЕ в рантайме кадра.
  Связано с классом «невидимая мачта» [[invisible-object-input-layout]].

</details>

## ✅ 3. smart_cover cover_id из Lua — ЗАКРЫТО (РЕАЛЬНЫЙ фикс, 17.08)
`CScriptGameObject::set_dest_smart_cover(cover_id)` НЕ валидировал id — мод мог передать любую строку;
несуществующий не-пустой id уходил в движок и позже ронял `CCoverManager::smart_cover()` (промах
`lower_bound` → `return *end()` = мусорный указатель → краш у вызывающего). Пустая строка уже отсекалась,
несуществующая — нет. Фикс у источника: добавлен не-крашащий `CCoverManager::has_smart_cover()`
(тот же lower_bound без разыменования); Lua-вход режет битый id со script_log-сообщением модеру, не
трогая горячий AI-путь и контракт smart_cover(). Файлы: cover_manager.h/.cpp, script_game_object_smart_covers.cpp.
Остальное из §3 (loopholes m_path, current_transition) — структурные инварианты, подтверждены §5-аудитом.

<details><summary>Исходная запись</summary>

## 3. smart_cover / cover_manager: cover_id и level_vertex_id из Lua
- `src/xrGame/cover_manager.cpp:233→236` — `return (*found)` по `cover_id` из Lua (`set_dest_smart_cover`).
  Отклонено обоснованно: стандартный upstream, реальный Lua-путь идёт через `cover_id()` где `""` отсечён,
  id — от существующих smart_cover; `return nullptr` лишь сместил бы краш в immediate-deref в fov_range.
- `stalker_movement_manager_smart_cover_loopholes.cpp` — `m_path[1]`, `m_current/m_target.cover()` —
  структурные инварианты пути (ветки достижимы только при валидном cover). Батч 06.
- `smart_cover::action_level_vertex_id` — МЁРТВЫЙ код (0 вызовов, как l07_military). `level_vertex_id` —
  структурный инвариант петли. См. [[verify-recon-batch]].
- **2-й проход:** трассировать ВСЕ Lua-входы `set_dest_smart_cover`/`cover_id` из скриптов мода; если мод
  умеет подсунуть id несуществующего cover — тогда guard у источника (cover_manager), не у листа.

## ✅ 4. Стек-буфер рестрикторов — ЗАКРЫТО (РЕАЛЬНЫЙ фикс, 17.08)
`CSpaceRestrictionHolder::normalize_string` кладёт указатели на рестрикторы в стек-массив `strings[128]`
(`MAX_RESTRICTION_PER_TYPE_COUNT`). `space_restrictors` — строка из конфига; при >128 запятых
`string_current` уходил за массив — переполнение стека (VERIFY снят в релизе). Реальный data-путь (секция
мода с длинным списком). Фикс: живой лимит-guard в обоих местах записи (в цикле — break с логом; финальный
элемент — под условием), лишние рестрикторы отбрасываются вместо порчи стека. Файл: space_restriction_holder.cpp.

<details><summary>Исходная запись</summary>

## 4. Записи в стек-буфер под VERIFY-границей
- `src/xrGame/space_restriction_holder.cpp:52,61` — запись в стек-буфер под VERIFY границы
  (`MAX_RESTRICTION_PER_TYPE_COUNT` — константа движка). Батч 06 оставил как безопасное (константа), но
  пометил под прицельную ручную валидацию.
- **2-й проход:** убедиться, что число рестрикторов на объект физически не превысит константу при
  данных мода (много рестрикторов в одной секции). Если может — clamp/skip.

## ✅ 5. Reference-return геттеры — ЗАКРЫТО (1 фикс, остальное инварианты, 17.08)
Аудит всех ~14 (агент + сверка). **1 реальный:** `UIActorMenu::InitPartnerInfo` дёргал
`GetModeSpecificPartnerInfo(...)->UIIcon()` (=`*m_icons[eIcon]`) без проверки; `m_icons[eIcon]` есть только
если узел присутствует в actor-menu XML (данные) — фикс: `if(partner_info->GetIcon(eIcon))` перед
разыменованием (UIActorMenu.cpp). **Остальные — структурные/lifecycle-инварианты, трогать нельзя:**
`current_transition()` (автомат smart-cover ставит перед использованием), `inventory_owner()` (предмет в
инвентаре), `FactionState get_war_state` (индекс 0..4 = размер массива), `Sound.h` геттеры (уже тернар-fallback),
`UICharacterInfo UIName/UICommunity` (внутренние вызывающие проверяют `if(m_icons[i])`, внешних нет),
loopholes `m_path[1]`/`cover()` (под `if(size>1)`/проверками cover), `action_planner object()` (ставится в ctor).

<details><summary>Исходная запись</summary>

## 5. Reference-return геттеры (структурные инварианты — нужен аудит вызывающих)
`return *m_object` / `return *m_current_transition` и т.п. — мягкий guard на ссылочный возврат невозможен,
защита должна стоять у вызывающего. ~14 мест по кампании.

- `stalker_movement_manager_smart_cover.cpp:432` — `return *m_current_transition`.
- `inventory_item_impl.h`, `FactionState_inline.h:88,94`, `Sound.h` геттеры, и др. (см. отчёты батчей 03/07/08/09).
- **2-й проход:** для каждого — проверить, все ли вызывающие гарантируют непустой контейнер ДО вызова.
  Это структурный, а не data-driven класс; массовая правка не нужна, точечный аудит по вызывающим.

## ✅ 6. Мёртвый код — ЗАКРЫТО как N/A (17.08)
Подтверждено нулём вызовов: `CSoundStream`/`CMusicStream` не инстанцируются вне xrSound (мёртвый
legacy-DirectSound); `CreateOccluder` — ноль вызовов; gamespy-консоль `CCC_GameSpyProfile` — мёртвая ветка
удалённого gamespy (сам логирует «removed from the engine»). Правки не требуются.

<details><summary>Исходная запись</summary>

## 6. Мёртвый / недостижимый код (закрыть как N/A, не чинить)
- `src/xrSound/xr_streamsnd.cpp:350→358` — `hf=FS.r_open(...ogg)` реально может вернуть null, VERIFY снят,
  но `CSoundStream`/`CMusicStream` нигде не инстанцируются вне xrSound — мёртвый legacy-DirectSound.
- `src/xrGame/account_manager_console.cpp:147` — консольная gamespy-команда, gamespy удалён (мёртвая ветка).
- `Frustum.cpp` `CreateOccluder` — без вызывающих в текущем дереве.
- **2-й проход:** подтвердить нулём вызовов через полный греп по дереву + gamedata; если так — вычеркнуть.

## 7. MP-только (SP-нерелевантно — парковка)
DA — одиночная игра. Весь блок game_sv_*/game_cl_* (deathmatch, teamdeathmatch, capture_the_artefact,
artefacthunt), UIMpTradeWnd/UIMpItemsStore/UITeamPanels/UITeamState, secure_messaging, account/login_manager
(gamespy), xrServer_CL_connect, NET_PlayersMonitor. Десятки мест. НЕ чинить, пока DA не станет MP.
- **2-й проход:** только если появится сетевой режим — тогда прогнать этот срез отдельно как SP-аналог.

## ✅ 8. game_sv_base:1080 — ЗАКРЫТО как безопасное (17.08)
`game_sv_GameState::on_death` → `e_src->ID`. Единственный не-MP вызывающий `xrServer_process_event.cpp:217`
УЖЕ имеет живой guard `VERIFY(e_src); if(!e_src){ Msg("! ERROR: SV: src killer not exist."); return; }`
прямо перед `on_death`. e_src не бывает null на этом пути. Правка не нужна. (game_sv_deathmatch:2276 — MP, §7.)

## 7/8 статус
§7 — парковка (MP, не чинить пока DA одиночная). §8 — закрыт выше.

---

### Как запускать 2-й проход
1. Регенерировать recall-дельту: `python scratchpad/verify_recall_probe.py src <known.csv> <delta.csv>`
   (probe уже покрывает мультисимвольные/окно; для nav/render щели расширить формы при нужде).
2. Приоритет: §1 (nav, прогоном) и §2 (render getVB/IB, аудит загрузчика OGF) — единственные с реальным
   data/asset-путём. §3-8 — точечная валидация/парковка.
3. Метод — как в основной кампании: adversarial-refute, полный контекст функции, мягкий guard (НЕ живой
   R_ASSERT в горячем пути [[live-assert-in-hot-datapath-regression]]), 2-й голос ручной сверкой.
