#include "stdafx.h"

// [DA_PORT] Defined in the engine (xr_ioc_cmd.cpp). Declared out here, not inside the namespace.
extern ENGINE_API float ps_r__reactive_object;
extern ENGINE_API int ps_r__reactive_dilate;
extern ENGINE_API float ps_r__reactive_deadzone;
extern ENGINE_API int ps_r__reactive_debug;
extern ENGINE_API int ps_r__reactive_ref_fps;
extern ENGINE_API int ps_r__fsr2;
extern ENGINE_API int ps_r__fsr3;
extern ENGINE_API int ps_r__xess;
extern ENGINE_API bool da_upscaler_active(); // [DA_PORT]

extern ENGINE_API int ps_r__reactive_selftest;
extern ENGINE_API int ps_r__probe_center; // [DA_PORT] 0 - ярчайший пиксель, 1 - перекрестье
extern ENGINE_API bool g_da_jitter_suppress; // [DA_PORT] подавление джиттера на время чтения экрана

namespace xray::render::RENDER_NAMESPACE
{
// [DA_PORT] Смещение и поворот камеры ЗА ПОСЛЕДНИЙ КАДР. Считаются покадрово в phase_combine,
// читаются пробой. Отдельно от неё именно потому, что проба срабатывает раз и разницы за кадр
// изнутри себя не увидит.
float g_da_probe_cam_moved = 0.f;
float g_da_probe_cam_turned = 0.f;
}
extern ENGINE_API float ps_r__reactive_emissive; // [DA_PORT] метка свечения, см. phase_reactive_emissive
extern ENGINE_API float ps_r__reactive_transparent; // [DA_PORT] то же для прозрачной геометрии
extern ENGINE_API float ps_r__reactive_water;       // [DA_PORT] и отдельно — только для воды

namespace xray::render::RENDER_NAMESPACE
{
namespace
{
// [DA_PORT] ---- Self-test: read the buffers back and put numbers in the log --------------------
//
// A render pass that produces nothing looks exactly like a render pass whose inputs are all empty,
// and from outside the two are the same black screen. Every attempt to tell them apart by eye costs
// a round trip through someone launching the game, walking to the right spot and judging a picture -
// and judgement is the part that cannot be checked afterwards. Numbers can.
//
// One-shot, triggered by r__reactive_selftest 1: it stalls the pipeline to map the targets, which is
// fine for a single frame and would not be for any other purpose.

float da_half(u16 h)
{
    const u32 sign = (h >> 15) & 1u;
    const u32 exp = (h >> 10) & 0x1fu;
    const u32 man = h & 0x3ffu;

    float v;
    if (exp == 0)
        v = float(man) * (1.f / 1024.f) * 6.103515625e-05f; // subnormal: 2^-14 * man/1024
    else if (exp == 31)
        v = man ? 0.f : flt_max; // NaN reported as zero, infinity as a huge number
    else
        v = (1.f + float(man) * (1.f / 1024.f)) * powf(2.f, float(int(exp) - 15));

    return sign ? -v : v;
}

// comp_offset/comp_count select which channels form the magnitude - the xy of a motion vector, the z
// of an eye-space position, the single channel of the mask.
void da_probe(pcstr name, const ref_rt& rt, u32 comps_in_pixel, u32 comp_offset, u32 comp_count)
{
    if (!rt || !rt->pTexture)
    {
        Msg("~ [DA_PROBE] %-18s : target does not exist", name);
        return;
    }

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    if (!res)
    {
        Msg("~ [DA_PROBE] %-18s : no surface", name);
        return;
    }

    ID3D11Texture2D* tex = nullptr;
    res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
    {
        Msg("~ [DA_PROBE] %-18s : not a 2D texture", name);
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
        Msg("~ [DA_PROBE] %-18s : staging copy refused (0x%08x)", name, hr);
        _RELEASE(tex);
        return;
    }

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
    ctx->CopyResource(staging, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr))
    {
        Msg("~ [DA_PROBE] %-18s : map refused (0x%08x)", name, hr);
        _RELEASE(staging);
        _RELEASE(tex);
        return;
    }

    // Every 4th pixel in both directions - sixteen times less work, and no statistic here needs more.
    const u32 step = 4;
    u32 samples = 0, nonzero = 0;
    float maxv = 0.f, sum = 0.f;

    for (u32 y = 0; y < desc.Height; y += step)
    {
        const u16* row = (const u16*)((const u8*)map.pData + size_t(y) * map.RowPitch);
        for (u32 x = 0; x < desc.Width; x += step)
        {
            float sq = 0.f;
            for (u32 c = 0; c < comp_count; ++c)
            {
                const float v = da_half(row[size_t(x) * comps_in_pixel + comp_offset + c]);
                sq += v * v;
            }
            const float m = _sqrt(sq);
            ++samples;
            if (m > 1e-5f)
                ++nonzero;
            if (m > maxv)
                maxv = m;
            sum += m;
        }
    }

    ctx->Unmap(staging, 0);
    _RELEASE(staging);
    _RELEASE(tex);

    Msg("~ [DA_PROBE] %-18s : %ux%u  nonzero %5.1f%%  max %.5f  mean %.6f", name, desc.Width,
        desc.Height, samples ? 100.f * float(nonzero) / float(samples) : 0.f, maxv,
        samples ? sum / float(samples) : 0.f);
}
} // namespace

// [DA_PORT] ---- Срез G-буфера по строке через прицел -------------------------------------------
//
// Средние по кадру отвечают на вопрос «пишет ли этот проход вообще что-нибудь». На вопрос «что
// творится ВОТ НА ЭТОЙ кромке» они не отвечают: дефект живёт в десятке пикселей, а среднее по
// миллиону их не видит. Здесь читается ОДНА строка вокруг центра экрана, по пикселям, из всех целей
// сразу — и рядом встают глубина, цвет, вектор и маска для одного и того же пикселя.
//
// Наводить прицел на дефект и снимать одним `da_gbuffer_probe`. Стоит кадрового простоя (карта
// ресурса), поэтому команда одноразовая.
// eight_bit: цель хранит по байту на канал (BGRA), а не половинные числа. Формат приходится знать
// заранее: у альбедо он A8R8G8B8, а у позиции и векторов — плавающий, и прочитанное «не тем» даёт
// правдоподобные, но бессмысленные числа (первый снимок так и вышел: 61408 вместо цвета).
static void da_probe_row(pcstr name, const ref_rt& rt, u32 comps, u32 x0, u32 count, u32 y,
    float* out, u32 out_stride, bool eight_bit = false)
{
    for (u32 i = 0; i < count * out_stride; ++i)
        out[i] = 0.f;

    if (!rt || !rt->pTexture)
    {
        Msg("~ [DA_ROW] %-14s : цели нет", name);
        return;
    }

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
    {
        Msg("~ [DA_ROW] %-14s : не 2D-текстура", name);
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
    if (FAILED(HW.pDevice->CreateTexture2D(&sd, nullptr, &staging)) || !staging)
    {
        Msg("~ [DA_ROW] %-14s : копия для чтения не создалась", name);
        _RELEASE(tex);
        return;
    }

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
    ctx->CopyResource(staging, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map)) && y < desc.Height)
    {
        const u8* row8 = (const u8*)map.pData + size_t(y) * map.RowPitch;
        const u16* row = (const u16*)row8;
        for (u32 i = 0; i < count; ++i)
        {
            const u32 x = x0 + i;
            if (x >= desc.Width)
                break;
            for (u32 c = 0; c < out_stride && c < comps; ++c)
            {
                if (eight_bit)
                {
                    // BGRA в памяти: печатаем как r,g,b,a
                    static const u32 swz[4] = { 2, 1, 0, 3 };
                    out[i * out_stride + c] = float(row8[size_t(x) * 4 + swz[c & 3]]) / 255.f;
                }
                else
                    out[i * out_stride + c] = da_half(row[size_t(x) * comps + c]);
            }
        }
        ctx->Unmap(staging, 0);
    }
    else
        Msg("~ [DA_ROW] %-14s : карта ресурса не удалась", name);

    _RELEASE(staging);
    _RELEASE(tex);
}

// [DA_PORT] Найти в кадре самый яркий пиксель. Прицеливаться руками оказалось невозможно: предмет в
// руке лежит СБОКУ от перекрестья, и семь замеров подряд ушли в землю рядом. Яркая точка — это и есть
// искомый объект, искать её должна машина.
static bool da_find_brightest(const ref_rt& rt, u32& out_x, u32& out_y)
{
    out_x = out_y = 0;
    if (!rt || !rt->pTexture)
        return false;

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);
    D3D11_TEXTURE2D_DESC sd = desc;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;

    ID3D11Texture2D* staging = nullptr;
    if (FAILED(HW.pDevice->CreateTexture2D(&sd, nullptr, &staging)) || !staging)
    {
        _RELEASE(tex);
        return false;
    }

    ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
    ctx->CopyResource(staging, tex);

    D3D11_MAPPED_SUBRESOURCE map{};
    bool ok = false;
    if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        u32 best = 0;
        for (u32 y = 0; y < desc.Height; ++y)
        {
            const u8* row = (const u8*)map.pData + size_t(y) * map.RowPitch;
            for (u32 x = 0; x < desc.Width; ++x)
            {
                const u32 sum = u32(row[x * 4 + 0]) + row[x * 4 + 1] + row[x * 4 + 2];
                if (sum > best)
                {
                    best = sum;
                    out_x = x;
                    out_y = y;
                }
            }
        }
        ctx->Unmap(staging, 0);
        ok = best > 0;
    }

    _RELEASE(staging);
    _RELEASE(tex);
    return ok;
}

// [DA_PORT] Покадровое наблюдение за НАКОПЛЕННЫМ СВЕТОМ в пикселе под перекрестьем.
//
// Зачем отдельно от среза G-буфера. Замер da_sun_log показал, что вход ближнего каскада стабилен:
// при неподвижной камере набор теневых объектов не меняется ни на один объект. А картинка при этом
// мерцает. Значит расхождение возникает ПОСЛЕ отбора, и считать объекты дальше бессмысленно —
// надо смотреть на результат.
//
// Читается rt_Accumulator: в нём лежит сумма света до того, как её заберёт phase_combine. Одно
// число в лог на кадр, подряд N кадров — и «мерцает» превращается в последовательность, у которой
// видно и размах, и период. Два значения через кадр и плавное блуждание — это разные болезни:
// первое означает, что где-то чередуются два состояния (ping-pong буфера, чётность кадра), второе —
// что величина считается заново и каждый раз чуть иначе.
//
// Рядом печатается глубина из rt_Position: она доказывает, что смотрим в один и тот же пиксель
// одной и той же поверхности, а не в дрожащий край.
// [DA_PORT] Один пиксель из цели — копированием ОДНОГО ПИКСЕЛЯ, а не всей цели.
//
// Первая версия наблюдения переиспользовала чтение строки от разового среза, а оно копирует
// полноэкранную цель в промежуточную текстуру целиком. Одноразово это работает, но покадрово —
// нет: драйвер освобождает такие копии с задержкой, и на каждом кадре в лог шло «копия для чтения
// не создалась», а замер молча выдавал нули. Нули при этом выглядели как честный результат.
//
// Здесь промежуточная текстура 1x1 и CopySubresourceRegion по одному пикселю: столько можно делать
// хоть каждый кадр.
// resolve_cache: под MSAA цель нельзя ни скопировать в промежуточную текстуру, ни отобразить в
// память — многосэмпловое staging запрещено, и CreateTexture2D просто отказывает. Поэтому сначала
// ResolveSubresource в обычную полноэкранную текстуру, и уже из неё берётся пиксель. Она держится
// между кадрами: создавать полноэкранную текстуру каждый кадр — ровно та ошибка, из-за которой
// предыдущая версия замера выдавала нули.
static bool da_probe_pixel(
    pcstr name, const ref_rt& rt, u32 comps, u32 x, u32 y, float* out, ID3D11Texture2D** resolve_cache)
{
    for (u32 c = 0; c < comps; ++c)
        out[c] = 0.f;

    if (!rt || !rt->pTexture)
        return false;

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    // Многосэмпловую цель сводим в обычную, дальше путь общий.
    if (desc.SampleDesc.Count > 1 && resolve_cache)
    {
        if (*resolve_cache)
        {
            D3D11_TEXTURE2D_DESC cd;
            (*resolve_cache)->GetDesc(&cd);
            if (cd.Width != desc.Width || cd.Height != desc.Height || cd.Format != desc.Format)
                _RELEASE(*resolve_cache);
        }
        if (!*resolve_cache)
        {
            D3D11_TEXTURE2D_DESC rd = desc;
            rd.SampleDesc.Count = 1;
            rd.SampleDesc.Quality = 0;
            rd.MipLevels = 1;
            rd.ArraySize = 1;
            rd.Usage = D3D11_USAGE_DEFAULT;
            rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            rd.CPUAccessFlags = 0;
            rd.MiscFlags = 0;
            HW.pDevice->CreateTexture2D(&rd, nullptr, resolve_cache);
        }
        if (*resolve_cache)
        {
            HW.get_context(CHW::IMM_CTX_ID)->ResolveSubresource(*resolve_cache, 0, tex, 0, desc.Format);
            _RELEASE(tex);
            tex = *resolve_cache;
            tex->AddRef();
            tex->GetDesc(&desc);
        }
    }

    bool ok = false;
    if (x < desc.Width && y < desc.Height && desc.SampleDesc.Count == 1)
    {
        D3D11_TEXTURE2D_DESC sd{};
        sd.Width = 1;
        sd.Height = 1;
        sd.MipLevels = 1;
        sd.ArraySize = 1;
        sd.Format = desc.Format;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* staging = nullptr;
        if (SUCCEEDED(HW.pDevice->CreateTexture2D(&sd, nullptr, &staging)) && staging)
        {
            ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
            const D3D11_BOX box = {x, y, 0, x + 1, y + 1, 1};
            ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, tex, 0, &box);

            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map)))
            {
                const u16* px = (const u16*)map.pData;
                for (u32 c = 0; c < comps; ++c)
                    out[c] = da_half(px[c]);
                ctx->Unmap(staging, 0);
                ok = true;
            }
            _RELEASE(staging);
        }
    }

    if (!ok)
        Msg("~ [DA_LIGHT] %s : пиксель не прочитан", name);

    _RELEASE(tex);
    return ok;
}

// [DA_PORT] Целая строка яркости во всю ширину экрана, одним копированием. Сумма r+g+b на пиксель.
// Промежуточная текстура держится между кадрами: создавать её каждый кадр — та самая ошибка, из-за
// которой первая версия наблюдения молча выдавала нули.
static bool da_probe_line(
    const ref_rt& rt, u32 w, u32 y, xr_vector<float>& out, ID3D11Texture2D** resolve_cache)
{
    out.clear();
    if (!rt || !rt->pTexture || !w)
        return false;

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    if (desc.SampleDesc.Count > 1 && resolve_cache)
    {
        if (*resolve_cache)
        {
            D3D11_TEXTURE2D_DESC cd;
            (*resolve_cache)->GetDesc(&cd);
            if (cd.Width != desc.Width || cd.Height != desc.Height || cd.Format != desc.Format)
                _RELEASE(*resolve_cache);
        }
        if (!*resolve_cache)
        {
            D3D11_TEXTURE2D_DESC rd = desc;
            rd.SampleDesc.Count = 1;
            rd.SampleDesc.Quality = 0;
            rd.MipLevels = 1;
            rd.ArraySize = 1;
            rd.Usage = D3D11_USAGE_DEFAULT;
            rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            rd.CPUAccessFlags = 0;
            rd.MiscFlags = 0;
            HW.pDevice->CreateTexture2D(&rd, nullptr, resolve_cache);
        }
        if (*resolve_cache)
        {
            HW.get_context(CHW::IMM_CTX_ID)->ResolveSubresource(*resolve_cache, 0, tex, 0, desc.Format);
            _RELEASE(tex);
            tex = *resolve_cache;
            tex->AddRef();
            tex->GetDesc(&desc);
        }
    }

    bool ok = false;
    const u32 count = _min(w, desc.Width);
    if (y < desc.Height && desc.SampleDesc.Count == 1 && count)
    {
        static ID3D11Texture2D* s_line = nullptr;
        if (s_line)
        {
            D3D11_TEXTURE2D_DESC ld;
            s_line->GetDesc(&ld);
            if (ld.Width != count || ld.Format != desc.Format)
                _RELEASE(s_line);
        }
        if (!s_line)
        {
            D3D11_TEXTURE2D_DESC ld{};
            ld.Width = count;
            ld.Height = 1;
            ld.MipLevels = 1;
            ld.ArraySize = 1;
            ld.Format = desc.Format;
            ld.SampleDesc.Count = 1;
            ld.Usage = D3D11_USAGE_STAGING;
            ld.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            HW.pDevice->CreateTexture2D(&ld, nullptr, &s_line);
        }
        if (s_line)
        {
            ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
            const D3D11_BOX box = {0, y, 0, count, y + 1, 1};
            ctx->CopySubresourceRegion(s_line, 0, 0, 0, 0, tex, 0, &box);

            D3D11_MAPPED_SUBRESOURCE map{};
            if (SUCCEEDED(ctx->Map(s_line, 0, D3D11_MAP_READ, 0, &map)))
            {
                const u16* px = (const u16*)map.pData;
                out.resize(count);
                for (u32 i = 0; i < count; ++i)
                    out[i] = da_half(px[i * 4 + 0]) + da_half(px[i * 4 + 1]) + da_half(px[i * 4 + 2]);
                ctx->Unmap(s_line, 0);
                ok = true;
            }
        }
    }

    _RELEASE(tex);
    return ok;
}

// [DA_PORT] ---- Замер кэша теневых карт: ВЕСЬ ЭКРАН, все величины разом -----------------------
//
// Прошлые пробы мерили одну точку под перекрестьем, и это дважды подводило: дефект живёт где угодно
// в кадре, а точка отвечает только за себя. Здесь берётся вся картина накопленного света и по ней
// считается, насколько она дрожит между кадрами.
//
// ⚠️ ДВЕ ФАЗЫ, И ЭТО НЕ УКРАШАТЕЛЬСТВО. Полноэкранное чтение буфера стоит кадров само по себе —
// померить им время кадра всё равно что взвешивать себя, стоя на весах с гирей. Поэтому:
//
//   фаза 1 — только время кадра, НИКАКИХ чтений (чистые числа для сравнения включён/выключен кэш);
//   фаза 2 — чтения ради поиска дрожания теней; время кадра в этой фазе не учитывается вовсе.
//
// Промежуточные текстуры создаются ОДИН РАЗ и живут между кадрами: создание полноэкранной копии на
// каждом кадре — та самая ошибка, из-за которой первая версия наблюдения молча выдавала нули.
namespace
{
struct da_shadow_stats
{
    // время кадра — все кадры БЕЗ чтения экрана
    xr_vector<float> times;
    u32 read_frames{0};       // сколько кадров ушло на чтение (в статистику времени не входят)

    // движение игрока за замер — без него числа не с чем соотнести
    float walked{0.f};        // метров пройдено
    float turned{0.f};        // градусов повёрнуто суммарно
    u32 started_ms{0};        // когда начали — чтобы прогоны были одной длины

    // дрожание картинки
    float since_shot_moved{0.f};  // сколько прошли с прошлого снимка
    float since_shot_turned{0.f}; // и насколько повернулись
    u32 shots_skipped{0};         // снимков отброшено из-за движения
    u32 shot_frames{0};
    u32 changed_max{0};
    float delta_max{0.f};
    u64 changed_sum{0};
    u32 samples{0};
};

da_shadow_stats g_shadow;
xr_vector<float> g_prev_screen;
ID3D11Texture2D* g_shadow_staging = nullptr;
ID3D11Texture2D* g_shadow_resolve = nullptr;

// Читает накопитель света целиком с шагом step и складывает яркость в out.
bool da_probe_screen(const ref_rt& rt, u32 step, xr_vector<float>& out, u32& out_w, u32& out_h)
{
    out.clear();
    if (!rt || !rt->pTexture)
        return false;

    ID3DBaseTexture* res = rt->pTexture->surface_get();
    ID3D11Texture2D* tex = nullptr;
    if (res)
        res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    _RELEASE(res);
    if (!tex)
        return false;

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    // Многосэмпловую цель сводим: staging многосэмпловым быть не может.
    if (desc.SampleDesc.Count > 1)
    {
        if (!g_shadow_resolve)
        {
            D3D11_TEXTURE2D_DESC rd = desc;
            rd.SampleDesc.Count = 1; rd.SampleDesc.Quality = 0;
            rd.MipLevels = 1; rd.ArraySize = 1;
            rd.Usage = D3D11_USAGE_DEFAULT;
            rd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            rd.CPUAccessFlags = 0; rd.MiscFlags = 0;
            HW.pDevice->CreateTexture2D(&rd, nullptr, &g_shadow_resolve);
        }
        if (g_shadow_resolve)
        {
            HW.get_context(CHW::IMM_CTX_ID)->ResolveSubresource(g_shadow_resolve, 0, tex, 0, desc.Format);
            _RELEASE(tex);
            tex = g_shadow_resolve;
            tex->AddRef();
            tex->GetDesc(&desc);
        }
    }

    if (!g_shadow_staging)
    {
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        HW.pDevice->CreateTexture2D(&sd, nullptr, &g_shadow_staging);
    }

    bool ok = false;
    if (g_shadow_staging)
    {
        ID3D11DeviceContext* ctx = HW.get_context(CHW::IMM_CTX_ID);
        ctx->CopyResource(g_shadow_staging, tex);

        D3D11_MAPPED_SUBRESOURCE map{};
        if (SUCCEEDED(ctx->Map(g_shadow_staging, 0, D3D11_MAP_READ, 0, &map)))
        {
            out_w = desc.Width / step;
            out_h = desc.Height / step;
            out.reserve(out_w * out_h);
            for (u32 y = 0; y < out_h; ++y)
            {
                const u16* row = (const u16*)((const u8*)map.pData + size_t(y * step) * map.RowPitch);
                for (u32 x = 0; x < out_w; ++x)
                {
                    const size_t o = size_t(x * step) * 4;
                    out.push_back(da_half(row[o + 0]) + da_half(row[o + 1]) + da_half(row[o + 2]));
                }
            }
            ctx->Unmap(g_shadow_staging, 0);
            ok = true;
        }
    }

    _RELEASE(tex);
    return ok;
}
} // namespace

void CRenderTarget::da_shadow_test_frame(int /*unused*/)
{
    extern u32 g_da_smap_skipped;
    extern double g_da_smap_gpu_ms;
    extern u32 g_da_smap_gpu_frames;

    // Движение копим КАЖДЫЙ кадр: на ходу оно и решает, срабатывает кэш или нет.
    g_shadow.walked += g_da_probe_cam_moved;
    g_shadow.turned += g_da_probe_cam_turned;
    g_shadow.since_shot_moved += g_da_probe_cam_moved;
    g_shadow.since_shot_turned += g_da_probe_cam_turned;

    // [DA_PORT] Чтение экрана — раз в десять кадров, и такие кадры в статистику времени НЕ идут.
    //
    // Прежняя схема делила замер на две фазы: сперва время, потом чтения. Для замера стоя это
    // годилось, а для игры — нет: за минуту ходьбы обе величины нужны об ОДНОМ И ТОМ ЖЕ отрезке,
    // иначе сравнивать нечего. Чередование даёт и то, и другое, а исключение читающих кадров из
    // времени сохраняет его чистым: полноэкранное чтение стоит кадров и испортило бы измерение.
    const bool read_this_frame = (Device.dwFrame % 10) == 0;

    if (!read_this_frame)
    {
        g_shadow.times.push_back(Device.fTimeDelta * 1000.f);
        return;
    }

    ++g_shadow.read_frames;

    g_da_jitter_suppress = true;

    constexpr u32 STEP = 4;
    xr_vector<float> cur;
    u32 gw = 0, gh = 0;
    if (!da_probe_screen(rt_Accumulator, STEP, cur, gw, gh) || cur.empty())
        return;

    g_shadow.samples = u32(cur.size());

    // [DA_PORT] Сравниваем ТОЛЬКО когда между снимками игрок почти не двигался.
    //
    // Иначе метрика меряет не тени, а перемещение: два прогона с ОДИНАКОВЫМИ настройками дали 21576
    // и 3275 изменившихся точек — разница в шесть раз, и вся она от того, сколько игрок прошёл между
    // снимками. Числом, которое так пляшет, пользоваться нельзя.
    //
    // Теперь снимок идёт в зачёт лишь при паузе в движении — а паузы в игре бывают постоянно. Кадр
    // при этом всё равно снимается: следующему нужен свежий предыдущий.
    const bool still_enough = (g_shadow.since_shot_moved < 0.05f && g_shadow.since_shot_turned < 0.5f);
    g_shadow.since_shot_moved = 0.f;
    g_shadow.since_shot_turned = 0.f;

    if (!still_enough)
        ++g_shadow.shots_skipped;

    if (still_enough && g_prev_screen.size() == cur.size())
    {
        u32 changed = 0;
        float worst = 0.f;
        for (size_t i = 0; i < cur.size(); ++i)
        {
            const float a = cur[i], b = g_prev_screen[i];
            const float m = _max(a, b);
            if (m < 0.01f)
                continue;
            const float d = _abs(a - b) / m;
            if (d > 0.20f)
                ++changed;
            worst = _max(worst, d);
        }
        g_shadow.changed_sum += changed;
        g_shadow.changed_max = _max(g_shadow.changed_max, changed);
        g_shadow.delta_max = _max(g_shadow.delta_max, worst);
        g_shadow.shot_frames++;
    }
    g_prev_screen = cur;
}

u32 da_shadow_test_elapsed_ms() { return Device.dwTimeGlobal - g_shadow.started_ms; }

void CRenderTarget::da_shadow_test_start()
{
    extern u32 g_da_smap_skipped;
    extern u32 g_da_smap_skipped_by[];
    extern u32 g_da_smap_drawn_by[];
    extern double g_da_smap_gpu_ms;
    extern u32 g_da_smap_gpu_frames;

    g_shadow = da_shadow_stats{};
    g_shadow.started_ms = Device.dwTimeGlobal;
    g_shadow.times.reserve(64 * 1024);
    g_prev_screen.clear();
    g_da_smap_skipped = 0;
    g_da_smap_gpu_ms = 0.0;
    g_da_smap_gpu_frames = 0;
    for (u32 i = 0; i < R__NUM_SUN_CASCADES; ++i)
    {
        g_da_smap_skipped_by[i] = 0;
        g_da_smap_drawn_by[i] = 0;
    }
    Msg("~ [DA_SHADOW] замер ПОШЁЛ на 60 секунд. Играй; закончится сам, или досрочно: da_shadow_test 0");
}

void CRenderTarget::da_shadow_test_report()
{
    extern u32 ps_da_smap_cache;
    extern int ps_da_smap_cache_near;
    extern u32 g_da_smap_skipped;
    extern u32 g_da_smap_skipped_by[];
    extern u32 g_da_smap_drawn_by[];
    extern double g_da_smap_gpu_ms;
    extern u32 g_da_smap_gpu_frames;

    g_da_jitter_suppress = false;

    Msg("~ [DA_SHADOW] ============ ЗАМЕР КЭША ТЕНЕВЫХ КАРТ ============");
    Msg("~ [DA_SHADOW] настройки: da_smap_cache %u мс, near %d, рендер %ux%u", ps_da_smap_cache,
        ps_da_smap_cache_near, Device.dwRenderWidth, Device.dwRenderHeight);
    const float secs = float(Device.dwTimeGlobal - g_shadow.started_ms) / 1000.f;
    Msg("~ [DA_SHADOW] длительность %.1f с | пройдено %.1f м | поворотов суммарно %.0f град", secs,
        g_shadow.walked, g_shadow.turned);

    // ВРЕМЯ КАДРА: среднего мало — заикания живут в хвосте распределения, а их игрок и замечает.
    if (!g_shadow.times.empty())
    {
        xr_vector<float> t = g_shadow.times;
        std::sort(t.begin(), t.end());
        const size_t n = t.size();
        double sum = 0.0;
        for (float v : t)
            sum += v;
        const double avg = sum / double(n);
        const auto pct = [&](float p) { return t[_min(n - 1, size_t(p * float(n)))]; };

        Msg("~ [DA_SHADOW] ВРЕМЯ КАДРА по %u кадрам (читающие %u исключены):", u32(n), g_shadow.read_frames);
        Msg("~ [DA_SHADOW]   среднее %.2f мс (%.0f к/с) | медиана %.2f | лучший %.2f", avg,
            avg > 0.0 ? 1000.0 / avg : 0.0, pct(0.50f), t.front());
        Msg("~ [DA_SHADOW]   ХВОСТ: 95%% %.2f | 99%% %.2f | худший %.2f мс (%.0f к/с в худшем)", pct(0.95f),
            pct(0.99f), t.back(), t.back() > 0.f ? 1000.f / t.back() : 0.f);
    }

    // ТЕНЕВЫЕ КАРТЫ: по каскадам — видно, какой реально кэшируется, а какой нет.
    Msg("~ [DA_SHADOW] ТЕНЕВЫЕ КАРТЫ: пропущено всего %u", g_da_smap_skipped);
    for (u32 i = 0; i < R__NUM_SUN_CASCADES; ++i)
    {
        const u32 all = g_da_smap_skipped_by[i] + g_da_smap_drawn_by[i];
        Msg("~ [DA_SHADOW]   каскад %u: из кэша %u, нарисовано %u -> попаданий %.0f%%", i,
            g_da_smap_skipped_by[i], g_da_smap_drawn_by[i],
            all ? 100.f * float(g_da_smap_skipped_by[i]) / float(all) : 0.f);
    }

    if (g_da_smap_gpu_frames)
        Msg("~ [DA_SHADOW] ФАЗА ТЕНЕВЫХ КАРТ на видеокарте: %.3f мс (кадров %u)",
            g_da_smap_gpu_ms / double(g_da_smap_gpu_frames), g_da_smap_gpu_frames);
    else
        Msg("~ [DA_SHADOW] ФАЗА ТЕНЕВЫХ КАРТ: нет данных - включи da_gpu_log ПЕРЕД замером");

    if (g_shadow.shot_frames)
    {
        Msg("~ [DA_SHADOW] ДРОЖАНИЕ (весь экран, %u точек, %u снимков, джиттер погашен):", g_shadow.samples,
            g_shadow.shot_frames);
        Msg("~ [DA_SHADOW]   в среднем %.1f точек за снимок | худший снимок %u (%.2f%% экрана)",
            double(g_shadow.changed_sum) / double(g_shadow.shot_frames), g_shadow.changed_max,
            g_shadow.samples ? 100.f * float(g_shadow.changed_max) / float(g_shadow.samples) : 0.f);
        Msg("~ [DA_SHADOW]   отброшено снимков из-за движения: %u (в зачёт идут только паузы)",
            g_shadow.shots_skipped);
    }
    Msg("~ [DA_SHADOW] =================================================");

    g_shadow = da_shadow_stats{};
    g_prev_screen.clear();
}

void CRenderTarget::da_light_watch()
{
    const u32 w = Device.dwRenderWidth, h = Device.dwRenderHeight;
    if (!w || !h)
        return;

    const u32 x = w / 2, y = h / 2;

    // Сведённые копии держим между кадрами, по одной на цель — см. пояснение у da_probe_pixel.
    static ID3D11Texture2D* s_resolve_acc = nullptr;
    static ID3D11Texture2D* s_resolve_pos = nullptr;

    float acc[4] = {}, pos[4] = {};
    da_probe_pixel("rt_Accumulator", rt_Accumulator, 4, x, y, acc, &s_resolve_acc);
    da_probe_pixel("rt_Position", rt_Position, 4, x, y, pos, &s_resolve_pos);

    // [DA_PORT] Кроме пикселя под перекрестьем — вся строка во всю ширину экрана.
    //
    // Первая версия смотрела в одну точку, и когда точка успокоилась, мерцание никуда не делось:
    // мигало рядом. Одна точка отвечает «здесь ли», а нужен ответ «где». Строка целиком берётся
    // одним копированием, это дёшево, и сразу видно и величину, и координату худшего пикселя.
    {
        static ID3D11Texture2D* s_resolve_line = nullptr;
        static xr_vector<float> prev_line;
        static u32 prev_w = 0;

        // Кадры с движущейся камерой в сравнение не идут: при повороте строка законно меняется
        // целиком, и счётчик «широких событий» насчитает дефект там, где его нет. Один такой прогон
        // уже дал 419 событий вместо десяти и выглядел как резкое ухудшение.
        extern float g_da_probe_cam_moved;
        extern float g_da_probe_cam_turned;
        const bool still = (g_da_probe_cam_moved <= 0.002f && g_da_probe_cam_turned <= 0.03f);

        xr_vector<float> line;
        if (da_probe_line(rt_Accumulator, w, y, line, &s_resolve_line) && !line.empty())
        {
            if (still && prev_w == w && prev_line.size() == line.size())
            {
                float worst = 0.f;
                u32 worst_x = 0;
                u32 changed = 0;
                for (u32 i = 0; i < line.size(); ++i)
                {
                    const float a = line[i], b = prev_line[i];
                    const float m = _max(a, b);
                    if (m < 0.005f) // тьма: относительная разница там бессмысленна
                        continue;
                    const float d = _abs(a - b) / m;
                    if (d > 0.20f)
                        ++changed;
                    if (d > worst)
                    {
                        worst = d;
                        worst_x = i;
                    }
                }
                if (worst > 0.20f)
                    Msg("~ [DA_LIGHT] строка y=%u: худший пиксель x=%u  %.4f -> %.4f (%.0f%%), пикселей "
                        "изменилось больше чем на 20%%: %u из %u",
                        y, worst_x, prev_line[worst_x], line[worst_x], 100.f * worst, changed, u32(line.size()));
            }
            prev_line = line;
            prev_w = w;
        }
    }

    static float prev_lum = -1.f;
    const float lum = acc[0] + acc[1] + acc[2];

    pcstr mark = "";
    if (prev_lum >= 0.f)
    {
        const float d = _abs(lum - prev_lum);
        if (d > 0.5f * _max(lum, prev_lum))
            mark = "   <== СВЕТ УПАЛ/ВЫРОС БОЛЬШЕ ЧЕМ ВДВОЕ";
        else if (d > 0.05f * _max(lum, prev_lum))
            mark = "   <== заметное изменение";
    }
    prev_lum = lum;

    Msg("~ [DA_LIGHT] кадр %u | свет r/g/b %7.4f %7.4f %7.4f | сумма %7.4f | глубина %6.3f%s", Device.dwFrame,
        acc[0], acc[1], acc[2], lum, pos[2], mark);
}

// [DA_PORT] Ездит ли ГОТОВЫЙ кадр относительно предыдущего — числом, а не на глаз.
//
// Зачем понадобился. «Мир трясётся» — это наблюдение, и до сих пор его нечем было проверить: замеры
// показали, что камера стоит намертво, сдвиг джиттера штатный, постпроцесс единичный, а картинка
// всё равно дрожит. Все прежние приборы смотрят ДО апскейлера, где кадр обязан меняться — там сдвиг
// и есть смысл происходящего. Отвечает на вопрос только то, что выходит ПОСЛЕ него.
//
// Как считает. Берётся строка через середину экрана из выхода апскейлера (выходное разрешение) и
// сравнивается с такой же строкой прошлого кадра при сдвигах от -3 до +3 пикселей. Печатается тот
// сдвиг, при котором строки совпадают лучше всего, и рядом — насколько плохо они совпадают «на
// месте». Толкование прямое:
//
//   • лучший сдвиг 0 и несовпадение на месте малое — кадр стоит, дрожь не в геометрии картинки;
//   • лучший сдвиг ±1 и на месте заметно хуже — кадр РЕАЛЬНО ездит, апскейлер сдвиг не снимает;
//   • лучший сдвиг скачет по знаку каждый кадр — ездит вслед за джиттером (у Halton по основанию 2
//     знак меняется каждый кадр), и тогда виноват тот, кто джиттер не компенсирует.
//
// Целые пиксели, без подпиксельной точности: различить «стоит» и «ездит» этого достаточно, а
// уточнение потребовало бы интерполяции, которая на шумной строке сама создаёт ложный сдвиг.
void CRenderTarget::da_shift_watch()
{
    const u32 w = Device.dwWidth, h = Device.dwHeight; // ВЫХОДНОЕ разрешение: кадр уже собран
    if (!w || !h || !rt_FSR2_out)
        return;

    static ID3D11Texture2D* s_resolve = nullptr;
    static xr_vector<float> prev;
    static u32 prev_w = 0;

    xr_vector<float> line;
    if (!da_probe_line(rt_FSR2_out, w, h / 2, line, &s_resolve) || line.empty())
        return;

    if (prev_w == w && prev.size() == line.size() && line.size() > 16)
    {
        constexpr int R = 3;
        float sad_here = 0.f, sad_best = -1.f;
        int shift_best = 0;
        for (int s = -R; s <= R; ++s)
        {
            double sum = 0.0;
            u32 n = 0;
            for (u32 i = R; i + R < line.size(); ++i)
            {
                sum += _abs(line[i] - prev[u32(int(i) + s)]);
                ++n;
            }
            const float sad = n ? float(sum / double(n)) : 0.f;
            if (s == 0)
                sad_here = sad;
            if (sad_best < 0.f || sad < sad_best)
            {
                sad_best = sad;
                shift_best = s;
            }
        }
        Msg("~ [DA_SHIFT] кадр %u | лучше всего совпало при сдвиге %+d пикс | несовпадение: на месте "
            "%.5f, при сдвиге %.5f",
            Device.dwFrame, shift_best, sad_here, sad_best);
    }

    prev = line;
    prev_w = w;
}

void CRenderTarget::da_dump_gbuffer_row()
{
    const u32 w = Device.dwRenderWidth, h = Device.dwRenderHeight;
    if (!w || !h)
        return;

    constexpr u32 COUNT = 24; // столько пикселей хватает, чтобы кромка попала в срез целиком

    // [DA_PORT] Центр среза выбирается ручкой r__probe_center: по умолчанию самый яркий пиксель
    // кадра (так проба делалась под кромку свечения), но для растительности это бесполезно —
    // ярче всего в лесу небо, а не куст. Значение 1 берёт перекрестье, то есть то, на что смотрит
    // игрок.
    u32 bx = w / 2, by = h / 2;
    const bool by_crosshair = (::ps_r__probe_center == 1);
    const bool found = !by_crosshair && da_find_brightest(rt_Color, bx, by);
    const u32 y = found ? by : h / 2;
    const u32 cx = found ? bx : w / 2;
    Msg("~ [DA_ROW] центр среза: %s (%u, %u)", found ? "самый яркий пиксель кадра" : "перекрестье", cx, y);

    // [DA_PORT] Двигалась ли камера в момент снимка — печатаем прямо здесь.
    //
    // Без этого срез неинтерпретируем: у неподвижной камеры векторы скорости нулевые ПО
    // ОПРЕДЕЛЕНИЮ, и «нули в буфере» одинаково значат и «шейдер не пишет вектора», и «ты снял
    // стоя». Два захода подряд на это и ушли. Теперь замер отвечает на вопрос сам.
    Msg("~ [DA_ROW] камера за кадр: сместилась %.4f м, повернулась %.3f град -> %s", g_da_probe_cam_moved,
        g_da_probe_cam_turned,
        (g_da_probe_cam_moved > 0.005f || g_da_probe_cam_turned > 0.1f)
            ? "ДВИЖЕТСЯ (векторы обязаны быть ненулевыми)"
            : "стоит (нули в векторах ожидаемы и ничего не значат)");

    // [DA_PORT] Сколько ПИКСЕЛЕЙ должен был проехать неподвижный мир за этот кадр — грубая оценка
    // от поворота камеры. Нужна затем, что «0.0008 в буфере» само по себе не говорит ничего: это и
    // правильный ответ при вялом повороте, и почти-ноль при дыре в векторах. А «ожидалось 6 пикселей,
    // в буфере 0.01» — уже приговор.
    const float px_per_deg = float(h) / (Device.fFOV > 1.f ? Device.fFOV : 67.5f);
    const float expect_px = g_da_probe_cam_turned * px_per_deg;
    Msg("~ [DA_ROW] ожидаемый сдвиг статичного мира: ~%.2f пикс за кадр (поле зрения %.1f град)", expect_px,
        Device.fFOV);

    const u32 x0 = (cx > COUNT / 2) ? (cx - COUNT / 2) : 0;

    float pos[COUNT * 4], col[COUNT * 4], vel[COUNT * 2], react[COUNT * 1];
    da_probe_row("rt_Position", rt_Position, 4, x0, COUNT, y, pos, 4);
    da_probe_row("rt_Color", rt_Color, 4, x0, COUNT, y, col, 4, true); // альбедо: 8 бит на канал
    da_probe_row("rt_Velocity", rt_Velocity, 2, x0, COUNT, y, vel, 2);
    da_probe_row("rt_Reactive", rt_Reactive, 1, x0, COUNT, y, react, 1);

    Msg("~ [DA_ROW] ---- строка y=%u, x=%u..%u (разрешение рендера %ux%u) ----", y, x0, x0 + COUNT - 1, w, h);
    Msg("~ [DA_ROW]   x  |  глубина |    цвет r/g/b     | глянец |  вектор x/y, ПИКС | реакт.");

    // [DA_PORT] Вектора печатаем в пикселях, а не в долях экрана. Множитель тот же, что мы отдаём
    // апскейлерам в motionVectorScale (полширины и полвысоты) — то есть в логе ровно те числа,
    // которыми оперирует FSR/DLSS, а не абстракция, которую ещё надо в уме пересчитывать.
    const float mvx = 0.5f * float(w), mvy = 0.5f * float(h);
    float max_px = 0.f;
    for (u32 i = 0; i < COUNT; ++i)
    {
        const float vx = vel[i * 2 + 0] * mvx, vy = vel[i * 2 + 1] * mvy;
        max_px = std::max(max_px, std::max(_abs(vx), _abs(vy)));
        Msg("~ [DA_ROW] %4u | %8.3f | %6.2f %6.2f %6.2f | %6.3f | %+8.3f %+8.3f | %5.3f",
            x0 + i, pos[i * 4 + 2], col[i * 4 + 0], col[i * 4 + 1], col[i * 4 + 2], col[i * 4 + 3], vx, vy, react[i]);
    }
    Msg("~ [DA_ROW] ---- конец среза; наибольший вектор в строке %.3f пикс против ожидаемых ~%.2f ----", max_px,
        expect_px);

    // [DA_PORT] Карта силуэта: одна строка могла попасть в ровный участок кромки, а зубцы идут с шагом
    // в несколько пикселей ПО ВЕРТИКАЛИ. Печатаем прямоугольник: `#` — поверхность объекта по глубине,
    // `.` — фон. Ровная граница в буфере при пиле на экране означала бы, что зубцы делает уже
    // реконструкция; зубчатая — что они приходят из растеризации.
    {
        constexpr u32 ROWS = 16;
        const u32 y0 = (y > ROWS / 2) ? (y - ROWS / 2) : 0;

        // Порог: посередине между глубиной объекта и фона в снятой строке.
        float near_d = 1e6f, far_d = 0.f;
        for (u32 i = 0; i < COUNT; ++i)
        {
            const float d = pos[i * 4 + 2];
            if (d > 0.01f)
            {
                near_d = std::min(near_d, d);
                far_d = std::max(far_d, d);
            }
        }
        const float thr = (near_d + far_d) * 0.5f;

        Msg("~ [DA_MAP] силуэт по глубине, порог %.3f  ('#' ближе — объект, '.' дальше — фон)", thr);
        float row[COUNT * 4];
        for (u32 r = 0; r < ROWS; ++r)
        {
            da_probe_row("rt_Position", rt_Position, 4, x0, COUNT, y0 + r, row, 4);
            char line[COUNT + 1];
            for (u32 i = 0; i < COUNT; ++i)
                line[i] = (row[i * 4 + 2] > 0.01f && row[i * 4 + 2] < thr) ? '#' : '.';
            line[COUNT] = 0;
            Msg("~ [DA_MAP] y=%4u |%s|", y0 + r, line);
        }
        Msg("~ [DA_MAP] ---- конец карты ----");
    }

    FlushLog();
}

// Widens the reactive mask around things that are actually moving through the world, so that an
// upscaler stops trusting its history in the band a moving figure has just uncovered. That band is
// where ghosting lives: the figure itself reprojects correctly, but the ground behind it is being
// blended with a history that still holds the figure. The whole derivation is in da_reactive.ps.
//
// Reads the mask the G-buffer left, widens it, writes the result to the scratch target and copies it
// back - a draw cannot read and write the same target. Skipped entirely at zero scale.
void CRenderTarget::phase_reactive()
{
    if (ps_r__reactive_object <= 0.f || !s_reactive || !s_reactive_dilate_h || !s_reactive_dilate_v ||
        !rt_Reactive || !rt_Reactive_scratch || !rt_Reactive_scratch2)
        return;

    // Nothing but an upscaler ever reads this mask, so with all of them off the pass is pure cost.
    // [DA_PORT] Через общий список, а не перечислением: этот if уже дважды забывали обновить при
    // добавлении бэкенда, и оба раза маска молча переставала строиться.
    if (!da_upscaler_active())
        return;

    PIX_EVENT(DA_phase_reactive);

    // [DA_PORT] Everything this pass works in is travel PER FRAME, so every setting it takes is frame
    // rate dependent - a threshold that separates a walking figure from swaying grass at 130fps admits
    // the grass at 60, and a band wide enough for the trail at 130 covers half of it at 60. Values
    // tuned on one machine would be wrong on every other, which is no use in something meant to ship.
    //
    // Converted here rather than in the shader, because it costs nothing on the CPU and keeps the
    // arithmetic in one readable place: slower frames mean a proportionally larger threshold, a
    // proportionally smaller scale, and a proportionally wider band.
    const float dt = _max(Device.fTimeDelta, 0.0005f);
    const float dt_ref = 1.f / _max(float(ps_r__reactive_ref_fps), 1.f);
    const float k = dt_ref / dt; // below 1 when frames are slower than the reference

    const float deadzone = ps_r__reactive_deadzone / k;
    const float scale = ps_r__reactive_object * k;

    // Widening runs one axis at a time now, so the radius costs 2r+1 reads rather than its square and
    // a wide band is affordable - which it has to be, because at sixty frames a second the trail is
    // over twenty pixels across. The ceiling is only there so a low frame rate cannot turn one pass
    // into a thousand reads per pixel and make the frame rate worse still.
    int radius = iFloor(float(ps_r__reactive_dilate) / k + 0.5f);
    if (radius < 1)
        radius = 1;
    if (radius > 40)
        radius = 40;

    // [DA_PORT] Одна строка в лог на запуск прохода — без единой ручки.
    //
    // Стоила отдельного разбора: два замера подряд молчали, и «маска не строится» было неотличимо
    // от «инструмент не доехал». Отличить их изнутри игры нечем — оба выглядят как пустой лог.
    // Теперь любой лог сам отвечает, работает ли проход и с какими числами.
    //
    // Печатается и ширина размытия метки: она считается от времени кадра, и на низкой частоте
    // растёт в разы (3 пикселя при опорных 280 кадрах превращаются в 30 при тридцати). Каждый
    // такой пиксель апскейлер уводит к ТЕКУЩЕМУ кадру, а текущий кадр смещён джиттером.
    {
        static u32 s_last_report = 0;
        if (Device.dwFrame - s_last_report > 3000 || Device.dwFrame < s_last_report)
        {
            s_last_report = Device.dwFrame;
            Msg("* [DA_REACT] маска строится: кадр %.1f мс (опора %d) -> сила %.1f, ширина %d пикс, "
                "порог %.5f", dt * 1000.f, ps_r__reactive_ref_fps, scale, radius, deadzone);
        }
    }

    // [DA_PORT] Inputs measured before the draw touches anything, output after - see da_probe.
    const bool selftest = !!ps_r__reactive_selftest;
    if (selftest)
    {
        Msg("~ [DA_PROBE] ---- reactive pass, inputs ----");
        Msg("~ [DA_PROBE] set: scale %.1f  dilate %d  deadzone %.5f  ref %d fps",
            ps_r__reactive_object, ps_r__reactive_dilate, ps_r__reactive_deadzone,
            ps_r__reactive_ref_fps);
        Msg("~ [DA_PROBE] now: %.1f fps (dt %.5f)  ->  scale %.1f  dilate %d  deadzone %.5f",
            1.f / dt, dt, scale, radius, deadzone);
        da_probe("rt_Reactive in", rt_Reactive, 1, 0, 1);   // the mask the G-buffer left
        da_probe("rt_Velocity", rt_Velocity, 2, 0, 2);      // motion vectors, xy
        da_probe("rt_Position z", rt_Position, 4, 2, 1);    // eye-space depth
    }

    // One full-screen quad, drawn with whichever shader and into whichever target the caller names.
    const auto quad = [&](const ref_rt& target, const ref_shader& shader)
    {
        u_setrt(RCache, target, nullptr, nullptr, nullptr);

        RCache.set_Stencil(FALSE);
        RCache.set_Z(FALSE);
        RCache.set_CullMode(CULL_NONE);

        u32 Offset = 0;
        FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
        RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(shader->E[0]);
        RCache.set_Geometry(g_combine);
        return Offset;
    };

    // Motion through the world, one evaluation per pixel, into scratch2.
    const auto draw = [&](float debug_mode)
    {
        const u32 offset = quad(rt_Reactive_scratch2, s_reactive);
        RCache.set_c("da_reactive", scale, float(radius), deadzone, debug_mode);
        RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
    };

    // The widening, one axis per call. Across into scratch, then down into scratch2, where the mask
    // the G-buffer left joins undilated - the order matters only in that neither target is ever read
    // and written by the same draw.
    const auto dilate = [&](bool vertical)
    {
        const u32 offset = quad(vertical ? rt_Reactive_scratch2 : rt_Reactive_scratch,
            vertical ? s_reactive_dilate_v : s_reactive_dilate_h);
        RCache.set_c("da_dilate", vertical ? 0.f : 1.f, vertical ? 1.f : 0.f, float(radius),
            vertical ? 1.f : 0.f);
        RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);
    };

    // [DA_PORT] Fill the target with a value by hand first, then draw over it. The two readings that
    // follow separate the last two possibilities outright: if the marker survives the draw, the draw
    // reaches nothing; if it is gone, the draw lands and the shader is at fault. Every earlier probe
    // measured the two together and could not tell them apart.
    if (selftest && rt_Reactive_scratch2->pRT)
    {
        const float mark[4] = { 0.25f, 0.f, 0.f, 0.f };
        HW.get_context(CHW::IMM_CTX_ID)->ClearRenderTargetView(rt_Reactive_scratch2->pRT, mark);
        da_probe("marker 0.25", rt_Reactive_scratch2, 1, 0, 1);
    }

    draw(float(ps_r__reactive_debug));

    if (selftest)
    {
        Msg("~ [DA_PROBE] ---- reactive pass, output ----");
        da_probe("motion, undilated", rt_Reactive_scratch2, 1, 0, 1);

        // [DA_PORT] The same draw again per debug mode, each writing one ingredient AS THE SHADER SEES
        // IT rather than as the buffer holds it. That distinction is the whole reason for doing this:
        // the probes above prove the targets have content, not that this pass can read them, and a
        // constant or a texture that fails to arrive looks identical to arithmetic that returns zero.
        static pcstr what[] = { "base (mask in)", "velocity x200", "eye depth x0.02", "world motion x250" };
        for (int m = 1; m <= 4; ++m)
        {
            draw(float(m));
            da_probe(what[m - 1], rt_Reactive_scratch2, 1, 0, 1);
        }

        // Leave the buffer holding the real thing, not the last diagnostic.
        draw(float(ps_r__reactive_debug));
    }

    dilate(false); // across
    dilate(true);  // and down, folding in the G-buffer's own mask

    if (selftest)
    {
        da_probe("final mask", rt_Reactive_scratch2, 1, 0, 1);
        Msg("~ [DA_PROBE] ---- done ----");
        ps_r__reactive_selftest = 0; // one shot: the readback stalls the pipeline
    }

    // Back over the original, so the upscalers keep reading rt_Reactive and need no knowledge of
    // this pass at all - exactly as with the velocity guard.
    ID3DBaseTexture* src = rt_Reactive_scratch2->pTexture->surface_get();
    ID3DBaseTexture* dst = rt_Reactive->pTexture->surface_get();
    if (src && dst)
        HW.get_context(CHW::IMM_CTX_ID)->CopyResource(dst, src);
    _RELEASE(src);
    _RELEASE(dst);
}

// [DA_PORT] ---- Метка свечения в маске реактивности ---------------------------------------------
//
// Идёт СРАЗУ после отрисовки самосветящейся геометрии (см. вызов в r2_R_render.cpp), пока трафарет
// ещё помнит, куда она легла: следом начинается свет, а он переписывает трафарет своими маркерами
// источников, и к концу кадра от этой отметки не остаётся ничего.
//
// Пишет прямо в rt_Reactive, а не в рабочую пару phase_reactive - и это не небрежность, а
// единственный порядок, при котором метка доживает до апскейлера: phase_reactive читает rt_Reactive
// как "маску, оставленную G-буфером", берёт максимум с собственным результатом и копирует обратно.
// Значит метка, положенная сюда раньше, входит в итог сама. А если phase_reactive выключен нулевой
// силой или отсутствием апскейлера, метка просто остаётся лежать - тоже верно.
//
// Отбор пикселей целиком на трафарете: рисовавший проход уже решил, что здесь свечение. Никаких
// выборок из буферов, никакой геометрии - один полноэкранный четырёхугольник.
//
// Условие вынесено в отдельный метод и спрашивается ДВАЖДЫ: здесь и в r2_R_render.cpp, где в
// трафарет пишется добавочный бит. Разойдись эти два места - получили бы либо неснятый бит в
// трафарете, либо проход по пустому месту.
bool CRenderTarget::da_emissive_mark_ready() const
{
    // Маску читает только апскейлер: без него весь проход - чистые затраты.
    return ps_r__reactive_emissive > 0.f && da_upscaler_active() && s_reactive_emissive && rt_Reactive;
}

// То же самое для прозрачной геометрии - стекло, вода, частицы, всё из очередей mapSorted. Она тоже
// рисуется после G-буфера и тоже не оставляет о себе ни вектора, ни реактивности.
//
// Своя ручка, и по умолчанию НОЛЬ. Свечение занимает в кадре считанные пиксели, а прозрачного бывает
// пол-экрана: метка означает "не копить историю", и на большой поверхности воды это меняет картинку
// заметно. Включать осознанно и смотреть.
bool CRenderTarget::da_transparent_mark_ready() const
{
    return ps_r__reactive_transparent > 0.f && da_upscaler_active() && s_reactive_emissive && rt_Reactive;
}

// [DA_PORT] Только вода. Отличие от предыдущего не в силе метки, а в том, КТО ставит отметку в
// трафарете: там её ставит движок на весь прямой проход разом, здесь — сама вода своим блоком
// состояний (dx10stencil в effects_water.s и двух его зелёных близнецах).
//
// Почему это лучше грубой метки всего прозрачного:
//   • дождь, стёкла и частицы не задеваются, а прозрачного в кадре бывает пол-экрана;
//   • в меню и на локациях без воды отметки просто нет, и проход не делает ничего. Грубый вариант
//     этим и опасен: сохранённый в user.ltx, он применялся с самого старта и давал чёрный экран,
//     потому что метил кадр там, где сцены ещё нет.
bool CRenderTarget::da_water_mark_ready() const
{
    return ps_r__reactive_water > 0.f && da_upscaler_active() && s_reactive_emissive && rt_Reactive;
}

void CRenderTarget::phase_reactive_emissive()
{
    if (!da_emissive_mark_ready())
        return;

    PIX_EVENT(DA_phase_reactive_emissive);
    da_mark_reactive_from_stencil(ps_r__reactive_emissive);
}

// Прозрачное. Отличие от свечения только в двух вещах: своя сила метки и обязательный возврат цели -
// зовётся из середины phase_combine, где следом рисуется интерфейс, и оставить привязанной маску
// вместо кадра значило бы вылить интерфейс в маску реактивности.
void CRenderTarget::phase_reactive_transparent()
{
    // [DA_PORT] Два режима на один проход. Грубый метит весь прямой проход (отметку ставит движок
    // перед render_forward), точный — только воду (отметку ставит она сама). Если включены оба,
    // выигрывает грубый: он уже пометил в трафарете всё, включая воду, и второй проход был бы
    // просто повтором по тому же биту.
    const float value = da_transparent_mark_ready() ? ps_r__reactive_transparent
                                                    : (da_water_mark_ready() ? ps_r__reactive_water : 0.f);
    if (value <= 0.f)
        return;

    PIX_EVENT(DA_phase_reactive_transparent);
    da_mark_reactive_from_stencil(value);

    u_setrt(RCache, rt_Generic_0_r, nullptr, nullptr, rt_MSAADepth);
    RCache.set_Stencil(FALSE);
}

// Общая часть обеих меток: один полноэкранный четырёхугольник по трафаретному биту 0x02.
void CRenderTarget::da_mark_reactive_from_stencil(float value)
{
    // Один полноэкранный четырёхугольник в маску, с привязанным буфером глубины-трафарета: без него
    // трафаретный тест сравнивать не с чем и метка легла бы на весь экран.
    const auto quad = [&]()
    {
        u_setrt(RCache, rt_Reactive, nullptr, nullptr, rt_MSAADepth);

        RCache.set_Z(FALSE);
        RCache.set_CullMode(CULL_NONE);

        u32 Offset = 0;
        FVF::TL* pv = (FVF::TL*)RImplementation.Vertex.Lock(4, g_combine->vb_stride, Offset);
        pv->set(-1.f, 1.f, 0.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(-1.f, -1.f, 0.f, 0.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, 1.f, 1.f, 1.f, 0u, 0.f, 0.f); pv++;
        pv->set(1.f, -1.f, 1.f, 0.f, 0u, 0.f, 0.f); pv++;
        RImplementation.Vertex.Unlock(4, g_combine->vb_stride);

        RCache.set_Element(s_reactive_emissive->E[0]);
        RCache.set_Geometry(g_combine);
        return Offset;
    };

    // Один-единственный draw делает оба дела сразу: пишет метку в цвет и тут же гасит добавленный
    // бит трафарета. Разносить это на два прохода было бы хуже - между ними появилось бы состояние,
    // в котором метка уже поставлена, а трафарет ещё испорчен.
    //
    // Тест: пропускать там, где стоят ОБА младших бита, то есть где рисовалось свечение (0x01 общий
    // для всей непрозрачной геометрии кадра, 0x02 добавлен в r2_R_render.cpp ради этой отметки).
    //
    // Запись: маска 0x02 и операция ZERO гасят ровно добавленный бит, 0x01 остаётся. Возвращать
    // трафарет обязательно - свет и отражения сравнивают его с 0x01, и не только на "не меньше":
    // в accum_reflected стоят проверки на РАВЕНСТВО. Оставленная отметка вылечила бы мерцание и
    // отняла взамен отражения на тех же пикселях.
    //
    // Порядок обязателен: состояние ставится ПОСЛЕ set_Element внутри quad(). Блок состояний шейдера
    // применяется в set_Element, а StateManager - уже в самом Render(), и перекрывает его.
    //
    // ⚠️ Если этот draw будет молча отброшен (несовпадение раскладки вершины с VS - см.
    // da_fullscreen.vs), то не поставится не только метка: останется висеть и бит трафарета.
    // Поэтому шейдер здесь свой и рисуется той же геометрией g_combine, что и остальные проходы.
    const u32 offset = quad();
    RCache.set_Stencil(TRUE, D3DCMP_EQUAL, 0x03, 0x03, 0x02,
        D3DSTENCILOP_KEEP, D3DSTENCILOP_ZERO, D3DSTENCILOP_KEEP);
    RCache.set_c("da_mark", value, 0.f, 0.f, 0.f);
    RCache.Render(D3DPT_TRIANGLELIST, offset, 0, 4, 0, 2);

    RCache.set_Stencil(FALSE);
}
} // namespace xray::render::RENDER_NAMESPACE
