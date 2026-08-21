#include "pch.hpp"
#include "ui_base.h"
#include "Cursor/UICursor.h"
#include "xrCore/XML/XMLDocument.hpp"
#include "XML/UIXmlInitBase.h"
#include "XML/UITextureMaster.h"

CUICursor& GetUICursor() { return GEnv.UI->GetUICursor(); }
UICore& UI() { return *GEnv.UI; }
extern ENGINE_API Fvector2 g_current_font_scale;

void S2DVert::rotate_pt(const Fvector2& pivot, const float cosA, const float sinA, const float kx)
{
    Fvector2 t = pt;
    t.sub(pivot);
    pt.x = t.x * cosA + t.y * sinA;
    pt.y = t.y * cosA - t.x * sinA;
    pt.x *= kx;
    pt.add(pivot);
}
void C2DFrustum::CreateFromRect(const Frect& rect)
{
    m_rect.set(float(rect.x1), float(rect.y1), float(rect.x2), float(rect.y2));
    planes.resize(4);
    planes[0].build(rect.lt, Fvector2().set(-1, 0));
    planes[1].build(rect.lt, Fvector2().set(0, -1));
    planes[2].build(rect.rb, Fvector2().set(+1, 0));
    planes[3].build(rect.rb, Fvector2().set(0, +1));
}

sPoly2D* C2DFrustum::ClipPoly(sPoly2D& S, sPoly2D& D) const
{
    bool bFullTest = false;
    for (u32 j = 0; j < S.size(); j++)
    {
        if (!m_rect.in(S[j].pt))
        {
            bFullTest = true;
            break;
        }
    }

    sPoly2D* src = &D;
    sPoly2D* dest = &S;
    if (!bFullTest)
        return dest;

    for (u32 i = 0; i < planes.size(); i++)
    {
        // cache plane and swap lists
        const Fplane2& P = planes[i];
        std::swap(src, dest);
        dest->clear();

        // classify all points relative to plane #i
        float cls[UI_FRUSTUM_SAFE];
        for (u32 j = 0; j < src->size(); j++)
            cls[j] = P.classify((*src)[j].pt);

        // clip everything to this plane
        cls[src->size()] = cls[0];
        src->push_back((*src)[0]);
        Fvector2 dir_pt, dir_uv;
        float denum, t;
        for (u32 j = 0; j < src->size() - 1; j++)
        {
            if ((*src)[j].pt.similar((*src)[j + 1].pt, EPS_S))
                continue;
            if (negative(cls[j]))
            {
                dest->push_back((*src)[j]);
                if (positive(cls[j + 1]))
                {
                    // segment intersects plane
                    dir_pt.sub((*src)[j + 1].pt, (*src)[j].pt);
                    dir_uv.sub((*src)[j + 1].uv, (*src)[j].uv);
                    denum = P.n.dotproduct(dir_pt);
                    if (denum != 0)
                    {
                        t = -cls[j] / denum; // VERIFY(t<=1.f && t>=0);
                        dest->last().pt.mad((*src)[j].pt, dir_pt, t);
                        dest->last().uv.mad((*src)[j].uv, dir_uv, t);
                        dest->inc();
                    }
                }
            }
            else
            {
                // J - outside
                if (negative(cls[j + 1]))
                {
                    // J+1  - inside
                    // segment intersects plane
                    dir_pt.sub((*src)[j + 1].pt, (*src)[j].pt);
                    dir_uv.sub((*src)[j + 1].uv, (*src)[j].uv);
                    denum = P.n.dotproduct(dir_pt);
                    if (denum != 0)
                    {
                        t = -cls[j] / denum; // VERIFY(t<=1.f && t>=0);
                        dest->last().pt.mad((*src)[j].pt, dir_pt, t);
                        dest->last().uv.mad((*src)[j].uv, dir_uv, t);
                        dest->inc();
                    }
                }
            }
        }

        // here we end up with complete polygon in 'dest' which is inside plane #i
        if (dest->size() < 3)
            return 0;
    }
    return dest;
}

void UICore::ClientToScreenScaled(Fvector2& dest, float left, float top) const
{
    if (m_currentPointType != IUIRender::pttLIT)
        dest.set(ClientToScreenScaledX(left), ClientToScreenScaledY(top));
    else
        dest.set(left, top);
}

void UICore::ClientToScreenScaled(Fvector2& src_and_dest) const
{
    if (m_currentPointType != IUIRender::pttLIT)
        src_and_dest.set(ClientToScreenScaledX(src_and_dest.x), ClientToScreenScaledY(src_and_dest.y));
}

void UICore::ClientToScreenScaledWidth(float& src_and_dest) const
{
    if (m_currentPointType != IUIRender::pttLIT)
        src_and_dest /= m_current_scale->x;
}

void UICore::ClientToScreenScaledHeight(float& src_and_dest) const
{
    if (m_currentPointType != IUIRender::pttLIT)
        src_and_dest /= m_current_scale->y;
}

void UICore::AlignPixel(float& src_and_dest) const
{
    if (m_currentPointType != IUIRender::pttLIT)
        src_and_dest = (float)iFloor(src_and_dest);
}

void UICore::PushScissor(const Frect& r_tgt, bool overlapped)
{
    if (UI().m_currentPointType == IUIRender::pttLIT)
        return;

    Frect r_top = {0.0f, 0.0f, UI_BASE_WIDTH, UI_BASE_HEIGHT};
    Frect result = r_tgt;
    if (!m_Scissors.empty() && !overlapped)
    {
        r_top = m_Scissors.top();
    }
    if (!result.intersection(r_top, r_tgt))
        result.set(0.0f, 0.0f, 0.0f, 0.0f);

    if (!(result.x1 >= 0 && result.y1 >= 0 && result.x2 <= UI_BASE_WIDTH && result.y2 <= UI_BASE_HEIGHT))
    {
        Msg("! r_tgt [%.3f][%.3f][%.3f][%.3f]", r_tgt.x1, r_tgt.y1, r_tgt.x2, r_tgt.y2);
        Msg("! result [%.3f][%.3f][%.3f][%.3f]", result.x1, result.y1, result.x2, result.y2);
        VERIFY(result.x1 >= 0 && result.y1 >= 0 && result.x2 <= UI_BASE_WIDTH && result.y2 <= UI_BASE_HEIGHT);
    }
    m_Scissors.push(result);

    result.lt.x = ClientToScreenScaledX(result.lt.x);
    result.lt.y = ClientToScreenScaledY(result.lt.y);
    result.rb.x = ClientToScreenScaledX(result.rb.x);
    result.rb.y = ClientToScreenScaledY(result.rb.y);

    Irect r;
    r.x1 = iFloor(result.x1);
    r.x2 = iFloor(result.x2 + 0.5f);
    r.y1 = iFloor(result.y1);
    r.y2 = iFloor(result.y2 + 0.5f);
    GEnv.UIRender->SetScissor(&r);
}

void UICore::PopScissor()
{
    if (UI().m_currentPointType == IUIRender::pttLIT)
        return;

    VERIFY(!m_Scissors.empty());
    m_Scissors.pop();

    if (m_Scissors.empty())
        GEnv.UIRender->SetScissor(NULL);
    else
    {
        const Frect& top = m_Scissors.top();
        Irect tgt;
        tgt.lt.x = iFloor(ClientToScreenScaledX(top.lt.x));
        tgt.lt.y = iFloor(ClientToScreenScaledY(top.lt.y));
        tgt.rb.x = iFloor(ClientToScreenScaledX(top.rb.x));
        tgt.rb.y = iFloor(ClientToScreenScaledY(top.rb.y));

        GEnv.UIRender->SetScissor(&tgt);
    }
}

UICore::UICore()
{
    if (!GEnv.isDedicatedServer)
    {
        m_pUICursor = xr_new<CUICursor>();
        m_pFontManager = xr_new<CFontManager>();
    }
    else
    {
        m_pUICursor = nullptr;
        m_pFontManager = nullptr;
    }
    m_bPostprocess = false;

    // m_ui_offset_ выставит OnDeviceReset через GetUILayoutMetrics; указатель нужен ещё до него.
    m_ui_offset_.set(0.0f, 0.0f);
    m_pp_offset_.set(0.0f, 0.0f);
    m_current_offset = &m_ui_offset_;

    OnDeviceReset();
    OnUIReset();

    m_current_scale = &m_scale_;
    g_current_font_scale.set(1.0f, 1.0f);
    m_currentPointType = IUIRender::pttTL;
}

void UICore::OnDeviceReset()
{
    float ui_w, ui_h, ui_ox, ui_oy;
    GetUILayoutMetrics(ui_w, ui_h, ui_ox, ui_oy);

    m_scale_.set(ui_w / UI_BASE_WIDTH, ui_h / UI_BASE_HEIGHT);
    m_ui_offset_.set(ui_ox, ui_oy);

    // Отсекающий объём оставляем на весь экран: геометрия интерфейса сама уходит в рамку
    // через m_ui_offset_, а широкий объём лишним не бывает.
    m_2DFrustum.CreateFromRect(Frect().set(0.0f, 0.0f, float(Device.dwWidth), float(Device.dwHeight)));
}

void UICore::OnUIReset()
{
    CUIXmlInitBase::DeleteColorDefs();
    CUITextureMaster::FreeTexInfo();

    ReadTextureInfo();
    CUIXmlInitBase::InitColorDefs();
}

UICore::~UICore()
{
    xr_delete(m_pFontManager);
    xr_delete(m_pUICursor);
    CUIXmlInitBase::DeleteColorDefs();
    CUITextureMaster::FreeTexInfo();
}

void UICore::ReadTextureInfo()
{
    string_path buf;
    FS_FileSet files;

    const auto ParseFileSet = [&](pcstr path)
    {
        FS.file_list(files, "$game_config$", FS_ListFiles,
            strconcat(sizeof(buf), buf, path, DELIMITER "textures_descr" DELIMITER "*.xml")
        );
        for (const auto& file : files)
        {
            string_path path, name;
            _splitpath(file.name.c_str(), nullptr, path, name, nullptr);
            xr_strcat(name, ".xml");
            path[xr_strlen(path) - 1] = '\0'; // cut the latest '\\'

            CUITextureMaster::ParseShTexInfo(path, name);
        }
    };

    ParseFileSet(UI_PATH_DEFAULT);

    if (0 != xr_strcmp(UI_PATH, UI_PATH_DEFAULT))
        ParseFileSet(UI_PATH);

    if (pSettings->section_exist("texture_desc"))
    {
        string256 single_item;

        cpcstr itemsList = pSettings->r_string("texture_desc", "files");
        const u32 itemsCount = _GetItemCount(itemsList);

        for (u32 i = 0; i < itemsCount; i++)
        {
            _GetItem(itemsList, i, single_item);
            xr_strcat(single_item, ".xml");
            CUITextureMaster::ParseShTexInfo(single_item);
        }
    }
}

void UICore::pp_start()
{
    m_bPostprocess = true;

    m_pp_scale_.set(float(Device.dwWidth) / float(UI_BASE_WIDTH),
        float(Device.dwHeight) / float(UI_BASE_HEIGHT));
    m_2DFrustumPP.CreateFromRect(Frect().set(0.0f, 0.0f, float(Device.dwWidth),
        float(Device.dwHeight)));

    // Постпроцесс — полноэкранный (затемнение, блюр фона): ему pillarbox и safe zone не положены,
    // поэтому у него свой масштаб и НУЛЕВОЙ сдвиг.
    m_pp_offset_.set(0.0f, 0.0f);
    m_current_scale = &m_pp_scale_;
    m_current_offset = &m_pp_offset_;

    g_current_font_scale.set(float(Device.dwWidth) / float(Device.dwWidth),
        float(Device.dwHeight) / float(Device.dwHeight));
}

void UICore::pp_stop()
{
    m_bPostprocess = false;
    m_current_scale = &m_scale_;
    m_current_offset = &m_ui_offset_;
    g_current_font_scale.set(1.0f, 1.0f);
}

void UICore::RenderFont()
{
    ZoneScoped;
    Font().Render();
}

bool UICore::is_widescreen()
{
    return (Device.dwWidth) / float(Device.dwHeight) > (UI_BASE_WIDTH / UI_BASE_HEIGHT + 0.01f);
}

float UICore::get_current_kx()
{
    // Поправка квадратных элементов (иконки, маркеры карты) считается по ЭФФЕКТИВНОМУ
    // прямоугольнику интерфейса: с pillarbox он 16:9, и kx совпадает с обычным широким экраном.
    float w, h, ox, oy;
    GetUILayoutMetrics(w, h, ox, oy);

    float res = (h / w) / (UI_BASE_HEIGHT / UI_BASE_WIDTH);
    return res;
}

// [DA_PORT] На НЕширокоформатном экране всё равно берём широкоформатную разметку, если она есть.
//
// Так пришлось из-за данных самой Dead Air. Мод рисовался под широкий экран, и его настоящий
// интерфейс лежит только в файлах `_16`; файлы без суффикса остались от основы и с тех пор не
// обновлялись. Сверка пар по НАБОРУ ТЕКСТУР (координаты у вариантов обязаны различаться, а картинки
// описывают сам дизайн) даёт 17 расхождений из 37, и все меню — в их числе: главное меню на 4:3 это
// меню сборки teamEPIC с чужим фоном и половиной кнопок, настройки — стоковая разметка Call of
// Chernobyl без единой строки мода, туда же загрузка, сохранение, выбор группировки и погода.
// Хуже того, у `pda_disabled_16.xml` пары нет вовсе: прежний порядок искал бы несуществующий файл.
//
// Плата за это — координаты, рассчитанные на широкий экран, растянутся по холсту 4:3. Холст у обоих
// вариантов один и тот же (1024x768 виртуальных единиц), поэтому речь именно о пропорциях, а не о
// вылете элементов за экран. Пустое меню и чужой главный экран — цена заметно выше.
//
// Крутилка `ui_widescreen_layout 0` возвращает прежнее поведение: она нужна тому, кто соберёт на
// этом движке мод с честной парой разметок под оба соотношения сторон.
//
// ⚠️ Разрешение 4:3 — это не про редкое железо: список режимов строится из того, что рапортует сам
// монитор, и 4:3 есть у любого. Достаточно понизить разрешение ради кадров.
XRUICORE_API int ps_ui_widescreen_layout = 1;

// [DA_PORT] Pillarbox: на сверхшироком экране (21:9 и шире, порог — в is_ultra_widescreen)
// интерфейс ужимается в центральную область 16:9, а мир по-прежнему занимает весь экран.
//
// Зачем так. Разметки `_21` в поставке Dead Air нет ни одного файла, поэтому без pillarbox
// сверхширокий экран берёт разметку `_16` и растягивает её по всей ширине: КПК, меню, задания и
// инвентарь расползаются и размываются. Рисовать и поддерживать тридцать с лишним файлов `_21`
// — отдельный большой труд; ужатие в 16:9 даёт корректный вид сразу для ВСЕХ экранов ценой
// пустых полос по бокам. Тем же путём в Anomaly идёт DART (подбор 16:9-шаблонов для
// сверхшироких), только у него нет доступа к движку и он двигает каждый элемент по отдельности.
//
// Кому это мешает: тому, кто принесёт готовый набор `_21` (сообщество Monolith их рисует, наш
// суффикс совпадает) — он ставит `ui_pillarbox 0` и получает разметку на всю ширину.
XRUICORE_API int ps_ui_pillarbox = 1;

// [DA_PORT] Безопасная зона интерфейса, процентов с каждой из четырёх сторон (0..20).
// Аналог safe zone из DART/GAMMA: отодвигает HUD и меню от краёв экрана. Полезна на телевизорах
// с оверсканом и тем, кто любит HUD ближе к центру. Ноль — весь экран, как было.
XRUICORE_API int ps_ui_safe_zone = 0;

void UICore::GetUILayoutMetrics(float& w, float& h, float& ox, float& oy)
{
    w = float(Device.dwWidth);
    h = float(Device.dwHeight);
    ox = 0.0f;
    oy = 0.0f;

    if (ps_ui_pillarbox && is_ultra_widescreen())
    {
        const float target_w = h * (16.0f / 9.0f);
        if (target_w < w)
        {
            ox = (w - target_w) * 0.5f;
            w = target_w;
        }
    }

    if (ps_ui_safe_zone > 0)
    {
        const float f = float(ps_ui_safe_zone) / 100.0f;
        ox += w * f;
        oy += h * f;
        w *= 1.0f - 2.0f * f;
        h *= 1.0f - 2.0f * f;
    }
}

void UICore::UpdateLayout()
{
    // Дешёвая проверка за кадр: четыре числа против прошлых. Полный пересчёт — только при
    // изменении (смена разрешения, ползунки меню подгонки).
    static u32 last_w = 0, last_h = 0;
    static int last_pillarbox = -1, last_safe_zone = -1;

    if (last_w == Device.dwWidth && last_h == Device.dwHeight &&
        last_pillarbox == ps_ui_pillarbox && last_safe_zone == ps_ui_safe_zone)
        return;

    last_w = Device.dwWidth;
    last_h = Device.dwHeight;
    last_pillarbox = ps_ui_pillarbox;
    last_safe_zone = ps_ui_safe_zone;

    OnDeviceReset();
    if (m_pUICursor)
        m_pUICursor->OnDeviceReset();
}

bool UICore::is_ultra_widescreen()
{
    return float(Device.dwWidth) / float(Device.dwHeight) > 1.8f;
}

// [DA_PORT] Выбор набора разметки по соотношению сторон: `_21` -> `_16` -> базовый.
//
// Зачем понадобилась третья ступень. Прежний порядок знал только `_16` и базовый, поэтому на
// сверхшироком экране (21:9, 32:9) брался тот же файл, что и на обычном широком, и растягивался
// вдвое сильнее.
//
// ⭐ Имена и порог сверены с ЧУЖИМИ движками намеренно. Monolith (на нём работает Anomaly) и OGSR
// независимо пришли к одному и тому же: суффикс `_21`, порог соотношения 1.8, откат вниз при
// отсутствии файла. Разметку под сверхширокие рисует СООБЩЕСТВО — десятки готовых наборов лежат
// на ModDB и Nexus, — и совпадение имён означает, что чужой готовый файл у нас просто заработает.
// Разойдись мы хоть в суффиксе, хоть в пороге — он не нашёлся бы вовсе.
//
// ⚠️ В поставке самих Monolith и Dead Air файлов `_21` НЕТ НИ ОДНОГО. То есть сегодня эта ступень
// молча уходит в откат и ничего не меняет; она сделана ради совместимости, а не ради вида здесь и
// сейчас.
//
// Особенность Dead Air сохранена: на НЕширокоформатном экране всё равно берётся `_16`, потому что
// базовые файлы мода протухли и остались от основы (разбор — у ps_ui_widescreen_layout).
shared_str UICore::get_xml_name(pcstr path, pcstr fn)
{
    string_path str;

    // Сложить имя с суффиксом и ответить, существует ли такой файл.
    const auto try_suffix = [&](pcstr suffix) -> bool
    {
        if (strext(fn))
        {
            xr_strcpy(str, fn);
            *strext(str) = 0;
            xr_strcat(str, suffix);
            xr_strcat(str, ".xml");
        }
        else
            xr_sprintf(str, "%s%s", fn, suffix);

        string_path probe;
        return !!FS.exist(probe, "$game_config$", path, str);
    };

    bool found = false;

    if (is_ultra_widescreen())
        found = try_suffix("_21");

    if (!found && (is_widescreen() || ps_ui_widescreen_layout))
        found = try_suffix("_16");

    if (!found)
    {
        xr_sprintf(str, "%s", fn);
        if (nullptr == strext(fn))
            xr_strcat(str, ".xml");
    }

    return str;
}
