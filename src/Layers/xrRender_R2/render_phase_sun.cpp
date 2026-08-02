#include "stdafx.h"

#include "r2_R_sun_support.h"
#include "xrCore/Threading/ParallelFor.hpp"

namespace xray::render::RENDER_NAMESPACE
{
void render_sun::init()
{
    float fBias = -0.0000025f;

    m_sun_cascades[0].reset_chain = true;
    m_sun_cascades[0].size = 20;
    m_sun_cascades[0].bias = m_sun_cascades[0].size * fBias;

    m_sun_cascades[1].size = 40;
    m_sun_cascades[1].bias = m_sun_cascades[1].size * fBias;

    m_sun_cascades[2].size = 160;
    m_sun_cascades[2].bias = m_sun_cascades[2].size * fBias;

    // 	for( u32 i = 0; i < cascade_count; ++i )
    // 	{
    // 		m_sun_cascades[i].size = size;
    // 		size *= MAP_GROW_FACTOR;
    // 	}
    /// 	m_sun_cascades[m_sun_cascades.size()-1].size = 80;
    sun = (light*)RImplementation.Lights.sun._get();

    const Fcolor sun_color = sun->color;
    o.active = ps_r2_ls_flags.test(R2FLAG_SUN) && (u_diffuse2s(sun_color.r, sun_color.g, sun_color.b) > EPS);
    if (RImplementation.o.sunstatic)
        o.active = false;

    if (!o.active)
        return;

    // pre-allocate contexts
    for (u32 i = 0; i < R__NUM_SUN_CASCADES; ++i)
    {
        contexts_ids[i] = RImplementation.alloc_context();
        VERIFY(contexts_ids[i] != R_dsgraph_structure::INVALID_CONTEXT_ID);
    }

    o.mt_calc_enabled = RImplementation.o.mt_calculate;
    o.mt_draw_enabled = RImplementation.o.mt_render;
}

// [DA_PORT] ---- Кэш теневых карт солнца ------------------------------------------------------
//
// Зачем. Каждый кадр в теневую карту заново отрисовывается ВСЯ статика каскада: замером da_sun_log
// это 97 объектов у ближнего, 175 у среднего и 359 у дальнего. При этом содержимое дальних каскадов
// от кадра к кадру практически не меняется — дома и деревья неподвижны, солнце ползёт медленно, а
// ящик каскада при ходьбе смещается на малую долю своего размера (160 м у дальнего).
//
// Что делаем. Держим снятую карту в слоте и не перерисовываем её, пока она годна. Годность решают
// три условия, и все три обязательны:
//
//   • камера не ушла далеко — порог пропорционален размеру каскада, потому что смещение на метр для
//     ящика в 20 м и в 160 м это совершенно разные доли кадра;
//   • солнце не повернулось — иначе тени поедут, а это заметно даже на статике;
//   • карта не слишком стара — страховка на всё остальное: разрушаемые объекты, смена погоды,
//     подгрузка геометрии. Дешевле обновлять раз в N миллисекунд, чем перечислять причины.
//
// ⚠️ Срок жизни во ВРЕМЕНИ, а не в кадрах, и это принципиально. Сперва он считался в кадрах, и на
// быстрой машине всё выглядело прекрасно: 16 кадров при 270 к/с — это 60 мс устаревания. Но та же
// настройка при 30 к/с даёт уже 533 мс, то есть полсекунды замороженных теней. Хуже всего, что
// портится картинка тем сильнее, чем слабее компьютер, — а слабый компьютер и есть та аудитория,
// ради которой оптимизация делается. Замер на быстрой машине этого показать не может в принципе.
//
// ⚠️ ГЛАВНОЕ: если карта берётся из кэша, её МАТРИЦУ ТОЖЕ НЕЛЬЗЯ ПЕРЕСЧИТЫВАТЬ. Накопление света
// выбирает тень по матрице каскада; посчитай её заново для сдвинувшейся камеры — и выборка пойдёт
// из карты, снятой под другим ракурсом. Тени поедут по всему экрану, причём тем сильнее, чем дольше
// живёт кэш. Поэтому решение принимается ЗДЕСЬ, до записи xform, и render() обязан ему следовать.
//
// ⚠️ Динамика (NPC, актёр) рисуется в ту же карту, что и статика, поэтому в кэшированном каскаде её
// тень замирает вместе с остальным. Отсюда значение по умолчанию: кэшируются только средний и
// дальний каскады, где чужие фигуры далеко и их тень занимает единицы пикселей. Ближний каскад
// (0..20 м, где стоит сам игрок) кэшируется только принудительно, ключом da_smap_cache_near — это
// режим для замера, а не для игры.
// [DA_PORT] Умолчание 100 мс («средняя точность»), а не 0. Включено после проверки ГЛАЗАМИ: сравнение
// крайних положений — «низкая» (150 мс) против «ультра» (без кэша) — разницы в картинке не выявило.
// Числа этот вопрос закрыть не могли: тень на средних каскадах отстаёт по построению, и весь вопрос
// был в том, заметно ли. Ноль означал бы, что оптимизацию получают только открывшие меню.
u32 ps_da_smap_cache = 100;    // 0 = выключено; N = сколько МИЛЛИСЕКУНД карта живёт без перерисовки
// [DA_PORT] u32, а не int, потому что настройку показывает список в меню (CCC_Token хранит u32).
int ps_da_smap_cache_near = 0; // 1 = кэшировать и ближний каскад (только для замеров)
u32 g_da_smap_skipped = 0;     // сколько отрисовок пропущено с последнего замера
u32 g_da_smap_skipped_by[R__NUM_SUN_CASCADES] = {}; // и то же по каскадам — какой кэшируется реально
u32 g_da_smap_drawn_by[R__NUM_SUN_CASCADES] = {};   // сколько раз каскад всё же перерисовали

bool render_sun::da_smap_should_render(u32 cascade_ind, const Fmatrix& /*fresh_xform*/) const
{
    if (ps_da_smap_cache == 0)
        return true;

    // Ближний каскад — только по явному разрешению: в нём живут тени игрока и всего, что рядом.
    if (cascade_ind == 0 && !ps_da_smap_cache_near)
        return true;

    const smap_cache& c = m_smap_cache[cascade_ind];
    if (!c.valid)
        return true;

    if (Device.dwTimeGlobal - c.time_ms >= ps_da_smap_cache)
        return true;

    // Порог смещения — доля размера каскада. 1/32 подобрана так, чтобы у дальнего (160 м) он вышел
    // около пяти метров: на таком расстоянии сдвиг тени от статики в кадре не читается.
    const float move_limit = m_sun_cascades[cascade_ind].size / 32.f;
    if (Device.vCameraPosition.distance_to(c.cam_pos) > move_limit)
        return true;

    // Поворот солнца — через длину хорды между направлениями, а не через acos скалярного
    // произведения: у почти сонаправленных векторов acos теряет точность и выдаёт мусор порядка
    // 0.03 градуса из воздуха. На этом мы уже обжигались в пробе камеры.
    const float chord = sun->direction.distance_to(c.sun_dir);
    if (rad2deg(2.f * asinf(chord > 2.f ? 1.f : chord * 0.5f)) > 0.05f)
        return true;

    return false;
}

void render_sun::calculate()
{
    ZoneScoped;

    need_to_render_sunshafts = RImplementation.Target->need_to_render_sunshafts();
    last_cascade_chain_mode = m_sun_cascades[R__NUM_SUN_CASCADES - 1].reset_chain;
    if (need_to_render_sunshafts)
        m_sun_cascades[R__NUM_SUN_CASCADES - 1].reset_chain = true;

    // Lets begin from base frustum
    Fmatrix fullxform_inv = Device.mInvFullTransform;

    // Create approximate ortho-xform
    // view: auto find 'up' and 'right' vectors
    Fmatrix mdir_View, mdir_Project;
    Fvector L_dir, L_up, L_right, L_pos;
    L_pos.set(sun->position);
    L_dir.set(sun->direction).normalize();
    L_right.set(1, 0, 0);
    if (_abs(L_right.dotproduct(L_dir)) > .99f)
        L_right.set(0, 0, 1);
    L_up.crossproduct(L_dir, L_right).normalize();
    L_right.crossproduct(L_up, L_dir).normalize();
    mdir_View.build_camera_dir(L_pos, L_dir, L_up);

    // THIS NEED TO BE A CONSTATNT
    Fplane light_top_plane;
    light_top_plane.build_unit_normal(L_pos, L_dir);
    float dist = light_top_plane.classify(Device.vCameraPosition);

    // build viewport xform
    float view_dim = float(RImplementation.o.smapsize);
    Fmatrix m_viewport =
    {
        view_dim / 2.f, 0.0f, 0.0f, 0.0f,
        0.0f, -view_dim / 2.f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        view_dim / 2.f, view_dim / 2.f, 0.0f, 1.0f
    };
    Fmatrix m_viewport_inv;
#if defined(USE_OGL)
    XRMatrixInverse(&m_viewport_inv, nullptr, m_viewport);
#else
    XMStoreFloat4x4((XMFLOAT4X4*)&m_viewport_inv,
        XMMatrixInverse(nullptr, XMLoadFloat4x4((XMFLOAT4X4*)&m_viewport)));
#endif

    // Compute volume(s) - something like a frustum for infinite directional light
    // Also compute virtual light position and sector it is inside
    xr_vector<Fplane> cull_planes;

    CFrustum cull_frustum[R__NUM_SUN_CASCADES];
    Fvector3 cull_COP[R__NUM_SUN_CASCADES];
    Fmatrix cull_xform[R__NUM_SUN_CASCADES];

    // [DA_PORT] Решение о кэше принимается ОДНО НА ВСЕ кэшируемые каскады, а не отдельно на каждый.
    //
    // Дальний каскад строит свой световой объём по матрице ПРЕДЫДУЩЕГО (accum_direct_cascade,
    // ветка SE_SUN_FAR: `inv_XDcombine.invert(xform_prev)`). Замороженный средний рядом со свежим
    // дальним означал бы, что дальний накладывает свет по устаревшему ящику — а это ровно тот
    // дефект, который уже был: экран режет ровная диагональ с вершиной на солнце.
    //
    // Поэтому: нужен хоть одному — перерисовываем всю группу. Выигрыш от этого почти не страдает
    // (условия у соседних каскадов срабатывают почти одновременно), а целый класс рассогласований
    // закрывается сразу.
    // ⚠️ Голосуют ТОЛЬКО кэшируемые каскады. Первая версия опрашивала все подряд, включая ближний, а
    // он при выключенном da_smap_cache_near всегда отвечает «рисовать» — один такой голос делал
    // группу вечно свежей, и кэш не срабатывал НИ РАЗУ. В логе это выглядело как «пропущено
    // отрисовок 0» при включённом кэше: механизм на месте, а работы не убавилось.
    const auto cacheable = [](u32 i) { return i > 0 || ps_da_smap_cache_near != 0; };

    bool group_needs_render = false;
    for (u32 i = 0; i < R__NUM_SUN_CASCADES; ++i)
        if (cacheable(i) && da_smap_should_render(i, Fidentity))
            group_needs_render = true;

    for (u32 cascade_ind = 0; cascade_ind < R__NUM_SUN_CASCADES; ++cascade_ind)
    {
        cull_planes.clear();

        //******************************* Need to be placed after cuboid built **************************
        // COP - 100 km away
        cull_COP[cascade_ind].mad(Device.vCameraPosition, sun->direction, -tweak_COP_initial_offs);

#ifdef _DEBUG
        typedef FixedConvexVolume<true> t_cuboid;
#else
        typedef FixedConvexVolume<false> t_cuboid;
#endif

        t_cuboid light_cuboid;
        {
            // Initialize the first cascade rays, then each cascade will initialize rays for next one.
            if (cascade_ind == 0 || m_sun_cascades[cascade_ind].reset_chain)
            {
                Fvector3 near_p, edge_vec;
                light_cuboid.view_frustum_rays.reserve(4);
                for (int p = 0; p < 4; p++)
                {
                    near_p = wform(fullxform_inv, sun::corners[sun::facetable[4][p]]);

                    edge_vec = wform(fullxform_inv, sun::corners[sun::facetable[5][p]]);
                    edge_vec.sub(near_p);
                    edge_vec.normalize();

                    light_cuboid.view_frustum_rays.emplace_back(near_p, edge_vec);
                }
            }
            else
                light_cuboid.view_frustum_rays = m_sun_cascades[cascade_ind].rays;

            light_cuboid.view_ray.P = Device.vCameraPosition;
            light_cuboid.view_ray.D = Device.vCameraDirection;
            light_cuboid.light_ray.P = L_pos;
            light_cuboid.light_ray.D = L_dir;
        }

        float map_size = m_sun_cascades[cascade_ind].size;
#if defined(USE_OGL)
        XRMatrixOrthoOffCenterLH(&mdir_Project, -map_size * 0.5f, map_size * 0.5f, -map_size * 0.5f,
                                   map_size * 0.5f, 0.1f, dist + /*sqrt(2)*/1.41421f * map_size);
#else
        XMStoreFloat4x4((XMFLOAT4X4*)&mdir_Project, XMMatrixOrthographicOffCenterLH(
            -map_size * 0.5f, map_size * 0.5f, -map_size * 0.5f,
            map_size * 0.5f, 0.1f, dist + /*sqrt(2)*/ 1.41421f * map_size)
        );
#endif
        //////////////////////////////////////////////////////////////////////////
        // snap view-position to pixel
        cull_xform[cascade_ind].mul(mdir_Project, mdir_View);
        Fmatrix cull_xform_inv;
        cull_xform_inv.invert(cull_xform[cascade_ind]);

        for (int p = 0; p < 8; p++)
        {
            Fvector3 xf = wform(cull_xform_inv, sun::corners[p]);
            light_cuboid.light_cuboid_points[p] = xf;
        }

        // only side planes
        for (int plane = 0; plane < 4; plane++)
        {
            for (int pt = 0; pt < 4; pt++)
            {
                int asd = sun::facetable[plane][pt];
                light_cuboid.light_cuboid_polys[plane].points[pt] = asd;
            }
        }

        Fvector lightXZshift;
        light_cuboid.compute_caster_model_fixed(
            cull_planes, lightXZshift,
            m_sun_cascades[cascade_ind].size,
            m_sun_cascades[cascade_ind].reset_chain
        );

        // Initialize rays for the next cascade
        if (cascade_ind < R__NUM_SUN_CASCADES - 1)
            m_sun_cascades[cascade_ind + 1].rays = light_cuboid.view_frustum_rays;

#ifdef DEBUG
        static bool draw_debug = false;
        if (draw_debug && cascade_ind == 0)
            for (u32 it = 0; it < cull_planes.size(); it++)
                RImplementation.Target->dbg_addplane(cull_planes[it], it * 0xFFF);
#endif

        Fvector cam_shifted = L_pos;
        cam_shifted.add(lightXZshift);

        // rebuild the view transform with the shift.
        mdir_View.identity();
        mdir_View.build_camera_dir(cam_shifted, L_dir, L_up);
        cull_xform[cascade_ind].identity();
        cull_xform[cascade_ind].mul(mdir_Project, mdir_View);
        cull_xform_inv.invert(cull_xform[cascade_ind]);

        // Create frustum for query
        cull_frustum[cascade_ind]._clear();
        for (auto& cull_plane : cull_planes)
            cull_frustum[cascade_ind]._add(cull_plane);

        {
            Fvector cam_proj = Device.vCameraPosition;
            const float align_aim_step_coef = 4.f;
            cam_proj.set(floorf(cam_proj.x / align_aim_step_coef) + align_aim_step_coef / 2,
                         floorf(cam_proj.y / align_aim_step_coef) + align_aim_step_coef / 2,
                         floorf(cam_proj.z / align_aim_step_coef) + align_aim_step_coef / 2);
            cam_proj.mul(align_aim_step_coef);
            Fvector cam_pixel = wform(cull_xform[cascade_ind], cam_proj);
            cam_pixel = wform(m_viewport, cam_pixel);
            Fvector shift_proj = lightXZshift;
            cull_xform[cascade_ind].transform_dir(shift_proj);
            m_viewport.transform_dir(shift_proj);

            const float align_granularity = 4.f;
            shift_proj.x = shift_proj.x > 0 ? align_granularity : -align_granularity;
            shift_proj.y = shift_proj.y > 0 ? align_granularity : -align_granularity;
            shift_proj.z = 0;

            cam_pixel.x = cam_pixel.x / align_granularity - floorf(cam_pixel.x / align_granularity);
            cam_pixel.y = cam_pixel.y / align_granularity - floorf(cam_pixel.y / align_granularity);
            cam_pixel.x *= align_granularity;
            cam_pixel.y *= align_granularity;
            cam_pixel.z = 0;

            cam_pixel.sub(shift_proj);

            m_viewport_inv.transform_dir(cam_pixel);
            cull_xform_inv.transform_dir(cam_pixel);
            Fvector diff = cam_pixel;
            static float sign_test = -1.f;
            diff.mul(sign_test);
            Fmatrix adjust;
            adjust.translate(diff);
            cull_xform[cascade_ind].mulB_44(adjust);
        }

        // [DA_PORT] Решение о кэше принимается ЗДЕСЬ — до того, как матрица уйдёт дальше.
        //
        // Берём карту из кэша — значит и матрицу берём ту, с которой она снята. Иначе накопление
        // будет выбирать тень из карты, снятой под другим ракурсом, и тени поедут по всему экрану.
        m_smap_render[cascade_ind] = cacheable(cascade_ind) ? group_needs_render : true;

        if (m_smap_render[cascade_ind])
        {
            m_smap_cache[cascade_ind].xform = cull_xform[cascade_ind];
            m_smap_cache[cascade_ind].cam_pos = Device.vCameraPosition;
            m_smap_cache[cascade_ind].sun_dir = sun->direction;
            m_smap_cache[cascade_ind].time_ms = Device.dwTimeGlobal;
            m_smap_cache[cascade_ind].valid = true;
        }
        else
        {
            cull_xform[cascade_ind] = m_smap_cache[cascade_ind].xform;
            ++g_da_smap_skipped;
            ++g_da_smap_skipped_by[cascade_ind];
        }

        if (m_smap_render[cascade_ind])
            ++g_da_smap_drawn_by[cascade_ind];

        m_sun_cascades[cascade_ind].xform = cull_xform[cascade_ind];

        s32 limit = RImplementation.o.smapsize - 1;
        sun->X.D[cascade_ind].minX = 0;
        sun->X.D[cascade_ind].maxX = limit;
        sun->X.D[cascade_ind].minY = 0;
        sun->X.D[cascade_ind].maxY = limit;
        sun->X.D[cascade_ind].combine = cull_xform[cascade_ind];

        // full-xform
    }

    // [DA_PORT] Замер по каскадам солнца, крутилка da_sun_log N (N кадров подряд).
    //
    // Ставится потому, что мерцание целой тени на улице сопротивляется догадкам: за один вечер
    // отброшены семь версий (отсечение геометрии по размеру, HOM, ранний выход из модели
    // источников тени, reset_chain от солнечных лучей, потолок теневых ламп, экспозиция,
    // дальность каскадов). Форма симптома — пятно ЦЕЛИКОМ пропадает при малом сдвиге камеры —
    // означает бинарное решение, которое перещёлкивается; найти его чтением не удалось, значит
    // надо смотреть числа. Приём тот же, что в da_probe и r__reactive_selftest.
    //
    // Что печатается на каждый каскад: сколько плоскостей получила отсекающая пирамида (пустая =
    // модель источников тени не построилась), сколько статики и динамики попало в теневую карту
    // (резкое падение = отсечение), и куда встал сам объём каскада (скачок = объём уехал с
    // видимой области). Если на кадре пропажи падает счётчик — виновато отсечение; если счётчик
    // стоит, а прыгает перенос — виноват объём; если не меняется ничто — дело в выборке тени.
    u32 dbg_planes[R__NUM_SUN_CASCADES]{};
    u32 dbg_static[R__NUM_SUN_CASCADES]{};
    u32 dbg_dynamic[R__NUM_SUN_CASCADES]{};

    const auto process_cascade = [&, this](const TaskRange<u32>& range)
    {
        for (u32 cascade_ind = range.begin(); cascade_ind != range.end(); ++cascade_ind)
        {
            // [DA_PORT] Каскад берётся из кэша — отбор геометрии не нужен вовсе.
            //
            // Это половина выигрыша и не менее важная, чем пропуск отрисовки: build_subspace обходит
            // сцену и набивает список видимого, а у дальнего каскада это 359 объектов каждый кадр.
            // Пропускаем ЦЕЛИКОМ, вместе с самим контекстом — рисовать по пустому списку всё равно
            // нечего, а очистка карты глубины стёрла бы то, ради чего кэш и заводился.
            if (!m_smap_render[cascade_ind])
                continue;

            // Begin SMAP-render
            auto& dsgraph = RImplementation.get_context(contexts_ids[cascade_ind]);
            {
                //		sun->svis.begin					();
                dsgraph.o.phase = CRender::PHASE_SMAP;
                dsgraph.r_pmask(true, RImplementation.o.Tshadows);
                dsgraph.o.sector_id = RImplementation.get_largest_sector();
                dsgraph.o.xform = cull_xform[cascade_ind];
                dsgraph.o.view_frustum = cull_frustum[cascade_ind];
                dsgraph.o.view_pos = cull_COP[cascade_ind];

                // Счётчики берутся разницей: их никто не сбрасывает между кадрами, поэтому
                // абсолютное значение бессмысленно, а разница — ровно вклад этого каскада.
                u32 s0 = 0, d0 = 0;
                if (ps_da_sun_log > 0)
                    dsgraph.get_Counters(s0, d0);

                // Fill the database
                dsgraph.build_subspace();

                if (ps_da_sun_log > 0)
                {
                    u32 s1 = 0, d1 = 0;
                    dsgraph.get_Counters(s1, d1);
                    dbg_static[cascade_ind] = s1 - s0;
                    dbg_dynamic[cascade_ind] = d1 - d0;
                    // Число плоскостей — по маске: p_count закрыт, а маска это (1<<p_count)-1.
                    u32 mask = cull_frustum[cascade_ind].getMask(), n = 0;
                    while (mask) { n += (mask & 1u); mask >>= 1; }
                    dbg_planes[cascade_ind] = n;
                }
            }
        }
    };

    if (o.mt_calc_enabled)
    {
        xr_parallel_for(TaskRange<u32>(0, R__NUM_SUN_CASCADES), process_cascade);
    }
    else
    {
        process_cascade(TaskRange<u32>(0, R__NUM_SUN_CASCADES));
    }

    // Печать после параллельной части: из рабочих потоков в лог писать нельзя.
    //
    // [DA_PORT] Пишем КАЖДЫЙ кадр и сами помечаем, что изменилось с предыдущего.
    //
    // Отбор по движению камеры отсюда убран: дефект, ради которого замер и делался, мерцает при
    // ПОЛНОСТЬЮ неподвижной камере (проверено в игре). При неподвижной камере вход каскада от кадра
    // к кадру один и тот же, поэтому любое расхождение в числах — уже находка, а не шум. Именно
    // такой срез теперь и нужен.
    //
    // Смещение камеры печатается всё равно: оно доказывает, что камера действительно стояла, а не
    // «мне казалось». Перенос ящика — с двумя знаками: при одном мелкие сдвиги пропадали и всё
    // выглядело мёртво-стабильным.
    if (ps_da_sun_log > 0)
    {
        extern float g_da_probe_cam_moved; // считаются покадрово в phase_combine
        extern float g_da_probe_cam_turned;

        static u32 prev_static[R__NUM_SUN_CASCADES] = {};
        static u32 prev_planes[R__NUM_SUN_CASCADES] = {};
        static Fvector prev_org[R__NUM_SUN_CASCADES] = {};
        static bool have_prev = false;

        for (u32 i = 0; i < R__NUM_SUN_CASCADES; ++i)
        {
            const Fvector org = {cull_xform[i]._41, cull_xform[i]._42, cull_xform[i]._43};

            string256 mark;
            mark[0] = 0;
            if (have_prev)
            {
                if (prev_static[i] != dbg_static[i])
                    xr_strcat(mark, "  <== ТЕНЕОБРАЗУЮЩИЕ");
                if (prev_planes[i] != dbg_planes[i])
                    xr_strcat(mark, "  <== ПЛОСКОСТИ");
                if (!org.similar(prev_org[i], EPS_S))
                    xr_strcat(mark, "  <== ЯЩИК СДВИНУЛСЯ");
            }

            Msg("~ [DA_SUN] кадр %u каскад %u | %-8s | плоскостей %u | статики %u | динамики %u | "
                "объём (%.3f %.3f %.3f) | reset_chain %d | камера %.3f м / %.2f град%s",
                Device.dwFrame, i, m_smap_render[i] ? "рисуем" : "из кэша", dbg_planes[i], dbg_static[i],
                dbg_dynamic[i], org.x, org.y, org.z, m_sun_cascades[i].reset_chain ? 1 : 0,
                g_da_probe_cam_moved, g_da_probe_cam_turned, mark);

            prev_static[i] = dbg_static[i];
            prev_planes[i] = dbg_planes[i];
            prev_org[i] = org;
        }
        have_prev = true;

        if (--ps_da_sun_log == 0)
        {
            Msg("~ [DA_SUN] ---- готово ---- пропущено отрисовок теневых карт: %u (кэш %s)",
                g_da_smap_skipped, ps_da_smap_cache != 0 ? "включён" : "выключен");
            g_da_smap_skipped = 0;
        }
    }
}

void render_sun::render()
{
    if (!o.active)
        return;

    if (need_to_render_sunshafts)
        m_sun_cascades[R__NUM_SUN_CASCADES - 1].reset_chain = last_cascade_chain_mode;

    // Render shadow-map
    const auto render_cascade = [&, this](const TaskRange<u32>& range)
    {
        for (u32 cascade_ind = range.begin(); cascade_ind != range.end(); ++cascade_ind)
        {
#if defined(USE_DX11)
            //TracyD3D11Zone(HW.profiler_ctx, "render_sun::render_cascade");
#endif

            // [DA_PORT] Тот же пропуск, что и в calculate: карта в слоте уже готова, трогать её
            // нельзя. Первым делом phase_smap_direct очищает глубину — вот её и надо миновать.
            if (!m_smap_render[cascade_ind])
                continue;

            auto& dsgraph = RImplementation.get_context(contexts_ids[cascade_ind]);

            bool bNormal = !dsgraph.mapNormalPasses[0][0].empty() || !dsgraph.mapMatrixPasses[0][0].empty();
            bool bSpecial = !dsgraph.mapNormalPasses[1][0].empty() || !dsgraph.mapMatrixPasses[1][0].empty() ||
                !dsgraph.mapSorted.empty();
            if (bNormal || bSpecial)
            {
                RImplementation.Target->phase_smap_direct(dsgraph.cmd_list, sun, cascade_ind);
                dsgraph.cmd_list.set_xform_world(Fidentity);
                dsgraph.cmd_list.set_xform_view(Fidentity);
                dsgraph.cmd_list.set_xform_project(sun->X.D[cascade_ind].combine);
                dsgraph.render_graph(0);
                if (ps_r2_ls_flags.test(R2FLAG_SUN_DETAILS))
                    RImplementation.Details->Render(dsgraph.cmd_list);
                sun->X.D[cascade_ind].transluent = FALSE;
                if (bSpecial)
                {
                    VERIFY(RImplementation.o.Tshadows);
                    sun->X.D[cascade_ind].transluent = TRUE;
                    RImplementation.Target->phase_smap_direct_tsh(dsgraph.cmd_list, sun, cascade_ind);
                    dsgraph.render_graph(1); // normal level, secondary priority
                    dsgraph.render_sorted(); // strict-sorted geoms
                }
            }

            if (!RImplementation.o.support_rt_arrays)
            {
                accumulate_cascade(cascade_ind);
            }
        }
    };

    if (o.mt_draw_enabled)
    {
        xr_parallel_for(TaskRange<u32>(0, R__NUM_SUN_CASCADES), render_cascade);
    }
    else
    {
        render_cascade(TaskRange<u32>(0, R__NUM_SUN_CASCADES));
    }
}

void render_sun::flush()
{
    if (!o.active)
        return;

    if (RImplementation.o.support_rt_arrays)
    {
        for (u32 cascade_ind = 0; cascade_ind < R__NUM_SUN_CASCADES; ++cascade_ind)
        {
            accumulate_cascade(cascade_ind);
        }
    }

    auto &cmd_list_imm = RImplementation.get_imm_context().cmd_list;
    cmd_list_imm.Invalidate();

    // Restore XForms
    cmd_list_imm.set_xform_world(Fidentity);
    cmd_list_imm.set_xform_view(Device.mView);
    cmd_list_imm.set_xform_project(Device.mProject);
}

void render_sun::accumulate_cascade(u32 cascade_ind)
{
#if defined(USE_DX11)
    //TracyD3D11Zone(HW.profiler_ctx, "render_sun::accumulate_cascade");
#endif

    auto* target  = RImplementation.Target;
    auto& dsgraph = RImplementation.get_context(contexts_ids[cascade_ind]);

    // [DA_PORT] da_sun_only N — накапливать свет ТОЛЬКО от каскада N (1..3), 0 = как обычно.
    //
    // Против мерцания целой тени на улице. Замером da_sun_log уже показано, что теневые карты
    // наполняются стабильно, значит ломается выборка тени при накоплении. Каждый каскад
    // накапливается отдельным вызовом, поэтому изоляция по одному отвечает сразу на два вопроса:
    // к какому каскаду привязано мерцание, и не возникает ли оно только когда работают несколько
    // (то есть в стыке между ними).
    //
    // Тени от остальных каскадов при этом просто не рисуются — картинка станет неполной, это
    // ожидаемо и есть смысл замера, а не побочный дефект.
    //
    // Контекст обязательно отдать даже на пропуске: он взят из пула в calculate(), и невозвращённый
    // контекст роняет следующий кадр.
    if (ps_da_sun_only && u32(ps_da_sun_only - 1) != cascade_ind)
    {
        dsgraph.cmd_list.submit();
        RImplementation.release_context(dsgraph.context_id);
        return;
    }

    if ((cascade_ind == SE_SUN_NEAR) && target->use_minmax_sm_this_frame())
    {
        PIX_EVENT_CTX(dsgraph.cmd_list, SE_SUN_NEAR_MINMAX_GENERATE);
        target->create_minmax_SM(dsgraph.cmd_list);
    }

    // Accumulate
    target->rt_smap_depth->set_slice_read(cascade_ind);
    if (cascade_ind == 0)
    {
        PIX_EVENT_CTX(dsgraph.cmd_list, SE_SUN_NEAR);
        target->accum_direct_cascade(dsgraph.cmd_list, SE_SUN_NEAR, m_sun_cascades[cascade_ind].xform,
                                                     m_sun_cascades[cascade_ind].xform, m_sun_cascades[cascade_ind].bias);
    }
    else if (cascade_ind < R__NUM_SUN_CASCADES - 1)
    {
        PIX_EVENT_CTX(dsgraph.cmd_list, SE_SUN_MIDDLE);
        target->accum_direct_cascade(dsgraph.cmd_list, SE_SUN_MIDDLE, m_sun_cascades[cascade_ind].xform,
                                                     m_sun_cascades[cascade_ind - 1].xform, m_sun_cascades[cascade_ind].bias);
    }
    else
    {
        PIX_EVENT_CTX(dsgraph.cmd_list, SE_SUN_FAR);
        target->accum_direct_cascade(dsgraph.cmd_list, SE_SUN_FAR, m_sun_cascades[cascade_ind].xform,
                                                     m_sun_cascades[cascade_ind - 1].xform, m_sun_cascades[cascade_ind].bias);
    }

    dsgraph.cmd_list.submit(); // TODO: move into release (rename to submit?)
    RImplementation.release_context(dsgraph.context_id);
}
} // namespace xray::render::RENDER_NAMESPACE
