////////////////////////////////////////////////////////////////////////////
//	Module 		: level_graph.cpp
//	Created 	: 02.10.2001
//  Modified 	: 11.11.2003
//	Author		: Oles Shihkovtsov, Dmitriy Iassenev
//	Description : Level graph
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"
#include "level_graph.h"
#include "xrEngine/profiler.h"

CLevelGraph::CLevelGraph(const char* fileName)
    : m_level_id(GameGraph::_LEVEL_ID(-1))
{
    string256 filePath;
    strconcat(sizeof(filePath), filePath, fileName, LEVEL_GRAPH_NAME);
    Initialize(filePath);
}

CLevelGraph::CLevelGraph()
{
    string_path filePath;
    FS.update_path(filePath, "$level$", LEVEL_GRAPH_NAME);
    Initialize(filePath);
}

void CLevelGraph::Initialize(const char* filePath)
{
    m_reader = FS.r_open(filePath);
    R_ASSERT3(m_reader, "Please, compile AI for the level.", filePath);
    // m_header & data
    m_header = (CHeader*)m_reader->pointer();
    ASSERT_XRAI_VERSION_MATCH(header().version(), "Level graph");
    m_reader->advance(sizeof(CHeader));
    const auto& box = header().box();
    m_nodes = xr_new<CLevelGraphManager>(m_reader, header().vertex_count(), header().version());
    m_row_length = iFloor((box.vMax.z - box.vMin.z) / header().cell_size() + EPS_L + 1.5f);
    m_column_length = iFloor((box.vMax.x - box.vMin.x) / header().cell_size() + EPS_L + 1.5f);
    m_access_mask.assign(header().vertex_count(), true);
    unpack_xz(vertex_position(box.vMax), m_max_x, m_max_z);
}

CLevelGraph::~CLevelGraph()
{
    // [DA_PORT] Менеджер узлов удалялся... нигде. Деструктор закрывал только m_reader, а
    // m_nodes = xr_new<CLevelGraphManager>(...) из Initialize() оставался висеть в куче.
    //
    // Цена ошибки не в самом объекте, он крошечный. В режиме совместимости (а level.ai в DA именно
    // старой версии — XRAI_VERSION_CS_COP и ниже) конструктор менеджера ПЕРЕКЛАДЫВАЕТ узлы в свежий
    // массив: xr_alloc<NodeCompressed>(vertex_count + 1) в convert_nodes(). Освобождает его
    // ~CLevelGraphManager, до которого дело не доходило. На Кордоне это 15 624 200 байт, и граф ИИ
    // пересоздаётся на КАЖДУЮ перезагрузку сохранения (CAI_Space::load -> AISpaceBase::Load).
    //
    // Найдено замером: живые аллокации росли на 16-17 МБ за перезагрузку, ровно по одному блоку
    // в ведре 8-16 МБ, и ловушка аллокатора на точный размер назвала эту строку по стеку.
    //
    // Баг самого OpenXRay: наших правок в этом файле нет.
    xr_delete(m_nodes);
    FS.r_close(m_reader);
}
// [DA_PORT] Поиск ближайшей вершины столбцами вместо перебора всего графа.
//
// Сам движок называл прежний вариант "performing very slow full search": он считал расстояние до
// КАЖДОЙ из 1.48 млн вершин. Замер: 11.5 мс на вызов. Путей сюда три, и один из них игровой --
// сталкер, заходящий в укрытие (stalker_movement_manager_smart_cover); остальные два дают 172 мс на
// загрузку уровня (переходы между локациями).
//
// Вершины лежат отсортированными по упакованной координате xz = x * row_length + z, то есть массив
// разбит на столбцы сетки по x, и границы столбца находятся двоичным поиском. Идём от столбца
// искомой точки наружу и останавливаемся, когда следующий столбец заведомо дальше уже найденного:
// любая вершина столбца x отстоит по оси x не меньше чем на (|dx| - 1) * cell_size.
//
// Результат обязан совпадать с прежним. Ручка da_vertex_search_verify включает сверку: оба поиска
// считаются и расхождение печатается в лог.
XRAICORE_API int ps_da_vertex_search_verify = 0;

u32 CLevelGraph::da_vertex_slow(const Fvector& position) const
{
    float min_dist = flt_max;
    u32 selected;
    set_invalid_vertex(selected);
    for (u32 i = 0; i < header().vertex_count(); ++i)
    {
        float dist = distance(i, position);
        if (dist < min_dist)
        {
            min_dist = dist;
            selected = i;
        }
    }
    return (selected);
}

u32 CLevelGraph::vertex(const Fvector& position) const
{
    const u32 count = header().vertex_count();
    if (!count)
    {
        u32 selected;
        set_invalid_vertex(selected);
        return (selected);
    }

    const float cell = header().cell_size();
    const CLevelVertex* const B = m_nodes->begin();
    const CLevelVertex* const E = m_nodes->end();

    // Столбец искомой точки. Значение может выйти за пределы графа -- это нормально, поиск всё равно
    // пойдёт наружу и первым же шагом упрётся в существующие столбцы.
    const int target_x = iFloor((position.x - header().box().vMin.x) / cell);
    const int max_x = int(m_max_x);

    float min_dist = flt_max;
    u32 selected;
    set_invalid_vertex(selected);

    for (int radius = 0;; ++radius)
    {
        // Столбцы на удалении radius не могут дать выигрыш, если предыдущее кольцо уже ближе.
        if (valid_vertex_id(selected))
        {
            const float lower_bound_dist = float(radius - 1) * cell;
            if (lower_bound_dist > 0.f && lower_bound_dist * lower_bound_dist > min_dist)
                break;
        }

        bool any_column_in_range = false;

        for (int side = 0; side < 2; ++side)
        {
            if (radius == 0 && side == 1)
                continue;

            const int x = (side == 0) ? (target_x - radius) : (target_x + radius);
            if (x < 0 || x > max_x)
                continue;

            any_column_in_range = true;

            const u32 lo = u32(x) * m_row_length;
            const u32 hi = lo + m_row_length;

            const CLevelVertex* I = std::lower_bound(B, E, lo, CLevelGraph::vertex::predicate2);
            for (; I != E; ++I)
            {
                if (I->position().xz() >= hi)
                    break;

                const u32 vertex_id = u32(I - B);
                const float dist = distance(vertex_id, position);
                if (dist < min_dist)
                {
                    min_dist = dist;
                    selected = vertex_id;
                }
            }
        }

        // Оба направления вышли за пределы графа -- дальше искать негде.
        if (!any_column_in_range && radius > max_x)
            break;
    }

    if (ps_da_vertex_search_verify)
    {
        const u32 slow = da_vertex_slow(position);
        if (slow != selected)
            Msg("! [DA_PORT] поиск вершины разошёлся: быстрый [%u], полный [%u], точка [%f][%f][%f]",
                selected, slow, VPUSH(position));
    }

    VERIFY(valid_vertex_id(selected));
    return (selected);
}

u32 CLevelGraph::VertexInternal(u32 current_node_id, const Fvector& position) const
{
    u32 id;
    if (valid_vertex_position(position))
    {
        // so, our position is inside the level graph bounding box
        if (valid_vertex_id(current_node_id) && inside(vertex(current_node_id), position))
        {
            // so, our node corresponds to the position
            return (current_node_id);
        }

        // so, our node doesn't correspond to the position
        // try to search it with O(logN) time algorithm
        u32 _vertex_id = vertex_id(position);
        if (valid_vertex_id(_vertex_id))
        {
            // so, there is a node which corresponds with x and z to the position
            bool ok = true;
            if (valid_vertex_id(current_node_id))
            {
                {
                    CLevelVertex const& vertex = *this->vertex(current_node_id);
                    for (u32 i = 0; i < 4; ++i)
                    {
                        if (vertex.link(i) == _vertex_id)
                        {
                            return (_vertex_id);
                        }
                    }
                }
                {
                    CLevelVertex const& vertex = *this->vertex(_vertex_id);
                    for (u32 i = 0; i < 4; ++i)
                    {
                        if (vertex.link(i) == current_node_id)
                        {
                            return (_vertex_id);
                        }
                    }
                }

                float y0 = vertex_plane_y(current_node_id, position.x, position.z);
                float y1 = vertex_plane_y(_vertex_id, position.x, position.z);
                bool over0 = position.y > y0;
                bool over1 = position.y > y1;
                float y_dist0 = position.y - y0;
                float y_dist1 = position.y - y1;
                if (over0)
                {
                    if (over1)
                    {
                        if (y_dist1 - y_dist0 > 1.f)
                            ok = false;
                        else
                            ok = true;
                    }
                    else
                    {
                        if (y_dist0 - y_dist1 > 1.f)
                            ok = false;
                        else
                            ok = true;
                    }
                }
                else
                {
                    ok = true;
                }
            }
            if (ok)
            {
                return (_vertex_id);
            }
        }
    }

    if (!valid_vertex_id(current_node_id))
    {
        // so, we do not have a correct current node
        // performing very slow full search
        id = vertex(position);
        VERIFY(valid_vertex_id(id));
        return (id);
    }

    u32 new_vertex_id = guess_vertex_id(current_node_id, position);
    if (new_vertex_id != current_node_id)
        return (new_vertex_id);

    // so, our position is outside the level graph bounding box
    // or
    // there is no node for the current position
    // try to search the nearest one iteratively
    SContour _contour;
    Fvector point;
    u32 best_vertex_id = current_node_id;
    contour(_contour, current_node_id);
    nearest(point, position, _contour);
    float best_distance_sqr = position.distance_to_sqr(point);
    const_iterator i, e;
    begin(current_node_id, i, e);
    for (; i != e; ++i)
    {
        u32 level_vertex_id = value(current_node_id, i);
        if (!valid_vertex_id(level_vertex_id))
            continue;

        contour(_contour, level_vertex_id);
        nearest(point, position, _contour);
        float distance_sqr = position.distance_to_sqr(point);
        if (best_distance_sqr > distance_sqr)
        {
            best_distance_sqr = distance_sqr;
            best_vertex_id = level_vertex_id;
        }
    }
    return (best_vertex_id);
}

u32 CLevelGraph::vertex(u32 current_node_id, const Fvector& position) const
{
    START_PROFILE("Level_Graph::find vertex")
    NodeTime.Begin();
    u32 result = VertexInternal(current_node_id, position);
    NodeTime.End();
    return result;
    STOP_PROFILE
}

u32 CLevelGraph::vertex_id(const Fvector& position) const
{
    VERIFY2(valid_vertex_position(position),
        make_string("invalid position for CLevelGraph::vertex_id specified: [%f][%f][%f]", VPUSH(position)));

    CPosition _vertex_position = vertex_position(position);
    CLevelVertex* B = m_nodes->begin();
    CLevelVertex* E = m_nodes->end();
    CLevelVertex* I = std::lower_bound(B, E, _vertex_position.xz());
    if ((I == E) || ((*I).position().xz() != _vertex_position.xz()))
        return (u32(-1));

    u32 best_vertex_id = u32(I - B);
    float y = vertex_plane_y(best_vertex_id, position.x, position.z);
    for (++I; I != E; ++I)
    {
        if ((*I).position().xz() != _vertex_position.xz())
            break;

        u32 new_vertex_id = u32(I - B);
        float _y = vertex_plane_y(new_vertex_id, position.x, position.z);
        if (y <= position.y)
        {
            // so, current node is under the specified position
            if (_y <= position.y)
            {
                // so, new node is under the specified position
                if (position.y - _y < position.y - y)
                {
                    // so, new node is closer to the specified position
                    y = _y;
                    best_vertex_id = new_vertex_id;
                }
            }
        }
        else
            // so, current node is over the specified position
            if (_y <= position.y)
        {
            // so, new node is under the specified position
            y = _y;
            best_vertex_id = new_vertex_id;
        }
        else
            // so, new node is over the specified position
            if (_y - position.y < y - position.y)
        {
            // so, new node is closer to the specified position
            y = _y;
            best_vertex_id = new_vertex_id;
        }
    }

    return (best_vertex_id);
}

static const int max_guess_vertex_count = 4;

u32 CLevelGraph::guess_vertex_id(u32 const& current_vertex_id, Fvector const& position) const
{
    VERIFY(valid_vertex_id(current_vertex_id));

    CPosition vertex_position;
    if (valid_vertex_position(position))
        vertex_position = this->vertex_position(position);
    else
        vertex_position = vertex(current_vertex_id)->position();

    u32 x, z;
    unpack_xz(vertex_position, x, z);

    SContour vertex_contour;
    contour(vertex_contour, current_vertex_id);
    Fvector best_point;
    float result_distance = nearest(best_point, position, vertex_contour);
    u32 result_vertex_id = current_vertex_id;

    CLevelVertex const* B = m_nodes->begin();
    CLevelVertex const* E = m_nodes->end();
    u32 start_x = (u32)std::max(0, int(x) - max_guess_vertex_count);
    u32 stop_x = std::min(max_x(), x + (u32)max_guess_vertex_count);
    u32 start_z = (u32)std::max(0, int(z) - max_guess_vertex_count);
    u32 stop_z = std::min(max_z(), z + (u32)max_guess_vertex_count);
    for (u32 i = start_x; i <= stop_x; ++i)
    {
        for (u32 j = start_z; j <= stop_z; ++j)
        {
            u32 test_xz = i * m_row_length + j;
            CLevelVertex const* I = std::lower_bound(B, E, test_xz);
            if (I == E)
                continue;

            if ((*I).position().xz() != test_xz)
                continue;

            u32 best_vertex_id = u32(I - B);
            contour(vertex_contour, best_vertex_id);
            float best_distance = nearest(best_point, position, vertex_contour);
            for (++I; I != E; ++I)
            {
                if ((*I).position().xz() != test_xz)
                    break;

                u32 vertex_id = u32(I - B);
                Fvector point;
                contour(vertex_contour, vertex_id);
                float distance = nearest(point, position, vertex_contour);
                if (distance >= best_distance)
                    continue;

                best_point = point;
                best_distance = distance;
                best_vertex_id = vertex_id;
            }

            if (_abs(best_point.y - position.y) >= 3.f)
                continue;

            if (result_distance <= best_distance)
                continue;

            result_distance = best_distance;
            result_vertex_id = best_vertex_id;
        }
    }

    return (result_vertex_id);
}
