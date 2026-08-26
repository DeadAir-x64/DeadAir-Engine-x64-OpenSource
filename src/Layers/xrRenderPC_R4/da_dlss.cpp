#include "stdafx.h"

#include "da_dlss.h"

extern ENGINE_API void da_upscaler_mark_failed(pcstr who); // [DA_PORT]
extern ENGINE_API int ps_r__dlss_preset;              // [DA_PORT] пресет модели DLSS

namespace xray::render::RENDER_NAMESPACE
{
da_dlss g_da_dlss;

namespace
{
// ---- Загрузка прослойки -------------------------------------------------------------------------
//
// Все указатели разом, чтобы «библиотека есть, но старая» отсекалось одной проверкой, а не всплывало
// нулевым указателем посреди кадра.
struct ngx_shim
{
    HMODULE module{};
    bool tried{};
    bool ok{};

    int (*abi_version)(void){};
    void (*set_log)(da_ngx_log_fn){};
    int (*init)(void*, const wchar_t*){};
    void (*shutdown)(void*){};
    int (*available)(void){};
    int (*optimal_size)(int, unsigned, unsigned, unsigned*, unsigned*){};
    int (*create)(void*, unsigned, unsigned, unsigned, unsigned, int){};
    void (*set_preset)(int){};
    void (*destroy)(void*){};
    int (*evaluate)(void*, void*, void*, void*, void*, void*, float, float, float, float, int, unsigned,
                    unsigned){};

    // Каталог, из которого загрузилась прослойка. Там же обязан лежать nvngx_dlss.dll, и путь к нему
    // NGX принимает ТОЛЬКО абсолютный: с относительным он отвечает «файл отсутствует или повреждён»
    // при живом и целом файле. Стоило отдельного захода, поэтому записано здесь.
    wchar_t dir[MAX_PATH]{};
};

ngx_shim g_shim;

void ngx_say(const char* message)
{
    if (!message || !*message)
        return;
    // NGX присылает строки со своим переводом строки, наш Msg добавляет свой — обрезаем хвост, иначе
    // лог разъезжается пустыми строками.
    string1024 buf;
    xr_strcpy(buf, message);
    size_t n = xr_strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    if (n)
        Msg("%s", buf);
}

template <typename T>
bool bind(T& fn, pcstr name)
{
    fn = reinterpret_cast<T>(GetProcAddress(g_shim.module, name));
    if (!fn)
        Msg("! [DLSS] da_ngx.dll has no symbol [%s]", name);
    return fn != nullptr;
}

bool load_shim()
{
    if (g_shim.tried)
        return g_shim.ok;
    g_shim.tried = true;

    // Ищется по обычным правилам Windows, то есть в первую очередь рядом с exe. Отсутствие файла —
    // не ошибка: значит DLSS в этой поставке просто нет.
    g_shim.module = LoadLibraryA("da_ngx.dll");
    if (!g_shim.module)
    {
        Msg("* [DLSS] da_ngx.dll not found - DLSS is unavailable, everything else is unaffected");
        return false;
    }

    if (!bind(g_shim.abi_version, "da_ngx_abi_version") || !bind(g_shim.set_log, "da_ngx_set_log") ||
        !bind(g_shim.init, "da_ngx_init") || !bind(g_shim.shutdown, "da_ngx_shutdown") ||
        !bind(g_shim.available, "da_ngx_available") ||
        !bind(g_shim.optimal_size, "da_ngx_optimal_size") || !bind(g_shim.create, "da_ngx_create") ||
        !bind(g_shim.set_preset, "da_ngx_set_preset") ||
        !bind(g_shim.destroy, "da_ngx_destroy") || !bind(g_shim.evaluate, "da_ngx_evaluate"))
    {
        FreeLibrary(g_shim.module);
        g_shim.module = nullptr;
        return false;
    }

    const int have = g_shim.abi_version();
    if (have != DA_NGX_ABI_VERSION)
    {
        // Отказываемся осознанно. Несовпадение версий означает разъехавшиеся сигнатуры, а это падение
        // в чужой памяти, которое не отладить: обе стороны будут правы по-своему.
        Msg("! [DLSS] da_ngx.dll is version %d, this build needs %d - rebuild it with "
            "Externals/nvngx_shim/build.bat",
            have, DA_NGX_ABI_VERSION);
        FreeLibrary(g_shim.module);
        g_shim.module = nullptr;
        return false;
    }

    GetModuleFileNameW(g_shim.module, g_shim.dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(g_shim.dir, L'\\'))
        *slash = 0;

    g_shim.set_log(&ngx_say);
    g_shim.ok = true;
    return true;
}
} // namespace

// [DA_PORT] ⚠️ БЕЗ УСТРОЙСТВА ЭТОТ ОТВЕТ ВСЕГДА «НЕТ».
//
// `da_ngx_available` первым делом смотрит на `g_inited`, а инициализация NGX происходит в
// `da_ngx_init(device)` — то есть только когда DLSS уже выбран и создаётся. Спросить доступность
// «до» и получить осмысленный ответ нельзя.
//
// Поймано сразу: первая же версия отбора пунктов меню спрятала DLSS на RTX-карте, где он прекрасно
// работает. Поэтому проба обязана инициализировать NGX сама — отсюда параметр `device`.
//
// Инициализация идемпотентна (`if (g_inited) return 1`), поэтому оставляем её поднятой: если DLSS
// потом выберут, `create` просто продолжит с готового состояния.
bool da_dlss::supported(ID3D11Device* device)
{
    if (!load_shim())
        return false;
    if (!g_shim.available || !g_shim.init)
        return false;

    if (device && !g_shim.init(device, g_shim.dir))
        return false; // прослойка уже объяснила причину в лог

    return g_shim.available() != 0;
}

void da_dlss::render_size_for(u32 quality, u32 display_w, u32 display_h, u32& out_w, u32& out_h)
{
    // Спрашиваем саму NGX, а не берём из таблицы: NVIDIA меняет коэффициенты между версиями, и зашитые
    // числа разъедутся молча — сцена начнёт рендериться не в том разрешении, которое ждёт апскейлер.
    if (load_shim())
    {
        unsigned rw = 0, rh = 0;
        if (g_shim.optimal_size(int(quality), display_w, display_h, &rw, &rh) && rw && rh)
        {
            out_w = rw;
            out_h = rh;
            return;
        }
    }

    // Запасной вариант — до инициализации NGX (размеры целей нужны раньше, чем поднимется контекст).
    // Числа замерены на драйвере 2026 года: 1.0 / 1.5 / 1.5 / 1.72 / 2.0 / 3.0.
    // ⚠️ Ступени 1 и 2 совпадают не по ошибке: NGX не считает «ультра-качество» отдельным режимом и
    // отвечает на него тем же, что и на «качество».
    float ratio;
    switch (quality)
    {
    case 1: ratio = 1.5f; break;
    case 2: ratio = 1.5f; break;
    case 3: ratio = 1.72f; break;
    case 4: ratio = 2.0f; break;
    case 5: ratio = 3.0f; break;
    default: ratio = 1.0f; break;
    }
    out_w = u32(float(display_w) / ratio) & ~1u;
    out_h = u32(float(display_h) / ratio) & ~1u;
}

void da_dlss::mv_scale_for(u32 render_w, u32 render_h, float& out_x, float& out_y)
{
    // [DA_PORT] Знаки те же, что у FSR 2. Установлено НАБЛЮДЕНИЕМ, и иначе было нельзя.
    //
    // Симптом неверного знака по X — кадр плавно уводит вбок на ходу, пропорционально скорости.
    // С `-0.5` увод пропадает, с `+0.5` появляется; проверено в игре.
    //
    // ⚠️ Чтением НАШЕГО буфера этот вопрос не решается в принципе, и на попытках это сделать ушло
    // четыре захода. Замер говорит только о том, что мы храним; вопрос же о том, чего ждёт NGX.
    // Ответить на него может либо образец NVIDIA, либо глаз — измерять тут нечего.
    //
    // Заодно о том, почему прежние «замеры знака» противоречили друг другу: среднее по кадру
    // складывало три разных движения — мир, оружие в руках (движется ВМЕСТЕ с камерой) и радиальный
    // разлёт от бега вперёд. Плюс поворот мыши, дающий однородный сдвиг, который перекрывает стрейф
    // на порядок. Ни одно из этих слагаемых я поначалу не отделял.
    out_x = float(render_w) * -0.5f;
    out_y = float(render_h) * 0.5f;
}

bool da_dlss::create(const init_params& p)
{
    destroy();

    // Запоминаем ДО всех проверок и ровно один раз. Если создание провалится, повторять его каждый
    // кадр незачем: причина отказа от кадра к кадру не меняется, а лог зальёт одинаковыми строками.
    m_params = p;
    m_preset = ps_r__dlss_preset;

    if (!p.device || !p.display_width || !p.display_height || !p.render_width || !p.render_height)
        return false;
    if (!load_shim())
        return false;

    if (!g_shim.init(p.device, g_shim.dir))
        return false; // прослойка уже объяснила причину в лог

    if (!g_shim.available())
        return false; // и здесь тоже: старый драйвер или не та видеокарта

    // Создаём под РЕАЛЬНЫЙ размер сцены, а не под «оптимальный» по мнению NGX: кадр в фичу пойдёт
    // именно такой, и разойтись им нельзя.
    const u32 rw = p.render_width;
    const u32 rh = p.render_height;

    // Сверяться с da_ngx_optimal_size здесь НЕ надо, и это стоит объяснить, иначе кто-нибудь добавит
    // такую проверку и получит ложную тревогу. Масштаб рендера задаётся общей на все апскейлеры
    // таблицей (da_upscaler_scale в xr_ioc_cmd.cpp), чтобы одна и та же ступень в меню означала одно
    // и то же независимо от бэкенда. У DLSS с ней совпадают четыре ступени из пяти; первая просит
    // 1.3x там, где NGX предлагает 1.5x, — просто потому, что отдельного «ультра-качества» у NGX нет
    // и на этот режим он отвечает тем же, что и на «качество». Реконструировать произвольное
    // соотношение DLSS умеет, режим качества выбирает лишь модель.
    //
    // Важно здесь ровно одно: размер создания совпадает с размером кадра. Теперь это выполняется по
    // построению - оба приходят из Device.dwRenderWidth.

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);

    // Пресет отдаётся строго перед созданием: NGX читает его из блока параметров в этот момент.
    g_shim.set_preset(m_preset);

    if (!g_shim.create(ctx, rw, rh, p.display_width, p.display_height, int(p.quality)))
    {
        // [DA_PORT] Говорим это громко и полностью, потому что провал здесь МАСКИРУЕТСЯ ПОД УСПЕХ.
        //
        // Масштаб рендера уже понижен - его выставляет меню при выборе апскейлера, независимо от
        // того, поднимется он потом или нет. Поэтому при отказе игра продолжает рисовать сцену
        // уменьшенной и растягивать её обычным фильтром: кадры растут, картинка мягчает, и всё это
        // выглядит ровно как работающий DLSS. Один раз мы на это уже попались и час считали прирост
        // от растяжения приростом от DLSS.
        Msg("! [DLSS] feature creation FAILED - DLSS is NOT running.");

        // [DA_PORT] Тот же случай, что у FSR 3 на R9 290: без реконструкции джиттер трясёт экран.

        ::da_upscaler_mark_failed("DLSS");
        Msg("!   Сцена продолжает рисоваться в %ux%u и растягивается до %ux%u обычным фильтром.", rw,
            rh, p.display_width, p.display_height);
        Msg("!   Кадры при этом вырастут, но это выигрыш от пониженного разрешения, а не от DLSS.");
        Msg("!   Выберите другой апскейлер или «Выкл», чтобы вернуть полное разрешение.");
        return false;
    }

    m_device = p.device;
    m_created = true;
    return true;
}

void da_dlss::destroy()
{
    if (!m_created)
        return;

    if (g_shim.ok)
    {
        // [DA_PORT] Освобождаем ФИЧУ, но NGX не гасим — и это не мелочь, а причина потери устройства.
        //
        // Было: destroy() звал ещё и g_shim.shutdown(), то есть NVSDK_NGX_D3D11_Shutdown1 на ЖИВОМ
        // устройстве, а create() первым делом зовёт destroy() и следом init(). Значит каждая смена
        // апскейлера или ступени качества давала полный цикл «погасить NGX — поднять NGX» посреди
        // сеанса, с перезагрузкой ядер-кубинов, пока кадры ещё в полёте.
        //
        // 18.08.2026 это закончилось потерей устройства: в логе за полминуты до неё видна как раз
        // переинициализация NGX (NGXCubinKernelMap::InitCubins, CreateFeature_Validate, «DLSS ready:
        // 1920x1080 -> 1920x1080»), а затем DXGI_ERROR_DRIVER_INTERNAL_ERROR в фазе вывода кадра.
        // Видеопамяти было занято 18% из 15 ГБ — дело не в нехватке.
        //
        // Модель у NGX другая: Init один раз на устройство, дальше создаются и освобождаются ФИЧИ,
        // а Shutdown — только когда уходит само устройство. Ровно это и написано абзацем выше по
        // файлу («инициализация идемпотентна, поэтому оставляем её поднятой») — код просто
        // противоречил собственному комментарию.
        g_shim.destroy(HW.get_context(CHW::IMM_CTX_ID));
    }
    m_device = nullptr;
    m_created = false;
}

bool da_dlss::draw(const draw_params& p)
{
    if (!m_created || !g_shim.ok)
        return false;

    // [DA_PORT] Смена пресета применяется здесь, пересозданием фичи.
    //
    // Пересоздавать посреди кадра можно: фича живёт на непосредственном контексте, а вызов стоит ДО
    // единственной работы с ней в этом кадре. История реконструкции при этом теряется - на один кадр
    // картинка станет как после загрузки уровня. Это ожидаемо и происходит только в момент смены.
    if (m_preset != ps_r__dlss_preset)
    {
        const init_params saved = m_params; // create() затирает m_params, читаем копию
        Msg("* [DLSS] render preset %d -> %d, recreating", m_preset, ps_r__dlss_preset);
        if (!create(saved))
            return false;
    }

    // Наши векторы лежат в NDC (разность двух позиций в клиповом пространстве после деления на w),
    // DLSS хочет пиксели. Половина размера рендера переводит одно в другое, знак по x — из того же
    // соображения, что и у FSR 2: NDC растёт вправо, а библиотека ждёт обратного.
    //
    // ⚠️ Значения взяты у FSR 2, где они выверены в игре и стоили ночи отладки. Если DLSS начнёт
    // мазать движущиеся объекты в одну сторону — проверять надо ЗДЕСЬ, и менять по одному знаку за
    // раз. Перебор вслепую по обоим осям неотличим от правильной настройки на глаз.
    float mv_scale_x, mv_scale_y;
    mv_scale_for(p.render_width, p.render_height, mv_scale_x, mv_scale_y);

    const int ok = g_shim.evaluate(p.context, p.colour, p.depth, p.velocity, p.output, p.reactive,
                                   p.jitter_x, p.jitter_y, mv_scale_x, mv_scale_y, p.reset ? 1 : 0,
                                   p.render_width, p.render_height);
    return ok != 0;
}
} // namespace xray::render::RENDER_NAMESPACE
