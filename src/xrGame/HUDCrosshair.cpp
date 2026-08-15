// HUDCrosshair.cpp:  крестик прицела, отображающий текущую дисперсию
//
//////////////////////////////////////////////////////////////////////

#include "StdAfx.h"

#include "HUDCrosshair.h"
#include "xrUICore/ui_base.h"


// [DA_PORT] НАСТРАИВАЕМОЕ ПЕРЕКРЕСТИЕ: вид, толщина, длина, зазор, цвет, контур, точка.
//
// Почему это дёшево. Перекрестие в X-Ray и так рисуется процедурно — отрезками в экранных
// координатах, — поэтому новые виды это геометрия, а не текстуры: ни одного нового файла в
// gamedata, и любой вид доступен при любом разрешении.
//
// ⚠️ Рисуем ПРЯМОУГОЛЬНИКАМИ из треугольников, а не списком линий. У линий в DX11 толщина всегда
// один пиксель, и ползунок толщины на них не подействовал бы вовсе — молча, без ошибки.
//
// ⚠️ Геометрия сперва собирается в буфер и только потом отдаётся: StartPrimitive резервирует ровно
// заявленное число вершин, а PushPoint сверх резерва прикрыт лишь VERIFY, которого в релизе нет.
// Считать вершины «на глазок» здесь значит однажды писать за границу буфера.
// Значения по умолчанию — РОВНО КАК В DEAD AIR, а не «как нам нравится». Взяты из стока:
//   вид         — классический крест из четырёх штрихов, он и рисовался всегда;
//   длина 2 пикс — cross_length = 0.001 от ширины экрана (system.ltx), это ~1.9 пикс на 1920;
//   зазор 0      — min_radius = 0.0 там же, раствор давал только разброс оружия;
//   цвет 178     — cross_color = 0.7,0.7,0.7,0.7, то есть 0.7 * 255;
//   контур и точка выключены — в стоке их не было вовсе.
// Игрок, ничего не трогавший, должен видеть привычную марку, а не нашу.
int ps_da_cross_style = 0;
int ps_da_cross_thick = 1;
int ps_da_cross_len = 2;
int ps_da_cross_gap = 0;
int ps_da_cross_dot = 0;
int ps_da_cross_outline = 0;
int ps_da_cross_dynamic = 1;
int ps_da_cross_r = 178, ps_da_cross_g = 178, ps_da_cross_b = 178, ps_da_cross_a = 178;

// [DA_PORT] Предпросмотр: рисовать марку ПОКА ОТКРЫТО окно подгонки.
//
// Без этого настройка вслепую: при открытом диалоге перекрестие не рисуется, и правки видны только
// после закрытия — то есть ровно то, ради чего окно и делалось, не работает. Флаг взводит и снимает
// сам скрипт окна (ui_da_tune.script), поэтому в обычной игре он всегда ноль.
int ps_da_cross_preview = 0;

// [DA_PORT] Подсветка по отношению того, кто под прицелом. 1 — включена.
// Заполняется в CHUDTarget::Render: 0 никого, 1 враг, 2 нейтрал, 3 союзник.
int ps_da_cross_relation = 1;
int g_da_cross_target = 0;

namespace
{
struct da_pt
{
    float x, y;
};
// Пары точек: отрезок = две подряд идущие вершины.
xr_vector<da_pt> g_da_cross_geom;

// ⛔ РИСУЕМ ЛИНИЯМИ, а не прямоугольниками из треугольников — и это не выбор стиля.
//
// Шейдер перекрестия создаётся как `hud\crosshair` БЕЗ текстуры и написан под список линий, тогда
// как работающая рядом точка прицела берёт `hud\cursor` вместе с `ui\cursor`, то есть текстурный.
// Смена примитива на треугольники при том же шейдере просто отбрасывает рисование — без ошибки, без
// строки в логе, без единого признака. Ровно тот класс молчаливого отказа, на котором мы уже теряли
// время с разметкой вершин.
//
// Толщина при этом сохраняется: кладём `thick` параллельных линий со сдвигом по нормали. Способ
// грубее, зато рисует тем путём, который в этом движке заведомо работает.
void da_bar(float x0, float y0, float x1, float y1, float thick)
{
    float dx = x1 - x0, dy = y1 - y0;
    const float len = _sqrt(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    dx /= len;
    dy /= len;
    const float nx = -dy, ny = dx;

    const int n = clampr(int(thick + 0.5f), 1, 16);
    const float base = -(float(n) - 1.f) * 0.5f;
    for (int i = 0; i < n; ++i)
    {
        const float o = base + float(i);
        g_da_cross_geom.push_back({ x0 + nx * o, y0 + ny * o });
        g_da_cross_geom.push_back({ x1 + nx * o, y1 + ny * o });
    }
}

// Заливная точка. Рисуется строками поперёк круга: у нас в распоряжении только линии (шейдер
// перекрестия текстуры не знает и написан под них), а заливка строками даёт ровный круглый диск
// без единого треугольника. Шаг в полпикселя, иначе на краю видны просветы.
void da_disc(float cx, float cy, float r)
{
    if (r < 0.5f)
        r = 0.5f;
    const int n = clampr(int(r * 4.f), 4, 96);
    for (int i = 0; i <= n; ++i)
    {
        const float y = -r + 2.f * r * float(i) / float(n);
        const float half = _sqrt(_max(0.f, r * r - y * y));
        if (half < 0.25f)
            continue;
        g_da_cross_geom.push_back({ cx - half, cy + y });
        g_da_cross_geom.push_back({ cx + half, cy + y });
    }
}

// Кольцо из отрезков. Сегментов берём по радиусу: у мелкого круга частить незачем.
void da_ring(float cx, float cy, float r, float thick, float from = 0.f, float to = PI_MUL_2)
{
    const int seg = clampr(int(r * 0.6f), 12, 48);
    const float step = (to - from) / float(seg);
    for (int i = 0; i < seg; ++i)
    {
        const float a0 = from + step * float(i), a1 = a0 + step;
        da_bar(cx + r * _cos(a0), cy + r * _sin(a0), cx + r * _cos(a1), cy + r * _sin(a1), thick);
    }
}
} // namespace

int da_cross_style_count() { return 24; }

CHUDCrosshair::CHUDCrosshair()
{
    hShader->create("hud" DELIMITER "crosshair");
    radius = 0;
}

CHUDCrosshair::~CHUDCrosshair() {}
void CHUDCrosshair::Load()
{
    //все размеры в процентах от длины экрана
    //длина крестика
    cross_length_perc = pSettings->r_float(HUD_CURSOR_SECTION, "cross_length");
    min_radius_perc = pSettings->r_float(HUD_CURSOR_SECTION, "min_radius");
    max_radius_perc = pSettings->r_float(HUD_CURSOR_SECTION, "max_radius");
    cross_color = pSettings->r_fcolor(HUD_CURSOR_SECTION, "cross_color").get();
}

//выставляет radius от min_radius до max_radius
void CHUDCrosshair::SetDispersion(float disp)
{
    Fvector4 r;
    Fvector R = {VIEWPORT_NEAR * _sin(disp), 0.f, VIEWPORT_NEAR};
    Device.mProject.transform(r, R);

    Fvector2 scr_size{ float(Device.dwWidth), float(Device.dwHeight) };
    float radius_pixels = _abs(r.x) * scr_size.x / 2.0f;
    target_radius = radius_pixels;
}

#ifdef DEBUG
void CHUDCrosshair::SetFirstBulletDispertion(float fbdisp)
{
    Fvector4 r;
    Fvector R = {VIEWPORT_NEAR * _sin(fbdisp), 0.f, VIEWPORT_NEAR};
    Device.mProject.transform(r, R);

    Fvector2 scr_size{ float(Device.dwWidth), float(Device.dwHeight) };
    fb_radius = _abs(r.x) * scr_size.x / 2.0f;
}

BOOL g_bDrawFirstBulletCrosshair = FALSE;

void CHUDCrosshair::OnRenderFirstBulletDispertion()
{
    VERIFY(g_bRendering);

    Fvector2 scr_size{ float(Device.dwWidth), float(Device.dwHeight) };
    Fvector2 center{ scr_size.x / 2.0f, scr_size.y / 2.0f };

    GEnv.UIRender->StartPrimitive(10, IUIRender::ptLineList, UI().m_currentPointType);

    u32 fb_cross_color = color_rgba(255, 0, 0, 255); // red

    float cross_length = /*cross_length_perc*/ 0.008f * scr_size.x;
    float min_radius = min_radius_perc * scr_size.x;
    float max_radius = max_radius_perc * scr_size.x;

    clamp(target_radius, min_radius, max_radius);

    float x_min = min_radius + fb_radius;
    float x_max = x_min + cross_length;

    float y_min = x_min;
    float y_max = x_max;

    // 0
    GEnv.UIRender->PushPoint(center.x, center.y + y_min, 0, fb_cross_color, 0, 0);
    GEnv.UIRender->PushPoint(center.x, center.y + y_max, 0, fb_cross_color, 0, 0);
    // 1
    GEnv.UIRender->PushPoint(center.x, center.y - y_min, 0, fb_cross_color, 0, 0);
    GEnv.UIRender->PushPoint(center.x, center.y - y_max, 0, fb_cross_color, 0, 0);
    // 2
    GEnv.UIRender->PushPoint(center.x + x_min, center.y, 0, fb_cross_color, 0, 0);
    GEnv.UIRender->PushPoint(center.x + x_max, center.y, 0, fb_cross_color, 0, 0);
    // 3
    GEnv.UIRender->PushPoint(center.x - x_min, center.y, 0, fb_cross_color, 0, 0);
    GEnv.UIRender->PushPoint(center.x - x_max, center.y, 0, fb_cross_color, 0, 0);

    // point
    GEnv.UIRender->PushPoint(center.x - 0.5f, center.y, 0, fb_cross_color, 0, 0);
    GEnv.UIRender->PushPoint(center.x + 0.5f, center.y, 0, fb_cross_color, 0, 0);

    // render
    GEnv.UIRender->SetShader(*hShader);
    GEnv.UIRender->FlushPrimitive();
}
#endif

extern ENGINE_API bool g_bRendering;
void CHUDCrosshair::OnRender()
{
    VERIFY(g_bRendering);

    if (ps_da_cross_style < 0)
        return; // отрицательный вид — прицел скрыт целиком

    Fvector2 scr_size{ float(Device.dwWidth), float(Device.dwHeight) };
    const float cx = scr_size.x / 2.0f, cy = scr_size.y / 2.0f;

    // ⛔ Длина и зазор — В ПИКСЕЛЯХ, а не в процентах от заводских величин. Это не вкусовщина:
    // в system.ltx стоит cross_length = 0.001 (около двух пикселей на 1920) и min_radius = 0.0.
    // Проценты от нуля дают ноль при любом положении ползунка — зазор не двигался вовсе, а длина
    // при 400% давала семь пикселей. Так это и настраивают в играх: абсолютными пикселями.
    const float len_px = float(clampr(ps_da_cross_len, 1, 200));
    const float gap_px = float(clampr(ps_da_cross_gap, 0, 200));
    const float thick = float(clampr(ps_da_cross_thick, 1, 12));

    const float min_radius = min_radius_perc * scr_size.x;
    const float max_radius = max_radius_perc * scr_size.x;
    clamp(target_radius, min_radius, max_radius);

    // Расхождение по разбросу оружия прибавляется К зазору: марка «дышит» вокруг заданного
    // игроком раствора, а не подменяет его. Ползунком выключается — часть игроков предпочитает
    // неподвижную марку.
    const float dyn = ps_da_cross_dynamic ? radius : 0.f;
    const float g = gap_px + dyn;               // зазор от центра
    const float e = g + len_px;                 // внешний край штриха
    const float cross_length = len_px;          // для видов, считающих от длины штриха

    const int st = ps_da_cross_style;

    // Геометрия собирается ЗАНОВО для каждого прохода: контур это та же фигура, но толще, и
    // строить её сдвигом готовых вершин у линий нельзя — сдвиг разъехался бы по направлению.
    auto build = [&](float thick)
    {
        g_da_cross_geom.clear();

    auto arms = [&](bool up, bool dn, bool lf, bool rt)
    {
        if (up) da_bar(cx, cy - g, cx, cy - e, thick);
        if (dn) da_bar(cx, cy + g, cx, cy + e, thick);
        if (lf) da_bar(cx - g, cy, cx - e, cy, thick);
        if (rt) da_bar(cx + g, cy, cx + e, cy, thick);
    };
    auto diag = [&]()
    {
        const float k = 0.7071f;
        da_bar(cx - g * k, cy - g * k, cx - e * k, cy - e * k, thick);
        da_bar(cx + g * k, cy + g * k, cx + e * k, cy + e * k, thick);
        da_bar(cx + g * k, cy - g * k, cx + e * k, cy - e * k, thick);
        da_bar(cx - g * k, cy + g * k, cx - e * k, cy + e * k, thick);
    };
    auto corners = [&](float sz, bool inward)
    {
        const float d = inward ? -1.f : 1.f;
        for (int i = 0; i < 4; ++i)
        {
            const float sx = (i & 1) ? 1.f : -1.f, sy = (i & 2) ? 1.f : -1.f;
            da_bar(cx + sx * e, cy + sy * e, cx + sx * e - d * sx * sz, cy + sy * e, thick);
            da_bar(cx + sx * e, cy + sy * e, cx + sx * e, cy + sy * e - d * sy * sz, thick);
        }
    };
    auto box = [&](float r)
    {
        da_bar(cx - r, cy - r, cx + r, cy - r, thick);
        da_bar(cx - r, cy + r, cx + r, cy + r, thick);
        da_bar(cx - r, cy - r, cx - r, cy + r, thick);
        da_bar(cx + r, cy - r, cx + r, cy + r, thick);
    };
    auto diamond = [&](float r)
    {
        da_bar(cx, cy - r, cx + r, cy, thick);
        da_bar(cx + r, cy, cx, cy + r, thick);
        da_bar(cx, cy + r, cx - r, cy, thick);
        da_bar(cx - r, cy, cx, cy - r, thick);
    };

    switch (st)
    {
    case 0: arms(true, true, true, true); break;   // классический крест
    case 1: arms(true, true, true, true); break;   // крест с точкой (точка ниже по коду)
    case 2: break;                                 // только точка
    case 3: arms(false, true, true, true); break;  // без верхнего штриха
    case 4: arms(true, false, true, true); break;  // без нижнего
    case 5: arms(false, false, true, true); break; // только горизонталь
    case 6: arms(true, true, false, false); break; // только вертикаль
    case 7: da_ring(cx, cy, e, thick); break;      // окружность
    case 8: da_ring(cx, cy, e, thick); arms(true, true, true, true); break;
    case 9: da_ring(cx, cy, e, thick); arms(false, true, true, true); break;
    case 10: box(e); break;                        // квадрат
    case 11: box(e); arms(true, true, true, true); break;
    case 12: diamond(e); break;                    // ромб
    case 13: corners(cross_length * 0.5f, true); break;  // уголки внутрь
    case 14: corners(cross_length * 0.5f, false); break; // уголки наружу
    case 15:                                       // скобки
        da_bar(cx - e, cy - g, cx - e, cy + g, thick);
        da_bar(cx + e, cy - g, cx + e, cy + g, thick);
        break;
    case 16: diag(); break;                        // X
    case 17: diag(); arms(true, true, true, true); break; // звезда
    case 18:                                       // шеврон вверх
        da_bar(cx - e, cy + g, cx, cy - g, thick);
        da_bar(cx, cy - g, cx + e, cy + g, thick);
        break;
    case 19:                                       // шеврон вниз
        da_bar(cx - e, cy - g, cx, cy + g, thick);
        da_bar(cx, cy + g, cx + e, cy - g, thick);
        break;
    case 20: arms(true, true, true, true); da_ring(cx, cy, e * 1.6f, thick); break;
    case 21:                                       // двойной крест
        arms(true, true, true, true);
        da_bar(cx, cy - e * 1.4f, cx, cy - e * 1.9f, thick);
        da_bar(cx, cy + e * 1.4f, cx, cy + e * 1.9f, thick);
        da_bar(cx - e * 1.4f, cy, cx - e * 1.9f, cy, thick);
        da_bar(cx + e * 1.4f, cy, cx + e * 1.9f, cy, thick);
        break;
    case 22:                                       // мил-дот: стойка с делениями
        da_bar(cx, cy + g, cx, cy + e * 2.0f, thick);
        for (int i = 1; i <= 3; ++i)
        {
            const float y = cy + g + (e * 2.0f - g) * float(i) / 4.f;
            da_bar(cx - cross_length * 0.25f, y, cx + cross_length * 0.25f, y, thick);
        }
        arms(false, false, true, true);
        break;
    case 23:                                       // две дуги
        da_ring(cx, cy, e, thick, -PI_DIV_2 - PI_DIV_4, -PI_DIV_2 + PI_DIV_4);
        da_ring(cx, cy, e, thick, PI_DIV_2 - PI_DIV_4, PI_DIV_2 + PI_DIV_4);
        break;
    default: arms(true, true, true, true); break;
    }

    // Точка в центре: у видов 1 и 2 она часть самого вида, у остальных — по ползунку.
    // Размер берём от толщины: отдельная ручка ради одной величины только загромоздила бы список,
    // а «толще» для точки естественно читается как «крупнее». Контурный проход приходит сюда с
    // толщиной на две больше и потому обводит точку сам, отдельного кода не нужно.
    if (ps_da_cross_dot || st == 1 || st == 2)
        da_disc(cx, cy, _max(1.f, thick) * 0.9f);
    }; // build


    const int alpha = clampr(ps_da_cross_a, 0, 255);
    u32 col = color_rgba(clampr(ps_da_cross_r, 0, 255), clampr(ps_da_cross_g, 0, 255),
        clampr(ps_da_cross_b, 0, 255), alpha);

    // Подсветка перебивает цвет только пока под прицелом кто-то есть; прозрачность остаётся
    // авторской, иначе подобранная видимость марки менялась бы вместе с цветом.
    if (ps_da_cross_relation && g_da_cross_target)
    {
        switch (g_da_cross_target)
        {
        case 1: col = color_rgba(255, 48, 48, alpha); break;   // враг
        case 2: col = color_rgba(255, 224, 64, alpha); break;  // нейтрал
        case 3: col = color_rgba(64, 255, 64, alpha); break;   // союзник
        default: break;
        }
    }

    // Контур рисуем ПЕРВЫМ и раздвинутым наружу: без него светлая марка теряется на светлом фоне —
    // это первое, на что жалуются в любой игре с настраиваемым прицелом.
    auto emit = [&](u32 c)
    {
        const size_t n = g_da_cross_geom.size();
        if (!n)
            return;
        GEnv.UIRender->StartPrimitive(u32(n), IUIRender::ptLineList, UI().m_currentPointType);
        for (size_t i = 0; i < n; ++i)
            GEnv.UIRender->PushPoint(g_da_cross_geom[i].x, g_da_cross_geom[i].y, 0, c, 0, 0);
        GEnv.UIRender->SetShader(*hShader);
        GEnv.UIRender->FlushPrimitive();
    };

    // Контур первым и толще на две линии: без него светлая марка теряется на светлом фоне.
    if (ps_da_cross_outline)
    {
        build(thick + 2.f);
        emit(color_rgba(0, 0, 0, clampr(ps_da_cross_a, 0, 255)));
    }
    build(thick);
    emit(col);

    if (!fsimilar(target_radius, radius))
        radius = target_radius;
#ifdef DEBUG
    if (g_bDrawFirstBulletCrosshair)
        OnRenderFirstBulletDispertion();
#endif
}
