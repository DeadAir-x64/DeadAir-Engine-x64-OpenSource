//////////////////////////////////////////////////////////////////////////
// monster_community.cpp: структура представления группировки для монстров
//
//////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "monster_community.h"

#define MONSTER_RELATIONS_SECT "monster_communities"
#define MONSTER_COMMUNITIES "communities"
#define MONSTER_RELATIONS_TABLE "monster_relations"

//////////////////////////////////////////////////////////////////////////
MONSTER_COMMUNITY_DATA::MONSTER_COMMUNITY_DATA(MONSTER_COMMUNITY_INDEX idx, MONSTER_COMMUNITY_ID idn, LPCSTR team_str)
{
    index = idx;
    id = idn;
    team = (u8)atoi(team_str);
}

//////////////////////////////////////////////////////////////////////////
MONSTER_COMMUNITY::MONSTER_RELATION_TABLE MONSTER_COMMUNITY::m_relation_table;

//////////////////////////////////////////////////////////////////////////
MONSTER_COMMUNITY::MONSTER_COMMUNITY() { m_current_index = NO_MONSTER_COMMUNITY_INDEX; }
MONSTER_COMMUNITY::~MONSTER_COMMUNITY() {}
// [DA_PORT] Неизвестное сообщество больше НЕ роняет игру.
//
// Цепочка была такая: в секции существа нет `species` -> сообщество ищется по пустому имени ->
// не найдено -> проверка в GetById в релизе вырезана (см. release-assert-macros) -> IdToIndex
// возвращает -1 -> этим числом лезут в массив. Падало в strcmp внутри GetByIndex, то есть стек
// показывал на поиск, а виновата была ОПЕЧАТКА В КОНФИГЕ за десять шагов до него.
//
// Ошибку конфига движок при этом честно печатал («Can't find variable species in [...]») и шёл
// дальше — то есть сообщение было, а связи между ним и вылетом никакой.
//
// Теперь неизвестное сообщество откатывается к первому известному: существо получит не свои
// отношения, но игра доживёт до конца сессии, а в логе останется строка с именем.
void MONSTER_COMMUNITY::set(MONSTER_COMMUNITY_ID id)
{
    const MONSTER_COMMUNITY_INDEX idx = IdToIndex(id, MONSTER_COMMUNITY_INDEX(-1), true);
    if (idx < 0 || (size_t)idx >= m_pItemDataVector->size())
    {
        static xr_vector<shared_str> seen;
        bool known = false;
        for (const auto& it : seen)
            if (it == id)
                known = true;
        if (!known)
        {
            seen.push_back(id);
            Msg("! [DA_PORT] сообщество монстров [%s] не найдено в [%s]; беру первое известное. "
                "Обычно это отсутствующая или ошибочная строка species в секции существа",
                id.c_str(), MONSTER_RELATIONS_SECT);
        }
        m_current_index = m_pItemDataVector->empty() ? NO_MONSTER_COMMUNITY_INDEX : 0;
        return;
    }
    m_current_index = idx;
}
void MONSTER_COMMUNITY::set(MONSTER_COMMUNITY_INDEX index) { m_current_index = index; }
MONSTER_COMMUNITY_ID MONSTER_COMMUNITY::id() const { return IndexToId(m_current_index); }
MONSTER_COMMUNITY_INDEX MONSTER_COMMUNITY::index() const { return m_current_index; }
u8 MONSTER_COMMUNITY::team() const
{
    // [DA_PORT] та же защита: индекс мог остаться отрицательным у существа без species.
    if (m_current_index < 0 || (size_t)m_current_index >= m_pItemDataVector->size())
        return 0;
    return (*m_pItemDataVector)[m_current_index].team;
}
void MONSTER_COMMUNITY::InitIdToIndex()
{
    section_name = MONSTER_RELATIONS_SECT;
    line_name = MONSTER_COMMUNITIES;
    m_relation_table.set_table_params(MONSTER_RELATIONS_TABLE);
}

int MONSTER_COMMUNITY::relation(MONSTER_COMMUNITY_INDEX to) { return relation(m_current_index, to); }
int MONSTER_COMMUNITY::relation(MONSTER_COMMUNITY_INDEX from, MONSTER_COMMUNITY_INDEX to)
{
    VERIFY(from >= 0 && from < (int)m_relation_table.table().size());
    VERIFY(to >= 0 && to < (int)m_relation_table.table().size());

    // [DA_PORT] та же защита, что и в team(): индекс мог остаться отрицательным/вне таблицы
    // у существа без species; VERIFY в релизе исчезает -> OOB. Нейтральное отношение при промахе.
    const int _sz = (int)m_relation_table.table().size();
    if (from < 0 || from >= _sz || to < 0 || to >= _sz)
        return 0;

    return m_relation_table.table()[from][to];
}

void MONSTER_COMMUNITY::DeleteIdToIndexData()
{
    m_relation_table.clear();
    inherited::DeleteIdToIndexData();
}
