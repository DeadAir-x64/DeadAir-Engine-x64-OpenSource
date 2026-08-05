#include "stdafx.h"

#include "da_dlss.h"

// [DA_PORT] Определены в движке (xr_ioc_cmd.cpp). Объявляются СНАРУЖИ пространства имён: внутри
// линкер будет искать символ в xray::render::RENDER_NAMESPACE и не найдёт — грабля описана в
// docs/07_TROUBLESHOOTING.md.
extern ENGINE_API int ps_r__dlss;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

extern ENGINE_API bool da_upscaler_history_reset(); // [DA_PORT]
extern ENGINE_API void da_upscaler_report_failure(pcstr who, bool failed); // [DA_PORT]
extern ENGINE_API int ps_r__dlss_reactive; // [DA_PORT] см. xr_ioc_cmd.cpp

extern ENGINE_API int ps_r__dlss_selftest; // [DA_PORT]

namespace xray::render::RENDER_NAMESPACE
{
// Общий с FSR 2 и XeSS штамп кадра: постобработке важно лишь то, что кадр КЕМ-ТО реконструирован.
extern u32 g_da_fsr2_frame;

namespace
{
// [DA_PORT] ---- Замер векторов движения: числа в лог вместо перебора знаков --------------------
//
// Знак вектора нельзя вывести из кода: у AMD и NVIDIA соглашения разные, обе стороны выглядят
// правдоподобно, а на глаз отличить «история тянется не туда» от «резкость не та» невозможно - что и
// стоило нам нескольких кругов скриншотов. Поэтому здесь измеряется то, что есть на самом деле.
//
// Проверка построена на движении вбок. Идёшь вправо - мир на экране едет влево, значит ПРЕДЫДУЩЕЕ
// положение каждой точки было ПРАВЕЕ текущего. Вектор движения указывает назад, в прошлое положение,
// то есть в пикселях он обязан быть ПОЛОЖИТЕЛЬНЫМ по X. Это и проверяется.

float da_half_to_float(u16 h)
{
    const u32 sign = (h >> 15) & 1u;
    const u32 exp = (h >> 10) & 0x1fu;
    const u32 man = h & 0x3ffu;

    float v;
    if (exp == 0)
        v = float(man) * (1.f / 1024.f) * 6.103515625e-05f;
    else if (exp == 31)
        v = 0.f; // бесконечности и NaN в статистику не пускаем
    else
        v = (1.f + float(man) * (1.f / 1024.f)) * powf(2.f, float(int(exp) - 15));

    return sign ? -v : v;
}

void da_dlss_measure(const ref_rt& velocity, u32 render_w, u32 render_h, float mv_scale_x,
                     float mv_scale_y)
{
    if (!velocity || !velocity->pTexture)
    {
        Msg("~ [DLSS_TEST] буфера скоростей нет");
        return;
    }

    ID3DBaseTexture* res = velocity->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
    {
        Msg("~ [DLSS_TEST] буфер скоростей не 2D-текстура");
        return;
    }

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    HRESULT hr = HW.pDevice->CreateTexture2D(&sd, nullptr, &staging);
    if (FAILED(hr) || !staging)
    {
        Msg("~ [DLSS_TEST] копия для чтения не создалась (0x%08x)", hr);
        _RELEASE(tex);
        return;
    }

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
    ctx->CopyResource(staging, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr))
    {
        Msg("~ [DLSS_TEST] чтение отклонено (0x%08x)", hr);
        _RELEASE(staging);
        _RELEASE(tex);
        return;
    }

    // Сетка 3x3 по всему кадру: усреднять по одной области бессмысленно, потому что в кадре живут
    // сразу две сущности с ПРОТИВОПОЛОЖНЫМИ векторами - мир и оружие в руках. Оружие движется вместе
    // с камерой, и попав в общее среднее, оно разворачивает знак. Карта показывает, где вектора есть
    // и куда они смотрят, вместо одного числа, которое врёт.
    struct cell
    {
        double sum_x = 0.0, sum_y = 0.0;
        u32 samples = 0, moving = 0, pos_x = 0, neg_x = 0;
    } grid[3][3];

    // [DA_PORT] Отдельно - узкая полоса по центру экрана. Только по ней можно судить о знаке X.
    //
    // Движение ВПЕРЁД разгоняет картинку от центра: слева всё едет влево, справа вправо. Среднее по
    // всему кадру от этого стремится к нулю, и его знак определяет случайный перекос, а не боковой
    // сдвиг. Дважды на этом обжёгся: два замера с одинаковым боковым ходом дали противоположные
    // вердикты, потому что различались бегом вперёд (5 м против 15 м за кадр).
    //
    // В центральной колонке радиальная составляющая по горизонтали равна нулю по построению, так что
    // там остаётся только боковое движение - именно то, что проверяется.
    double strip_x = 0.0;
    u32 strip_n = 0;
    const u32 strip_lo = desc.Width * 47 / 100;
    const u32 strip_hi = desc.Width * 53 / 100;

    for (u32 y = 0; y < desc.Height; y += 4)
    {
        const u16* row = (const u16*)((const u8*)map.pData + size_t(y) * map.RowPitch);
        const u32 gy = std::min(2u, y * 3 / desc.Height);
        for (u32 x = 0; x < desc.Width; x += 4)
        {
            const u32 gx = std::min(2u, x * 3 / desc.Width);
            cell& c = grid[gy][gx];

            const float vx = da_half_to_float(row[size_t(x) * 2 + 0]);
            const float vy = da_half_to_float(row[size_t(x) * 2 + 1]);
            ++c.samples;
            if (_abs(vx) > 1e-5f || _abs(vy) > 1e-5f)
            {
                ++c.moving;
                c.sum_x += vx;
                c.sum_y += vy;
                if (vx > 0.f)
                    ++c.pos_x;
                else if (vx < 0.f)
                    ++c.neg_x;

                if (x >= strip_lo && x < strip_hi)
                {
                    strip_x += vx;
                    ++strip_n;
                }
            }
        }
    }

    // Общая статистика - только по НЕНУЛЕВЫМ: нули это не «вектор равен нулю», это «никто не писал».
    double sum_x = 0.0, sum_y = 0.0;
    u32 samples = 0, moving = 0;
    for (auto& r : grid)
        for (auto& c : r)
        {
            sum_x += c.sum_x;
            sum_y += c.sum_y;
            samples += c.samples;
            moving += c.moving;
        }

    ctx->Unmap(staging, 0);
    _RELEASE(staging);
    _RELEASE(tex);

    if (!samples)
    {
        Msg("~ [DLSS_TEST] нечего мерить");
        return;
    }

    // Среднее по НЕНУЛЕВЫМ, а не по всем: ноль в буфере значит «никто не писал», и подмешивать его
    // в среднее — то же самое, что считать неотвеченные анкеты за ответ «ноль».
    const u32 denom = moving ? moving : 1;
    const float ndc_x = float(sum_x / denom);
    const float ndc_y = float(sum_y / denom);
    const float px_x = ndc_x * mv_scale_x;
    const float px_y = ndc_y * mv_scale_y;

    // Куда сдвинулась камера с прошлого кадра, в её собственных осях.
    static Fvector prev_pos{};
    static bool have_prev = false;
    Fvector delta{};
    if (have_prev)
        delta.sub(Device.vCameraPosition, prev_pos);
    prev_pos = Device.vCameraPosition;
    have_prev = true;

    const float move_right = delta.dotproduct(Device.vCameraRight);
    const float move_up = delta.dotproduct(Device.vCameraTop);
    const float move_fwd = delta.dotproduct(Device.vCameraDirection);

    // [DA_PORT] Поворот камеры за кадр, пересчитанный В ПИКСЕЛИ.
    //
    // Без этого весь замер бессмысленен, и три круга ушло именно на это. Поворот даёт ОДНОРОДНЫЙ
    // горизонтальный сдвиг по всему кадру, а стрейф — сдвиг, зависящий от глубины, и в пикселях он
    // куда меньше. Полградуса мыши за кадр перекрывают несколько метров бокового хода. Пока эти два
    // вклада не разделены, знак читается из того, чего было больше, а не из того, что проверяется.
    static Fvector prev_dir{};
    static bool have_dir = false;
    float turn_px = 0.f;
    if (have_dir)
    {
        // Угол между прошлым и текущим направлением взгляда в горизонтальной плоскости.
        Fvector2 a{ prev_dir.x, prev_dir.z }, b{ Device.vCameraDirection.x, Device.vCameraDirection.z };
        a.normalize_safe();
        b.normalize_safe();
        const float cosang = clampr(a.x * b.x + a.y * b.y, -1.f, 1.f);
        const float yaw = acosf(cosang); // радианы, знак не нужен — важна величина
        const float fov_h = deg2rad(Device.fFOV); // fFOV у X-Ray ГОРИЗОНТАЛЬНЫЙ
        if (fov_h > EPS)
            turn_px = yaw / fov_h * float(render_w);
    }
    prev_dir = Device.vCameraDirection;
    have_dir = true;

    Msg("~ [DLSS_TEST] ---------------------------------------------------------------");
    Msg("~ [DLSS_TEST] кадр %ux%u, ненулевых векторов %u из %u (%.0f%%)", render_w, render_h, moving,
        samples, 100.f * float(moving) / float(samples));
    Msg("~ [DLSS_TEST] буфер (NDC)   x=%+.6f  y=%+.6f", ndc_x, ndc_y);
    Msg("~ [DLSS_TEST] множители     x=%+.1f   y=%+.1f", mv_scale_x, mv_scale_y);
    Msg("~ [DLSS_TEST] в NGX (пиксели) x=%+.3f  y=%+.3f", px_x, px_y);
    Msg("~ [DLSS_TEST] камера за кадр: вправо %+.4f м, вверх %+.4f м, вперёд %+.4f м", move_right,
        move_up, move_fwd);

    // Карта кадра. В каждой ячейке: доля пикселей с вектором и куда он смотрит по X.
    // Читается так: пустые ячейки - геометрия, которая вектора не пишет вовсе (для апскейлера это
    // значит «стояло на месте»). Ячейка со знаком, противоположным соседям, - объект, движущийся
    // вместе с камерой, то есть оружие в руках.
    // ⚠️ Проценты ниже — НЕ «доля шейдеров, пишущих вектор». Это доля пикселей, у которых вектор
    // длиннее порога 1e-5, а порог при 300 кадрах в секунду перебивается медленной ходьбой: за 3 мс
    // далёкая геометрия смещается меньше него, хотя вектор пишет честно. Поэтому цифры сильно зависят
    // от того, как быстро шёл игрок в ЭТОТ кадр, и сравнивать два замера между собой можно только при
    // одинаковой скорости. Годятся они для одного: увидеть ЦЕЛУЮ область в нулях при явном движении.
    Msg("~ [DLSS_TEST] карта кадра (доля с вектором длиннее порога | знак X: + вправо, - влево):");
    Msg("~ [DLSS_TEST] проценты зависят от скорости в этот кадр — между замерами не сравнивать");
    for (u32 gy = 0; gy < 3; ++gy)
    {
        string256 line;
        xr_strcpy(line, "~ [DLSS_TEST]   ");
        for (u32 gx = 0; gx < 3; ++gx)
        {
            const cell& c = grid[gy][gx];
            const float frac = c.samples ? 100.f * float(c.moving) / float(c.samples) : 0.f;
            pcstr sign = "~";
            if (c.pos_x > c.neg_x * 3)
                sign = "+";
            else if (c.neg_x > c.pos_x * 3)
                sign = "-";
            string64 tmp;
            xr_sprintf(tmp, "[%3.0f%% %s] ", frac, sign);
            xr_strcat(line, tmp);
        }
        Msg("%s", line);
    }

    // ---- Вердикт по знаку X -------------------------------------------------------------------
    //
    // Условия жёсткие, и это намеренно: лучше отказать в вердикте, чем выдать уверенное число,
    // прочитанное из шума. Оба прежних вердикта были именно такими и противоречили друг другу.
    const float strip_ndc = strip_n ? float(strip_x / strip_n) : 0.f;
    const float strip_px = strip_ndc * mv_scale_x;

    Msg("~ [DLSS_TEST] центральная полоса: %u точек, x=%+.6f -> в пикселях %+.3f", strip_n, strip_ndc,
        strip_px);

    // Бег вперёд здесь НЕ запрещён, и это осознанно: в центральной полосе радиальный разлёт
    // симметричен относительно её середины и в среднем гасится сам. Запрещать его — значит требовать
    // от игрока техники, которой тест по построению не требует. Отсекаем только явно бесполезные
    // случаи: почти нет бокового хода, либо полоса пуста.
    Msg("~ [DLSS_TEST] поворот камеры за кадр: ~%.2f пикселя однородного сдвига", turn_px);

    if (turn_px > 0.5f)
    {
        Msg("~ [DLSS_TEST] ВЕРДИКТА НЕТ: поворот даёт %.2f пикселя — это перекрывает стрейф.", turn_px);
        Msg("~ [DLSS_TEST] Стрейфить НЕ ТРОГАЯ МЫШЬ. Убери руку с мыши совсем и повтори.");
    }
    else if (_abs(move_right) < 0.05f)
    {
        Msg("~ [DLSS_TEST] ВЕРДИКТА НЕТ: боковое движение %+.4f м слишком мало.", move_right);
        Msg("~ [DLSS_TEST] Нужен стрейф вбок — вперёд бежать при этом можно.");
    }
    else if (strip_n < 500)
    {
        Msg("~ [DLSS_TEST] ВЕРДИКТА НЕТ: в центральной полосе всего %u векторов.", strip_n);
    }
    else if (!strip_n)
    {
        Msg("~ [DLSS_TEST] ВЕРДИКТА НЕТ: в центральной полосе нет ни одного вектора.");
    }
    else
    {
        // Шли вправо -> мир едет влево -> прошлое положение точки ПРАВЕЕ -> вектор в пикселях > 0.
        const bool want_positive = move_right > 0.f;
        const bool got_positive = strip_px > 0.f;
        Msg("~ [DLSS_TEST] ось X: шли %s, ожидаем %s, получили %+.3f -> %s",
            want_positive ? "вправо" : "влево", want_positive ? "плюс" : "минус", strip_px,
            (want_positive == got_positive) ? "ЗНАК ВЕРНЫЙ"
                                            : "ЗНАК ЗЕРКАЛЬНЫЙ — править da_dlss::mv_scale_for");
    }
    Msg("~ [DLSS_TEST] ---------------------------------------------------------------");
}
} // namespace

bool CRenderTarget::phase_dlss()
{
    if (!ps_r__dlss || !g_da_dlss.ready())
        return false;

    PIX_EVENT(DA_phase_dlss);

    // [DA_PORT] Сначала отвязать цели — та же ловушка, из-за которой FSR 2 реконструировал чёрный
    // кадр и рапортовал об успехе. Combine оставляет rt_Color привязанным как цель отрисовки, а
    // rt_Base_Depth как буфер глубины: это ровно те два ресурса, которые мы ниже читаем, а D3D11
    // молча подсовывает шейдеру нули за всё, что одновременно привязано на запись.
    u_setrt(RCache, Device.dwWidth, Device.dwHeight, get_base_rt(), 0, 0, nullptr);

    ID3DBaseTexture* colour = rt_Color->pTexture->surface_get();
    ID3DBaseTexture* depth = rt_Base_Depth->pTexture->surface_get();
    ID3DBaseTexture* velocity = rt_Velocity->pTexture->surface_get();
    // Та же цель, в которую пишут FSR 2 и XeSS: разрешение экрана с неупорядоченным доступом. В одном
    // кадре они не работают вместе, так что делить нечего.
    ID3DBaseTexture* output = rt_FSR2_out->pTexture->surface_get();
    ID3DBaseTexture* reactive = rt_Reactive ? rt_Reactive->pTexture->surface_get() : nullptr;

    bool ok = false;
    if (colour && depth && velocity && output)
    {
        da_dlss::draw_params p;
        p.context = HW.get_context(CHW::IMM_CTX_ID);

        p.colour = colour;
        p.depth = depth;
        p.velocity = velocity;
        p.output = output;
        // [DA_PORT] Маска отключается ручкой: у NVIDIA этот параметр значит не то же, что у AMD,
        // и на листве она может вредить. Сравнивать глазами, стоя на месте.
        p.reactive = ::ps_r__dlss_reactive ? reactive : nullptr;

        p.render_width = Device.dwRenderWidth;
        p.render_height = Device.dwRenderHeight;

        // В пикселях разрешения рендера — ровно то, что просит NGX («Jitter offset must be in
        // input/render pixel space»), и ровно то, что выдаёт CCameraManager. Преобразовывать нечего.
        // В Device.mProject дрожание не попадает и попадать не должно: та матрица ведёт ещё тени,
        // партиклы и HUD, а апскейлер их не компенсирует.
        // [DA_PORT] Дрожание — как у FSR 2, знак проверен в игре. Шейдеры применяют его с
        // инвертированным Y (r2.cpp, cl_taa_jitter), и NGX эта пара устраивает.
        p.jitter_x = ::g_da_fsr2_jitter_px.x;
        p.jitter_y = ::g_da_fsr2_jitter_px.y;

        // [DA_PORT] Через общий сброс: загрузка уровня и телепорт тоже выбрасывают историю,
        // а прежнее `dwFrame < 3` покрывало только запуск сессии. См. xr_ioc_cmd.cpp.
        p.reset = ::da_upscaler_history_reset();

        // [DA_PORT] Замер идёт ДО отрисовки: буфер скоростей уже заполнен, а конвейер ещё не занят
        // работой апскейлера. Разовый - чтение останавливает конвейер.
        if (::ps_r__dlss_selftest)
        {
            // Множители берутся ТОЙ ЖЕ функцией, что и отрисовка: иначе лог отчитывается об одних
            // числах, а в NGX уходят другие, и вердикт замера становится ложью. Так уже было.
            float sx, sy;
            da_dlss::mv_scale_for(p.render_width, p.render_height, sx, sy);
            da_dlss_measure(rt_Velocity, p.render_width, p.render_height, sx, sy);
            ::ps_r__dlss_selftest = 0;
        }

        ok = g_da_dlss.draw(p);
        if (ok)
            g_da_fsr2_frame = Device.dwFrame;
    }

    // [DA_PORT] Сторож, которого здесь не было ни одного.
    //
    // У FSR 2, FSR 3 и XeSS этот вызов стоит сразу за диспетчем, у DLSS его не было вовсе — тот же
    // недосмотр «добавили бэкенд, не добавили в список», что уже дважды ломал маску реактивности и
    // джиттер. Цена пропуска именно здесь наибольшая: без штампа кадра постобработка растягивает
    // сырую сцену (см. fsr2_active в r2_rendertarget_phase_PP.cpp), а сцена смещена джиттером,
    // который никто не выключил. То есть отказ DLSS выглядел как дрожание мира и не оставлял в
    // логе ни строки.
    //
    // Вызов стоит ЗА блоком, а не внутри: если не нашлось хотя бы одной цели, диспетча не было
    // вовсе, и это ровно такой же несобранный кадр, о котором надо сказать.
    ::da_upscaler_report_failure("DLSS", !ok);

    _RELEASE(colour);
    _RELEASE(depth);
    _RELEASE(velocity);
    _RELEASE(output);
    _RELEASE(reactive);
    return ok;
}
} // namespace xray::render::RENDER_NAMESPACE
