////////////////////////////////////////////////////////////////////////////
//	Module 		: patrol_path_storage.cpp
//	Created 	: 15.06.2004
//  Modified 	: 15.06.2004
//	Author		: Dmitriy Iassenev
//	Description : Patrol path storage
////////////////////////////////////////////////////////////////////////////

#include "pch.hpp"
#include "patrol_path_storage.h"
#include "patrol_path.h"
#include "patrol_point.h"
#include "Common/LevelGameDef.h"

// [DA_PORT] Освобождение реестра. Удалять его через delete_data НЕЛЬЗЯ, хотя именно так и было.
//
// В реестре один и тот же CPatrolPath* может лежать под ДВУМЯ ключами: add_alias_if_exist() кладёт
// второе имя, указывающее на тот же объект (`m_registry.emplace(duplicate_name, it->second)`), и
// делает это по ходу игры — из задач смарт-террейнов. Такая запись НЕ владеет путём, она ссылка.
// delete_data проходит по всем парам подряд и освобождает каждую, то есть аллокатор получает один и
// тот же блок дважды.
//
// Двойное освобождение почти никогда не падает на месте: оно портит кучу, а убивает позже и в
// стороне. Поэтому искать его по месту падения бесполезно — здесь оно названо по построению.
//
// Указатели сначала собираются и обеззначиваются, и только потом удаляются по одному разу.
void CPatrolPathStorage::destroy_registry()
{
    xr_vector<CPatrolPath*> owned;
    owned.reserve(m_registry.size());
    for (auto& it : m_registry)
        if (it.second)
            owned.push_back(it.second);

    std::sort(owned.begin(), owned.end());
    owned.erase(std::unique(owned.begin(), owned.end()), owned.end());

    for (CPatrolPath* path : owned)
        xr_delete(path);

    m_registry.clear();
}

CPatrolPathStorage::~CPatrolPathStorage() { destroy_registry(); }
void CPatrolPathStorage::load_raw(
    const CLevelGraph* level_graph, const CGameLevelCrossTable* cross, const CGameGraph* game_graph, IReader& stream)
{
    ZoneScoped;

    IReader* chunk = stream.open_chunk(WAY_PATROLPATH_CHUNK);

    if (!chunk)
        return;

    u32 chunk_iterator;
    for (IReader* sub_chunk = chunk->open_chunk_iterator(chunk_iterator); sub_chunk;
         sub_chunk = chunk->open_chunk_iterator(chunk_iterator, sub_chunk))
    {
        R_ASSERT(sub_chunk->find_chunk(WAYOBJECT_CHUNK_VERSION));
        R_ASSERT(sub_chunk->r_u16() == WAYOBJECT_VERSION);
        R_ASSERT(sub_chunk->find_chunk(WAYOBJECT_CHUNK_NAME));

        shared_str patrol_name;
        sub_chunk->r_stringZ(patrol_name);
        VERIFY3(m_registry.find(patrol_name) == m_registry.end(), "Duplicated patrol path found", patrol_name.c_str());
        m_registry.emplace(
            patrol_name, &(xr_new<CPatrolPath>(patrol_name))->load_raw(level_graph, cross, game_graph, *sub_chunk)
		);
    }

    chunk->close();
}

void CPatrolPathStorage::load(IReader& stream)
{
    ZoneScoped;

    IReader* chunk = stream.open_chunk(0);
    const u32 size = chunk->r_u32();
    chunk->close();

    // [DA_PORT] Через destroy_registry(), а не clear(): в реестре лежат УКАЗАТЕЛИ, и clear() про них
    // ничего не знает. Утечки здесь, впрочем, не было — единственный путь к load() это
    // AISpaceBase::patrol_path_storage(), а он всегда пересоздаёт хранилище, так что реестр пуст.
    // Правка приводит владение в соответствие с деструктором на случай второго вызова.
    destroy_registry();

    chunk = stream.open_chunk(1);
    for (u32 i = 0; i < size; ++i)
    {
        PATROL_REGISTRY::value_type pair{};

        IReader* chunk1 = chunk->open_chunk(i);
        IReader* chunk2 = chunk1->open_chunk(0);

        load_data(pair.first, *chunk2);
        chunk2->close();

        chunk2 = chunk1->open_chunk(1);
        load_data(pair.second, *chunk2);
        chunk2->close();

        chunk1->close();

        iterator I = m_registry.find(pair.first);
        VERIFY3(I == m_registry.end(), "Duplicated patrol path found ", pair.first.c_str());
        if (I != m_registry.end())
        {
            Log("~ Duplicated patrol path found ", pair.first.c_str());
            // [DA_PORT] И этот путь надо освободить. insert по существующему ключу не отбрасывает
            // новое значение, а ПЕРЕЗАПИСЫВАЕТ им старое (AssociativeVector::insert: `*I = value`),
            // поэтому вытесненный указатель просто выпадал из карты. В данных DA дубликаты
            // настоящие — около семи имён на каждую загрузку.
            xr_delete(I->second);
        }

#ifdef DEBUG
        pair.second->name(pair.first);
#endif

        m_registry.insert(pair);
    }

    chunk->close();
}

void CPatrolPathStorage::save(IWriter& stream)
{
    stream.open_chunk(0);
    stream.w_u32(m_registry.size());
    stream.close_chunk();

    stream.open_chunk(1);

    PATROL_REGISTRY::iterator I = m_registry.begin();
    PATROL_REGISTRY::iterator E = m_registry.end();
    for (int i = 0; I != E; ++I, ++i)
    {
        stream.open_chunk(i);

        stream.open_chunk(0);
        save_data((*I).first, stream);
        stream.close_chunk();

        stream.open_chunk(1);
        save_data((*I).second, stream);
        stream.close_chunk();

        stream.close_chunk();
    }

    stream.close_chunk();
}

const CPatrolPath* CPatrolPathStorage::add_alias_if_exist(shared_str patrol_name, shared_str duplicate_name)
{
    const_iterator it = patrol_paths().find(patrol_name);
    if (it == patrol_paths().end())
        return nullptr;

    m_registry.emplace(duplicate_name, it->second );
    return it->second;
}
