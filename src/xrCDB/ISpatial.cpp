#include "stdafx.h"

#include "ISpatial.h"

#include "xrCore/_fbox.h"
#include "xrCore/Threading/Lock.hpp"
#include "xrCore/Threading/ScopeLock.hpp"

#include "xrEngine/Engine.h"

#ifdef DEBUG
#include "xrEngine/xr_object.h"
#include "xrEngine/PS_instance.h"
#endif

Fvector c_spatial_offset[8] = {
    {-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}};

//////////////////////////////////////////////////////////////////////////
SpatialBase::SpatialBase(ISpatial_DB& space)
{
    spatial.sphere.P.set(0, 0, 0);
    spatial.sphere.R = 0;
    spatial.node_center.set(0, 0, 0);
    spatial.node_radius = 0;
    spatial.node_ptr = NULL;
    spatial.sector_id = IRender_Sector::INVALID_SECTOR_ID;
    spatial.space = &space;
}
SpatialBase::~SpatialBase(void) { spatial_unregister(); }
bool SpatialBase::spatial_inside()
{
    float dr = -(-spatial.node_radius + spatial.sphere.R);
    if (spatial.sphere.P.x < spatial.node_center.x - dr)
        return FALSE;
    if (spatial.sphere.P.x > spatial.node_center.x + dr)
        return FALSE;
    if (spatial.sphere.P.y < spatial.node_center.y - dr)
        return FALSE;
    if (spatial.sphere.P.y > spatial.node_center.y + dr)
        return FALSE;
    if (spatial.sphere.P.z < spatial.node_center.z - dr)
        return FALSE;
    if (spatial.sphere.P.z > spatial.node_center.z + dr)
        return FALSE;
    return true;
}

bool verify_sp(ISpatial* sp, Fvector& node_center, float node_radius)
{
    float dr = -(-node_radius + sp->GetSpatialData().sphere.R);
    if (sp->GetSpatialData().sphere.P.x < node_center.x - dr)
        return FALSE;
    if (sp->GetSpatialData().sphere.P.x > node_center.x + dr)
        return FALSE;
    if (sp->GetSpatialData().sphere.P.y < node_center.y - dr)
        return FALSE;
    if (sp->GetSpatialData().sphere.P.y > node_center.y + dr)
        return FALSE;
    if (sp->GetSpatialData().sphere.P.z < node_center.z - dr)
        return FALSE;
    if (sp->GetSpatialData().sphere.P.z > node_center.z + dr)
        return FALSE;
    return TRUE;
}

void SpatialBase::spatial_register()
{
    spatial.type |= STYPEFLAG_INVALIDSECTOR;
    if (spatial.node_ptr)
    {
        // already registered - nothing to do
    }
    else
    {
        // register
        R_ASSERT(spatial.space);
        spatial.space->insert(this);
        spatial.sector_id = IRender_Sector::INVALID_SECTOR_ID;
    }
}

void SpatialBase::spatial_unregister()
{
    if (spatial.node_ptr)
    {
        // remove
        spatial.space->remove(this);
        spatial.node_ptr = NULL;
        spatial.sector_id = IRender_Sector::INVALID_SECTOR_ID;
    }
    else
    {
        // already unregistered
    }
}

void SpatialBase::spatial_move()
{
    ZoneScoped;
    if (spatial.node_ptr)
    {
        //*** somehow it was determined that object has been moved
        spatial.type |= STYPEFLAG_INVALIDSECTOR;

        //*** check if we are supposed to correct it's spatial location
        if (spatial_inside())
            return; // ???
        spatial.space->remove(this);
        spatial.space->insert(this);
    }
    else
    {
        //*** we are not registered yet, or already unregistered
        //*** ignore request
    }
}

void SpatialBase::spatial_updatesector_internal(IRender_Sector::sector_id_t sector_id)
{
    ZoneScoped;
    spatial.type &= ~STYPEFLAG_INVALIDSECTOR;
    if (sector_id != IRender_Sector::INVALID_SECTOR_ID)
        spatial.sector_id = sector_id;
}

//////////////////////////////////////////////////////////////////////////
void ISpatial_NODE::_init(ISpatial_NODE* _parent)
{
    parent = _parent;
    children[0] = children[1] = children[2] = children[3] = children[4] = children[5] = children[6] = children[7] =
        NULL;
    items.clear();
}

void ISpatial_NODE::_insert(ISpatial* S)
{
    S->GetSpatialData().node_ptr = this;
    items.push_back(S);
    S->GetSpatialData().space->Stats.ObjectCount++;
}

void ISpatial_NODE::_remove(ISpatial* S)
{
    S->GetSpatialData().node_ptr = NULL;
    xr_vector<ISpatial*>::iterator it = std::find(items.begin(), items.end(), S);
    // [DA_PORT] Проверка вместо VERIFY: он исчезает в релизе, а erase(end()) — неопределённое
    // поведение, оно портит контейнер и рушится потом в стороне. Пропуск удаления безвреден:
    // элемента и так нет.
    if (it == items.end())
        return;

    items.erase(it);
    S->GetSpatialData().space->Stats.ObjectCount--;
}

//////////////////////////////////////////////////////////////////////////

ISpatial_DB::ISpatial_DB(pcstr name)
#ifdef CONFIG_PROFILE_LOCKS
    : cs(MUTEX_PROFILE_ID(ISpatial_DB)),
#endif // CONFIG_PROFILE_LOCKS

{
    xr_strcpy(Name, name);
}

ISpatial_DB::~ISpatial_DB()
{
    if (m_root)
    {
        _node_destroy(m_root);
    }

    while (!allocator_pool.empty())
    {
        allocator.destroy(allocator_pool.back());
        allocator_pool.pop_back();
    }
}

void ISpatial_DB::initialize(const Fbox& BB)
{
    ZoneScoped;

    // initialize
    Fvector bbc, bbd;
    BB.get_CD(bbc, bbd);

    allocator_pool.reserve(128);
    m_center.set(bbc);
    m_bounds = _max(_max(bbd.x, bbd.y), bbd.z);
    rt_insert_object = NULL;
    if (0 == m_root)
        m_root = _node_create();
    m_root->_init(NULL);
}
ISpatial_NODE* ISpatial_DB::_node_create()
{
    Stats.NodeCount++;
    if (allocator_pool.empty())
        return allocator.create();
    else
    {
        ISpatial_NODE* N = allocator_pool.back();
        allocator_pool.pop_back();
        return N;
    }
}
void ISpatial_DB::_node_destroy(ISpatial_NODE*& P)
{
    VERIFY(P->_empty());
    Stats.NodeCount--;
    allocator_pool.push_back(P);
    P = NULL;
}

void ISpatial_DB::_insert(ISpatial_NODE* N, Fvector& n_C, float n_R)
{
    //*** we are assured that object lives inside our node
    float n_vR = 2 * n_R;
    VERIFY(N);
    VERIFY(verify_sp(rt_insert_object, n_C, n_vR));

    // we have to make sure we aren't the leaf node
    if (n_R <= c_spatial_min)
    {
        // this is leaf node
        N->_insert(rt_insert_object);
        rt_insert_object->GetSpatialData().node_center.set(n_C);
        rt_insert_object->GetSpatialData().node_radius = n_vR; // vR
        return;
    }

    // we have to check if it can be putted further down
    float s_R = rt_insert_object->GetSpatialData().sphere.R; // spatial bounds
    float c_R = n_R / 2; // children bounds
    if (s_R < c_R)
    {
        // object can be pushed further down - select "octant", calc node position
        Fvector& s_C = rt_insert_object->GetSpatialData().sphere.P;
        u32 octant = _octant(n_C, s_C);
        Fvector c_C;
        c_C.mad(n_C, c_spatial_offset[octant], c_R);
        VERIFY(octant == _octant(n_C, c_C)); // check table assosiations
        ISpatial_NODE*& chield = N->children[octant];

        if (0 == chield)
        {
            chield = _node_create();
            VERIFY(chield);
            chield->_init(N);
            VERIFY(chield);
        }
        VERIFY(chield);
        _insert(chield, c_C, c_R);
        VERIFY(chield);
    }
    else
    {
        // we have to "own" this object (potentially it can be putted down sometimes...)
        N->_insert(rt_insert_object);
        rt_insert_object->GetSpatialData().node_center.set(n_C);
        rt_insert_object->GetSpatialData().node_radius = n_vR;
    }
}

void ISpatial_DB::insert(ISpatial* S)
{
    ScopeLock scope(&cs);

    // [DA_PORT] Нечисло не попадает в дерево. Проверка ниже — та же, но она под #ifdef DEBUG,
    // то есть в релизе её НЕТ ВОВСЕ (а там ещё и xrDebug::Fatal, то есть не защита, а падение).
    //
    // ⭐ Чем это опасно именно здесь: сравнения с NaN всегда ложны, поэтому нечисло проходит все
    // заслоны НАСКВОЗЬ, а не отсекается ими.
    //   - `verify_sp` перебирает шесть условий `< / >` и на каждом получает «нет» — значит
    //     возвращает TRUE, «объект внутри узла», хотя он не внутри ничего;
    //   - `_octant` так же получает три «нет» и всегда отдаёт нулевой октант;
    //   - `spatial_inside` устроена как verify_sp и тоже говорит «внутри», поэтому spatial_move
    //     объект уже не переставит.
    // В итоге объект уходит вглубь дерева по ложному пути и остаётся там навсегда, а СТРУКТУРА
    // дерева перестаёт соответствовать содержимому.
    //
    // ⭐ Это объясняет нашу же правку в `_remove` ниже по файлу: «N_sub не среди детей N,
    // рассогласование дошло до релизной сборки». Там закрыто СЛЕДСТВИЕ (не индексировать
    // `children[0xFFFFFFFF]`), причина же — вот она, на входе.
    //
    // Отказ во вставке честнее порчи: объект просто не участвует в пространственных запросах.
    // `spatial.node_ptr` остаётся нулевым, и `spatial_move` такой объект молча пропускает.
    //
    // ⚠️ Сообщение придушено: если у объекта поехали координаты, это повторяется каждый кадр.
    // Имя берём только у игрового объекта — у частиц и прочих ISpatial его нет.
    {
        const auto& da_sd = S->GetSpatialData();
        if (!_valid(da_sd.sphere.P) || !_valid(da_sd.sphere.R))
        {
            static u32 da_hits = 0;
            ++da_hits;
            // ⚠️ Имя объекта НЕ печатаем: в xrCDB `IGameObject` только объявлен вперёд, и
            // dynamic_cast до него не доберётся (отладочная ветка ниже это может себе позволить,
            // релизная нет). Координат, радиуса и счётчика хватает, чтобы опознать источник —
            // а тянуть сюда заголовок игры ради диагностики значит менять зависимости модулей.
            if (da_hits <= 5 || (da_hits % 500) == 0)
                Msg("! [DA] пространственная база: нечисловые координаты (%f,%f,%f) или радиус %f "
                    "— на учёт не берём (случаев %u)",
                    VPUSH(da_sd.sphere.P), da_sd.sphere.R, da_hits);
            return;
        }
    }

#ifdef DEBUG
    Stats.Insert.Begin();

    const auto& spatialData = S->GetSpatialData();

    bool bValid = _valid(spatialData.sphere.R) && _valid(spatialData.sphere.P);
    if (!bValid)
    {
        IGameObject* O = dynamic_cast<IGameObject*>(S);
        if (O)
            xrDebug::Fatal(DEBUG_INFO, "Invalid OBJECT position or radius (%s)", O->cName().c_str());
        else
        {
#ifdef XR_PLATFORM_WINDOWS
            CPS_Instance* P = dynamic_cast<CPS_Instance*>(S);
            if (P)
                xrDebug::Fatal(DEBUG_INFO, "Invalid PS spatial position{%3.2f,%3.2f,%3.2f} or radius{%3.2f}",
                    VPUSH(spatialData.sphere.P), spatialData.sphere.R);
            else
                xrDebug::Fatal(DEBUG_INFO, "Invalid OTHER spatial position{%3.2f,%3.2f,%3.2f} or radius{%3.2f}",
                    VPUSH(spatialData.sphere.P), spatialData.sphere.R);
#else
            // In Linux there is a linking issue because `CPS_Instance` belongs to xrEngine
            // and is not available to xrCDB due to source code organization
            xrDebug::Fatal(DEBUG_INFO, "Invalid PS or OTHER spatial position{%3.2f,%3.2f,%3.2f} or radius{%3.2f}",
                VPUSH(spatialData.sphere.P), spatialData.sphere.R);
#endif
        }
    }
#endif

    if (verify_sp(S, m_center, m_bounds))
    {
        // Object inside our DB
        rt_insert_object = S;
        _insert(m_root, m_center, m_bounds);
        VERIFY(S->spatial_inside());
    }
    else
    {
        // Object outside our DB, put it into root node and hack bounds
        // Object will reinsert itself until fits into "real", "controlled" space
        m_root->_insert(S);
        S->GetSpatialData().node_center.set(m_center);
        S->GetSpatialData().node_radius = m_bounds;
    }
#ifdef DEBUG
    Stats.Insert.End();
#endif
}

void ISpatial_DB::_remove(ISpatial_NODE* N, ISpatial_NODE* N_sub)
{
    if (0 == N)
        return;

    //*** we are assured that node contains N_sub and this subnode is empty
    u32 octant = u32(-1);
    if (N_sub == N->children[0])
        octant = 0;
    else if (N_sub == N->children[1])
        octant = 1;
    else if (N_sub == N->children[2])
        octant = 2;
    else if (N_sub == N->children[3])
        octant = 3;
    else if (N_sub == N->children[4])
        octant = 4;
    else if (N_sub == N->children[5])
        octant = 5;
    else if (N_sub == N->children[6])
        octant = 6;
    else if (N_sub == N->children[7])
        octant = 7;
    // [DA_PORT] VERIFY(octant < 8) removed: the if-guard below handles the error
    // gracefully instead of crashing. Under MinGW x64 the spatial tree can reach
    // an inconsistent state during level load (RenderEnd cleanup path).
    if (octant >= 8)
    {
        // [DA_PORT] N_sub is not among N's children: tree inconsistency reached the release
        // build (VERIFY above is a no-op in release). The original code would index
        // N->children[0xFFFFFFFF] -> access violation. Skip the prune instead of crashing,
        // and log so we can trace the upstream corruption (spatial_register/_insert path).
        // [DA_PORT] Сообщение придушено, а узел всё же возвращается в пул.
        //
        // Было два хвоста. Первый: печать без ограничителя и с принудительным сбросом на диск —
        // соседний заслон на нечисло (выше в этом файле) печатает первые пять и далее каждый
        // пятисотый, здесь не было ничего. Один случай за сессию безобиден, но если дерево начнёт
        // рассогласовываться покадрово, это поток строк, а буфер лога живёт в памяти.
        // Второй: ранний выход уносил с собой и возврат узла — он пуст, из детей родителя выпал,
        // в пул не вернулся. Около сотни байт за случай, зато механизм ровно тот, что мы ищем.
        static u32 da_hits = 0;
        if (++da_hits <= 5 || (da_hits % 500) == 0)
        {
            Msg("! [DA_PORT] ISpatial_DB::_remove: N_sub %p не значится среди детей N %p — подрезка "
                "пропущена (случаев %u)",
                (void*)N_sub, (void*)N, da_hits);
        }
        _node_destroy(N_sub); // осиротевший узел вернуть в пул, иначе слот и его вектор потеряны
        return;
    }
    VERIFY(N_sub->_empty());
    _node_destroy(N->children[octant]);

    // Recurse
    if (N->_empty())
        _remove(N->parent, N);
}

void ISpatial_DB::remove(ISpatial* S)
{
    ScopeLock scope(&cs);
#ifdef DEBUG
    Stats.Remove.Begin();
#endif
    ISpatial_NODE* N = S->GetSpatialData().node_ptr;
    N->_remove(S);

    // Recurse
    if (N->_empty())
        _remove(N->parent, N);

#ifdef DEBUG
    Stats.Remove.End();
#endif
}

void ISpatial_DB::update(u32 /*nodes = 8 */)
{
#ifdef DEBUG
    if (0 == m_root)
        return;
    ScopeLock scope(&cs);
    VERIFY(verify());
#endif
}
