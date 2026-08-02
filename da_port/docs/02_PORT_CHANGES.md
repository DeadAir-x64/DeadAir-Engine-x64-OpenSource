# Карта правок порта

Всё, что отличает этот порт от чистого OpenXRay, помечено в исходниках маркером `[DA_PORT]`
(инфраструктурные вещи — просто `DA:`). Ниже — полный список: **1553 правк(и) в 279 файлах**.

Список сгенерирован из самих исходников, не написан вручную — значит он не разойдётся с кодом,
пока маркеры на месте. Пересобрать: `python xray-16/da_port/docs/_regen_changes.py`.

> Как читать: каждая строка — это `файл:строка` и первая строка комментария, объясняющего правку.
> Развёрнутое объяснение «почему» лежит рядом с кодом — комментарии писались как основная документация,
> потому что они не теряются при переносе и видны тому, кто читает функцию.


## Рендер — DirectX 11 (R4)

*88 файл(ов), 653 правк(и)*


### `Layers/xrRender/Blender_Recorder_R2.cpp`

- **:33** — Разметка по программам. Именно ЭТА перегрузка r_Pass используется рендером DX11

### `Layers/xrRender/Blender_Recorder_StandartBinding.cpp`

- **:11** — Ручки луж живут в глобальном пространстве имён (xr_ioc_cmd.cpp), объявлять их ВНУТРИ
- **:26** — extern ENGINE_API float g_da_rain_wetness; // [DA_PORT] сюда кладём накопленную влажность для игры
- **:44** — BIND_DECLARE(wvp_old); // [DA_PORT] motion vectors
- **:45** — BIND_DECLARE(wvp_nojit); // [DA_PORT] motion vectors
- **:59** — DECLARE_TREE_BIND(wave_old); // [DA_PORT]
- **:60** — DECLARE_TREE_BIND(wind_old); // [DA_PORT]
- **:229** — ---- Лужи: сила дождя и накопленная влажность --------------------------------------
- **:258** — При включённой отладке — раз в секунду в лог. Нужно, чтобы отделить «значение
- **:267** — Msg("* [DA_PORT] лужи: дождь %.3f, влажность %.3f, размер %.2f, force %.2f, вкл %d",
- **:307** — Отдаём наружу: по этому числу игровой код решает, плескать ли под ногами.
- **:317** — Вид воды: зеркальность, потемнение, глянец просто мокрой земли, сила ряби.
- **:319** — Вторая константа вида: дальность, за которой луж не рисуем. Отдельной сделана потому,
- **:334** — Отметка о том, что константу вообще спросили. Если этой строки в логе нет, а
- **:344** — Msg("* [DA_PORT] лужи, вид: глянец %.2f, темнее в %.2f, мокрота %.2f, рябь %.2f",
- **:474** — Deliberately the plain window size, i.e. stock behaviour. Deriving it from the current
- **:539** — Motion vectors. Registered here, with the transforms, rather than as ordinary
- **:550** — r_Constant("wave_old", &tree_binder_wave_old); // [DA_PORT] motion vectors
- **:551** — r_Constant("wind_old", &tree_binder_wind_old); // [DA_PORT] motion vectors
- **:574** — лужи: дождь сейчас + накопленная влажность, и отдельно — их вид

### `Layers/xrRender/D3DXRenderBase.cpp`

- **:367** — The same breakdown, into the LOG instead of the screen.
- **:397** — Light counts, on their own line.

### `Layers/xrRender/D3DXRenderBase.h`

- **:76** — Свободный контекст ищем ТОЛЬКО среди параллельных. Взято у Dead-Air-Refined,
- **:128** — Отказ настоящий, а не только под VERIFY.

### `Layers/xrRender/DetailManager.cpp`

- **:23** — Множители качания травы, см. MT_Render ниже. Определены в xrEngine/xr_ioc_cmd.cpp.
- **:107** — motion vectors: no previous sway yet, and an uninitialised flag here would let the very
- **:148** — И здесь тоже: удаление приходит не только через Unload. См. WaitCalcTask.
- **:246** — Дождаться фоновой задачи расчёта травы. Без этого её нельзя ни выгружать, ни удалять.
- **:265** — Ожидания по указателю на задачу ОКАЗАЛОСЬ МАЛО - падение повторилось с тем же
- **:284** — Msg("! [DA_PORT] расчёт травы не завершился за секунду, продолжаю без него");
- **:288** — Msg("~ [DA_PORT] ожидание расчёта травы: %u уступок времени", spins);
- **:295** — WaitCalcTask(); // [DA_PORT] см. WaitCalcTask: ниже освобождается то, что задача читает
- **:471** — Проверка на пустоту обязательна: задачу теперь снимают и в WaitCalcTask, поэтому
- **:484** — Сильный ветер: смешиваем исходное качание с модовским, доля задаётся r__grass_sway.
- **:521** — ⭐ Одновременно может считаться только ОДНА задача травы. Здесь причина падений.
- **:542** — Признак снимается на ЛЮБОМ выходе, включая ранние: иначе ожидание в

### `Layers/xrRender/DetailManager.h`

- **:141** — The same three phases as of the previous frame, for motion vectors. Grass sway is
- **:234** — void WaitCalcTask(); // [DA_PORT] дождаться расчёта травы перед выгрузкой или удалением
- **:236** — Признак «задача расчёта работает». Ставится при постановке в очередь, снимается

### `Layers/xrRender/DetailManager_Decompress.cpp`

- **:175** — Номер модели приходит из данных уровня и до сих пор не проверялся ничем.
- **:187** — Msg("! [DA_PORT] трава: модель %u при списке из %u, слот (%d %d) - пропускаю",
- **:302** — REVERTED - kept as a note, do not re-apply without measuring first.

### `Layers/xrRender/FTreeVisual.cpp`

- **:10** — Vegetation sway scale, 0 = frozen. Defined in the engine; declared outside the namespace
- **:120** — The same two as they were on the previous frame, for motion vectors. Without them the
- **:129** — Sway phase, accumulated rather than derived from the clock. See calculate().
- **:147** — The phase is ACCUMULATED, not computed as time * speed.
- **:166** — Wrapped to one turn. The phase is fed to periodic functions (calc_cyclic in the
- **:181** — r__wind_scale: 0 freezes the trees. See xr_ioc_cmd.cpp for why it exists.
- **:204** — Exactly once per frame, and provably so.
- **:217** — ⚠️ Мало вычислить один раз — надо ещё дождаться, пока вычислят.
- **:253** — Foliage stands still in the SHADOW pass, while swaying normally on screen.
- **:276** — cmd_list.tree.set_wave_old(tvs.wave_old); // [DA_PORT] motion vectors
- **:277** — cmd_list.tree.set_wind_old(wind_old); // [DA_PORT] motion vectors

### `Layers/xrRender/ParticleEffect.cpp`

- **:20** — extern ENGINE_API float g_hud_fov_current; // [DA_PORT] nearwall

### `Layers/xrRender/R_Backend_Runtime.cpp`

- **:11** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here: an extern written inside
- **:46** — Глубину привязываем ТОЛЬКО когда её размер совпадает с целью.
- **:476** — Compensate the mip selection for r__render_scale. Rendering the scene smaller makes the
- **:485** — With TAA on, optionally ask for sharper mips than the hardware would pick: mip selection
- **:508** — Результат обязательно проверять и говорить о нём в лог. Интерфейс появился в
- **:518** — Msg("* [DA_PORT] отладочные метки GPU недоступны (нет ID3DUserDefinedAnnotation, "

### `Layers/xrRender/R_Backend_Runtime.h`

- **:55** — see R_Backend_xform.h - these follow m_wvp so they are correct per object, not per pass.

### `Layers/xrRender/R_Backend_tree.cpp`

- **:16** — c_wave_old = nullptr; // [DA_PORT]
- **:17** — c_wind_old = nullptr; // [DA_PORT]
- **:70** — void R_tree::set_wave_old(Fvector4& vec) // [DA_PORT] motion vectors
- **:76** — void R_tree::set_wind_old(Fvector4& vec) // [DA_PORT] motion vectors

### `Layers/xrRender/R_Backend_tree.h`

- **:13** — R_constant* c_wave_old; // [DA_PORT] motion vectors
- **:14** — R_constant* c_wind_old; // [DA_PORT] motion vectors

### `Layers/xrRender/R_Backend_xform.cpp`

- **:6** — Camera matrices for motion vectors, owned by the engine. Declared outside the namespace:
- **:19** — Default the previous-frame world to the current one; movers overwrite it via set_W_old
- **:26** — Rebuilt for THIS object, on the same footing as m_wvp - see the header for why they
- **:46** — Supplies the real previous-frame transform for something that moves. Always called right
- **:109** — c_wvp_old = nullptr; // [DA_PORT]
- **:110** — c_wvp_nojit = nullptr; // [DA_PORT]
- **:117** — m_w_old.identity(); // [DA_PORT] motion vectors
- **:124** — m_wvp_old.identity(); // [DA_PORT]
- **:125** — m_wvp_nojit.identity(); // [DA_PORT]

### `Layers/xrRender/R_Backend_xform.h`

- **:16** — World matrix this object had on the PREVIOUS frame, for motion vectors. set_W keeps it
- **:23** — The two motion-vector matrices, kept here rather than behind R_constant_setup binders.
- **:36** — Оружие и предметы в руках рисуются СВОЕЙ проекцией (узкий HUD-обзор,
- **:68** — void set_W_old(const Fmatrix& m); // [DA_PORT] motion vectors
- **:81** — IC void set_c_wvp_old(R_constant* C); // [DA_PORT]
- **:82** — IC void set_c_wvp_nojit(R_constant* C); // [DA_PORT]

### `Layers/xrRender/ResourceManager_Loader.cpp`

- **:106** — shadow_world пропускаем молча: этот рендер его не реализует ПО ЗАМЫСЛУ —
- **:126** — Это не ошибка, и знак «!» здесь вводил в заблуждение.
- **:136** — Msg("~ [DA_PORT] шейдер '%s': описание версии %u при текущей %u - читается как есть",

### `Layers/xrRender/ResourceManager_Scripting.cpp`

- **:441** — Ошибка в скрипте шейдера больше не убивает игру молча.
- **:457** — Msg("! [DA_PORT] шейдер [%s], элемент [%s]: ОШИБКА В СКРИПТЕ", namesp, name);
- **:461** — Msg("! [DA_PORT]   %s", lua_tostring(L, -1));
- **:467** — Msg("! [DA_PORT] шейдер [%s], элемент [%s]: исключение: %s", namesp, name, e.what());
- **:471** — Msg("! [DA_PORT] шейдер [%s], элемент [%s]: неизвестное исключение", namesp, name);

### `Layers/xrRender/SH_Texture.h`

- **:116** — Releases every shader resource view this texture owns, minding that m_pSRView

### `Layers/xrRender/ShaderResourceTraits.h`

- **:8** — Путь исходника для манифеста прогрева, определён в r4_shaders.cpp. Объявление именно
- **:705** — Путь исходника — прогреву кэша, см. da_warm_record в r4_shaders.cpp. Отсюда,

### `Layers/xrRender/SkeletonCustom.cpp`

- **:120** — motion vectors
- **:125** — Roll the visual's world matrix one frame back. Called from the render path with the matrix
- **:139** — "Previous" only means anything if the visual was actually drawn on the previous frame.
- **:165** — The bones have to be seeded here too, not just the world matrix. Leaving them alone

### `Layers/xrRender/SkeletonCustom.h`

- **:133** — ---- Motion vectors: this visual's world matrix on the previous frame ---------------

### `Layers/xrRender/SkeletonX.cpp`

- **:16** — shared_str s_bones_array_old_const; // [DA_PORT] previous-frame poses, for motion vectors
- **:47** — Motion vectors: remember where this visual was, and tell the backend, so the shader's
- **:98** — The same poses as they were on the previous frame, for motion vectors. Without this
- **:190** — s_bones_array_old_const = "sbones_array_old"; // [DA_PORT]

### `Layers/xrRender/blenders/blender_bloom_build.cpp`

- **:235** — NOTE: this C++ blender is NOT what builds the post-process pass in Dead Air —

### `Layers/xrRender/blenders/blender_taa.cpp`

- **:35** — Маска реактивности - «здесь истории верить нельзя». До 01.08 наша темпоралка её не

### `Layers/xrRender/blenders/blender_taa.h`

- **:5** — temporal anti-aliasing resolve (R4 only). Blends the previous frame into the current one after

### `Layers/xrRender/dxRainRender.cpp`

- **:4** — Размеры и плотность дождя — ручками, а не константами 2007 года. См. xr_ioc_cmd.cpp.
- **:33** — const int particles_cache = 1500; // [DA_PORT] было 400 — при плотном дожде всплески обрывались
- **:76** — Цвет капли множится на r__rain_bright: в конфигах он тёмно-серо-бурый, и тонкая капля
- **:83** — Всплеск на земле — своя яркость. Общий цвет с каплей давал белую крупу на тёмной земле.

### `Layers/xrRender/light.cpp`

- **:42** — vis.miss_streak = 0; // [DA_PORT]

### `Layers/xrRender/light.h`

- **:24** — u32 bNeverDemote : 1; // [DA_PORT] см. IRender_Light::set_never_demote
- **:76** — Сколько проверок подряд сказали «не видно». Гасим лампу только после нескольких:

### `Layers/xrRender/light_vis.cpp`

- **:84** — Гистерезис вместо мгновенного приговора: лампа гаснет только после нескольких

### `Layers/xrRender/r__dsgraph_build.cpp`

- **:40** — ---- Geometry cut-off by size and distance --------------------------------------------
- **:347** — if (!da_is_valuable_dynamic(pVisual, xform, o.phase)) // [DA_PORT] geometry cut-off
- **:436** — if (!da_is_valuable_static(pVisual, o.phase)) // [DA_PORT] geometry cut-off
- **:649** — if (!da_is_valuable_static(pVisual, o.phase)) // [DA_PORT] geometry cut-off

### `Layers/xrRender/r__dsgraph_render.cpp`

- **:11** — extern ENGINE_API float g_hud_fov_current; // [DA_PORT] nearwall: == psHUD_FOV unless modulated
- **:32** — This used to be `if (equal) return false; return left.ssa >= right.ssa;`, which is not a
- **:159** — HUD-камера для векторов движения: эта и прошлого кадра. Живут в самом объекте, потому
- **:194** — Вектор движения для всего, что в руках, должен считаться в ТОЙ ЖЕ проекции, в
- **:227** — cmd_list.xforms.da_set_VP_overrides(nullptr, nullptr); // [DA_PORT] сцена снова считает по себе

### `Layers/xrRender/xrRender_console.cpp`

- **:26** — Which entry the upscaler list stands on; needed by CCC_MSAA further down. Declared HERE,
- **:113** — 8x убран из списка (значение 3 в стоковой таблице). Три причины, и все три — про то, что
- **:166** — The author's value; governs STATIC visuals - trees and bushes, grass is unaffected by it.
- **:185** — 16 rather than the stock 8, and this is not a "more is prettier" bump. Under any upscaler
- **:217** — 64 is what the original actually rendered with, not merely the stock value: the author's
- **:231** — R2FLAG_DOF убран из значений по умолчанию.
- **:244** — ⚠️ R2FLAGEXT_SUN_ZCULLING ДОБАВЛЕН в набор по умолчанию (в апстриме его тут нет).
- **:264** — Tone mapping and bloom below are the author's values, taken from the Dead Air sources
- **:269** — float ps_r2_tonemap_adaptation = 2.f; // r2-only  [DA_PORT] was 1.f
- **:270** — float ps_r2_tonemap_low_lum = 0.2f; // r2-only  [DA_PORT] was 0.0001f
- **:271** — float ps_r2_tonemap_amount = 0.4f; // r2-only  [DA_PORT] was 0.7f
- **:273** — float ps_r2_ls_bloom_kernel_b = .5f; // r2-only  [DA_PORT] was .7f
- **:275** — float ps_r2_ls_bloom_kernel_scale = 0.55f; // r2-only // gauss  [DA_PORT] was .7f
- **:279** — float ps_r2_ls_bloom_threshold = 0.f; // r2-only  [DA_PORT] was .00001f (author's value)
- **:294** — float ps_r2_sun_near_border = 1.0f; // [DA_PORT] was 0.75f (author's value)
- **:295** — 180, the value beside it, which is the author's and also the console maximum. At 100 the
- **:299** — 51 instead of the stock 180.
- **:310** — The author's lighting balance: a much stronger sun against a heavily damped ambient and
- **:313** — float ps_r2_sun_lumscale = 1.6f; // [DA_PORT] was 1.0f
- **:314** — float ps_r2_sun_lumscale_hemi = 0.6f; // [DA_PORT] was 1.0f
- **:315** — float ps_r2_sun_lumscale_amb = 0.4f; // [DA_PORT] was 1.0f
- **:338** — Dead Air compatibility stubs (x32 mod commands)
- **:366** — Vibrance, not saturation: it lifts muted colours and leaves vivid ones alone, so night
- **:376** — Ready-made grading profiles, so a player can pick a look instead of hunting for numbers.
- **:391** — Default grading for Dead Air's palette: a slight push towards warm. The mod's world is
- **:403** — Ceiling on shadow-casting lights per frame. 0 restores the stock "no limit".
- **:411** — Дефолт - «Минимум». Проверено в игре 01.08: разницы в картинке почти нет, а кадр по
- **:439** — float ps_r2_gloss_factor = 6.0f; // [DA_PORT] was 4.0f (author's value)
- **:579** — ---- Geometry cut-off (author's optimisation, see xrRender_console.h) -----------------
- **:586** — Both levels default to off, unlike the author's 1/1. This subsystem comes from his
- **:605** — Счётчик кадров для da_sun_log (замер по каскадам солнца). 0 = молчит.
- **:608** — da_sun_only N: накапливать солнечный свет только от каскада N (1..3), 0 = все.
- **:612** — Диагностические крутилки регистрируются через CCC_DaDebugInteger / CCC_DaDebugFloat
- **:633** — Presets for the shadow-casting light ceiling, exposed in the video options.
- **:635** — One-touch performance preset for the Performance tab.
- **:648** — Пятый пункт («Ультра-производительность», st_opt_perf_max_fps) убран намеренно:
- **:652** — Пятый пункт списка — «Своё». Он ничего не применяет, и это не заглушка, а починка.
- **:681** — pcstr smap_cache; // [DA_PORT] срок жизни кэша теневых карт солнца, МИЛЛИСЕКУНДЫ (0 = выключен)
- **:686** — Тени от ламп сдвинуты на ступень вниз - иначе применение набора качества в меню
- **:688** — Последний столбец — кэш теневых карт солнца, в МИЛЛИСЕКУНДАХ.
- **:755** — Отсюда значение и берут: и список настроек для показа, и Save для записи в конфиг
- **:779** — Размер теневой карты - единственное в наборе, что не применяется до перезапуска
- **:788** — Msg("~ [DA_PORT] набор настроек: размер теневой карты %s применится после перезапуска игры",
- **:802** — Note the first entry, and that it is 0 rather than a count: 0 means "no budget at all",
- **:820** — Кэш теневых карт солнца. Значение токена — САМ СРОК ЖИЗНИ В МИЛЛИСЕКУНДАХ, лишней
- **:846** — Настройка из меню. Числа консоль здесь НЕ принимает, и это защита, а не строгость.
- **:884** — Отладочная форма той же настройки: точный срок жизни в миллисекундах.
- **:909** — r2_sun_details: у автора это ТРИ состояния, у нас остаётся флаг — и это осознанно.
- **:1082** — Multisampling may not run next to a RECONSTRUCTING upscaler, so picking it clears those -
- **:1116** — Applies one of the grading profiles. Values mirror the .ltx profiles shipped in
- **:1401** — Была под DEBUG, а зовёт её наш da_mem_test — в релизе он получал «Unknown command»
- **:1411** — Geometry cut-off, ported from the author's build. Defaults match his: both levels on
- **:1413** — Render breakdown into the log, N frames. Needs rs_stats 1 as well: the sub-counters are
- **:1417** — Потолок поднят с 2000 до 200000: значение — ЧИСЛО КАДРОВ, а минута игры при
- **:1421** — Замер по каскадам солнца: da_sun_log N печатает N кадров подряд. Против
- **:1423** — ⚠️ ЧЕРЕЗ CCC_DaDebugInteger, а не CCC_Integer: диагностика не должна оседать в
- **:1428** — Кэш теневых карт солнца. Разбор — в render_phase_sun.cpp, у da_smap_should_render.
- **:1436** — ⚠️ ЧЕРЕЗ CCC_DaDebugInteger: это ручка для замеров, а не настройка.
- **:1443** — GPU time per render phase, N frames into the log. See da_gpu_timer.h.
- **:1449** — Прогрев кэша шейдеров в несколько потоков. Разбор — у da_shader_warmup в
- **:1475** — CMD3(CCC_PerfPreset, "r__perf_preset", &ps_r__perf_preset, q_perf_preset); // [DA_PORT]
- **:1477** — CMD3(CCC_Token, "r__light_shadow_budget", &ps_r__light_shadow_budget, q_light_shadow_budget); // [DA_PORT]
- **:1545** — The author's bounds, not the stock ones: he raised the ceiling to 100 and, more to the
- **:1609** — Un-commented: this drives r_dtex_range, the distance over which the detail texture
- **:1670** — CMD3(CCC_MSAA, "r3_msaa", &ps_r3_msaa, qmsaa_token); // [DA_PORT] clears the upscaler list
- **:1701** — Dead Air compatibility stub commands
- **:1727** — CMD4(CCC_Float, "r2_vibrance_val",       &ps_r2_vibrance_val, -1.f, 1.f); // [DA_PORT] negative = desaturate
- **:1728** — CMD3(CCC_GradingPreset, "r__grading_preset", &ps_r_grading_preset, qgrading_preset_token); // [DA_PORT]

### `Layers/xrRender/xrRender_console.h`

- **:58** — How many lights may cast shadows in one frame; 0 = no limit (stock behaviour).
- **:232** — Dead Air compatibility stub variables (x32 mod commands absent in OpenXRay)
- **:259** — extern ECORE_API u32 ps_r_grading_preset; // [DA_PORT] ready-made colour grading profiles
- **:272** — ---- Geometry cut-off by size and distance (author's optimisation) --------------------
- **:358** — extern ECORE_API int ps_da_sun_log;  // [DA_PORT] замер по каскадам солнца
- **:359** — extern ECORE_API int ps_da_sun_only; // [DA_PORT] изолировать один каскад солнца

### `Layers/xrRender/xr_effgamma.cpp`

- **:94** — On Windows this call is a no-op that still reports success: the OS has ignored

### `Layers/xrRender/xr_effgamma.h`

- **:8** — True only while the hardware gamma ramp is genuinely in effect, i.e. exclusive fullscreen

### `Layers/xrRenderDX11/Blender_Recorder_R3.cpp`

- **:150** — Отметки по программам: отказ в сборке шейдера не оставляет ни стека, ни сообщения,
- **:152** — Msg("*       [DA_PORT] сборка ps[%s]", _ps);
- **:157** — Msg("*       [DA_PORT] сборка vs[%s]", _vs);
- **:159** — Msg("*       [DA_PORT] сборка gs[%s]", _gs ? _gs : "null");
- **:161** — Msg("*       [DA_PORT] программы собраны");

### `Layers/xrRenderDX11/StateManager/dx11SamplerStateCache.cpp`

- **:38** — rec.m_desc = desc; // [DA_PORT] see StateRecord: spares FindState a GetDesc per lookup
- **:88** — The device is going away, so nothing is bound any more. Leaving the counts behind
- **:109** — Hand D3D this shader's slots plus the tail the previous bind used, so the leftovers
- **:191** — Keep the cached description AND the hash in step with the state we just rebuilt.
- **:219** — See SetMaxAnisotropy: description and hash must follow the rebuilt state.

### `Layers/xrRenderDX11/StateManager/dx11SamplerStateCache.h`

- **:43** — Keep the description alongside the state. FindState used to ask D3D for it
- **:54** — Returns how many slots actually have to be handed to D3D: the ones this shader uses,
- **:76** — How many sampler slots the previous bind occupied, per context and shader stage.

### `Layers/xrRenderDX11/dx11ConstantBuffer.cpp`

- **:6** — Global scope on purpose: the cvar lives in xrEngine, not in the render namespace.
- **:57** — xr_malloc does not zero memory. Constant buffers with the same layout are
- **:67** — Shadow of the last upload; see r__cb_skip_redundant. Zeroed for the same reason as
- **:114** — Access() flags the buffer on any write, whether or not the value differs (the
- **:141** — Remember exactly what went to the GPU so the comparison above has something

### `Layers/xrRenderDX11/dx11ConstantBuffer.h`

- **:47** — Byte-for-byte copy of what was last handed to the GPU, so Flush can tell a real

### `Layers/xrRenderDX11/dx11DetailManager_VS.cpp`

- **:7** — Vegetation sway scale, 0 = frozen. Defined in the engine; declared outside the namespace
- **:49** — Remember where the sway was before this frame advances it - see DetailManager.h.
- **:61** — Wrapped to one turn. The phase is fed to periodic functions (calc_cyclic in the
- **:87** — r__wind_scale: 0 freezes the grass. Applied to the previous-frame copies too, so the
- **:91** — r__wind_shadow 0: grass stands still in the SHADOW pass while swaying on screen.
- **:113** — The same directions one frame back, built exactly the same way.
- **:166** — static shared_str strWaveOld("wave_old"); // [DA_PORT] motion vectors
- **:167** — static shared_str strDir2DOld("dir2D_old"); // [DA_PORT] motion vectors
- **:205** — cmd_list.set_c(strWaveOld, wave_old); // [DA_PORT]
- **:206** — cmd_list.set_c(strDir2DOld, wind_old); // [DA_PORT]

### `Layers/xrRenderDX11/dx11HW.cpp`

- **:3** — Defined in the engine (xr_ioc_cmd.cpp).
- **:35** — Полноэкранным режимом распоряжается ТОЛЬКО SDL — отсюда и пустота в этих двух функциях.
- **:132** — The validation layer, on a console variable and available in Release.
- **:143** — Msg("* [DA_PORT] D3D11 validation layer requested (r__d3d_debug)");
- **:197** — The validation layer is refused outright when the "Graphics Tools" Windows feature is
- **:202** — Msg("! [DA_PORT] device creation failed WITH the validation layer - is the 'Graphics Tools' "
- **:208** — Drain the validation layer into our own log.
- **:227** — Msg("* [DA_PORT] validation layer active, messages will appear in this log");
- **:390** — Цепочка всегда оконная: полноэкранный режим держит SDL, см. CHW::OnAppActivate.
- **:448** — Цепочка всегда оконная (пустое описание полноэкранного = nullptr): полноэкранный
- **:493** — Условие теперь не срабатывает никогда — цепочка всегда оконная, полноэкранным
- **:507** — Отвязать всё от конвейера и слить очередь уничтожения ПЕРЕД тем, как считать живые
- **:533** — Поимённый список живых объектов D3D при выходе.
- **:554** — Msg("* [DA_PORT] живые объекты D3D на выходе:");
- **:557** — Перепись уходит в очередь сообщений слоя отладки, а не в наш лог, и очередь
- **:583** — Msg("* [DA_PORT] перепись: потолок очереди %llu",
- **:597** — Msg("! [DA_PORT] перепись(%s): очередь сообщений недоступна, список ушёл в отладочный вывод",
- **:606** — Msg("! [DA_PORT] перепись(%s): очередь вернула %llu сообщений, обрезаю", stage,
- **:611** — Msg("* [DA_PORT] перепись(%s): строк %u, выброшено очередью %u", stage, (u32)count, (u32)dropped);
- **:636** — Msg("* [DA_PORT] перепись окончена");
- **:641** — Msg("! [DA_PORT] ID3D11Debug недоступен: слой отладки DirectX не установлен");
- **:656** — Ни SetFullscreenState, ни ResizeTarget здесь больше нет.
- **:770** — Pull whatever the validation layer has to say into the engine log, once per frame.

### `Layers/xrRenderDX11/dx11R_Backend_Runtime.h`

- **:828** — Проверка на пустоту обязательна. ID3DUserDefinedAnnotation — часть рантайма D3D11.1,

### `Layers/xrRenderDX11/dx11ResourceManager_Resources.cpp`

- **:189** — Look in the other contexts before giving up. Buffers are filed under the context that

### `Layers/xrRenderDX11/dx11ResourceManager_Scripting.cpp`

- **:235** — Разметка прохода: именно здесь компилируются программы, и именно здесь отказ не
- **:578** — Пошаговая разметка компиляции элемента. Отказ на этом пути не оставляет ни стека, ни
- **:594** — Ошибка в скрипте шейдера больше не убивает игру молча.
- **:615** — Msg("! [DA_PORT] шейдер [%s], элемент [%s]: ОШИБКА В СКРИПТЕ: %s", namesp, name, e.what());
- **:619** — Msg("! [DA_PORT] шейдер [%s], элемент [%s]: неизвестное исключение", namesp, name);

### `Layers/xrRenderDX11/dx11SH_Texture.cpp`

- **:44** — Releases every view this texture owns.
- **:100** — Drop the views of the outgoing surface before it goes. Doing this piecemeal further
- **:170** — No _RELEASE here any more - release_surface_views() above already emptied
- **:555** — Основное представление текстуры (m_pSRView) здесь не освобождалось ВООБЩЕ, хотя

### `Layers/xrRenderDX11/dx11Texture.cpp`

- **:135** — Об одной и той же пропаже сообщаем ОДИН раз за сеанс.

### `Layers/xrRenderPC_R4/da_dlss.cpp`

- **:5** — extern ENGINE_API void da_upscaler_mark_failed(pcstr who); // [DA_PORT]
- **:115** — ⚠️ БЕЗ УСТРОЙСТВА ЭТОТ ОТВЕТ ВСЕГДА «НЕТ».
- **:174** — Знаки те же, что у FSR 2. Установлено НАБЛЮДЕНИЕМ, и иначе было нельзя.
- **:225** — Говорим это громко и полностью, потому что провал здесь МАСКИРУЕТСЯ ПОД УСПЕХ.
- **:234** — Тот же случай, что у FSR 3 на R9 290: без реконструкции джиттер трясёт экран.

### `Layers/xrRenderPC_R4/da_dlss.h`

- **:3** — NVIDIA DLSS — третий временной апскейлер, рядом с FSR 2 и XeSS.
- **:64** — Деструктор НАМЕРЕННО ничего не сворачивает.
- **:93** — Перевод векторов из NDC в пиксели. ОДНО место на весь порт: замер и отрисовка обязаны

### `Layers/xrRenderPC_R4/da_fsr2.cpp`

- **:43** — ⚠️ HIGH_DYNAMIC_RANGE снят: кадр к этому месту УЖЕ тонемаплен.
- **:102** — Reactive mask. Alpha-tested foliage is marked here (deffer_base_aref_*.ps): a branch
- **:107** — Transparency-and-composition mask. Fed the SAME buffer as the reactive one, on a
- **:156** — Step 1 has no counterpart in the library - AMD's highest mode is 1.5x. It exists so

### `Layers/xrRenderPC_R4/da_fsr2.h`

- **:3** — AMD FidelityFX Super Resolution 2.
- **:38** — ID3D11Resource* reactive{}; // [DA_PORT] 1 where the history must not be trusted
- **:39** — ID3D11Resource* tandc{};    // [DA_PORT] transparency-and-composition, null to disable

### `Layers/xrRenderPC_R4/da_fsr3.cpp`

- **:5** — extern ENGINE_API void da_upscaler_mark_failed(pcstr who); // [DA_PORT]
- **:11** — Type-free entry points, see da_fsr3_api.h for why they exist.
- **:36** — The three buffers FSR 3 shares with whatever runs after it. Formats and usage are taken
- **:113** — ⚠️ HIGH_DYNAMIC_RANGE снят — ровно по той же причине, что и у FSR 2 (см. da_fsr2.cpp,
- **:126** — DEBUG_CHECKING только в отладочной сборке, как у FSR 2. Бэкенд отвечает на неудачу
- **:137** — Гасим апскейлер сразу: контекст не создался - значит не создастся и дальше, а

### `Layers/xrRenderPC_R4/da_fsr3.h`

- **:3** — AMD FidelityFX Super Resolution 3 — upscaler only, no frame generation.

### `Layers/xrRenderPC_R4/da_fsr3_api.h`

- **:3** — The few FSR 3 entry points the rest of the renderer needs, declared WITHOUT dragging in

### `Layers/xrRenderPC_R4/da_gpu_timer.cpp`

- **:6** — Switched on with "da_gpu_log <frames>".
- **:13** — Копилка для замера кэша теневых карт (da_shadow_test).

### `Layers/xrRenderPC_R4/da_gpu_timer.h`

- **:3** — GPU timing per render phase.

### `Layers/xrRenderPC_R4/da_upscaler.h`

- **:3** — Common interface for temporal upscalers: FSR 2, XeSS, DLSS.

### `Layers/xrRenderPC_R4/da_win7_compat.cpp`

- **:3** — Совместимость с Windows 7: убираем импорт CreateFile2.

### `Layers/xrRenderPC_R4/da_xess.cpp`

- **:5** — extern ENGINE_API void da_upscaler_mark_failed(pcstr who); // [DA_PORT]
- **:53** — XESS_RESULT_ERROR_UNSUPPORTED_DEVICE (-1) is the expected answer on most machines,
- **:82** — LDR_INPUT_COLOR: кадр к моменту апскейла УЖЕ тонемаплен (combine_1 сводит сцену в
- **:94** — Тот же случай, что у FSR 3 на R9 290: без реконструкции джиттер трясёт экран.
- **:104** — ⚠️ Это ИДЕАЛЬНЫЙ вход по таблице Intel, а НЕ тот размер, в котором рисуется сцена.

### `Layers/xrRenderPC_R4/da_xess.h`

- **:3** — Intel XeSS — the second temporal upscaler, alongside FSR 2.
- **:43** — ID3D11Resource* reactive{}; // [DA_PORT] Intel calls it the responsive pixel mask

### `Layers/xrRenderPC_R4/r4_rendertarget.h`

- **:59** — ref_rt rt_SSR; // screen-space reflections output (R4)
- **:60** — ref_rt rt_TAA_history; // previous frame, kept for temporal reprojection (R4)
- **:61** — ref_rt rt_TAA_scratch; // second output of the resolve — the copy that goes into the history
- **:62** — ref_rt rt_TAA_out;     // first output of the resolve — the copy that goes back on screen
- **:63** — ref_rt rt_Velocity;    // screen-space motion vectors, RG16F (R4). Groundwork for FSR 2.
- **:64** — Velocity after the guard pass - see phase_velocity_guard.
- **:66** — ref_rt rt_Reactive;    // reactive mask for the upscalers, R8 — 1 where history must not be trusted
- **:67** — Working pair for the reactive widening - see phase_reactive.
- **:70** — u32 da_velocity_cleared_frame{}; // phase_scene_begin runs twice per frame when the scene is split
- **:71** — Same guard for the position clear - see phase_scene_begin.
- **:73** — ref_rt rt_FSR2_out;    // FSR 2 output, at OUTPUT resolution and writable from a compute shader
- **:157** — ref_shader s_taa; // temporal AA resolve
- **:158** — ref_shader s_velocity_guard; // damps vegetation motion next to glossy surfaces
- **:159** — ref_shader s_xess_mv; // [DA_PORT] знаковая копия векторов движения для XeSS, см. da_xess_mv.s
- **:160** — ref_shader s_puddle_refl; // [DA_PORT] отражения в лужах, полноэкранный проход
- **:161** — ref_shader s_reactive; // marks pixels around genuinely moving objects as reactive
- **:162** — ref_shader s_reactive_dilate_h; // widens that mark, horizontally
- **:163** — ref_shader s_reactive_dilate_v; // and vertically, folding in the mask the G-buffer left
- **:164** — ref_shader s_reactive_emissive; // [DA_PORT] метка самосветящейся геометрии, см. phase_reactive_emissive
- **:165** — ref_shader s_sky_velocity; // the sky writes no motion vectors of its own
- **:251** — ID3DDepthStencilView* zb); // [DA_PORT] reactive mask as a fourth target
- **:252** — Convenience form taking the depth buffer as a render target, mirroring the three-target
- **:292** — void phase_taa(); // temporal AA resolve
- **:295** — void phase_da_puddle_refl(); // [DA_PORT] // [DA_PORT] damp vegetation motion next to glossy surfaces
- **:296** — void phase_reactive(); // [DA_PORT] widen the reactive mask around moving objects, against ghosting
- **:297** — Помечает свечение в маске реактивности. Зовётся сразу после отрисовки свечения, а
- **:300** — То же для прозрачной геометрии, из середины phase_combine. Своя ручка, по умолчанию 0.
- **:308** — Срез G-буфера по строке через прицел: глубина, цвет, вектор и маска ОДНОГО пикселя
- **:311** — void da_light_watch(); // [DA_PORT] покадровое наблюдение за светом в пикселе, см. .cpp
- **:313** — Замер кэша теневых карт: фаза 1 — только время, фаза 2 — дрожание по всему экрану.
- **:317** — void phase_sky_velocity(); // [DA_PORT] motion vectors for the sky, which no shader writes
- **:318** — bool phase_fsr3(); // [DA_PORT] FSR 3 upscaler, same slot in the frame
- **:319** — bool phase_dlss(); // [DA_PORT] NVIDIA DLSS, same slot in the frame
- **:320** — bool phase_xess(); // [DA_PORT] Intel XeSS, same slot in the frame // FSR 2 upscale; false when it did not run, so the caller can fall back
- **:321** — Знаковая копия векторов движения для XeSS; пустая ссылка = отдавать буфер как есть.

### `Layers/xrRenderPC_R4/r4_rendertarget_accum_direct.cpp`

- **:231** — Разрешение РЕНДЕРА, а не окна. Дальше по файлу и в phase_combine_volumetric — то же
- **:578** — float scale_X = float(Device.dwRenderWidth) / float(TEX_jitter); // [DA_PORT] см. accum_direct
- **:602** — ⛔ [DA_PORT] ПРАВКА ОТКАЧЕНА — она давала видимый дефект. Возвращено поведение
- **:1044** — float scale_X = float(Device.dwRenderWidth) / float(TEX_jitter); // [DA_PORT] см. accum_direct
- **:1155** — float scale_X = float(Device.dwRenderWidth) / float(TEX_jitter); // [DA_PORT] см. accum_direct

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_combine.cpp`

- **:6** — Разовый срез G-буфера (xr_ioc_cmd.cpp). Объявляем ВНЕ пространства имён: extern внутри
- **:9** — extern ENGINE_API int ps_r__light_watch; // [DA_PORT] сколько кадров подряд писать свет под перекрестьем
- **:10** — extern ENGINE_API int ps_r__shadow_test;  // [DA_PORT] замер кэша теневых карт, см. da_shadow_test_frame
- **:16** — Defined in dx11HW.cpp - see the note there on why the layer needs draining by hand.
- **:212** — Visor rain-droplet ("lens water") intensity. blender_combine puts the combine_1.ps
- **:250** — Scene-grab for water screen-space reflections (SSLR).
- **:279** — Отражения в лужах — здесь и только здесь: копия кадра уже снята (её и отражаем), а
- **:289** — Пока прямой проход рисует, пусть он оставляет о себе след в трафарете: бит 0x02
- **:306** — До интерфейса: он рисуется следом и тоже оставил бы след в трафарете, а метить
- **:329** — The stand-alone full-screen SSR pass is retired: reflections are done inside Dead Air's
- **:377** — u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
- **:384** — u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
- **:499** — Temporal AA, on the assembled frame.
- **:513** — FSR 2 belongs HERE, not after phase_combine returns. phase_pp is called from inside
- **:523** — Снимок среза — ровно здесь, перед апскейлером: кадр уже собран (в rt_Color лежит
- **:526** — Значение — ЗАДЕРЖКА В КАДРАХ, а не просто «включить». 1 (как было) снимает срез
- **:532** — Движение камеры за кадр — считаем ВСЕГДА, читает проба. См. пояснение там же.
- **:557** — Отсчёт идёт ТОЛЬКО по кадрам, в которых камера действительно движется.
- **:566** — Наблюдение за светом — здесь же и по той же причине: накопитель ещё не разобран
- **:574** — Замер кэша теней. Первая половина отведённых кадров идёт БЕЗ чтений буфера —
- **:577** — Замер кэша теней — ВКЛЮЧАЕМЫЙ: `da_shadow_test 1` пошёл, `0` закончил и выдал
- **:597** — Автостоп через 60 секунд: прогоны обязаны быть одной длины.
- **:626** — phase_sky_velocity(); // [DA_PORT] fill the sky in before anything reads the velocity buffer
- **:627** — phase_reactive(); // [DA_PORT] reads the honest velocity, so it goes before the guard touches it
- **:628** — phase_velocity_guard(); // [DA_PORT] must run BEFORE any upscaler reads the buffer
- **:630** — phase_fsr3(); // [DA_PORT]
- **:632** — phase_dlss(); // [DA_PORT] NVIDIA DLSS, тот же слот кадра; включён всегда только один
- **:633** — da_d3d_debug_drain(); // [DA_PORT] validation layer output, see dx11HW.cpp // [DA_PORT] the other upscaler; only one is ever enabled
- **:788** — Разрешение рендера, а не окна: цели rt_Generic_*_r заводятся размером

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_da_puddle_refl.cpp`

- **:3** — Ручки живут в движке (xr_ioc_cmd.cpp), объявлять их внутри namespace нельзя — имя

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_dlss.cpp`

- **:5** — Определены в движке (xr_ioc_cmd.cpp). Объявляются СНАРУЖИ пространства имён: внутри
- **:11** — extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
- **:12** — extern ENGINE_API int ps_r__dlss_reactive; // [DA_PORT] см. xr_ioc_cmd.cpp
- **:14** — extern ENGINE_API int ps_r__dlss_selftest; // [DA_PORT]
- **:23** — ---- Замер векторов движения: числа в лог вместо перебора знаков --------------------
- **:111** — Отдельно - узкая полоса по центру экрана. Только по ней можно судить о знаке X.
- **:199** — Поворот камеры за кадр, пересчитанный В ПИКСЕЛИ.
- **:318** — Сначала отвязать цели — та же ловушка, из-за которой FSR 2 реконструировал чёрный
- **:342** — Маска отключается ручкой: у NVIDIA этот параметр значит не то же, что у AMD,
- **:353** — Дрожание — как у FSR 2, знак проверен в игре. Шейдеры применяют его с
- **:358** — Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
- **:362** — Замер идёт ДО отрисовки: буфер скоростей уже заполнен, а конвейер ещё не занят

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_fsr2.cpp`

- **:5** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
- **:11** — extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
- **:13** — extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]
- **:17** — Set only when the upscaler actually produced a frame. The post-process pass keys off
- **:33** — Release the outputs first. Combine leaves rt_Color bound as the render target and
- **:42** — The real depth buffer, not rt_Position. That one holds eye-space POSITIONS: four
- **:62** — Same buffer, second input - see da_fsr2.cpp.
- **:81** — Device.fFOV is the HORIZONTAL angle — the projection is built from it together with
- **:90** — Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
- **:94** — The library's own RCAS pass, on the same slider FSR 1.0 uses. Reconstruction from a
- **:106** — Три провала подряд — гасим джиттер и объясняем в логе. Иначе на экран

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_fsr3.cpp`

- **:5** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
- **:12** — extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
- **:14** — extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]
- **:18** — Shared with FSR 2 and XeSS on purpose: only one upscaler reconstructs a given frame, and
- **:31** — r__fsr3_debug 1: everything exists, nothing runs. See xr_ioc_cmd.cpp for what it splits.
- **:78** — Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
- **:89** — Три провала подряд — гасим джиттер и объясняем в логе. Иначе на экран
- **:93** — Put the pipeline back the way it was found.

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_reactive.cpp`

- **:3** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
- **:12** — extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]
- **:15** — extern ENGINE_API int ps_r__probe_center; // [DA_PORT] 0 - ярчайший пиксель, 1 - перекрестье
- **:16** — extern ENGINE_API bool g_da_jitter_suppress; // [DA_PORT] подавление джиттера на время чтения экрана
- **:20** — Смещение и поворот камеры ЗА ПОСЛЕДНИЙ КАДР. Считаются покадрово в phase_combine,
- **:26** — extern ENGINE_API float ps_r__reactive_emissive; // [DA_PORT] метка свечения, см. phase_reactive_emissive
- **:27** — extern ENGINE_API float ps_r__reactive_transparent; // [DA_PORT] то же для прозрачной геометрии
- **:33** — ---- Self-test: read the buffers back and put numbers in the log --------------------
- **:153** — ---- Срез G-буфера по строке через прицел -------------------------------------------
- **:239** — Найти в кадре самый яркий пиксель. Прицеливаться руками оказалось невозможно: предмет в
- **:302** — Покадровое наблюдение за НАКОПЛЕННЫМ СВЕТОМ в пикселе под перекрестьем.
- **:317** — Один пиксель из цели — копированием ОДНОГО ПИКСЕЛЯ, а не всей цели.
- **:424** — Целая строка яркости во всю ширину экрана, одним копированием. Сумма r+g+b на пиксель.
- **:525** — ---- Замер кэша теневых карт: ВЕСЬ ЭКРАН, все величины разом -----------------------
- **:662** — Чтение экрана — раз в десять кадров, и такие кадры в статистику времени НЕ идут.
- **:688** — Сравниваем ТОЛЬКО когда между снимками игрок почти не двигался.
- **:837** — Кроме пикселя под перекрестьем — вся строка во всю ширину экрана.
- **:913** — Центр среза выбирается ручкой r__probe_center: по умолчанию самый яркий пиксель
- **:924** — Двигалась ли камера в момент снимка — печатаем прямо здесь.
- **:935** — Сколько ПИКСЕЛЕЙ должен был проехать неподвижный мир за этот кадр — грубая оценка
- **:955** — Вектора печатаем в пикселях, а не в долях экрана. Множитель тот же, что мы отдаём
- **:970** — Карта силуэта: одна строка могла попасть в ровный участок кромки, а зубцы идут с шагом
- **:1022** — Через общий список, а не перечислением: этот if уже дважды забывали обновить при
- **:1029** — Everything this pass works in is travel PER FRAME, so every setting it takes is frame
- **:1054** — Inputs measured before the draw touches anything, output after - see da_probe.
- **:1111** — Fill the target with a value by hand first, then draw over it. The two readings that
- **:1129** — The same draw again per debug mode, each writing one ingredient AS THE SHADER SEES
- **:1164** — ---- Метка свечения в маске реактивности ---------------------------------------------

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_sky_velocity.cpp`

- **:7** — extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]
- **:22** — Через общий список: пропущенный здесь бэкенд оставляет небо без векторов, а ноль

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_taa.cpp`

- **:3** — Определена в движке (xr_ioc_cmd.cpp), объявляется ВНЕ пространства имён.
- **:6** — temporal anti-aliasing resolve. Blends the previous frame (rt_TAA_history) into the current one
- **:18** — extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]
- **:19** — extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
- **:30** — Not alongside an upscaler. Each of them is a temporal resolve in its own right, working
- **:39** — Через общий список: забытый здесь бэкенд оставляет ДВА временных фильтра на кадре.
- **:50** — Склейка: истории нет, смешивать не с чем.

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_velocity_guard.cpp`

- **:3** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
- **:22** — One decision for the whole frame: is the camera moving?
- **:42** — Recalibrated. These thresholds were driven down and down against jitter that this pass

### `Layers/xrRenderPC_R4/r4_rendertarget_phase_xess.cpp`

- **:5** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
- **:9** — extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
- **:11** — extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]
- **:13** — extern ENGINE_API int ps_r__xess_mv_sign; // [DA_PORT] см. da_xess_mv.s
- **:17** — Знаковая копия буфера скоростей для XeSS.
- **:69** — Знаковая копия векторов — ДО освобождения целей ниже: проход сам ставит свою цель
- **:73** — Release the outputs first — same trap that made FSR 2 reconstruct a black frame and
- **:113** — Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
- **:121** — Три провала подряд — гасим джиттер и объясняем в логе. Иначе на экран

### `Layers/xrRenderPC_R4/r4_rendertarget_u_set_rt.cpp`

- **:48** — Slot 3 must be released here. The scene pass binds four targets (the reactive mask is
- **:55** — NOTE: deliberately does NOT set the viewport, unlike CBackend::set_pass_targets.
- **:65** — Four colour targets: the G-buffer plus motion vectors plus the reactive mask. Same body as
- **:118** — NOTE: deliberately does NOT set the viewport, unlike CBackend::set_pass_targets.
- **:140** — Unlike the ref_rt overloads (see CBackend::set_pass_targets), this raw-view path never

### `Layers/xrRenderPC_R4/r4_shaders.cpp`

- **:5** — #include "xrCore/Threading/ParallelFor.hpp" // [DA_PORT] прогрев кэша шейдеров, см. da_shader_warmup
- **:43** — Tell a mod author when their shader will not work with the upscalers.
- **:80** — Name-gated, and the first attempt without it was wrong.
- **:97** — Msg("! [DA_PORT] shader [%s] draws into the G-buffer but writes no motion vector "
- **:106** — Второй молчаливый отказ той же природы: ВЕРШИННЫЙ шейдер без джиттера.
- **:142** — Msg("! [DA_PORT] шейдер [%s] рисует геометрию, но не применяет джиттер "
- **:325** — Режим прогрева: компилировать в кэш, не создавая объект на устройстве.
- **:335** — Путь исходника текущей компиляции. Ставится в ShaderResourceTraits::CreateShader.
- **:349** — Подменённый список дефайнов БЛЕНДЕРА на время воспроизведения манифеста.
- **:358** — Манифест прогрева: что и с какими макросами компилировать.
- **:450** — При воспроизведении манифеста берём записанные дефайны блендера, иначе текущие.
- **:614** — Motion vectors as an extra G-buffer output — see f_deffer in common_iostructs.h. Must
- **:617** — appendShaderOption(o.velocity_debug_ids, "DA_DEBUG_SHADER_IDS", "1"); // [DA_PORT] see r2.cpp
- **:637** — Enable Dead Air's visor rain-droplet ("lens water") effect that lives in the
- **:646** — Enable Dead Air's OWN screen-space reflections on water. DA's archive water.ps/waterd.ps
- **:682** — Do NOT inject H_*/L_*/PIXEL_SIZE/eye_direction macros here!
- **:782** — Путь записи внутри кэша — один и тот же и для личного кэша игрока, и для
- **:798** — Достать шейдер из готовой записи кэша. Вынесено в лямбду, потому что мест теперь два.
- **:830** — Сначала личный кэш игрока, потом поставляемый с игрой.
- **:847** — В режиме прогрева объект шейдера не нужен — достаточно, чтобы запись легла в кэш.
- **:866** — Запись в манифест — здесь, а не в ветке компиляции: нам нужен ПОЛНЫЙ перечень того,
- **:886** — Было `pErrorBuf = nullptr` — указатель просто терялся, а сам буфер
- **:901** — Запись кэша — под общим замком, потому что прогрев компилирует в несколько
- **:922** — В прогреве останавливаемся здесь: запись в кэш легла, а объект на устройстве
- **:937** — Оба блоба D3D не освобождались ВООБЩЕ — ни при удаче, ни при ошибке. Течёт по
- **:948** — Параллельный прогрев кэша шейдеров.
- **:963** — Сохранить манифест прогрева. Кладётся рядом с личным кэшем; в релизный пакет кладём его
- **:977** — Сливаем с тем, что уже лежит, а не затираем.
- **:1129** — Вся работа — через обычную shader_compile в режиме «только в кэш». Своей записи

### `Layers/xrRenderPC_R4/xrRender_R4.cpp`

- **:38** — Trim the exposed mode list to 2 clear choices instead of 4 near-duplicate

### `Layers/xrRender_R2/r2.cpp`

- **:21** — Defined in the engine (xr_ioc_cmd.cpp). Declared outside the namespace on purpose: an
- **:28** — "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
- **:116** — These describe the G-BUFFER, not the window: the deferred shaders rebuild eye-space
- **:126** — Previous frame's view-projection, for temporal reprojection (TAA, temporal SSR).
- **:146** — World-view-projection of the PREVIOUS frame, for motion vectors. Static level geometry is
- **:155** — Current frame's view-projection WITHOUT the temporal-AA jitter. Motion vectors have to be
- **:163** — The projection jitter, for shaders to apply themselves.
- **:177** — Any temporal upscaler, not FSR 2 alone - see da_upscaler_active(). While this named
- **:180** — Сдвиг выдаётся ВСЕГДА, когда джиттер вообще включён — и для апскейлеров, и для
- **:186** — Converted to clip space HERE, from the pixel offset the upscaler is handed, so
- **:197** — Вес растительности растёт вместе с длиной кадра.
- **:224** — Detail-bump damping weights, see xr_ioc_cmd.cpp for what they are for. An ordinary pass
- **:227** — Gloss-driven reactive mask, see xr_ioc_cmd.cpp. x = weight, y = gloss threshold.
- **:237** — Motion-driven reactivity, see da_motion_reactive in common_functions.h.
- **:255** — Motion-vector camera matrices WITHOUT any world part, for geometry that is already in world
- **:276** — Same reasoning: this is the G-buffer's size, used for texel-exact fetches.
- **:634** — Motion vectors. R4 only — the extra target and the shader option are DX11-side, and R2
- **:639** — Every consumer of the velocity buffer has to be listed here, FSR 3 included. Missing
- **:645** — Через da_upscaler_active(), а не перечислением. Перечисление тут и подвело: строка
- **:656** — Mode 3 turns the velocity buffer into a map of WHICH SHADER drew each pixel: every
- **:660** — The debug map and an upscaler must not run together: FSR 2 reads the very buffer the
- **:666** — if (o.velocity_debug_ids && da_upscaler_active()) // [DA_PORT] любой апскейлер, не FSR 2 один
- **:667** — Msg("! [DA_PORT] r__motion_vectors 3 with an upscaler enabled: it is being fed shader "
- **:722** — Resources->RegisterConstantSetup("m_prev_VP", &binder_prev_vp); // [DA_PORT] temporal reprojection
- **:723** — Resources->RegisterConstantSetup("m_taa_jitter", &binder_taa_jitter); // [DA_PORT] jitter for shaders
- **:724** — Resources->RegisterConstantSetup("m_VP_nojit_ws", &binder_vp_nojit_ws); // [DA_PORT] world-space geometry
- **:725** — Resources->RegisterConstantSetup("m_VP_old_ws", &binder_vp_old_ws); // [DA_PORT] world-space geometry
- **:726** — Resources->RegisterConstantSetup("da_detail_fix", &binder_detail_fix); // [DA_PORT] detail-bump damping
- **:727** — Resources->RegisterConstantSetup("da_reactive_motion", &binder_reactive_motion); // [DA_PORT]
- **:728** — Resources->RegisterConstantSetup("da_gloss_reactive", &binder_gloss_reactive); // [DA_PORT] gloss opts out of history
- **:733** — Msg("* [DA_PORT] create: before CRenderTarget"); FlushLog();
- **:735** — Msg("* [DA_PORT] create: after CRenderTarget"); FlushLog();
- **:740** — Msg("* [DA_PORT] create: after Models/PSLibrary/HWOCC"); FlushLog();
- **:744** — Msg("* [DA_PORT] create: after q_sync_point"); FlushLog();
- **:748** — Msg("* [DA_PORT] create: before FluidManager.Initialize"); FlushLog();
- **:752** — Msg("* [DA_PORT] create: after FluidManager"); FlushLog();
- **:755** — g_da_gpu_timer.create(); // [DA_PORT] per-phase GPU timing, see da_gpu_timer.h
- **:757** — Msg("* [DA_PORT] create: DONE"); FlushLog();
- **:763** — У create() не было пары, и 52 объекта запросов D3D11 (4 кольца по 13) не отпускались

### `Layers/xrRender_R2/r2.h`

- **:151** — Кэш теневых карт солнца. Разбор — в render_phase_sun.cpp, у da_smap_should_render.
- **:271** — Motion vectors written as an extra G-buffer target. Latched once when the renderer
- **:275** — u32 velocity_debug_ids : 1; // [DA_PORT] mode 3: shaders stamp their identity instead of motion
- **:377** — Объёмы объёмного тумана течь не должны, а текли — по одному набору на каждую
- **:479** — Параллельный прогрев кэша шейдеров, см. пояснение у реализации в r4_shaders.cpp.
- **:481** — void da_shader_manifest_save(); // [DA_PORT] снять манифест прогрева, см. r4_shaders.cpp

### `Layers/xrRender_R2/r2_R_calculate.cpp`

- **:72** — The OUTPUT size, not the render target's. Every level-of-detail threshold below is

### `Layers/xrRender_R2/r2_R_render.cpp`

- **:3** — GPU timing per phase - see da_gpu_timer.h. R4 only; the GL branch has no D3D11 queries.
- **:25** — Defined in the engine (device.cpp). Declared out here, not inside the function that uses it:
- **:29** — Разовый дамп очередей, рисуемых после G-буфера, см. r__emissive_probe.
- **:34** — Лампы, у которых бюджет теней снял флаг bShadow на этот кадр. Флаг - постоянное свойство
- **:58** — The composite path below draws the menu into rt_Generic_0 — but the UI lays itself out in
- **:84** — Target->u_setrt(RCache, Device.dwWidth, Device.dwHeight, Target->get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
- **:144** — Target->u_setrt(RCache, Device.dwWidth, Device.dwHeight, Target->get_base_rt(), 0, 0, nullptr); // [DA_PORT] no depth on the back buffer
- **:267** — Keep only the most prominent lights casting shadows; demote the rest.
- **:281** — Привилегированные источники (сейчас это фонарь в руках игрока, см.
- **:293** — Бюджет забивается БЛИЖАЙШИМИ источниками, и меряется расстояние не до самой
- **:338** — ⚠️ [DA_PORT] Флаг ОБЯЗАН сняться. Прежний комментарий здесь утверждал обратное -
- **:522** — К общему биту 0x01 добавляется свой, 0x02, когда включена метка свечения. Иначе
- **:545** — Разовый дамп: в какой очереди лежит то, что мерцает. Всё перечисленное рисуется
- **:568** — Пока трафарет ещё помнит, где легло свечение: дальше идёт свет, а он переписывает
- **:588** — Свет отрисован - возвращаем флаг тем, у кого его занял бюджет теней.
- **:600** — FSR 2 used to be dispatched here, which looked like "after the frame is assembled but
- **:604** — Remember this frame's camera for the next one. Temporal effects reproject a pixel into

### `Layers/xrRender_R2/r2_loader.cpp`

- **:15** — Объявляется СНАРУЖИ пространства имён: внутри линкер искал бы символ в
- **:21** — Счётчик ссылок на устройство D3D — без слоя отладки DirectX.
- **:45** — История временных фильтров относится к прошлому уровню и переносить её некуда:
- **:49** — Отметки стадий загрузки уровня рендером.
- **:56** — Msg("* [DA_PORT] level_Load: начало | ссылок на устройство: %u", (u32)da_device_refs());
- **:67** — Msg("* [DA_PORT] level_Load: шейдеры уровня | ссылок на устройство: %u", (u32)da_device_refs());
- **:84** — The result was dereferenced unchecked: a level shader entry without a '/' wrote
- **:88** — Msg("! [DA_PORT] level shader [%s] has no '/' separator - skipped", n_sh);
- **:93** — Имя ПЕРЕД созданием, а не после. Создание шейдера уровня умеет уронить игру
- **:97** — Msg("* [DA_PORT] level_Load: шейдер %u/%u [%s] / [%s]", i, count, n_sh, n_tlist);
- **:104** — Msg("* [DA_PORT] level_Load: следы и детали | ссылок на устройство: %u", (u32)da_device_refs());
- **:111** — Msg("* [DA_PORT] level_Load: геометрия | ссылок на устройство: %u", (u32)da_device_refs());
- **:130** — Msg("* [DA_PORT] level_Load: визуалы | ссылок на устройство: %u", (u32)da_device_refs());
- **:137** — Msg("* [DA_PORT] level_Load: детальные объекты | ссылок на устройство: %u", (u32)da_device_refs());
- **:143** — Msg("* [DA_PORT] level_Load: секторы и порталы | ссылок на устройство: %u", (u32)da_device_refs());
- **:153** — Msg("* [DA_PORT] level_Load: HOM | ссылок на устройство: %u", (u32)da_device_refs());
- **:157** — Msg("* [DA_PORT] level_Load: источники света | ссылок на устройство: %u", (u32)da_device_refs());
- **:165** — Msg("* [DA_PORT] level_Load: готово | ссылок на устройство: %u", (u32)da_device_refs());
- **:169** — Манифест прогрева сохраняем и ЗДЕСЬ, а не только при выходе с уровня.
- **:183** — Освобождение объёмов объёмного тумана. Подробности — у m_fluid_volumes в r2.h.
- **:216** — Манифест прогрева сохраняем при выходе с уровня — то есть сам собой, по ходу игры.
- **:227** — До всего остального: объёмы тумана висят детьми у корней секторов, и их надо
- **:230** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "объёмы тумана", (u32)da_device_refs());
- **:234** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "HOM", (u32)da_device_refs());
- **:238** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "детали", (u32)da_device_refs());
- **:243** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "порталы", (u32)da_device_refs());
- **:249** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "контексты", (u32)da_device_refs());
- **:254** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "свет", (u32)da_device_refs());
- **:263** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "визуалы", (u32)da_device_refs());
- **:269** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "SWI", (u32)da_device_refs());
- **:295** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "буферы VB/IB", (u32)da_device_refs());
- **:305** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "детали и следы", (u32)da_device_refs());
- **:309** — Msg("* [DA_PORT] выгрузка: после \"%s\" ссылок на устройство: %u", "шейдеры", (u32)da_device_refs());
- **:310** — Msg("* [DA_PORT] выгрузка завершена, ссылок на устройство: %u", (u32)da_device_refs());
- **:640** — Запомнить владение: см. m_fluid_volumes в r2.h.

### `Layers/xrRender_R2/r2_rendertarget.cpp`

- **:13** — #include "Layers/xrRender/blenders/blender_taa.h" // temporal AA
- **:15** — #include "Layers/xrRenderPC_R4/da_fsr2.h" // FSR 2
- **:17** — #include "Layers/xrRenderPC_R4/da_dlss.h" // [DA_PORT] NVIDIA DLSS
- **:18** — #include "Layers/xrRenderPC_R4/da_fsr3_api.h" // Intel XeSS
- **:20** — extern ENGINE_API void da_upscaler_set_available(u32 mask, pcstr why_hidden); // [DA_PORT]
- **:22** — extern ENGINE_API int ps_r__dlss; // [DA_PORT]
- **:216** — Make sure the internal render resolution is current before any target is sized from it.
- **:226** — Msg("* [DA_PORT] render targets: output %ux%u, scene %ux%u", Device.dwWidth, Device.dwHeight,
- **:242** — The stock report just below is #ifdef DEBUG, so a release build never says whether
- **:247** — Msg("* [DA_PORT] MSAA: %u samples, per-sample lighting on edges only: %s", SampleCount,
- **:250** — Msg("* [DA_PORT] MSAA: off");
- **:293** — Scene targets follow the internal render resolution ("r__render_scale"); only the
- **:316** — scene-grab target for water SSLR ("$user$ssr", bound to s_image by r3\effects_water.s).
- **:365** — temporal AA buffers. They live on rt_Color, which is where the finished frame lands after
- **:380** — Motion vectors: where each pixel was on the previous frame, in screen space.
- **:390** — Reactive mask: one channel, 1 where the pixel belongs to something a temporal
- **:401** — Same format and size as the velocity buffer: the guard pass writes here and the
- **:407** — Working pair for phase_reactive. Widening happens one axis at a time - a maximum
- **:415** — FSR 2 writes its result here. Two things set it apart from every other target:
- **:422** — ---- Подрезаем список апскейлеров под железо -------------------------------
- **:475** — Intel XeSS, the alternative. Created alongside rather than instead: both are
- **:480** — Intel XeSS. Was nested inside the FSR 2 branch above, which meant it could only
- **:493** — NVIDIA DLSS. Независимо от остальных, по той же причине, что и XeSS: выбор одного
- **:511** — FSR 3 создаётся под КОНКРЕТНЫЙ размер рендера, как DLSS выше, а не под предел,
- **:795** — temporal AA resolve (R4-only). Skipped under MSAA: common.h then types s_position as
- **:801** — Script blender, unlike the TAA one - it needs no textures beyond two
- **:804** — Знаковая копия векторов для XeSS: у него, в отличие от FSR и DLSS, нет
- **:807** — Отражения в лужах: читает копию освещённого кадра, пишет поверх сцены.
- **:809** — Object-motion reactivity, see phase_reactive. Two blenders share one pixel
- **:815** — Метка свечения: не читает вообще ничего, пишет константу по трафарету.
- **:884** — see phase_pp: no depth when targeting the back buffer (sizes may differ)
- **:952** — ⭐ Контексты апскейлеров уничтожаются ЗДЕСЬ. Раньше их не уничтожал никто.

### `Layers/xrRender_R2/r2_rendertarget_phase_PP.cpp`

- **:3** — Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace: an
- **:8** — extern ENGINE_API int ps_r__upscale_show_input; // [DA_PORT] показать вход апскейлера вместо выхода
- **:9** — extern ENGINE_API int ps_r__dlss; // [DA_PORT] эти двое резкости не имеют своей
- **:144** — no depth here on purpose: this composites a full-screen quad onto the back buffer,
- **:204** — Colour grading. r__color_base_r/g/b and r2_vibrance_val existed as console variables
- **:211** — Sharpening strength for the upscale (RCAS, the second half of FSR 1.0).
- **:218** — `r__upscale_show_input 1` — на экран идёт ВХОД апскейлера, а не его выход. Сам он при
- **:227** — w = 1, когда кадр сделал апскейлер, который НЕ точит сам.
- **:239** — Gamma / brightness / contrast. These reach the screen through the hardware gamma ramp,

### `Layers/xrRender_R2/r2_rendertarget_phase_bloom.cpp`

- **:8** — ---- Why this pass forces a window-sized viewport ------------------------------------
- **:120** — see the note at the top of this file — every quad below depends on this.
- **:578** — Hand the viewport back exactly as it was — the passes after this one inherit it too.

### `Layers/xrRender_R2/r2_types.h`

- **:14** — #define     r2_RT_SSR           "$user$ssr"         // screen-space reflections (R4)
- **:15** — #define     r2_RT_taa_history   "$user$taa_history" // previous resolved frame, for temporal AA (R4)
- **:16** — #define     r2_RT_taa_scratch   "$user$taa_scratch" // un-sharpened resolve, on its way into the history
- **:17** — #define     r2_RT_taa_out       "$user$taa_out"     // sharpened resolve, on its way back into rt_Color
- **:18** — #define     r2_RT_velocity      "$user$velocity"    // per-pixel screen-space motion, for FSR 2 (R4)
- **:19** — #define     r2_RT_velocity_guard "$user$velocity_guard" // velocity after the guard pass (R4)
- **:20** — #define     r2_RT_reactive      "$user$reactive"    // reactive mask for the upscalers (R4)
- **:21** — #define     r2_RT_reactive_scratch "$user$reactive_scratch" // reactive, first axis of the dilate (R4)
- **:22** — #define     r2_RT_reactive_scratch2 "$user$reactive_scratch2" // reactive, motion in and result out (R4)
- **:23** — #define     r2_RT_fsr2_out      "$user$fsr2_out"    // FSR 2 result, at OUTPUT resolution (R4)

### `Layers/xrRender_R2/r3_rendertarget_mark_msaa_edges.cpp`

- **:38** — scene-space pass: viewport follows the scene targets, not the window

### `Layers/xrRender_R2/r3_rendertarget_phase_occq.cpp`

- **:8** — scene-space pass: viewport follows the scene targets, not the window
- **:11** — scene-space pass: viewport follows the scene targets, not the window

### `Layers/xrRender_R2/r3_rendertarget_phase_scene.cpp`

- **:25** — This is the G-buffer pass — the viewport has to match the scene targets, not the
- **:48** — same reasoning: depth here is the scene depth, so the viewport follows the scene
- **:62** — With motion vectors on, rt_Velocity is bound as the last colour target. Only the
- **:72** — Clear the position target too, once per frame, whatever else is switched on.
- **:86** — REVERTED - clearing it here made every model vanish: NPCs, the weapon in hand, the actor.
- **:109** — Clear the velocity target ONCE PER FRAME. Pixels the G-buffer never covers — the sky

### `Layers/xrRender_R2/render_phase_sun.cpp`

- **:49** — ---- Кэш теневых карт солнца ------------------------------------------------------
- **:81** — Умолчание 100 мс («средняя точность»), а не 0. Включено после проверки ГЛАЗАМИ: сравнение
- **:86** — u32, а не int, потому что настройку показывает список в меню (CCC_Token хранит u32).
- **:179** — Решение о кэше принимается ОДНО НА ВСЕ кэшируемые каскады, а не отдельно на каждый.
- **:342** — Решение о кэше принимается ЗДЕСЬ — до того, как матрица уйдёт дальше.
- **:378** — Замер по каскадам солнца, крутилка da_sun_log N (N кадров подряд).
- **:400** — Каскад берётся из кэша — отбор геометрии не нужен вовсе.
- **:455** — Пишем КАЖДЫЙ кадр и сами помечаем, что изменилось с предыдущего.
- **:529** — Тот же пропуск, что и в calculate: карта в слоте уже готова, трогать её
- **:607** — da_sun_only N — накапливать свет ТОЛЬКО от каскада N (1..3), 0 = как обычно.


## Движок и устройство

*23 файл(ов), 230 правк(и)*


### `xrEngine/CameraManager.cpp`

- **:18** — TAA: defined in xr_ioc_cmd.cpp / device.cpp — see the jitter block in ApplyDevice below.
- **:21** — extern ENGINE_API bool g_da_jitter_suppress; // [DA_PORT] см. xr_ioc_cmd.cpp
- **:23** — "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
- **:345** — TAA projection jitter. Reprojecting the previous frame only removes temporal noise; the
- **:351** — Пиксельный джиттер сбрасывается ЗДЕСЬ ЖЕ. Раньше он только присваивался внутри
- **:363** — Every temporal upscaler needs the jitter just as much as our own temporal AA does — it
- **:375** — Generated exactly the way FSR 2 specifies, because it has to undo this offset and
- **:412** — Джиттер НЕ идёт в матрицу проекции ни в одном режиме — его накладывают сами

### `xrEngine/CustomHUD.h`

- **:20** — dedicated bit for the "hud_draw_map" compat alias (see console_commands.cpp) - it used
- **:25** — Dead Air's own flag, gating the bottom-left readout: health bar, stamina bar, ammo counts,

### `xrEngine/Device.cpp`

- **:26** — u32 ps_fps_limit = 1000; // [DA_PORT] token cvar now; 1000 == unlimited
- **:28** — Frames still to write into the log, see ProcessFrame. Counts itself down.
- **:31** — Frame-time watchdog: milliseconds above which a frame is worth a line in the log. Zero is
- **:37** — Filled by the parallel task, read after the wait - see ProcessFrame.
- **:187** — The dump forces gathering on for its own duration. Without this the timers it prints
- **:211** — TAA plumbing.
- **:217** — "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
- **:221** — The same jitter in PIXELS, which is the form FSR 2 takes it in. Kept separately
- **:252** — see g_da_taa_unjittered_VP above. The jitter is two entries of the projection matrix, so
- **:260** — Вычитать больше нечего: джиттер не попадает в mProject НИ В ОДНОМ режиме — его
- **:323** — const u64 frameStartTime = TimerGlobal.GetElapsed_ns(); // [DA_PORT] ns: whole ms cannot express 165 fps
- **:324** — Pace the frame on a clock that CANNOT be paused. TimerGlobal can: while paused it
- **:328** — Own timers for the dump, because the engine's own are peak-hold, not per-frame:
- **:350** — Split for the dump. This sequence turned out to be the frame - some eleven
- **:361** — Sum the entries individually as well as timing the loop around them, because those
- **:407** — The frame has three parts and the statistics only count two of them. ENGINE times
- **:418** — The same figures the statistics overlay shows, written to the log for N frames.
- **:428** — Watchdog: silent until a frame actually misbehaves.
- **:487** — The budget is computed in NANOSECONDS.
- **:500** — Time the sleep, and the whole of this function, because the parts measured above stopped
- **:816** — Диагностика зависания при альт-табе.
- **:832** — Msg("* [DA_PORT] окно: %s (b_is_Active=%d, always_active=%d)", activated ? "фокус получен" : "фокус потерян",
- **:848** — При rs_always_active планировщик НЕ останавливаем.

### `xrEngine/Device.h`

- **:65** — Internal resolution the 3D scene is actually rendered at, driven by "r__render_scale".

### `xrEngine/Device_create.cpp`

- **:39** — Msg("* [DA_PORT] Dev::Create before Render->Create"); FlushLog();
- **:41** — Msg("* [DA_PORT] Dev::Create after Render->Create"); FlushLog();
- **:47** — Msg("* [DA_PORT] Dev::Create after SetupStates"); FlushLog();
- **:51** — Msg("* [DA_PORT] Dev::Create after OnDeviceCreate"); FlushLog();
- **:53** — Msg("* [DA_PORT] Dev::Create after CreateImGuiRender"); FlushLog();
- **:55** — Msg("* [DA_PORT] Dev::Create after ImGui OnDeviceCreate"); FlushLog();
- **:57** — Msg("* [DA_PORT] Dev::Create DONE"); FlushLog();

### `xrEngine/Device_mode.cpp`

- **:6** — extern ENGINE_API int ps_r__render_scale; // [DA_PORT] defined in xr_ioc_cmd.cpp
- **:11** — Одно разрешение — одна строка, с наибольшей частотой обновления.
- **:55** — Msg("* [DA_PORT] монитор %d: режимов %d, разрешений %u", monitorID, modeCount, (u32)best.size());
- **:180** — Порядок здесь был перевёрнут, и из-за этого монопольный полный экран включался
- **:222** — Принимаем то, что монитор реально показал. Драйвер вправе дать не тот режим, о
- **:231** — Msg("~ [DA_PORT] полный экран: просили %ux%u, монитор показывает %dx%d - берём второе",
- **:325** — Derive the internal scene resolution from "r__render_scale" (a percentage of the output).
- **:346** — Msg("* [DA_PORT] render scale %d%%: scene renders at %ux%u, presented at %ux%u", scale, dwRenderWidth,

### `xrEngine/Engine.cpp`

- **:11** — See Engine.h for why this exists rather than a MasterGold build. Read once: the command
- **:48** — Ключ -rgl убран вместе с GL-рендерером. Раньше он выбирал режим, которого

### `xrEngine/Engine.h`

- **:48** — Was the game started with "-dev"?

### `xrEngine/EngineAPI.cpp`

- **:144** — Only R4 is offered. Every change this port makes - temporal upscalers, motion

### `xrEngine/Environment_misc.cpp`

- **:249** — Msg("! [DA_PORT] CEnvAmbient '%s': no sound channels and no effects, skipping", sect.c_str());

### `xrEngine/IGame_ObjectPool.cpp`

- **:47** — не крашиться, если класс не создался (битый/незарегистрированный clsid) — залогировать и вернуть null

### `xrEngine/Rain.cpp`

- **:7** — Ручки дождя, см. xr_ioc_cmd.cpp
- **:34** — Пул всплесков. Был 1000 при отказе на каждый второй удар; теперь всплеск даёт каждая
- **:200** — Было `if (0 != ::Random.randI(2)) return;` — половина капель падала бесследно, без

### `xrEngine/Render.h`

- **:68** — Источник, который потолок теневых ламп не вытесняет НИКОГДА и который не занимает

### `xrEngine/XR_IOConsole.cpp`

- **:251** — Очистить строку ввода после выполнения. Её не очищал никто: команда
- **:466** — Несколько команд в одной строке через ';'.

### `xrEngine/defines.cpp`

- **:12** — По умолчанию — полный экран в окне, а не окно без рамки. Во-первых, это то, чего

### `xrEngine/key_binding_registrator_script.cpp`

- **:73** — value("kWPN_7",                     int(kWPN_7)), // [DA_PORT]
- **:74** — value("kWPN_8",                     int(kWPN_8)), // [DA_PORT]

### `xrEngine/line_edit_control.cpp`

- **:737** — Срезаем ХВОСТОВОЙ пробел. Без этого функция, названная remove_spaces, схлопывает

### `xrEngine/x_ray.cpp`

- **:192** — Headless tooling runs (-da_export_scripts/-da_export_configs) and explicit
- **:284** — Перечисление устройств должно закончиться ДО user.ltx, а не после.
- **:313** — Msg("* [DA_PORT] App: before TaskScheduler->Wait(lightAnim)"); FlushLog();
- **:315** — Msg("* [DA_PORT] App: after TaskScheduler->Wait"); FlushLog();
- **:323** — Msg("* [DA_PORT] App: after create_persistent"); FlushLog();
- **:326** — Msg("* [DA_PORT] App: before OnAppStart"); FlushLog();
- **:328** — Msg("* [DA_PORT] App: after OnAppStart"); FlushLog();
- **:392** — Msg("* [DA_PORT] Run: before HideSplash"); FlushLog();
- **:394** — Msg("* [DA_PORT] Run: before Device.Run"); FlushLog();
- **:396** — Msg("* [DA_PORT] Run: after Device.Run, entering loop"); FlushLog();

### `xrEngine/xr_input.cpp`

- **:526** — std::locale("") throws std::runtime_error under MinGW GCC on Windows
- **:714** — Кто сейчас владеет вводом.
- **:732** — Msg("~ [DA_PORT] ввод %s, приёмников %u:%s", what, (u32)stack.size(), tail);
- **:740** — Один приёмник — одна запись в стеке. Повторный захват ПЕРЕНОСИТ его наверх, а не
- **:757** — Msg("~ [DA_PORT] ввод: приёмник %s захватывает повторно — переносим наверх, дубликат не создаём",
- **:792** — Раньше эта ветка молчала, и в логе выглядело так, будто захватов больше,

### `xrEngine/xr_ioc_cmd.cpp`

- **:22** — Frame-rate cap offered as a list in the video options. The numeric token names are
- **:237** — Skip comment lines. Every other .ltx in the game marks comments with ';' and the
- **:371** — Internal render resolution, as a percentage of the output. Every scene render target is
- **:376** — FSR 2 quality mode. Setting it also sets the render scale, because the two are not free
- **:392** — ---- Upscaler registry: only one may be on ------------------------------------------
- **:401** — extern ENGINE_API int ps_r__dlss; // [DA_PORT]
- **:419** — Our own temporal AA belongs here too: it owns the frame's history exactly as the
- **:425** — Is a TEMPORAL upscaler reconstructing this frame?
- **:436** — Апскейлер выбран, но НЕ РАБОТАЕТ на этой машине — взводится самим бэкендом после
- **:451** — ---- Отказ апскейлера: гасим тихо и говорим громко ------------------------------------
- **:481** — Msg("! [DA_PORT] %s не работает на этой видеокарте: три неудачных кадра подряд.", who);
- **:482** — Msg("! [DA_PORT] Субпиксельный сдвиг выключен, иначе картинка тряслась бы: сцена сдвигается, а");
- **:483** — Msg("! [DA_PORT] собрать её обратно некому. Сейчас кадр просто растягивается — будет мягче.");
- **:484** — Msg("! [DA_PORT] Выберите другой апскейлер в настройках видео: FSR 2.0 работает на любой карте.");
- **:487** — Мгновенный отказ: контекст библиотеки вообще не создался.
- **:498** — Msg("! [DA_PORT] %s не запустился на этой видеокарте.", who ? who : "апскейлер");
- **:499** — Msg("! [DA_PORT] Субпиксельный сдвиг выключен, иначе картинка тряслась бы: сцена сдвигается, а");
- **:500** — Msg("! [DA_PORT] собрать её обратно некому. Кадр просто растягивается - будет мягче, но ровно.");
- **:501** — Msg("! [DA_PORT] Выберите другой апскейлер в настройках видео: FSR 2.0 работает на любой карте.");
- **:508** — Msg("* [DA_PORT] отметка «апскейлер не работает» снята: выбран другой режим");
- **:513** — ---- Сброс истории временных фильтров -------------------------------------------------
- **:530** — Msg("* [DA_PORT] temporal history discarded: %s", why);
- **:567** — Msg("! [DA_PORT] %s switched off - only one upscaler may reconstruct a frame, and two at once "
- **:573** — ---- What the menu actually shows: which upscaler, and how hard -----------------------
- **:584** — Our own temporal AA belongs in this list, not beside it.
- **:595** — Порядок строк — это порядок пунктов в меню, и он выбран, а не унаследован: сначала то,
- **:615** — ---- Список апскейлеров подрезается под ЖЕЛЕЗО --------------------------------------
- **:627** — Взводится только на время подмены апскейлера ВНУТРИ создания устройства — см. пояснение
- **:648** — Msg("* [DA_PORT] апскейлер «%s» скрыт из меню: %s", src->name, why_hidden ? why_hidden : "не поддерживается");
- **:661** — Msg("! [DA_PORT] выбранный апскейлер на этой видеокарте недоступен - переключаюсь на FSR 2.0.");
- **:705** — Новый выбор — новая попытка: прошлый отказ к нему отношения не имеет.
- **:720** — ⚠️ Резкость задаёт КАЖДАЯ ветка, и это не украшательство.
- **:765** — MSAA cannot run next to a RECONSTRUCTING upscaler, and loses to it.
- **:790** — ⚠️ [DA_PORT] `b_is_Ready` НЕ означает «создание устройства закончено».
- **:803** — Msg("* [DA_PORT] upscaler %d, quality step %d: scene renders at %d%% of the output", ps_r__upscaler,
- **:822** — Same idea as CCC_FSR2 below: the quality mode also sets the render scale, because Intel's
- **:849** — Msg("* [DA_PORT] XeSS mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
- **:853** — DLSS. Устроен как XeSS выше, но коэффициенты свои: они замерены у самой NGX через
- **:885** — Msg("* [DA_PORT] DLSS mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
- **:933** — Msg("* [DA_PORT] FSR 2 mode %d: scene renders at %d%% of the output", *value, ps_r__render_scale);
- **:955** — Upscaling presets (FSR 1.0). Scales follow the usual naming: quality is a barely visible
- **:1027** — Частоту приводим к той, что есть в списке для этого разрешения.
- **:1066** — Msg("~ [DA_PORT] режим %s: в списке для этого разрешения только %u Гц, беру её",
- **:1136** — Режима три, а не четыре, и порядок в списке — от оконного к полноэкранному.
- **:1159** — Старое имя режима из прежних конфигов приводим к ближайшему из оставшихся.
- **:1164** — Msg("~ [DA_PORT] режим окна без рамки больше не предлагается, включён оконный");
- **:1191** — Выключение полного экрана даёт полный экран В ОКНЕ, а не окно без рамки:
- **:1210** — Exported: the renderer now applies these itself when the hardware gamma ramp is
- **:1341** — Устройства с таким именем больше нет — не ругаемся «Invalid syntax» в пустоту, а
- **:1458** — Effective per-frame HUD FOV. Equals psHUD_FOV unless the opt-in "nearwall" weapon
- **:1476** — Percentage of the output resolution the 3D scene is rendered at (see
- **:1480** — Temporal anti-aliasing. Lives in the engine rather than the renderer because the camera
- **:1486** — Sharpening applied after the render-scale upscale (the RCAS half of FSR 1.0), in percent.
- **:1491** — Motion vectors: an extra G-buffer target recording where each pixel was last frame. This is
- **:1497** — AMD FidelityFX Super Resolution 2: 0 off, 1 quality, 2 balanced, 3 performance,
- **:1502** — Intel XeSS. A separate variable rather than one shared "upscaler" enum: each builds its
- **:1505** — ENGINE_API int ps_r__dlss = 0; // [DA_PORT] NVIDIA DLSS: 0 выкл, 1-5 ступень качества
- **:1507** — Отдавать ли DLSS нашу реактивную маску.
- **:1518** — Разовый замер векторов движения: числа в лог вместо перебора знаков глазами.
- **:1520** — Показать ВХОД апскейлера вместо его результата.
- **:1530** — Разовый снимок среза G-буфера по строке через прицел (см. CRenderTarget::
- **:1536** — Покадровая запись накопленного света в пикселе под перекрестьем, N кадров подряд.
- **:1541** — Замер кэша теневых карт. Значение = число кадров: первая половина меряет ВРЕМЯ КАДРА без
- **:1547** — FSR 3 upscaler. A separate variable rather than a mode of r__fsr2: the two build
- **:1552** — D3D11 validation layer, plus draining its messages into the engine log. Off by
- **:1557** — Halves the FSR 3 path so the damage can be attributed without a validation layer.
- **:1563** — Насколько апскейлеры должны не доверять своей истории на альфа-тестовой растительности.
- **:1580** — Reactivity from screen-space motion, against ghosting behind moving objects. The
- **:1590** — Reactivity from motion THROUGH THE WORLD, against the ghost trailing an NPC.
- **:1609** — Метка самосветящейся геометрии - лампочка, экран телевизора, светящаяся палочка в руке.
- **:1626** — Насколько маска реактивности гасит накопление в НАШЕЙ темпоралке (r__taa).
- **:1637** — То же для прозрачной геометрии - стекло, вода, частицы. По умолчанию НОЛЬ, и это не
- **:1643** — Не загружать константный буфер, если его содержимое не отличается от уже загруженного.
- **:1656** — Куда целится срез G-буфера (da_dump_gbuffer_row, r__reactive_selftest).
- **:1663** — Знак векторов движения, отдаваемых XeSS. См. da_xess_mv.s.
- **:1676** — Сколько объектов лежит в очередях, которые рисуются ПОСЛЕ G-буфера, и сколько света в
- **:1704** — Which ingredient of the pass to write out instead of the mask, so that "r__motion_vectors 4"
- **:1711** — One-shot readback of the pass's inputs and output into the log, with figures rather than a
- **:1715** — The frame rate the three settings above are stated at.
- **:1730** — Motion vectors for the sky, which no shader writes - see da_sky_velocity.ps. A switch
- **:1734** — Put a stalker that has slid off the navigation mesh back onto it, rather than let it fail
- **:1744** — Velocity guard: damps vegetation motion near glossy standing surfaces, so that FSR's
- **:1766** — ---- Detail-bump stability under a temporal upscaler --------------------------------
- **:1790** — Paints the damping weight instead of the surface: 1 = the weight the normal path applies,
- **:1797** — Scales the sway amplitude of trees and grass. 0 freezes the vegetation completely while
- **:1804** — ---- Glossy surfaces opt out of temporal accumulation -------------------------------
- **:1818** — 0 = vegetation stands still in the shadow map while still swaying on screen. See
- **:1822** — Качание ТРАВЫ на сильном ветру: 0 - как в исходном движке, 1 - как в Dead Air.
- **:1836** — Частота качания травы, множителем к тому, что получилось выше. Единица - не вмешиваться.
- **:1844** — ---- Лужи в дождь ------------------------------------------------------------------
- **:1867** — Вид воды — отдельной константой, чтобы правился В ИГРЕ, а не пересборкой шейдера.
- **:1886** — --- Сезон -------------------------------------------------------------------------------
- **:1904** — ⚠️ Хранилище у сезона РОВНО ОДНО — файл-признак. В user.ltx команда не пишется намеренно.
- **:1935** — Msg("* [DA_PORT] сезон: %s (применится после перезапуска игры)", value);
- **:1938** — Msg("! [DA_PORT] сезон: не удалось записать %s", marker);
- **:1942** — Поднять текущий сезон из того же признака, по которому файловая система уже смонтировала
- **:1962** — Msg("* [DA_PORT] сезон: %s", (1 == ps_da_season) ? "лето" : "осень");
- **:1975** — Msg("* [DA_PORT] сезон: лето (признака нет, летний архив на месте)");
- **:1980** — Msg("* [DA_PORT] сезон: осень (признака нет, летнего архива тоже)");
- **:1983** — Яркость луча фонарей. Множитель к цвету лампы, а не к дальности: цвет фонарей задаёт
- **:1991** — ---- Дождь -------------------------------------------------------------------------
- **:2027** — ---- «Стоим ли мы в луже» для остальной игры -------------------------------------------
- **:2077** — ---- Пункты меню «Дождь» ------------------------------------------------------------
- **:2081** — Отражения в лужах: отдельный полноэкранный проход (см. r4_rendertarget_phase_da_puddle_refl).
- **:2198** — 0 = vegetation reports no motion at all to the upscaler. FSR 2 dilates velocity from the
- **:2203** — Feeds the foliage mask to FSR 2's transparency-and-composition input as well as its
- **:2217** — Which way round the jitter is handed to FSR 2: 0 = (+x,-y), 1 = (-x,+y), 2 = (+x,+y),
- **:2223** — Sign of the motion vectors handed to FSR 2: 0 = (-x,+y), 1 = (-x,-y), 2 = (+x,+y),
- **:2229** — One control instead of two. The pair that actually drives the upscale — render scale and
- **:2241** — Post-resolve sharpening, in percent. Temporal accumulation is inherently softening — every
- **:2246** — Show the temporal resolve where it fetches history from - see da_taa.ps.
- **:2252** — How much of the normal history weight the SKY keeps, in percent. 100 is the behaviour
- **:2275** — Negative mip bias to pair with TAA, in hundredths of a level. Off by default: it sharpens
- **:2279** — Projection jitter, separately switchable. TAA has two halves that fail in different ways —
- **:2284** — Временное подавление джиттера на время замеров. ОТДЕЛЬНО от ps_r__taa_jitter, и вот
- **:2338** — Above 100 the scene is rendered LARGER than the window and downsampled on the way out,
- **:2343** — What the options menu shows. The three per-vendor variables above stay for the console.
- **:2347** — Диагностика, не настройка: ноль — рабочее состояние (буфер скоростей включает сам
- **:2369** — Обе — диагностика (см. CCC_DaDebug в xr_ioc_cmd.h): da_perf_dump печатает в лог
- **:2381** — Detail-bump damping, see the declarations. Sensitivity and strength in one number:
- **:2393** — Качание травы: 0 = как в исходном движке (по умолчанию), 1 = как в моде.
- **:2397** — Лужи в дождь. Действуют сразу, без перезапуска: все четыре числа читаются на кадр.
- **:2402** — Принудительная сырость — отладочная: держит мокрый асфальт в ясную погоду, поэтому
- **:2411** — Яркость фонарей. Действуют сразу: цвет ламп пересчитывается каждый кадр.
- **:2415** — Сезон: осень / лето. Применяется со следующего запуска игры.
- **:2420** — Дождь. Действуют сразу, без перезапуска.
- **:2426** — Пункты меню «Дождь»: две ступени вместо восьми чисел.
- **:2440** — CMD3(CCC_DLSS, "r__dlss", (u32*)&ps_r__dlss, qdlss_token); // [DA_PORT]
- **:2441** — CMD4(CCC_Integer, "r__dlss_reactive", &ps_r__dlss_reactive, 0, 1); // [DA_PORT] применяется сразу
- **:2442** — CMD4(CCC_DaDebugInteger, "r__dlss_selftest", &ps_r__dlss_selftest, 0, 1); // [DA_PORT] разовый замер в лог
- **:2443** — CMD4(CCC_DaDebugInteger, "r__upscale_show_input", &ps_r__upscale_show_input, 0, 1); // [DA_PORT] вход вместо выхода
- **:2444** — Срез по строке в лог. Значение — сколько ПОДВИЖНЫХ кадров пропустить: отсчёт идёт
- **:2448** — Свет под перекрестьем, N кадров подряд — для мерцания при неподвижной камере.
- **:2450** — Полный замер кэша теней: время кадра + дрожание по всему экрану, одной командой.
- **:2451** — Включаемый: 1 — начать сбор, 0 — закончить и выдать отчёт. Не отсчёт кадров.
- **:2454** — restart to apply. 1 — слой проверки DirectX, 2 — он же плюс перепись живых объектов
- **:2458** — CMD4(CCC_DaDebugInteger, "r__fsr3_debug", &ps_r__fsr3_debug, 0, 1); // [DA_PORT] 1 = create but never dispatch
- **:2459** — CMD4(CCC_Integer, "r__fsr3", &ps_r__fsr3, 0, 5); // [DA_PORT] quality step, restart to apply // sets r__render_scale to match; needs a renderer res...
- **:2460** — CMD4(CCC_Integer, "r__upscale_sharpness", &ps_r__upscale_sharpness, 0, 100); // [DA_PORT] FSR-style RCAS
- **:2461** — CMD4(CCC_Integer, "r__taa", &ps_r__taa, 0, 1); // [DA_PORT]
- **:2463** — CMD4(CCC_Integer, "r__taa_sky", &ps_r__taa_sky, 0, 100); // [DA_PORT]
- **:2464** — CMD4(CCC_Integer, "r__taa_sharp", &ps_r__taa_sharp, 0, 100); // [DA_PORT]
- **:2465** — CMD4(CCC_Integer, "r__taa_mipbias", &ps_r__taa_mipbias, 0, 100); // [DA_PORT]
- **:2466** — CMD4(CCC_Integer, "r__taa_jitter", &ps_r__taa_jitter, 0, 1); // [DA_PORT]
- **:2471** — CMD3(CCC_Token, "rs_fps_limit", &ps_fps_limit, fps_limit_token); // [DA_PORT] list, not a raw number

### `xrEngine/xr_ioc_cmd.h`

- **:433** — Крутилка «на один сеанс»: ведёт себя как обычная, но НЕ пишется в user.ltx.

### `xrEngine/xr_level_controller.cpp`

- **:68** — { "wpn_7",                  kWPN_7,                     _both }, // [DA_PORT] bound to G by the mod
- **:69** — { "wpn_8",                  kWPN_8,                     _both }, // [DA_PORT] reserved, as in the original

### `xrEngine/xr_level_controller.h`

- **:54** — Dead Air adds these two and binds wpn_7 to G in default_controls.ltx. Without them the


## Игровая логика

*90 файл(ов), 453 правк(и)*


### `xrGame/Actor.cpp`

- **:378** — Weight-based sprint penalty (Actor_Movement.cpp). The alpha reads these as required
- **:385** — hold-breath sway multiplier (see UpdateCL). [actor] breath_koef = 0.02
- **:1044** — like CoC-Xray (Actor.cpp): fake fov a zoom texture (scope overlay) is calibrated
- **:1057** — Dead Air (CoC lineage) treats zoom factors as optical MAGNIFICATION
- **:1072** — if (m_item_placement_active) // [DA_PORT] keep the placement ghost tracking the crosshair
- **:1140** — Dead Air aim-sway: extra sway from actor psy/power (scripts feed it each frame
- **:1479** — ...except while placing an item. The placement ghost is drawn as part of the actor, so
- **:1565** — defined in script_game_object_script3.cpp (a script TU): calls the Lua functor
- **:1570** — --- "Установить" item placement preview (kerosene lamp etc.) ---
- **:1583** — Ghost highlight: a soft cyan point light plus a glow sprite, so the preview reads as a
- **:1604** — Close the inventory: placement is aimed with the crosshair, so the menu that started it
- **:1691** — In first person during placement the actor is forced visible purely to get the ghost
- **:1700** — draw the placement-preview ghost at the crosshair.
- **:1969** — Dead Air's slot 14 (== GRENADE_SLOT) is a MANUAL utility slot (holds a grenade OR a
- **:2049** — Dead Air runs artefact and outfit effects at double rate (`update_time*2` in the
- **:2068** — Artefact radiation is deliberately NOT applied here — Dead Air's own engine has
- **:2085** — Helmets were read but never applied: CHelmet loads health/radiation/power/bleeding/
- **:2106** — as above: the outfit's stamina drain only bites while sprinting
- **:2126** — How much artefacts take off a hit of this type. ONE function, used both by the damage path
- **:2165** — Кислородный баллон гасит химию ПОРОГОМ, а не арифметикой.

### `xrGame/Actor.h`

- **:24** — class IRenderVisual; // [DA_PORT] item placement-preview ghost
- **:208** — Belt AND backpack slot - the tank and the exo backpack are artefacts too. Shared by the
- **:211** — Порог кислородного баллона против химии — тот же, что в скрипте мода. См. Actor.cpp.
- **:448** — how hard the carried weapon / worn outfit cut into sprint speed (sprint_*_koef)
- **:499** — "Установить" placement-preview mode (kerosene lamp etc.): a ghost of the item follows
- **:509** — item placement-preview state
- **:515** — Ghost highlight. The model itself cannot be tinted from here — IRenderVisual does not
- **:527** — hold-breath + DA zoom-inertion (see CActor::UpdateCL where zoom inertion is applied):
- **:534** — float m_fCamRecoilCoeff{1.f}; // [DA_PORT] см. SetCamRecoilCoeff
- **:539** — Множитель отдачи камеры, через который работает перк «Твёрдая рука». Скрипты ставят

### `xrGame/ActorAnimation.cpp`

- **:29** — PITCH factors bend the actor's THIRD-PERSON spine/head bones to follow the first-person

### `xrGame/ActorBackpack.cpp`

- **:18** — Summand, not multiplier - see the note in ActorHelmet.cpp. Undeclared contributes nothing.

### `xrGame/ActorCondition.cpp`

- **:174** — ---- Dying vision ------------------------------------------------------------------------
- **:302** — UpdateDyingVision(); // [DA_PORT]
- **:303** — UpdateRadiationVision(); // [DA_PORT]
- **:433** — Fixed divisor instead of the live time factor, as in Dead Air.
- **:466** — Radiation from the ground is NOT folded in here — see the separate hit at the end of the
- **:523** — Radiation from radioactive ground, as its own hit into the spine (as in Dead Air).
- **:587** — Satiety saturates at half full before it feeds anything.
- **:631** — выносливость тратится ТОЛЬКО на спринте (ходьба/ускорение — без траты); перегруз усиливает трату спринта
- **:634** — Overload is measured against the WALK limit, exactly as the author wrote it.
- **:975** — физ.урон — звук только при damage>0.1; шок/ожоги — при >1.0; радиация — без звука

### `xrGame/ActorCondition.h`

- **:49** — Vision fades and greys out as health runs out, so the player feels themselves dying
- **:53** — Blurred and doubled vision from radiation sickness, before it starts eating health.

### `xrGame/ActorHelmet.cpp`

- **:61** — Defaults to 0, not 1, because for a helmet this value is a SUMMAND.
- **:293** — Same 10x nerf as in CCustomOutfit::HitThroughArmor — Dead Air drops the factor
- **:347** — См. CCustomOutfit::HitThroughArmor, ветка CS: делитель на 10 снят и здесь,

### `xrGame/ActorInput.cpp`

- **:42** — item placement-preview mode: fire = confirm, use/reload = cancel; swallow the rest so
- **:126** — Переключение камеры с клавиш отключено — как у автора (`//case kCAM_1: ...` в
- **:138** — Night vision is owned by the Lua script: itms_manager.script on_key_press catches
- **:149** — Фонарь принадлежит скрипту — ровно как ночное зрение выше, и по той же причине.
- **:309** — Отладочный пульс отсюда снят: он показал, что команда доходит до движения нормально,

### `xrGame/Actor_Movement.cpp`

- **:15** — #include "CustomOutfit.h" // [DA_PORT] sprint weight penalty needs the outfit up here
- **:29** — A failing actor moves like one. Below DA_FAILING_HEALTH ordinary movement speed falls off
- **:233** — Scale a local copy, not the member.
- **:323** — The carried weapon and worn outfit eat into sprint speed. Both koefs
- **:359** — see DA_FAILING_HEALTH above. Sprint is deliberately untouched — it is governed
- **:623** — зум приоритетнее приседа — нельзя быть «ускоренным» (спринт) в прицеле
- **:724** — res += outfit->m_additional_weight * outfit->GetCondition(); // грузоподъёмность костюма скейлится по состоянию
- **:730** — DA's backpacks are scripted artefacts (config class SCRPTART), not the engine

### `xrGame/AnselManager.cpp`

- **:226** — Msg("! [DA_PORT_STUB] AnselCameraEffector::ProcessCam: Ansel delay-load not supported on GCC/MinGW");

### `xrGame/CameraEffector.h`

- **:19** — Fading vision as the actor dies — see CActorCondition::UpdateDyingVision.
- **:21** — Vision affected by radiation sickness — see CActorCondition::UpdateRadiationVision.

### `xrGame/CustomDetector.cpp`

- **:14** — #include "xrEngine/LightAnimLibrary.h" // [DA_PORT] world lamp light (device_kerosinka) color animator
- **:15** — #include "ParticlesObject.h" // [DA_PORT] world flame particle (device_kerosinka kerosine_glow)
- **:180** — void CCustomDetector::OnHiddenItem() { DaStopHudEffects(); } // [DA_PORT] см. DaStopHudEffects
- **:195** — m_world_light.destroy(); // [DA_PORT] release the world lamp light
- **:196** — if (m_world_particles) // [DA_PORT] release the world flame particle
- **:198** — if (m_hud_particles) // [DA_PORT] release the HUD flame particle
- **:200** — if (m_held_light) // [DA_PORT] release the held lighter glow
- **:204** — --- World light for a light-emitting DET_SIMP lying in the world (device_kerosinka) ---
- **:245** — world flame particle (kerosine_glow) rides the same on/off as the light.
- **:271** — keep the flame on the lamp as it settles; runs even if this item has no light.
- **:331** — warm glow so the held lighter lights the environment (world-space, near the actor - the HUD
- **:362** — a light item spawned straight into the world (e.g. a placed kerosene lamp) burns now.
- **:381** — light_enabled=true DET_SIMP items (kerosene lamp) emit a world light when on the ground.
- **:383** — particles_enabled=true DET_SIMP items (kerosene lamp -> "kerosine_glow") show a world flame.
- **:391** — hud_particles_enabled=true (device_lighter) -> flame on the first-person HUD model bone.
- **:428** — hud_ui_* generic 3D artefact screen -------------------------------------
- **:503** — Погасить пламя и подсветку зажигалки НЕМЕДЛЕННО, а не «когда-нибудь на обновлении».
- **:576** — keep the world lamp's light positioned + flickering; runs for the independent
- **:581** — keep the held lighter's HUD flame on its bone (self-gates on hud_particles_enabled + drawn).
- **:596** — ActivateWorldLight(false); // [DA_PORT] picked up -> no world light (handheld uses device_torch)
- **:602** — dropped into the world -> a light item (kerosene lamp) starts burning on the ground.
- **:626** — DaStopHudEffects(); // [DA_PORT] убрали в рюкзак — огонёк туда не летит

### `xrGame/CustomDetector.h`

- **:126** — optional generic hud_ui_* 3D artefact screen (simple/advanced/craft)
- **:156** — hud_ui_* 3D screen hooks (no-op unless the HUD section defines hud_ui_*)
- **:178** — lazily builds m_hud_ui from HUD-section hud_ui_* keys, then feeds it
- **:188** — World light for dropped/placed light items (e.g. the device_kerosinka kerosene lamp,
- **:198** — world flame particle (device_kerosinka particles_enabled=true -> "kerosine_glow"). Plays and
- **:206** — HUD flame particle (device_lighter hud_particles_enabled=true -> "gas_light_glow" attached to
- **:209** — Гасит пламя и подсветку зажигалки по событию, а не по обновлению — убранный предмет
- **:216** — the held lighter must actually illuminate. device_lighter has no light_* of its own and its

### `xrGame/CustomOutfit.cpp`

- **:116** — Dead Air: outfit may forbid a backpack (scientific suit). Default true (allowed), like helmet.
- **:196** — Stock scaled non-bullet protection (radiation, chemical burn, psi, burn, shock)
- **:252** — Тот же делитель на 10, что снят выше в ветке COP — и ЭТА ветка как раз
- **:324** — mirror the helmet kick for the backpack: putting on an outfit that forbids a

### `xrGame/CustomOutfit.h`

- **:64** — Dead Air outfit flag next to helmet_avaliable (default true). When false the outfit forbids

### `xrGame/CustomZone.cpp`

- **:88** — Inverse filters from the alpha. Read optionally with a false default (author's call):
- **:288** — Dead Air adds detailed volumetric light params (CoC/port had only on/off).
- **:395** — apply Dead Air detailed volumetric params
- **:651** — (!object_info.small_object && m_zone_flags.test(eIgnoreBig)) || // [DA_PORT]
- **:653** — (!object_info.nonalive_object && m_zone_flags.test(eIgnoreAlive)) || // [DA_PORT]

### `xrGame/CustomZone.h`

- **:111** — Dead Air's inverse filters: skip living entities / skip large objects. The alpha
- **:252** — Dead Air detailed volumetric idle-light params

### `xrGame/EffectorShot.cpp`

- **:53** — Отдача растёт у сломанного оружия. Перенесено из исходников автора (EffectorShot.cpp).
- **:67** — И множитель отдачи от перка «Твёрдая рука» — см. m_recoil_coeff.
- **:89** — Горизонталь множится отдельно, как у автора: вертикаль уже получила множитель через
- **:214** — Множитель забирается у актёра каждый кадр, а не запоминается при создании эффектора.

### `xrGame/EffectorShot.h`

- **:36** — Множитель отдачи камеры. Единица — как задумано оружием.
- **:86** — Множитель отдачи обновляется и здесь, не только в ProcessCam.

### `xrGame/EntityCondition.cpp`

- **:14** — #include "ActorBackpack.h" // [DA_PORT] CBackpack::m_fPowerLoss in HitPowerEffect
- **:15** — #include "Actor.h" // [DA_PORT] da_rad_log: разбор радиационного хита пишем только по актёру
- **:17** — см. console_commands.cpp, команда da_rad_log
- **:310** — The backpack gets its turn first, before outfit and helmet, as in Dead Air.
- **:343** — power_loss stays a MULTIPLIER. The author's summand form is deliberately NOT used.
- **:408** — Dead Air starts from "this hit wounds" and lets the switch below veto it, rather than
- **:434** — No per-bone scaling on burns - Dead Air drops m_fHitBoneScale here. Fire damage
- **:462** — Разбор хита по слагаемым — включается `da_rad_log 1`. Защита ВЫЧИТАЕТСЯ, а не

### `xrGame/GameObject.cpp`

- **:1053** — Объект без формы столкновений. Так бывает у уже уничтоженного объекта: net_Destroy
- **:1071** — Msg("~ [DA_PORT] зрение: у объекта '%s' нет формы столкновений (уничтожается?) - точка взята без габаритов",

### `xrGame/GamePersistent.cpp`

- **:3** — #include "da_memory_probe.h" // [DA_PORT]
- **:484** — Отложенная команда — ИМЕННО ЗДЕСЬ, а не в CLevel::OnFrame.
- **:491** — И стартовая команда из `-da_cmd` — тем же обработчиком и по той же причине: он
- **:695** — Загрузка сохранения ИЗНУТРИ игры — отдельный путь: уровень остаётся в памяти,
- **:701** — Погасить все звучащие эмиттеры перед сносом мира.

### `xrGame/HudItem.cpp`

- **:10** — #include "xrCDB/xr_collide_defs.h" // [DA_PORT] nearwall: collide::rq_result for the forward wall ray
- **:21** — nearwall weapon-collision HUD FOV (opt-in). Default OFF => g_hud_fov_current stays
- **:196** — nearwall: smoothly pull the weapon HUD FOV toward target_fov as a wall gets close

### `xrGame/Inventory.cpp`

- **:18** — #include "Torch.h" // [DA_PORT] запрет второго скрытого фонаря, см. CanTakeItem
- **:52** — false, // [DA_PORT] script animation slot (13)
- **:53** — true, // [DA_PORT] grenade slot (14) - DA relocated hand grenades here (DA slot_active_14 = true)
- **:54** — false // [DA_PORT] backpack slot (15)
- **:99** — Only warn when the config actually defines MORE slots than the engine enum knows
- **:192** — Dead Air's slot 14 (GRENADE_SLOT) is a MANUAL utility slot the actor shares between a
- **:612** — Skip the auto-refill for the ACTOR: Dead Air's slot 14 is a manual utility
- **:753** — The mod's own key, bound to G. A TOGGLE rather than a select: press once to take the
- **:1059** — Equipped gear counts at 30% of its weight, as in Dead Air.
- **:1267** — Dead Air: an outfit with backpack_avaliable=false (scientific suit) forbids a backpack.
- **:1290** — Dead Air backpacks are artefact-class (belt=true from af_base) but must NEVER live on the
- **:1360** — Второй скрытый фонарь не берём.
- **:1552** — Сообщение осталось только на ОТКАЗ вернуть слот — молчание вместо шума.
- **:1567** — Msg("~ [DA_PORT] слот %d не вернулся после блокировки: предмет %s, слот %s", PrevActiveSlot,
- **:1588** — Убранное сообщение: см. TryActivatePrevSlot. Само по себе убирание оружия под

### `xrGame/InventoryBox.cpp`

- **:34** — ⚠️ [DA_PORT] Предмета может УЖЕ НЕ БЫТЬ, и это роняло игру.
- **:50** — Msg("! [DA_PORT] ящик: предмет [%d] уже снят с учёта (выгрузка уровня?) - событие "
- **:81** — То же самое, что и в ветке взятия выше, плюс своя мина: `erase` по итератору
- **:86** — Msg("! [DA_PORT] ящик: предмет [%d] уже снят с учёта - событие возврата пропущено", id);
- **:93** — Msg("! [DA_PORT] ящик: предмет [%d] не числится в этом ящике - возврат пропущен", id);
- **:106** — И здесь тоже: приведение типа может не удаться, а результат уходил в Lua

### `xrGame/InventoryOwner.cpp`

- **:323** — The equipped backpack extends the carry limit too - without this the inventory

### `xrGame/Level.cpp`

- **:53** — #include "da_memory_probe.h" // [DA_PORT]
- **:416** — DA_MemTick(); // [DA_PORT] досчитать объекты после спавна, см. da_memory_probe.h
- **:590** — 10, which is what the author's Dead Air uses. OpenXRay raised it to 100 upstream and left

### `xrGame/Level_input.cpp`

- **:132** — Dead Air's eKeyPress script hook reacts to the raw ESCAPE scancode by force-opening
- **:142** — ESC first closes any open UI dialog/inventory/PDA/talk,
- **:511** — Почему нажатие не доходит до актёра — замер вместо гадания.
- **:527** — Msg("! [DA_PORT] ввод отброшен: %s", reason);
- **:533** — Оставлена только эта отметка. Пульсы «клавиша дошла / команда дошла до движения»

### `xrGame/Level_network_start_client.cpp`

- **:15** — #include "da_memory_probe.h" // [DA_PORT] замер памяти по фазам загрузки
- **:124** — DA_MemMark("геометрия, CDB, AI-граф"); // [DA_PORT]
- **:144** — DA_MemMark("физический мир"); // [DA_PORT]
- **:225** — DA_MemMark("текстуры"); // [DA_PORT]
- **:252** — DA_MemMark("HUD"); // [DA_PORT]

### `xrGame/Level_start.cpp`

- **:15** — #include "da_memory_probe.h" // [DA_PORT] замер памяти по фазам загрузки
- **:25** — Сброс блокировки ввода при старте уровня — иначе она переживает смерть и загрузку.
- **:54** — Начало прогона замера памяти. Отметка «начало» снимается ЗДЕСЬ, то есть уже после
- **:288** — DA_MemRunEnd(); // [DA_PORT] конец прогона: снимок по подсистемам и печать таблицы

### `xrGame/MainMenu.cpp`

- **:141** — "-da_export_scripts": dump every script the VFS actually resolves (packed
- **:169** — Msg("! [DA_PORT] -da_export_scripts: exported %u/%u VFS scripts to appdata\\logs\\vfs_scripts\\, quitting",
- **:175** — "-da_export_configs": same idea for $game_config$ (ltx/xml) - needed to study
- **:201** — Msg("! [DA_PORT] -da_export_configs: exported %u/%u VFS configs to appdata\\logs\\vfs_configs\\, quitting",
- **:384** — Drawing the paused scene behind the menu was tried and reverted: the menu is composed

### `xrGame/Missile.cpp`

- **:465** — Печать номера кадра на каждый бросок была под MASTER_GOLD, а его в нашей сборке нет —

### `xrGame/PhraseDialog.cpp`

- **:229** — A dialog with neither a phrase list nor a working init_func stays

### `xrGame/PhraseScript.cpp`

- **:87** — A missing function used to take the game down (see da_script_functor.h).
- **:113** — A precondition we cannot evaluate must not open the phrase up:
- **:135** — Skip an action we cannot resolve; the info transfer below still runs.
- **:159** — See above: unresolvable precondition means the phrase stays hidden.
- **:184** — Skip an action we cannot resolve instead of taking the game down.

### `xrGame/RegistryFuncs.cpp`

- **:15** — Отсутствие ключа — норма, а не ошибка, и печаталась она четыре раза за запуск.
- **:27** — Msg("~ [DA_PORT] розничный STALKER не установлен (%s) - профиль сетевой игры не читается, "

### `xrGame/RocketLauncher.cpp`

- **:51** — Runs for every underbarrel grenade load (CWeaponMagazinedWGrenade), where
- **:67** — VERIFY below is compiled out in Release, and a null rocket then reached
- **:92** — Was (*It): the iterator of a different container, which at this point

### `xrGame/ScriptXMLInit.cpp`

- **:88** — DA's UI scripts reference nodes that may be absent in the port's UI XMLs (version
- **:94** — Msg("~ [DA_PORT] UI node [%s] missing - control skipped (DA/port UI compat)", path);
- **:150** — if (m_xml.NavigateToNode(path, 0)) // [DA_PORT] tolerate missing node (DA/port UI compat)
- **:153** — Msg("~ [DA_PORT] UI node [%s] missing - control skipped (DA/port UI compat)", path);
- **:185** — if (m_xml.NavigateToNode(path, 0)) // [DA_PORT] tolerate missing node (DA/port UI compat)
- **:188** — Msg("~ [DA_PORT] UI node [%s] missing - control skipped (DA/port UI compat)", path);
- **:196** — if (m_xml.NavigateToNode(path, 0)) // [DA_PORT] tolerate missing node (DA/port UI compat)
- **:199** — Msg("~ [DA_PORT] UI node [%s] missing - control skipped (DA/port UI compat)", path);
- **:256** — if (m_xml.NavigateToNode(path, 0)) // [DA_PORT] tolerate missing node (DA/port UI compat)
- **:259** — Msg("~ [DA_PORT] UI node [%s] missing - control skipped (DA/port UI compat)", path);

### `xrGame/Torch.cpp`

- **:22** — Яркость луча фонарей (xrEngine, xr_ioc_cmd.cpp) — множители к цвету ламп.
- **:44** — m_da_color.set(1.f, 1.f, 1.f, 1.f); // [DA_PORT] default white; overridden per item by torch_set_color_*
- **:46** — Налобный фонарь. Значения - те же, что каждый тик шлёт xr_actor.script (UpdateTorch),
- **:192** — Модель автора: у ИГРОКА светит одна лампа — light_render, а light_omni ему не светит
- **:203** — Единственное место, где решается, горит ли лампа игрока.
- **:217** — У ДВУХ ЛАМП РАЗНЫЕ ВЫКЛЮЧАТЕЛИ, и это не усложнение, а суть механики.
- **:241** — Фонарь В РУКАХ ИГРОКА потолок теневых ламп не вытесняет и места в бюджете не
- **:251** — Отчёт о КОНЕЧНОМ состоянии лампы, по факту его смены. Ставится здесь, а не в
- **:264** — Msg("* [DA_PORT] лампа: луч %d, рассеянная %d (torch1 %d, torch2 %d, предмет %d), "
- **:273** — Сообщить СКРИПТУ, что фонарь погашен.
- **:297** — Msg("%s [DA_PORT] фонарь: состояние скрипта %s", ok ? "*" : "~",
- **:301** — Сколько скрытых фонарей у владельца. См. Switch2.
- **:324** — "torch2" - the real flashlight beam (light_render, spot+shadow), toggled
- **:342** — Здесь действует АБСОЛЮТНАЯ семантика: скрипт присылает состояние, а не «переключи».
- **:356** — Налобный включается только если он ОДИН. Второй скрытый фонарь снимается с трупа
- **:362** — Msg("~ [DA_PORT] налобный фонарь не включён: скрытых фонарей в инвентаре %d, должен быть один",
- **:370** — Отчёт о состоянии луча в момент переключения. Ставится не "на всякий случай": когда
- **:378** — Msg("* [DA_PORT] фонарь: %s, луч %s, дальность %.1f, конус %.0f, цвет %.2f/%.2f/%.2f, дин.свет %d, текстура %s",
- **:459** — Always start the actor's torch in the OFF state.
- **:489** — Фонарь игрока обязан быть работоспособен СРАЗУ, как попал к нему в руки, а не после
- **:507** — Фонарь игрока ВСЕГДА начинает погашенным — при новой игре, загрузке сохранения и
- **:530** — --- Dead Air per-item torch light tuning (driven by xr_actor.script apply_torch_type) ---
- **:538** — Ручной фонарь загорается сам при выборе — как палочка и зажигалка.
- **:594** — --- Налобный фонарь (torch2) ---------------------------------------------------------
- **:618** — Чёрный цвет = лампа не светит, хотя формально включена. Так и получалось у фонарика:
- **:630** — Своя текстура проекции налобному. Прожектор светит СКВОЗЬ неё, и без пригодной
- **:685** — Смена аниматора меняет и то, кто задаёт цвет лампы: с ним — он, без него — наш цвет.
- **:696** — Msg("~ [DA_PORT] фонарь: цветовой аниматор '%s' не найден - цвет берётся из настроек предмета", name);
- **:711** — Как у автора: переключается ТИП одной лампы, а не выбор между двумя источниками.
- **:723** — Смена источника света в руках ГАСИТ всё: взял фонарик — палочка погасла, и наоборот.
- **:736** — Either light (torch/omni or torch2/spot) being on needs position/rotation updates.
- **:855** — Здесь цвет луча задаёт АНИМАТОР, кадр за кадром, минуя DaApplyBeam — значит и
- **:897** — Actor's torch/torch2 must stay under local key-press control, not server state.

### `xrGame/Torch.h`

- **:24** — Dead Air's scripts drive two independent lights: "torch" (itms_manager.script
- **:35** — Runtime light tuning driven by Dead Air's xr_actor.script. DA carries ONE hidden
- **:43** — ВТОРОЕ семейство настроек - для налобного фонаря (torch2). DA настраивает луч двумя
- **:81** — "torch2" - the real player-controlled flashlight beam (see m_switched_on2 above).
- **:86** — Dead Air per-item light tuning (xr_actor.script -> torch_set_* bindings). Applied to
- **:101** — Налобный фонарь (torch2_set_* из xr_actor.script). См. m_da2_* выше.
- **:110** — Единственное место, решающее, горит ли лампа игрока. См. Torch.cpp.
- **:113** — Сообщить скрипту, что фонарь погашен (itms_manager.Torch2). См. Torch.cpp.

### `xrGame/UIGameSP.cpp`

- **:125** — The engine deliberately does NOT open the PDA here - the author disabled this in his

### `xrGame/UITimeDilator.cpp`

- **:5** — Ход времени, выбранный игроком (console_commands.cpp). См. stopTimeDilation ниже.
- **:79** — Возвращаем ход времени, выбранный игроком, а не жёсткую единицу.

### `xrGame/Weapon.cpp`

- **:107** — multiplier domain (CoC lineage): identity magnification, not fov degrees
- **:265** — Размытие (глубина резкости) при прицеливании и перезарядке — ВЫКЛЮЧЕНО по умолчанию.
- **:463** — Запоминаем конфигурационные статусы: биты поломок 28/29/30 глушат крепление в
- **:601** — pick the malfunction mask back up from the server object, which is what carried it
- **:666** — P.w_u32(m_weapon_condition_type); // [DA_PORT] malfunction mask - see CSE_ALifeItemWeapon
- **:696** — must mirror CSE_ALifeItemWeapon::UPDATE_Write exactly - an unread field here would
- **:948** — Поломка КРЕПЛЕНИЯ аддона (биты маски 28 — прицел, 29 — глушитель, 30 — подствольник).
- **:1265** — Dead Air belt-only reload for the actor: backpack ammo can't be chambered,
- **:1315** — шанс осечки = базовый (из патрона) + вклад активных битов поломок (m_weapon_condition_type).
- **:1501** — Это и есть ГЛАВНОЕ размытие при прицеливании, а вовсе не zoom_dof выше.
- **:1538** — multiplier domain: back to identity magnification (was g_fov degrees)

### `xrGame/Weapon.h`

- **:132** — Weapons Evolution compat: unjam_motion_mark.script clears the jam from an
- **:191** — Значение из конфига, запомненное при Load. Биты поломок 28/29/30 временно переводят
- **:379** — float fAmmoMisfire{ 0.f }; // базовый шанс осечки из конфига патрона (misfire_chance)

### `xrGame/WeaponAmmo.cpp`

- **:122** — Dead Air prices the box itself on top of the rounds, for price micro-balance.
- **:261** — return res + m_boxCost; // [DA_PORT] box surcharge (0 unless the section sets box_cost)

### `xrGame/WeaponAmmo.h`

- **:81** — u32 m_boxCost; // [DA_PORT] flat surcharge for the box itself, on top of the per-round price

### `xrGame/WeaponBinoculars.cpp`

- **:103** — multiplier domain like CoC-Xray (Weapon.h GetZoomData): scope_factor is an

### `xrGame/WeaponDispersion.cpp`

- **:16** — Dead Air replaced the stock formula entirely, and the port had kept the stock one.
- **:61** — PLUS, not times - see GetConditionDispersionFactor above. The factor is now an additive

### `xrGame/WeaponFire.cpp`

- **:72** — A damaged weapon wears out faster - the author's factor, from

### `xrGame/WeaponMagazined.cpp`

- **:25** — "g_weapon_malfunctions" - see state_Fire. Defined in console_commands.cpp.
- **:76** — see the members in the header. Both optional: a weapon without them simply never picks
- **:79** — Default 0x00FFFFFF, not 0 - otherwise "applies to all weapons" would be a lie.
- **:221** — Dead Air core mechanic: the actor reloads only with ammo carried on the belt
- **:602** — A broken weapon loses rate of fire, and unevenly - the author's block, from
- **:639** — Breakages from firing - the author's block, revived behind "g_weapon_malfunctions".
- **:1013** — Сломанное крепление (биты 28/29/30) не принимает аддон.
- **:1044** — Сломанное крепление держит аддон намертво — снять его нельзя, пока не починишь.
- **:1199** — multiplier domain (CoC lineage): default was 50.0 DEGREES, which under
- **:1410** — Переводчик огня: бит 27 маски поломок.

### `xrGame/WeaponMagazined.h`

- **:60** — Dead Air core mechanic: the actor reloads only with ammo carried on the
- **:127** — Dead Air's two malfunction keys, finally read by the engine.
- **:163** — void TryJamFireModeSelector(); // [DA_PORT] бит 27 маски поломок

### `xrGame/abstract_path_manager_inline.h`

- **:40** — The VERIFY below is compiled out in Release, and CLevelGraph::vertex() is

### `xrGame/action_planner_inline.h`

- **:91** — The THROW(!solution().empty()) that used to sit here crashed the game whenever an
- **:127** — The other half, and by elimination the expensive one. Planning turned out to cost

### `xrGame/ai/crow/ai_crow.cpp`

- **:137** — Dead Air's [m_crow] uses randomized ranges (speed_min/speed_max etc.) instead

### `xrGame/ai/monsters/basemonster/base_monster.cpp`

- **:884** — Same guard the actor's handler already has (Actor_Events.cpp): the

### `xrGame/ai/monsters/poltergeist/poltergeist.cpp`

- **:22** — #include "xrEngine/LightAnimLibrary.h" // цветоанимация света полтергейста
- **:148** — параметры светящегося полтергейста (ключи есть в m_poltergeist.ltx)
- **:331** — обновление светящегося источника (позиция по кости + цветоанимация)
- **:390** — создать светящийся источник на кости головы
- **:413** — погасить и уничтожить свет

### `xrGame/ai/monsters/poltergeist/poltergeist.h`

- **:46** — светящийся полтергейст (point-light на кости головы + цветоанимация light_color_animmator)

### `xrGame/ai/monsters/snork/snork.cpp`

- **:64** — Sleeping/lying and corpse dragging, from the Dead Air alpha (which took the snork over
- **:100** — anim().LinkAction(ACT_LIE_IDLE, has_lie ? eAnimLieIdle : eAnimStandIdle); // [DA_PORT]
- **:105** — anim().LinkAction(ACT_SLEEP, has_lie ? eAnimSleep : eAnimStandIdle); // [DA_PORT]
- **:107** — anim().LinkAction(ACT_DRAG, has_drag ? eAnimDragCorpse : eAnimStandIdle); // [DA_PORT]

### `xrGame/ai/stalker/ai_stalker.cpp`

- **:461** — ⭐ Убитый NPC договаривал начатую фразу.
- **:759** — Counted at the door, because the numbers stopped adding up: sixteen of these run per

### `xrGame/ai/stalker/ai_stalker_fire.cpp`

- **:439** — скрипт форсировал оружие — зафиксировать и пометить актуальность
- **:450** — возвращена проверка стабильности оружия (Alundaio отключал как «тупую») —
- **:795** — считаем любую аномальную зону (без фильтра по restrictor_type) — стоковый фильтр пропускал часть аномалий

### `xrGame/ai_space.cpp`

- **:46** — Msg("* [DA_PORT] CAI_Space::init: before AISpaceBase::Initialize"); FlushLog();
- **:48** — Msg("* [DA_PORT] CAI_Space::init: after AISpaceBase::Initialize"); FlushLog();
- **:56** — Msg("* [DA_PORT] CAI_Space::init: before RestartScriptEngine"); FlushLog();
- **:58** — Msg("* [DA_PORT] CAI_Space::init: after RestartScriptEngine"); FlushLog();
- **:146** — Msg("* [DA_PORT] SetupScriptEngine: before ScriptEngine->init"); FlushLog();
- **:148** — Msg("* [DA_PORT] SetupScriptEngine: after ScriptEngine->init, before RegisterScriptClasses"); FlushLog();
- **:150** — Msg("* [DA_PORT] SetupScriptEngine: after RegisterScriptClasses, before register_script"); FlushLog();
- **:152** — Msg("* [DA_PORT] SetupScriptEngine: after register_script, before LoadCommonScripts"); FlushLog();
- **:154** — Msg("* [DA_PORT] SetupScriptEngine: after LoadCommonScripts"); FlushLog();

### `xrGame/alife_dynamic_object.cpp`

- **:30** — Walk up to the outermost container. A save with a dangling ID_Parent used

### `xrGame/alife_graph_registry.cpp`

- **:84** — The loop above has already added everything the graph points hold for this level.
- **:95** — Msg("! [DA_PORT] ALife: object [%s][%d] already on the level registry - skipping",
- **:129** — tolerate an item that was never level().add()'ed (spawned already attached
- **:198** — m_objects is a VECTOR indexed by the graph vertex id, and the only thing standing
- **:216** — Msg("! [DA_PORT] ALife: object [%s] section[%s] id[%d] has graph vertex %u but the registry "
- **:226** — Registering the same object at the same graph point twice is a hard assert inside
- **:234** — Msg("! [DA_PORT] ALife: object [%s][%d] already registered at graph point %d - skipping",
- **:250** — The SECOND registry, and it needed the same tolerance as the first.
- **:265** — Msg("! [DA_PORT] ALife: object [%s] section[%s] id[%d] already in the level registry - "
- **:285** — no_assert=true. An item spawned already attached to a parent
- **:292** — Same bounds guard as add() - this indexes the same vector with the same
- **:306** — Msg("~ [DA_PORT] graph().remove: object id[%u] absent from level registry "

### `xrGame/alife_schedule_registry.cpp`

- **:22** — The same duplicate tolerance the graph and level registries needed.
- **:36** — Msg("! [DA_PORT] ALife: object [%s] section[%s] id[%d] already in the schedule registry - "

### `xrGame/alife_simulator_base.cpp`

- **:92** — Scripted spawns (alife():create) reach here with whatever the script passed.
- **:97** — Msg("! [DA_PORT] spawn_item: invalid section '%s' - spawn rejected", section ? section : "(null)");
- **:103** — Msg("! [DA_PORT] spawn_item: invalid game_vertex_id %u for section '%s' - spawn rejected",
- **:135** — Release strips the VERIFY and the very next field write AV'd on null (found by
- **:140** — Печатаем и КЛАСС из конфига: без него сообщение называет симптом, но не даёт
- **:144** — Msg("! [DA_PORT] spawn_item: секция '%s' (класс '%s') не даёт серверный ALife-объект - спавн "
- **:285** — An object spawned into a parent that is not registered used to end the

### `xrGame/alife_storage_manager.cpp`

- **:29** — Уборка осиротевших `.tmp` от прерванных сохранений.
- **:119** — Пишем во ВРЕМЕННЫЙ файл и подменяем им настоящий, а не пишем поверх сейва игрока.

### `xrGame/alife_update_manager.cpp`

- **:178** — Level change with a stale holder id used to end the session here
- **:315** — All three are bound to Lua and are called by mod scripts during object release

### `xrGame/configs_dumper.cpp`

- **:85** — Iterate the low quick-access weapon slots (1..4). This used GRENADE_SLOT as the upper

### `xrGame/console_commands.cpp`

- **:2** — #include "xrEngine/Engine.h" // [DA_PORT] da_dev_mode()
- **:55** — #include "da_memory_probe.h" // [DA_PORT] инструменты замера и воспроизведения
- **:65** — ⚠️ [DA_PORT] Ниже — ВНЕ блока #ifdef DEBUG, и это намеренно: проверять отчёт о вылете
- **:68** — Отложенная команда: da_after_load <кадров> <команда с аргументами>.
- **:97** — Намеренная авария: проверка отчёта о вылете на той же сборке, что у игроков.
- **:105** — Msg("~ [DA_PORT] ===== НАМЕРЕННАЯ АВАРИЯ (da_crash_test) =====");
- **:106** — Msg("~ [DA_PORT] Это проверка отчёта о вылете, а НЕ дефект. Если такой лог пришёл от");
- **:107** — Msg("~ [DA_PORT] игрока - значит команду набрали руками. В расследование не брать.");
- **:133** — extern float g_scope_fov; // Actor.cpp [DA_PORT] CoC-Xray compat
- **:170** — see WeaponMagazined::state_Fire - weapons pick up breakages while being fired.
- **:249** — Dump the UI xml files the game actually loads, straight through the engine's VFS, into
- **:277** — Msg("~ [DA_PORT] ui dump: cannot open [%s]", src);
- **:296** — Msg("~ [DA_PORT] ui dump: cannot write [%s]", dst);
- **:300** — Msg("~ [DA_PORT] dumped %u/%u ui xml files to appdata" DELIMITER "logs" DELIMITER "vfs_ui" DELIMITER,
- **:307** — Same trick for shaders, needed to edit the G-buffer output structure for motion vectors.
- **:342** — Msg("~ [DA_PORT] shader dump: cannot open [%s]", src);
- **:360** — Msg("~ [DA_PORT] shader dump: cannot write [%s]", dst);
- **:364** — Msg("~ [DA_PORT] dumped %u/%u shader files from [%s] to appdata" DELIMITER "logs" DELIMITER
- **:382** — Поиск утечки памяти. Подробности и протокол — в da_memory_probe.h.
- **:397** — Крутилки, которые НЕ сохраняются в user.ltx и живут ровно один запуск, регистрируются
- **:401** — Снимок посреди игры: замер для ПОКАДРОВЫХ утечек, которые протокол загрузок не видит.
- **:409** — Весь протокол одной командой: перезагружает последнее сохранение подряд нужное число
- **:768** — Was Level().g_cl_Spawn(args, 0xff, M_SPAWN_OBJECT_LOCAL, pos), a purely client-side
- **:871** — Walk the live in-game HUD window tree and report what is actually on screen: every widget's
- **:892** — Msg("~ [DA_PORT] %s%s [%s] shown=%d abs=(%.0f,%.0f)-(%.0f,%.0f) size=%.0fx%.0f", pad, name ? name : "<noname>",
- **:904** — Msg("~ [DA_PORT] hud flags: draw=%d draw_info=%d draw_map=%d info=%d", psHUD_Flags.test(HUD_DRAW) ? 1 : 0,
- **:911** — Msg("! [DA_PORT] no in-game HUD right now - run this while in the game world");
- **:915** — Msg("~ [DA_PORT] --- HUD tree ---");
- **:917** — Msg("~ [DA_PORT] --- end of HUD tree ---");
- **:921** — Report what every belt item actually gives the actor.
- **:940** — Msg("! [DA_PORT] no level loaded - run this in the game world");
- **:947** — Msg("! [DA_PORT] no actor - run this in the game world");
- **:951** — Msg("~ [DA_PORT] --- belt contents (%u item(s)) ---", (u32)actor->inventory().m_belt.size());
- **:959** — Msg("~ [DA_PORT]   %s : NOT a CArtefact - contributes nothing", sect);
- **:963** — Msg("~ [DA_PORT]   %s : cond=%.3f power=%.5f health=%.5f satiety=%.5f bleed=%.5f rad=%.5f addw=%.2f", sect,
- **:969** — Msg("~ [DA_PORT]   summed power restore = %.5f/s (the artefact tick applies it at double rate)",
- **:974** — Msg("~ [DA_PORT]   load = %.2f kg, carry limit = %.2f, walk limit = %.2f, power now = %.3f",
- **:977** — Msg("~ [DA_PORT] --- end of belt ---");
- **:1781** — Выбранный ИГРОКОМ ход времени. Нужен отдельно от `Device.time_factor()`, потому что
- **:1800** — Сохраняется в user.ltx: иначе значение пришлось бы задавать заново каждый запуск.
- **:2527** — CMD1(CCC_DaMemDump, "da_mem_dump");   // [DA_PORT] таблица памяти по загрузкам
- **:2528** — CMD1(CCC_DaMemReset, "da_mem_reset"); // [DA_PORT] забыть накопленное
- **:2529** — CMD1(CCC_DaMemTest, "da_mem_test");   // [DA_PORT] авто-прогон: N загрузок подряд
- **:2530** — CMD1(CCC_DaMemSnap, "da_mem_snap");   // [DA_PORT] снимок посреди игры: покадровые утечки
- **:2532** — Размытие при прицеливании и перезарядке. По умолчанию выключено: эффект
- **:2539** — Разбор КАЖДОГО радиационного хита по актёру: сколько пришло, сколько сняли
- **:2548** — extern int g_da_mem_probe; // [DA_PORT] выключатель автоматических отметок
- **:2550** — extern int g_da_mem_heapwalk; // [DA_PORT] обход куч: живые аллокации вместо закоммиченного
- **:2552** — extern int g_da_mem_trap_size; // [DA_PORT] размер блока, содержимое которого показываем
- **:2560** — Ловушка в самом аллокаторе: печатает стек в момент выделения блока заданного
- **:2610** — Dead Air compatibility aliases
- **:2616** — "hud_draw_map" used to be mapped onto the shared HUD_DRAW bit - toggling it off
- **:2626** — psHUD_Flags.set(HUD_DRAW_INFO, true); // [DA_PORT] bottom-left readout is on unless the player says otherwise
- **:2633** — nearwall weapon-collision HUD FOV (opt-in, off by default; vars defined in HudItem.cpp)
- **:2645** — CMD4(CCC_Float, "scope_fov", &g_scope_fov, 5.0f, 180.0f); // [DA_PORT] CoC-Xray compat
- **:2647** — Weapons pick up breakages while firing - Dead Air's own mechanic, which its author left
- **:2786** — Developer commands: registered only when the game was started with "-dev".
- **:2804** — Msg("~ [DA_PORT] developer mode: cheat and script commands registered");
- **:2807** — Ход времени доступен в обычной игре, а не только в режиме разработчика: это не читерская
- **:2989** — Registered outside the DEBUG block on purpose: we need it in the Release build we ship
- **:3014** — Намеренная авария для проверки отчёта о вылете.
- **:3053** — Брать широкоформатную разметку и на узком экране. Подробности - у самой

### `xrGame/da_memory_probe.cpp`

- **:111** — Короткая сводка по каждой загрузке — отдельно от Run и с куда большим запасом.
- **:180** — Ловушка аллокатора обязана молчать на время обхода, иначе игра падает в HeapLock.
- **:201** — Обходим ТОЛЬКО СВОИ кучи, а не все кучи процесса.
- **:309** — По умолчанию ВЫКЛЮЧЕНО. Было включено на время охоты за утечкой, и это оказалось
- **:1147** — ---- Отложенное выполнение команды: da_after_load ---------------------------------------
- **:1204** — ---- Команда из командной строки: -da_cmd "<консольная команда>" -------------------

### `xrGame/da_memory_probe.h`

- **:3** — Поиск утечки памяти по повторным загрузкам одного и того же сохранения.
- **:60** — Отложенное выполнение консольной команды после загрузки уровня — см. .cpp.
- **:65** — Один раз за сессию выполняет команду из `-da_cmd "<команда>"`. См. run_headless.ps1.

### `xrGame/game_base.cpp`

- **:9** — This was hardcoded to 10 (10x real time speed) - normally overwritten immediately by

### `xrGame/game_sv_base.h`

- **:130** — GameEventQueue* event_queue() const { return m_event_queue; } // [DA_PORT] для замера памяти

### `xrGame/game_sv_event_queue.h`

- **:38** — Размеры для замера памяти. Очередь держит пул свободных событий, а каждое событие

### `xrGame/game_sv_single.cpp`

- **:3** — #include "da_memory_probe.h" // [DA_PORT]
- **:337** — Самая говорящая отметка перезагрузки: сколько памяти ВЕРНУЛОСЬ, когда старый

### `xrGame/level_path_builder.h`

- **:29** — Consecutive failures, for the hopeless case - see process().
- **:108** — Counted because this is the last unmeasured occupant of the parallel sequence, and
- **:126** — Back off only once a stalker has failed REPEATEDLY.

### `xrGame/material_manager.cpp`

- **:12** — Есть ли лужа в этой точке мира (xrEngine, xr_ioc_cmd.cpp).
- **:89** — Земля под ногами для звука шага. Шаги играет CStepManager через get_current_pair(), а не
- **:115** — Msg("* [DA_PORT] шаги по лужам: материал воды %s (индекс %u)",
- **:122** — Под крышей плеска быть не должно. Картинка это уже учитывает — там гейтом служит
- **:142** — Лог пишется ПО СМЕНЕ состояния и только для игрока, а не по таймеру и не для каждого
- **:153** — Msg("* [DA_PORT] под ногами %s: шум %d, крыша %d, влажность %.2f, пара с водой %d",
- **:165** — Msg("* [DA_PORT] шаги: влажность %.2f, в луже %d, крыша %d, пара с водой есть %d, земля %u",
- **:181** — Шаги по лужам. Земля под ногами считается водой, если в этой точке есть лужа — тогда

### `xrGame/material_manager.h`

- **:41** — Пара БЕЗ подмены на воду: нужна для частиц. Звук в луже должен быть водяным, а вот
- **:46** — Какой материал считать землёй под ногами: обычно тот, что под персонажем, но в луже —

### `xrGame/material_manager_inline.h`

- **:16** — в луже землёй считается вода — см. da_ground_material_idx()

### `xrGame/player_hud.cpp`

- **:91** — Some Dead Air weapon HUD configs reference decorative motions (e.g.
- **:99** — Msg("! [DA_PORT] player_hud_motion_container::load: motion not found [%s] in section [%s], skipping",
- **:439** — Some Dead Air weapon configs reference motion aliases with no matching model
- **:444** — Msg("! [DA_PORT] attachable_hud_item::anim_play: no motion for alias [%s] in model [%s], skipping",

### `xrGame/restricted_object.cpp`

- **:34** — Restriction lists are filled from scripts and can outlive the restrictor

### `xrGame/script_game_object.cpp`

- **:375** — Dead Air compat: index of the current scope variant in the weapon's scopes_sect
- **:398** — Dead Air compat: section name of the weapon's currently selected ammo type

### `xrGame/script_game_object.h`

- **:199** — u32 GetWeaponConditionType();           void SetWeaponConditionType(u32 t); // [DA_PORT] 32-bit malfunction bitmask
- **:276** — bool burer_get_force_anti_aim(); // [DA_PORT] Dead Air compat
- **:891** — Dead Air compat

### `xrGame/script_game_object_inventory_owner.cpp`

- **:28** — #include "ui/UIMainIngameWnd.h" // [DA_PORT] set/get_radiation_detector -> hud states window
- **:1057** — An already-accessible position answers itself - return the vertex under it.
- **:2270** — Dead Air's zone-detector switch. itms_manager.script calls this on the actor every tick:
- **:2305** — set_actor_zoom_inertion: DA scripts (xr_actor.script) push an extra aim-sway factor
- **:2315** — set_actor_recoil_coeff: множитель отдачи камеры. Раз-заглушен.
- **:2333** — Click period of the nearest radiation zone. Returns a float, not a bool — DA's scripts do
- **:2356** — Msg("! [DA_PORT_STUB] Called missing function: %s", __FUNCTION__);
- **:2384** — 32-bit bitmask (bit=malfunction). Was u8 -> truncated bits 8..31 (scripts use bit 28, loop 0..31).

### `xrGame/script_game_object_use2.cpp`

- **:66** — Dead Air compat: DA's rename of CoC's get_force_anti_aim (xr_conditions.burer_anti_aim).

### `xrGame/script_sound.cpp`

- **:30** — Это уведомление, а не ошибка, и стек Lua к нему не нужен.
- **:44** — Msg("~ [DA_PORT] звук \"%s\" отпущен скриптом до конца воспроизведения и остановлен",

### `xrGame/stalker_movement_manager_obstacles_path.cpp`

- **:28** — Putting a stalker that has slid off the navigation mesh back onto it - see below.
- **:179** — Put a stalker that has fallen off the navigation mesh back onto it.
- **:223** — Msg("~ [DA_PORT] AI: [%s] was off the navigation mesh, moved %.2fm back "
- **:228** — Msg("! [DA_PORT] AI: [%s] is stuck off the navigation mesh and the "
- **:236** — Say WHO, and stop saying it every frame.

### `xrGame/step_manager.cpp`

- **:205** — Частицы берутся из ПАРЫ ЗЕМЛИ, а не из той, что пошла на звук. В луже звуковая

### `xrGame/trade2.cpp`

- **:160** — Dead Air reads the condition exponent from config ([trade] buy_condition_koeff),
- **:163** — .11f and the clamp are the author's, and they go together: at full condition the base

### `xrGame/xrGame.cpp`

- **:51** — Dead Air's own [alife]/time_factor is 10 (confirmed via trace) - genuinely their
- **:97** — Msg("* [DA_PORT] create_persistent: before object_factory"); FlushLog();
- **:99** — Msg("* [DA_PORT] create_persistent: after object_factory, before CGamePersistent"); FlushLog();
- **:101** — Msg("* [DA_PORT] create_persistent: after CGamePersistent"); FlushLog();

### `xrGame/xrServer_process_event_reject.cpp`

- **:46** — Печатался УКАЗАТЕЛЬ e_parent под %d вместо id_parent. В логе это выглядело как


## Интерфейс (UI)

*28 файл(ов), 92 правк(и)*


### `xrGame/ui/ArtefactDetectorUI.cpp`

- **:37** — CUIArtefactDetectorHudUI — generic hud_ui 3D artefact screen.

### `xrGame/ui/ArtefactDetectorUI.h`

- **:98** — Generic hud_ui 3D screen: renders artefact blips on a device screen

### `xrGame/ui/Restrictions.cpp`

- **:15** — Answered once per section and remembered.

### `xrGame/ui/UIActorInfo.cpp`

- **:75** — Origin is (0,0), not the parent's position: UICharacterInfo is attached as a child of
- **:95** — The actor's name is deliberately NOT drawn here.
- **:114** — Choose the layout scheme by what the XML actually contains, not by a build flag.
- **:337** — Support both ways of naming a statistic section, instead of picking one at compile time.

### `xrGame/ui/UIActorMenu.cpp`

- **:349** — slot 14 grenade/binocular cell must count as a slot list, else double-click and
- **:353** — slot 5 sidearm cell must count as a slot list too, so double-click/drag-out unequip works.
- **:665** — [[maybe_unused]] CBackpack* backpack = smart_cast<CBackpack*>(item); // [DA_PORT] DA backpacks aren't CBackpack; see BACKPACK_SLOT check below
- **:684** — Dead Air backpacks are artefact-class (SCRPTART), so the CBackpack smart_cast is null and
- **:990** — if (m_pLists[eInventoryBinocularList]) // [DA_PORT] slot 14 grenade/binocular cell - must be cleared
- **:992** — if (m_pLists[eInventorySidearmList]) // [DA_PORT] slot 5 sidearm cell - clear like every other slot list

### `xrGame/ui/UIActorMenu.h`

- **:97** — Dead Air's actor_menu.xml "dragdrop_binocular" cell is engine slot 14 (GRENADE_SLOT).
- **:101** — Dead Air's "dragdrop_sidearm" cell is engine slot 5 (BINOCULAR_SLOT). DA puts every
- **:194** — CUI3tButton* m_trade_barter_button{}; // [DA_PORT] Dead Air barter (goods-for-goods)
- **:365** — bool bFree = false); // [DA_PORT] bFree: barter transfer, no money movement
- **:403** — void OnBtnPerformTradeBarter(CUIWindow* w, void* d); // [DA_PORT] Dead Air barter

### `xrGame/ui/UIActorMenuInitialize.cpp`

- **:211** — slot 14 cell (binocular/grenade). required=false so menus whose xml lacks it don't fatal.
- **:213** — slot 5 cell (sidearm: pistols + binocular item). Has its own condition bar + highlight
- **:230** — "backpack_over" blocker overlay: shown (via capacity->0 in UpdateOutfit) when the worn
- **:268** — remember the backpack cell's full capacity so UpdateOutfit can shrink it to 0 (drawing the
- **:288** — Dead Air's actor_menu.xml has a third trade button (present in the x32 engine's
- **:537** — if (m_trade_barter_button) // [DA_PORT] optional Dead Air barter button
- **:563** — BindDragDropListEvents(m_pLists[eInventoryBinocularList]); // [DA_PORT] slot 14 grenade/binocular cell
- **:564** — BindDragDropListEvents(m_pLists[eInventorySidearmList]);   // [DA_PORT] slot 5 sidearm (pistol/binocular) cell

### `xrGame/ui/UIActorMenuInventory.cpp`

- **:54** — ShowIfExist(m_pLists[eInventorySidearmList], true); // [DA_PORT] slot 5 sidearm (pistol/binocular) cell
- **:252** — m_pLists[eInventorySidearmList], // [DA_PORT] slot 5 sidearm cell
- **:270** — Печать на каждую раскладку предмета: под MASTER_GOLD, которого у нас нет, и вдобавок
- **:396** — Dead Air equips items to small "utility" slot cells whose authored capacity (rows_num x
- **:433** — A Dead Air slot cell can be authored smaller than the item it must hold (the slot-14
- **:548** — Dead Air shares engine slot 14 between the hand grenade and the binocular. Double-
- **:615** — block equipping a backpack (incl. force/drag) when the worn outfit forbids it.
- **:660** — same slot-cell-too-small guard as InitCellForSlot: grow the target cell to fit the
- **:822** — Dead Air backpacks are artefact-class (belt=true) but must never go on the artefact belt.
- **:874** — ⚠️ [DA_PORT] ЯЧЕЙКА МОЖЕТ БЫТЬ ПУСТА, и раньше это роняло игру.
- **:919** — engine slot 14 = Dead Air's binocular/grenade utility slot (GRENADE_SLOT == 14). Route
- **:928** — engine slot 5 = Dead Air's sidearm slot: every pistol and the binocular item live here
- **:1110** — hide "move to slot" for a backpack the worn outfit forbids (scientific suit) - equipping is
- **:1131** — Only offer "unequip / move to bag" for an item that is actually equipped (slot or belt).
- **:1679** — same blocker treatment for the backpack cell: shrink to 0 (draws backpack_over) when the

### `xrGame/ui/UIActorMenuTrade.cpp`

- **:51** — Dead Air barter: characters flagged with <barter_mode> in their profile trade
- **:161** — ShowIfExist(m_trade_barter_button, false); // [DA_PORT]
- **:535** — Dead Air barter: goods-for-goods exchange, money never moves. Prices come from

### `xrGame/ui/UIActorMenu_action.cpp`

- **:83** — Pick the cell under the CURSOR, not under the dragged icon's top-left corner.

### `xrGame/ui/UIActorStateInfo.cpp`

- **:95** — This bar shows SATIETY, not stamina.

### `xrGame/ui/UICellCustomItems.cpp`

- **:37** — Numbering starts at 1, not 0. Every layered icon in Dead Air is declared as

### `xrGame/ui/UIDragDropReferenceList.cpp`

- **:78** — Та же дыра, что и в ветке обмена на поясе (UIActorMenuInventory.cpp): ячейка

### `xrGame/ui/UIHudStatesWnd.cpp`

- **:10** — #include "xrEngine/CustomHUD.h" // [DA_PORT] psHUD_Flags / HUD_DRAW_INFO
- **:249** — The bottom-left bars follow the "hud_draw_info" option, as they do in Dead Air. Their
- **:362** — Ammo, fire mode and grenade count are part of the same readout as the health and
- **:490** — The weapon icon belongs to the same readout as the ammo counts - see Update().
- **:692** — expose the radiation zone's click rate to scripts (game_object:get_radiation_detector)
- **:702** — only click when the actor carries a powered detector (see m_zone_sound_enabled)
- **:874** — Zone detector on/off, driven per tick by itms_manager.script from the actor's geiger +

### `xrGame/ui/UIHudStatesWnd.h`

- **:80** — Dead Air gates the zone-detector clicking on the actor actually carrying a working
- **:111** — driven from Lua via game_object:set_radiation_detector()/get_radiation_detector()

### `xrGame/ui/UIInventoryUtilities.cpp`

- **:372** — Dead Air displays the actor's MaxWalkWeight (base [actor] max_walk_weight ~20kg + gear)
- **:419** — float max = DA_DisplayMaxWeight(pInvOwner); // [DA_PORT] actor shows MaxWalkWeight (see helper above)

### `xrGame/ui/UIItemInfo.cpp`

- **:26** — #include "xrEngine/StringTable/StringTable.h" // [DA_PORT] weapon condition-type localized strings
- **:33** — Список поломок оружия в описании предмета.
- **:353** — append the weapon "condition type" (malfunction) section, as Dead Air does.

### `xrGame/ui/UIMainIngameWnd.cpp`

- **:301** — this used to force the minimap/radar on unconditionally, ignoring hud_draw_map -

### `xrGame/ui/UIOutfitInfo.cpp`

- **:13** — Armour condition as a percentage - the same rule the weapon side uses.
- **:82** — Число рядом с полоской показывается ВСЕГДА, если разметка его описала.
- **:136** — Со знаком процента: величина показывается в процентах (magnitude = 100 в
- **:163** — Condition row: icon, label, value. All three optional - a layout without them is
- **:247** — da_set_outfit_condition_text(m_textCondition2, *cur_outfit); // [DA_PORT]

### `xrGame/ui/UIOutfitInfo.h`

- **:48** — Armour condition as a number, the way Dead Air shows it.

### `xrGame/ui/UIRankingWnd.cpp`

- **:97** — The actor portrait block is deliberately not built on this screen.

### `xrGame/ui/UIWpnParams.cpp`

- **:12** — The condition percentage, written the way Dead Air writes it.
- **:81** — AttachChild(&m_textConditionW);  // [DA_PORT]
- **:82** — AttachChild(&m_textConditionW2); // [DA_PORT]
- **:104** — The numeric condition beside the bars - see da_set_condition_text. Optional nodes, so a
- **:181** — da_set_condition_text(m_textConditionW2, cur_wpn); // [DA_PORT] numeric condition, see the helper
- **:306** — Optional on purpose: layouts that never had these nodes keep working untouched,
- **:334** — da_set_condition_text(m_textCondition2, cur_item); // [DA_PORT]

### `xrGame/ui/UIWpnParams.h`

- **:45** — see CUIConditionParams below - same pair, same author, in the weapon parameter block.
- **:66** — The numeric condition, which the port was missing.

### `xrGame/ui/ui_af_params.cpp`

- **:55** — Артефакт в контейнере? Отличаем по хвосту секции — ровно так же, как это делает сам мод
- **:73** — Значение, которое реально живёт на предмете, а не в его секции.
- **:122** — The caption was passed as a raw string-table KEY, so the inventory showed the literal
- **:162** — Числа берём у САМОГО предмета, а не у его секции.
- **:205** — У артефакта в контейнере строка радиации отвечает на другой вопрос — сколько её

### `xrGame/ui/ui_af_params.h`

- **:47** — Подпись строки может зависеть от предмета: у артефакта в контейнере строка радиации

### `xrUICore/ComboBox/UIComboBox.cpp`

- **:88** — Order swapped, and it is not cosmetic. Both texture sets are complete in this mod's
- **:209** — Пустой список больше не роняет игру.
- **:230** — Msg("! [DA_PORT] список [%s]: нельзя выбрать пункт %d, в списке %u пунктов "
- **:281** — An expanded list has to cover whatever sits below it, and by default it does not: windows

### `xrUICore/ComboBox/UIComboBox.h`

- **:65** — Raise this control above its siblings while its list is open, and put it back after.
- **:77** — Where this control sat among its parent's children before its list was opened, so the

### `xrUICore/Windows/UIFrameWindow.cpp`

- **:198** — Some Dead Air-defined windows (e.g. the debug menu from ui_debug_main.script) are


## Мост Lua ↔ C++

*13 файл(ов), 44 правк(и)*


### `xrGame/alife_simulator_script.cpp`

- **:142** — Reachable from any script. THROW2 does fire in Release, but it fires by
- **:193** — if (!item) // [DA_PORT] spawn_item now rejects bad section/vertex instead of crashing
- **:253** — if (!item) // [DA_PORT] bad section/vertex rejected inside - give Lua nil, don't THROW
- **:269** — if (!item) // [DA_PORT] bad section/vertex rejected inside - give Lua nil, don't THROW

### `xrGame/base_client_classes_script.cpp`

- **:26** — Msg("* [DA_PORT] CGameObject::script_register: before module"); FlushLog();
- **:66** — Msg("* [DA_PORT] CGameObject::script_register: after module"); FlushLog();

### `xrGame/da_script_functor.cpp`

- **:6** — See da_script_functor.h. Preconditions are re-evaluated every time a

### `xrGame/da_script_functor.h`

- **:3** — Safe lookup of a named Lua function.

### `xrGame/fs_registrator_script.cpp`

- **:180** — Dead Air's debug menu (ui_debug_main.script - the "Advanced"/animations tab, opened via

### `xrGame/level_script.cpp`

- **:195** — Dead Air's name for the current rain intensity; it is the same 0..1 value rain_factor()
- **:203** — guard like xray-monolith: DA scripts feed stored/offline vertex ids here and
- **:207** — Msg("! [DA_PORT] vertex_in_direction: invalid level_vertex_id %u", level_vertex_id);
- **:221** — guard like xray-monolith (level_script.cpp:425): invalid id -> zero vector
- **:225** — Msg("! [DA_PORT] vertex_position: invalid level_vertex_id %u", level_vertex_id);

### `xrGame/script_game_object_script3.cpp`

- **:12** — #include "Torch.h" // [DA_PORT] for the real torch_set_* light-tuning bindings
- **:13** — #include "Actor.h" // [DA_PORT] for start_item_placement binding
- **:40** — Called from CActor::ConfirmItemPlacement (Actor.cpp is not a script TU, so luabind can't
- **:273** — Состояние луча из движка. У мода на этот счёт есть свой флаг
- **:280** — flashlight/glowstick/lighter light tuning — REAL impl. Dead Air's xr_actor.script
- **:294** — "Установить" placement preview: start the ghost-follows-crosshair mode for an item.
- **:297** — Налобный фонарь. Были заглушками - и налобный свет не работал вовсе: щелчок

### `xrScriptEngine/ScriptEngineScript.cpp`

- **:18** — was #ifndef MASTER_GOLD - our Release defines MASTER_GOLD, which silently

### `xrScriptEngine/ScriptExporter.cpp`

- **:48** — if (build_count > 500) { Msg("* [DA_PORT] sort: LOOP DETECTED in node list at %zu", build_count); FlushLog(); break; }

### `xrScriptEngine/script_callback_ex.h`

- **:21** — "-da_lua_trace": log every engine->script object-callback dispatch with the
- **:119** — da_trace_script_callback(m_functor); // [DA_PORT] no-op without -da_lua_trace
- **:151** — da_trace_script_callback(m_functor); // [DA_PORT] no-op without -da_lua_trace

### `xrScriptEngine/script_engine.cpp`

- **:143** — Прежний буфер обязан освободиться здесь. Освобождал его только деструктор
- **:368** — permanent load trace: which scripts actually load and in what order (cheap -
- **:690** — "-da_lua_trace" launch flag: whole-mod engine->script dispatch tracing
- **:702** — this pcall error handler runs BEFORE the stack unwinds - the only moment the
- **:818** — Msg("* [DA_PORT] ScriptEngine::init: before reinit"); FlushLog();
- **:820** — Msg("* [DA_PORT] ScriptEngine::init: after reinit, before luabind::open"); FlushLog();
- **:822** — Msg("* [DA_PORT] ScriptEngine::init: after luabind::open"); FlushLog();
- **:838** — Msg("* [DA_PORT] ScriptEngine::init: before setup_callbacks"); FlushLog();
- **:840** — Msg("* [DA_PORT] ScriptEngine::init: after setup_callbacks, before exporter"); FlushLog();
- **:843** — Msg("* [DA_PORT] ScriptEngine::init: after exporter"); FlushLog();
- **:861** — Msg("* [DA_PORT] ScriptEngine::init: before open_lib base"); FlushLog();
- **:878** — Msg("* [DA_PORT] ScriptEngine::init: after open_lib, before randomize"); FlushLog();
- **:880** — Dead Air compat: register globals that DA scripts expect early
- **:939** — Msg("* [DA_PORT] ScriptEngine::init: before process_file_if_exists(_G)"); FlushLog();
- **:941** — Msg("* [DA_PORT] ScriptEngine::init: after process_file_if_exists(_G)"); FlushLog();
- **:946** — Msg("* [DA_PORT] ScriptEngine::init: DONE"); FlushLog();

### `xrScriptEngine/script_thread.cpp`

- **:49** — Текст команды заворачивается в функцию, и раньше результат складывался в

### `xrUICore/ui_export_script.cpp`

- **:635** — Takes ONE packed colour, not four components.


## Серверные сущности / ALife

*7 файл(ов), 16 правк(и)*


### `xrServerEntities/inventory_space.h`

- **:14** — Stock CoP/OpenXRay puts hand grenades on this slot (=4, config slot 3). Dead Air
- **:28** — Dead Air defines more inventory slots than stock CoC (system.ltx [inventory] has

### `xrServerEntities/object_factory_inline.h`

- **:19** — Msg("* [DA_PORT] object_factory: creating new (g_object_factory=null)"); FlushLog();
- **:21** — Msg("* [DA_PORT] object_factory: after xr_new, before init()"); FlushLog();
- **:23** — Msg("* [DA_PORT] object_factory: after init()"); FlushLog();

### `xrServerEntities/specific_character.cpp`

- **:79** — Dead Air barter traders flag (character_desc profiles)
- **:165** — bool CSpecificCharacter::barter_mode() const { return data()->m_barter_mode; } // [DA_PORT]

### `xrServerEntities/specific_character.h`

- **:35** — Dead Air: character profiles mark barter traders with <barter_mode>1</barter_mode>
- **:133** — bool barter_mode() const; // [DA_PORT] Dead Air barter traders

### `xrServerEntities/xrServer_Objects.h`

- **:168** — 128 -> 129: the weapon packet gained condition_type, the malfunction mask. Bumping this is

### `xrServerEntities/xrServer_Objects_ALife_Items.cpp`

- **:496** — condition_type = 0; // [DA_PORT] no breakages until something says otherwise
- **:537** — the malfunction mask, carried client -> server object so that going offline keeps it.
- **:555** — tNetPacket.w_u32(condition_type); // [DA_PORT] see UPDATE_Read
- **:574** — Version-gated exactly the way every field above it is, which is what makes adding it
- **:592** — tNetPacket.w_u32(condition_type); // [DA_PORT] see STATE_Read - guarded there by SPAWN_VERSION 129

### `xrServerEntities/xrServer_Objects_ALife_Items.h`

- **:209** — The malfunction mask, on the SERVER object so that it survives.


## Звук

*6 файл(ов), 18 правк(и)*


### `xrSound/OpenALDeviceList.cpp`

- **:38** — OpenAL Soft returns device names as UTF-8 on Windows, but the game's fonts and string
- **:77** — Имя без служебного префикса бэкенда (см. Sound.h).
- **:93** — Слева от скобки стоит не имя, а РОЛЬ выхода — Windows пишет так, когда имя самого
- **:124** — Имя устройства для ЛЮДЕЙ: то, что стоит В МЕНЮ, в консоли и в user.ltx.
- **:273** — Первым пунктом — «системное устройство». Это не имя конкретной звуковой карты, а
- **:281** — Only the string shown in the UI/console is transcoded; m_devices[i].name keeps the
- **:286** — Если без скобки два устройства свелись к одной строке (две пары наушников — обе
- **:333** — Разрешает ВЫБОР игрока (snd_device_id) в индекс устройства, которое сейчас будем

### `xrSound/Sound.h`

- **:53** — Два разных числа, и путать их нельзя.
- **:73** — OpenAL Soft приписывает это к имени КАЖДОГО устройства: «OpenAL Soft on Наушники (JBL
- **:79** — Имя устройства для показа и для user.ltx: без служебного префикса и без хвостовой скобки

### `xrSound/SoundRender_Core.cpp`

- **:20** — XRSOUND_API int psSoundTargets = 256; // больше одновременных звуков (было 32) — меньше обрезания

### `xrSound/SoundRender_CoreA.cpp`

- **:44** — Открываем то, что разрешил SelectBestDevice, а не выбор игрока: «авто» индексом не

### `xrSound/SoundRender_Emitter.cpp`

- **:15** — if (source()->channels_num() == 1 && _valid(pos)) // не ставить NaN-позицию (краш/глитч звука)
- **:151** — open() теперь честно возвращает ноль на неразбираемом файле (см. там же). Молчание

### `xrSound/SoundRender_Source.cpp`

- **:140** — не const: FS.r_close берёт ссылку на изменяемый указатель
- **:146** — Результат НЕ проверялся вовсе, и структура возвращалась в любом случае. На битом
- **:166** — Ноль сюда теперь доходит штатно — open() возвращает его на неразбираемом файле,


## Ядро и прочее

*24 файл(ов), 47 правк(и)*


### `Include/xrRender/xrRender.h`

- **:5** — OpenGL-рендерер удалён из сборки: остался единственный модуль — R4.

### `xrAICore/AISpaceBase.cpp`

- **:20** — See the header for why these exist. Slot 0 is the engine everything used before and

### `xrAICore/Components/problem_solver_inline.h`

- **:361** — Split the two halves of this apart, because they cost very differently and only one of

### `xrAICore/Navigation/PatrolPath/patrol_path_storage.cpp`

- **:15** — Освобождение реестра. Удалять его через delete_data НЕЛЬЗЯ, хотя именно так и было.
- **:82** — Через destroy_registry(), а не clear(): в реестре лежат УКАЗАТЕЛИ, и clear() про них
- **:110** — И этот путь надо освободить. insert по существующему ключу не отбрасывает

### `xrAICore/Navigation/PatrolPath/patrol_path_storage.h`

- **:36** — Освободить пути, помня про алиасы: одно значение лежит под несколькими ключами,

### `xrAICore/Navigation/level_graph.cpp`

- **:46** — Менеджер узлов удалялся... нигде. Деструктор закрывал только m_reader, а

### `xrCDB/ISpatial.cpp`

- **:340** — VERIFY(octant < 8) removed: the if-guard below handles the error
- **:345** — N_sub is not among N's children: tree inconsistency reached the release
- **:349** — Msg("! [DA_PORT] ISpatial_DB::_remove: N_sub %p not a child of N %p, skipping prune", (void*)N_sub, (void*)N);

### `xrCore/Containers/FixedMap.h`

- **:134** — Старый массив узлов освобождался сырым xr_free, без деструкторов. Для

### `xrCore/Debug/StackTrace.cpp`

- **:67** — was GetModuleHandleA (only finds an already-loaded module); the MinGW build

### `xrCore/FTimer.cpp`

- **:6** — Counters for the GOAP planner, filled in problem_solver_inline.h and printed by the

### `xrCore/FTimer.h`

- **:163** — See FTimer.cpp - GOAP planner accounting for the performance dump.

### `xrCore/LocatorAPI.cpp`

- **:522** — --- Выбор сезона ------------------------------------------------------------------------
- **:570** — Осенью летний архив не подключаем вовсе — см. da_seasonal_archive_enabled.

### `xrCore/Threading/Lock.cpp`

- **:66** — Lock out-of-line: GCC LTO was eliding the inline Lock() ctor for members

### `xrCore/Threading/TaskManager.cpp`

- **:320** — Give the core away while there is nothing to do here.

### `xrCore/log.cpp`

- **:33** — Ловушка аллокатора обязана молчать, пока работает логгер: она печатает найденное через
- **:222** — Оставляем последние N логов, остальные удаляем.
- **:270** — ---- Свой лог на КАЖДЫЙ запуск: ..._001.log, _002.log и так далее ----------------

### `xrCore/string_concatenations.cpp`

- **:108** — Msg("! [DA_PORT] check_stack_overflow: stack near limit (sp=0x%IX low=0x%IX inc=%u)",

### `xrCore/xrDebug.cpp`

- **:16** — #   include <tlhelp32.h> // [DA_PORT] карта модулей при аварии
- **:517** — ---- Карта загруженных модулей для расшифровки стека -------------------------------
- **:565** — Сначала - ЧТО и ГДЕ, потом стек. Прежде отчёт начинался сразу со стека, и вид
- **:586** — Msg("! [DA_PORT] отказ: %s (код %08X), адрес кода %p", kind, er->ExceptionCode,
- **:593** — Msg("! [DA_PORT]   %s по адресу %p", what, (void*)er->ExceptionInformation[1]);
- **:616** — Карта модулей - то, без чего чужой стек не расшифровывается.
- **:792** — Перехватчик отказов, которых штатный обработчик НЕ ВИДИТ.
- **:829** — Msg("! [DA_PORT] СМЕРТЕЛЬНЫЙ ОТКАЗ: %s (код %08X), обычный обработчик такое не увидит", name, code);
- **:855** — Первым в цепочке: см. da_fatal_vectored.

### `xrCore/xrMemory.cpp`

- **:2** — #include "Debug/StackTrace.h" // [DA_PORT] ловушка на выделение памяти
- **:191** — Приведённое выше рассуждение для нашей сборки НЕ работает, и замер это показал.
- **:226** — Msg("* [DA_PORT] mem_compact: %u -> %u МБ (вернулось %d МБ) за %.1f мс",
- **:243** — Ловушка на выделение памяти заданного размера — со снимком стека.
- **:262** — Пока счётчик больше нуля, ловушка молчит. Взводит его логгер (log.cpp) на время своей
- **:300** — Снятие стека сериализуется. BuildStackTrace идёт через DbgHelp, а он НЕ
- **:327** — da_alloc_trap(size); // [DA_PORT]

### `xrNetServer/NET_Client.h`

- **:27** — Счётчики для замера памяти. Пул хранит NET_Packet целиком — по шестнадцать
- **:118** — см. INetQueue::ready_count

### `xrNetServer/NET_PlayersMonitor.h`

- **:15** — CRITICAL_SECTION csPlayersCS; // [DA_PORT] direct CRITICAL_SECTION, not Lock —

### `xrNetServer/empty/NET_Client.cpp`

- **:105** — ПОЧИНЕНА УТЕЧКА. Здесь возврат пакета в пул был закомментирован, а сам указатель

### `xrNetServer/empty/NET_Client.h`

- **:28** — Счётчики для замера памяти: пул хранит NET_Packet целиком, по 16 килобайт на штуку.
- **:114** — см. INetQueue::ready_count

### `xrUICore/ui_base.cpp`

- **:335** — На НЕширокоформатном экране всё равно берём широкоформатную разметку, если она есть.

### `xr_3da/entry_point.cpp`

- **:24** — Рендерер ровно один — R4. GL убран из сборки целиком: он не компилировался и при
