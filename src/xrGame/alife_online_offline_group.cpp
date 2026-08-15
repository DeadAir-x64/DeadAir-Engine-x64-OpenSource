////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_online_offline_group.cpp
//	Created 	: 25.10.2005
//  Modified 	: 25.10.2005
//	Author		: Dmitriy Iassenev
//	Description : ALife Online Offline Group class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "xrServer_Objects_ALife_Monsters.h"
#include "ai_space.h"
#include "alife_simulator.h"
#include "alife_object_registry.h"
#include "alife_graph_registry.h"
#include "alife_schedule_registry.h"
#include "xrAICore/Navigation/game_level_cross_table.h"
#include "alife_online_offline_group_brain.h"
#include "xrAICore/Navigation/level_graph.h"
#include "alife_monster_movement_manager.h"
#include "alife_monster_detail_path_manager.h"

extern void setup_location_types_line(GameGraph::TERRAIN_VECTOR& m_vertex_types, LPCSTR string);

CSE_ALifeItemWeapon* CSE_ALifeOnlineOfflineGroup::tpfGetBestWeapon(ALife::EHitType& tHitType, float& fHitPower)
{
    return (0);
}

ALife::EMeetActionType CSE_ALifeOnlineOfflineGroup::tfGetActionType(
    CSE_ALifeSchedulable* tpALifeSchedulable, int iGroupIndex, bool bMutualDetection)
{
    return (ALife::eMeetActionTypeIgnore);
}

bool CSE_ALifeOnlineOfflineGroup::bfActive() { return (!m_bOnline && !m_members.empty()); }
CSE_ALifeDynamicObject* CSE_ALifeOnlineOfflineGroup::tpfGetBestDetector() { return (0); }
bool CSE_ALifeOnlineOfflineGroup::need_update(CSE_ALifeDynamicObject* object) { return true; }
// [DA_PORT] Снимок состава: копия указателей на бойцов, без нулевых.
//
// Нужен всем обходам, которые внутри цикла уводят управление наружу (add_online, remove_online,
// а через них — события, скрипты, смерть бойца). Любой такой вызов может снять бойца с учёта, а
// снятие делает erase у AssociativeVector, то есть у ВЕКТОРА: элементы сдвигаются, итератор
// обесценивается, и обход продолжается уже по чужой памяти.
//
// Копия дороже итератора ровно на один проход по составу — а состав это единицы бойцов.
xr_vector<CSE_ALifeOnlineOfflineGroup::MEMBER*> CSE_ALifeOnlineOfflineGroup::da_members_snapshot() const
{
    xr_vector<MEMBER*> result;
    result.reserve(m_members.size());

    for (const auto& pair : m_members)
    {
        // Нулевые пропускаем здесь, чтобы вызывающие о них не думали: состав, поднятый из
        // сохранения, держит {номер, nullptr} до register_member (см. STATE_Read).
        if (pair.second)
            result.push_back(pair.second);
    }

    return result;
}

void CSE_ALifeOnlineOfflineGroup::update()
{
    if (m_bOnline)
    {
        // [DA_PORT] Две проверки на две строки, и обе обязательны.
        //
        // begin() у ПУСТОГО состава — чтение за концом контейнера. Пустым он бывает штатно: отряд
        // остаётся жить, пока из него уходит последний боец.
        //
        // 🔑 А `.second` бывает НУЛЁМ по построению: CSE_ALifeOnlineOfflineGroup::STATE_Read кладёт
        // в состав пары {номер, nullptr} — указатели на бойцов при загрузке ещё неизвестны и
        // проставляются позже, в register_member. До этого момента отряд уже живёт и обновляется.
        // Разыменование такого нуля — падение в самом начале сессии, без единой ошибки в логе.
        MEMBER* commander = m_members.empty() ? nullptr : (*m_members.begin()).second;
        if (commander)
        {
            o_Position = commander->o_Position;
            m_tNodeID = commander->m_tNodeID;
            m_tGraphID = commander->m_tGraphID;
        }
    }
    if (!bfActive())
        return;

    brain().update();

    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
    {
        MEMBER* m = (*I).second;

        // Тот же случай: боец из сохранения ещё не связан с объектом — пропускаем до register_member.
        // Метка жизни — по вылету у игрока в switch_offline: освобождённый боец даёт не ноль, а
        // мусор, и проверка на ноль его пропускает. Ниже идёт ЗАПИСЬ в его поля.
        if (!m || !m->da_object_alive())
            continue;

        m->o_Position = o_Position;
        m->m_tNodeID = m_tNodeID;
        m->m_tGraphID = m_tGraphID;
        m->m_fDistance = m_fDistance;
    }
    return;
}

void CSE_ALifeOnlineOfflineGroup::on_location_change() const { brain().on_location_change(); }
void CSE_ALifeOnlineOfflineGroup::register_member(ALife::_OBJECT_ID member_id)
{
    VERIFY(m_members.find(member_id) == m_members.end());

    // [DA_PORT] Состав отряда приходит из СОХРАНЕНИЯ, а не из кода, и доверять ему нельзя.
    //
    // Здесь стояло: взять объект по номеру, привести к монстру, VERIFY(monster) — и дальше
    // безусловно разыменовать. VERIFY в релизе исчезает (xrDebug_macros.h), и каждая следующая
    // строка работает с нулём: object->m_bOnline, monster->m_group_id.
    //
    // Когда номер в составе не монстр или его вовсе нет: сохранение от другой сборки мода,
    // снятый аддон, повторно использованный номер. Отряд при этом жив и должен продолжать —
    // потеря одного бойца несравнимо лучше конца сессии.
    //
    // no_assert = true у object(): иначе реестр сам уронит игру раньше, чем мы проверим.
    CSE_ALifeDynamicObject* object = ai().alife().objects().object(member_id, true);
    CSE_ALifeMonsterAbstract* monster = smart_cast<CSE_ALifeMonsterAbstract*>(object);
    if (!monster)
    {
        Msg("! [DA] отряд [%d]: участник [%d] (%s) не монстр, пропущен", ID, member_id,
            object ? object->name_replace() : "объекта нет в реестре");
        return;
    }
    VERIFY(monster->g_Alive());

    bool empty = m_members.empty();
    if (!object->m_bOnline)
    {
        if (m_bOnline)
        {
            object->switch_online();
            VERIFY(object->ID_Parent == 0xffff);
            alife().graph().level().remove(object);
        }
        else
        {
            alife().graph().remove(object, object->m_tGraphID);
            alife().scheduled().remove(object);
        }
    }
    else
    {
        if (!m_bOnline)
        {
            switch_online();
        }
        VERIFY(object->ID_Parent == 0xffff);
        alife().graph().level().remove(object);
    }
    VERIFY((monster->m_group_id == 0xffff) || (monster->m_group_id == ID));
    monster->m_group_id = ID;
    m_members.emplace(member_id, monster);

    if (!empty)
        return;

    o_Position = monster->o_Position;
    m_tNodeID = monster->m_tNodeID;
    m_tGraphID = monster->m_tGraphID;
    m_fGoingSpeed = monster->m_fGoingSpeed;
    m_fCurrentLevelGoingSpeed = monster->m_fCurrentLevelGoingSpeed;
    m_flags.set(flUsedAI_Locations, TRUE);
    alife().graph().update(this);
}

void CSE_ALifeOnlineOfflineGroup::unregister_member(ALife::_OBJECT_ID member_id)
{
    CALifeGraphRegistry& graph = alife().graph();
    //	CALifeLevelRegistry			&level = graph.level();

    MEMBERS::iterator I = m_members.find(member_id);

    // [DA_PORT] Проверка вместо VERIFY: тот исчезает в релизе, а под ним разыменование end() и
    // erase(end()) — то есть порча самого списка членов отряда.
    //
    // Отсутствие бойца здесь законно: снятие с учёта зовётся из on_unregister, а до него можно
    // дойти дважды — освобождение рекурсивно, и один и тот же боец достаётся и рекурсии, и внешнему
    // обходу реестра.
    //
    // 🔑 Именно этим объясняется давнее падение в commander_id() с чтением по ffffffffffffffff:
    // отряд был ЖИВ, испорчен был его m_members, а commander_id читает ровно его —
    // (*m_members.begin()).first. Отсюда же ноль срабатываний сторожа висячих выдач: объект в
    // реестре числился и был настоящим, негодным стало его содержимое.
    if (I == m_members.end())
    {
        Msg("! [DA_PORT] отряд [%d]: боец [%d] не числится в составе, снятие с учёта пропущено",
            ID, member_id);
        return;
    }

    // [DA_PORT] Указатель на бойца может быть нулевым — состав, поднятый из сохранения, держит
    // пары {номер, nullptr} до register_member (см. STATE_Read). Боец может уйти из отряда и в это
    // окно: умереть, быть удалённым скриптом, не дожить до привязки. Тогда возвращать в мир нечего —
    // просто вычёркиваем запись.
    if (!(*I).second)
    {
        Msg("~ [DA_PORT] отряд [%d]: боец [%d] снят с учёта до привязки к объекту (состав из "
            "сохранения)",
            ID, member_id);
        m_members.erase(I);

        if (m_members.empty())
            m_flags.set(flUsedAI_Locations, FALSE);

        return;
    }

    // [DA_PORT] Метка жизни: если боец уже освобождён, трогать его поля и обновлять по нему граф
    // нельзя — а из состава запись всё равно надо вычеркнуть, иначе она останется висеть.
    if (!(*I).second->da_object_alive())
    {
        Msg("~ [DA_PORT] отряд [%d]: боец [%d] уже освобождён — вычёркиваем без обновления графа", ID,
            member_id);
        m_members.erase(I);

        if (m_members.empty())
            m_flags.set(flUsedAI_Locations, FALSE);

        return;
    }

    (*I).second->m_group_id = 0xffff;

    graph.update((*I).second);
    alife().scheduled().add((*I).second);
    m_members.erase(I);

    if (m_members.empty())
    {
        m_flags.set(flUsedAI_Locations, FALSE);
    }
}

CSE_ALifeOnlineOfflineGroup::MEMBER* CSE_ALifeOnlineOfflineGroup::member(ALife::_OBJECT_ID member_id, bool no_assert)
{
    MEMBERS::iterator I = m_members.find(member_id);
    if (I == m_members.end())
    {
        if (!no_assert)
            Msg("! There is no member with id %d in the OnlineOfflineGroup id %d", member_id, ID);
        VERIFY(no_assert);
        return (0);
    }
    return ((*I).second);
}

bool CSE_ALifeOnlineOfflineGroup::synchronize_location()
{
    if (m_bOnline)
    {
        // [DA_PORT] См. update(): состав бывает пустым, а указатель на бойца — нулевым сразу после
        // загрузки сохранения (STATE_Read кладёт nullptr до register_member).
        // Первый ЖИВОЙ, а не просто первый — тот же разбор, что в switch_offline.
        MEMBER* member = nullptr;
        for (const auto& it : m_members)
        {
            if (it.second && it.second->da_object_alive())
            {
                member = it.second;
                break;
            }
        }

        if (member)
        {
            o_Position = member->o_Position;
            m_tNodeID = member->m_tNodeID;
            m_tGraphID = member->m_tGraphID;
            m_fDistance = member->m_fDistance;
        }
    }

    return (true);
}

void CSE_ALifeOnlineOfflineGroup::try_switch_online()
{
    if (m_members.empty())
        return;

    if (!can_switch_online())
        return;

    if (!can_switch_offline())
    {
        inherited1::try_switch_online();
        return;
    }
    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
    {
        // [DA_PORT] Боец из сохранения ещё не связан с объектом (см. STATE_Read) — пропускаем.
        // Метка жизни здесь по той же причине, что и в switch_offline: указатель на
        // освобождённого бойца не ноль, а мусор, и проверка на ноль его пропускает.
        if (!(*I).second || !(*I).second->da_object_alive())
            continue;
        VERIFY3((*I).second->g_Alive(), "Incorrect situation : some of the OnlineOffline group members is dead",
            (*I).second->name_replace());
        VERIFY3((*I).second->can_switch_online(),
            "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their "
            "personal "
            "properties",
            (*I).second->name_replace());
        VERIFY3((*I).second->can_switch_offline(),
            "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their "
            "personal "
            "properties",
            (*I).second->name_replace());
        if (alife().graph().actor()->o_Position.distance_to((*I).second->o_Position) > alife().online_distance())
        {
            continue;
        }
        inherited1::try_switch_online();
        return;
    }
    on_failed_switch_online();
}

void CSE_ALifeOnlineOfflineGroup::try_switch_offline()
{
    if (m_members.empty())
        return;

    if (!can_switch_offline())
        return;

    if (!can_switch_online())
    {
        alife().switch_offline(this);
        return;
    }

    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
    {
        // [DA_PORT] Боец из сохранения ещё не связан с объектом (см. STATE_Read) — пропускаем.
        // Метка жизни здесь по той же причине, что и в switch_offline: указатель на
        // освобождённого бойца не ноль, а мусор, и проверка на ноль его пропускает.
        if (!(*I).second || !(*I).second->da_object_alive())
            continue;
        VERIFY3((*I).second->g_Alive(), "Incorrect situation : some of the OnlineOffline group members is dead",
            (*I).second->name_replace());
        VERIFY3((*I).second->can_switch_offline(),
            "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their "
            "personal "
            "properties",
            (*I).second->name_replace());
        VERIFY3((*I).second->can_switch_online(),
            "Incorrect situation : some of the OnlineOffline group members cannot be switched online due to their "
            "personal "
            "properties",
            (*I).second->name_replace());

        if (alife().graph().actor()->o_Position.distance_to((*I).second->o_Position) <= alife().offline_distance())
            return;
    }

    alife().switch_offline(this);
}

void CSE_ALifeOnlineOfflineGroup::switch_online()
{
    R_ASSERT(!m_bOnline);
    m_bOnline = true;

    // [DA_PORT] Идём по КОПИИ состава, а не по живому контейнеру.
    //
    // add_online уводит управление далеко: клиентская часть создаётся, идут события, работают
    // скрипты. Любое из этого может снять бойца с учёта — смерть, удаление, распад отряда, — а
    // снятие делает m_members.erase(). MEMBERS — это AssociativeVector, то есть вектор: erase
    // сдвигает элементы и обесценивает итератор, по которому мы идём прямо сейчас.
    //
    // Дальше обход читает уже не свои данные, и падение приходит не здесь, а там, где этот состав
    // прочитают в следующий раз — например, в commander_id с чтением по ffffffffffffffff. Отряд при
    // этом ЖИВОЙ и в реестрах числится правильно, поэтому ни метка жизни, ни санитар такого не ловят.
    const xr_vector<MEMBER*> members = da_members_snapshot();

    for (MEMBER* member : members)
    {
        // [DA_PORT] Снимок лечит обесценивание итератора, но сам может протухнуть: боец,
        // взятый в копию, способен быть освобождён, пока мы работаем с предыдущими. Здесь это
        // маловероятно — удаление объектов в обходе реестра отложено (m_da_pending_release в
        // alife_switch_manager), — но проверка метки жизни стоит чтения одного поля, а закрывает
        // весь класс «висячий элемент снимка» разом, включая пути, которых мы не знаем.
        if (!member->da_object_alive())
            continue;

        if (member->m_bOnline == false)
            alife().add_online(member, false);
    }

    alife().scheduled().remove(this);
    alife().graph().remove(this, m_tGraphID, false);
}

void CSE_ALifeOnlineOfflineGroup::switch_offline()
{
    R_ASSERT(m_bOnline);
    m_bOnline = false;

    // [DA_PORT] Берём ПЕРВОГО ЖИВОГО бойца, а не просто первого.
    //
    // Вылет у игрока (сборка 07d02d13): чтение по адресу 0x4adbeaf2 в synchronize_location прямо
    // отсюда. Не ноль -- мусор, то есть указатель на освобождённого бойца. Пустоту и ноль здесь мы
    // уже прикрывали, а проверку живости поставили только в цикле ниже -- и это место осталось
    // непокрытым. Тот же класс, что чинили в этом файле, просто пропущенная точка.
    //
    // Перебор, а не отказ при мёртвом первом: блок ниже переносит на группу положение, узел и
    // вершину графа. Взять их не с кого -- значит группа уйдёт в офлайн со старыми координатами,
    // и это заметнее, чем взять их со второго бойца.
    MEMBER* member = nullptr;
    for (const auto& it : m_members)
    {
        if (it.second && it.second->da_object_alive())
        {
            member = it.second;
            break;
        }
    }

    if (member)
    {
        member->synchronize_location();

        o_Position = member->o_Position;
        m_tNodeID = member->m_tNodeID;
        m_tGraphID = member->m_tGraphID;
        m_fDistance = member->m_fDistance;
    }

    // [DA_PORT] По КОПИИ состава — см. разбор в switch_online. Здесь опаснее вдвойне:
    // remove_online делает Perform_destroy, а это уничтожение клиентской части со всем каскадом
    // событий, включая смерть бойца и снятие его с учёта. То есть erase посреди нашего же обхода.
    const xr_vector<MEMBER*> members = da_members_snapshot();

    for (MEMBER* member : members)
    {
        // [DA_PORT] Снимок лечит обесценивание итератора, но сам может протухнуть: боец,
        // взятый в копию, способен быть освобождён, пока мы работаем с предыдущими. Здесь это
        // маловероятно — удаление объектов в обходе реестра отложено (m_da_pending_release в
        // alife_switch_manager), — но проверка метки жизни стоит чтения одного поля, а закрывает
        // весь класс «висячий элемент снимка» разом, включая пути, которых мы не знаем.
        if (!member->da_object_alive())
            continue;

        if (member->m_bOnline == true)
        {
            member->clear_client_data();
            alife().remove_online(member, false);
        }
    }

    alife().scheduled().add(this);
    alife().graph().add(this, m_tGraphID, false);
}

bool CSE_ALifeOnlineOfflineGroup::redundant() const { return (m_members.empty()); }
// [DA_PORT] Указатель приходит извне (смерть бойца) и может быть нулём: проверка вместо падения.
void CSE_ALifeOnlineOfflineGroup::notify_on_member_death(MEMBER* member)
{
    if (member)
        unregister_member(member->ID);
}
void CSE_ALifeOnlineOfflineGroup::on_before_register()
{
    m_tGraphID = GameGraph::_GRAPH_ID(-1);
    m_flags.set(flUsedAI_Locations, FALSE);
}

void CSE_ALifeOnlineOfflineGroup::on_after_game_load()
{
    if (m_members.empty())
        return;

    ALife::_OBJECT_ID* temp = (ALife::_OBJECT_ID*)xr_alloca(m_members.size() * sizeof(ALife::_OBJECT_ID));
    ALife::_OBJECT_ID *i = temp, *e = temp + m_members.size();

    {
        MEMBERS::const_iterator I = m_members.begin();
        MEMBERS::const_iterator E = m_members.end();
        for (; I != E; ++I, ++i)
        {
            VERIFY(!(*I).second);
            *i = (*I).first;
        }
    }

    m_members.clear();

    for (i = temp; i != e; ++i)
        register_member(*i);
}

ALife::_OBJECT_ID CSE_ALifeOnlineOfflineGroup::commander_id()
{
    // [DA_PORT] Метка жизни ПРЯМО ЗДЕСЬ, потому что сюда приходят из Lua по висячей ссылке.
    //
    // Скрипты мода держат отряды не по номеру, а объектами: sim_board хранит их в таблице
    // smarts[id].squads, и записи оттуда не вычёркиваются при удалении отряда. Полный стек с живого
    // падения:
    //   [C] commander_id
    //   sim_squad_scripted:get_script_target (107)
    //   smart_terrain.smart_terrain_squad_count (1559)  <- for k,v in pairs(board_smart_squads)
    //   sim_board:assign_squad_to_smart (229)
    //
    // Наши защиты сюда не достают: реестр ALife чист, расписание чисто, объект давно удалён — а
    // luabind зовёт метод по сохранённому в Lua адресу, минуя любые реестры. Единственное место,
    // где это ещё можно поймать, — начало самого метода.
    //
    // Проверка читает поле уже освобождённой памяти. Это осознанный компромисс (разбор в
    // xrServer_Object_Base.h): страница ещё отображена, а если её переписали — метка не сойдётся, и
    // мы вернём «командира нет» вместо падения. Скрипт такой ответ переживает: он и так возможен у
    // отряда без бойцов.
    if (!da_object_alive())
    {
        static u32 s_da_dead_calls = 0;
        ++s_da_dead_calls;
        if (s_da_dead_calls <= 10 || (s_da_dead_calls % 100) == 0)
            Msg("~ [DA_SQUAD] скрипт спросил командира у МЁРТВОГО отряда — отвечаю «нет», всего %u",
                s_da_dead_calls);

        return 0xffff;
    }

    if (!m_members.empty())
        return (*m_members.begin()).first;
    return 0xffff;
}

// [DA_PORT] Состав отдаётся В СКРИПТЫ (return_stl_iterator), и запрос может прийти по висячей
// ссылке — см. commander_id. У мёртвого отряда отдаём пустой список: скрипты обходят его циклом,
// и пустота для них штатна, а чужая память — нет.
CSE_ALifeOnlineOfflineGroup::MEMBERS const& CSE_ALifeOnlineOfflineGroup::squad_members() const
{
    if (!da_object_alive())
    {
        static MEMBERS s_da_empty;
        return s_da_empty;
    }

    return m_members;
}
// [DA_PORT] Те же висячие ссылки из Lua, что и в commander_id — проверяем метку жизни.
u32 CSE_ALifeOnlineOfflineGroup::npc_count() const
{
    return da_object_alive() ? u32(m_members.size()) : 0u;
}
void CSE_ALifeOnlineOfflineGroup::clear_location_types()
{
    m_tpaTerrain.clear();
    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
    {
        // [DA_PORT] Боец из сохранения ещё не связан с объектом (см. STATE_Read) — пропускаем.
        if ((*I).second)
            (*I).second->m_tpaTerrain.clear();
    }
}

void CSE_ALifeOnlineOfflineGroup::add_location_type(LPCSTR mask)
{
    setup_location_types_line(m_tpaTerrain, mask);
    MEMBERS::iterator I = m_members.begin();
    MEMBERS::iterator E = m_members.end();
    for (; I != E; ++I)
    {
        // [DA_PORT] Боец из сохранения ещё не связан с объектом (см. STATE_Read) — пропускаем.
        if ((*I).second)
            setup_location_types_line((*I).second->m_tpaTerrain, mask);
    }
}

void CSE_ALifeOnlineOfflineGroup::force_change_position(Fvector position)
{
    // [DA_PORT] Тот же дефект, что был в CLevelChanger::net_Spawn: vertex_id честно возвращает
    // u32(-1), если точка вне навигационного графа, а cross_table().vertex() проверяет индекс лишь
    // VERIFY — то есть в релизе читает далеко за концом отображённого файла.
    //
    // Позиция сюда приходит из скрипта (принудительный перенос отряда), так что «вне графа» —
    // обычное дело, а не исключение.
    const u32 new_level_vertex = ai().level_graph().vertex_id(position);
    if (!ai().level_graph().valid_vertex_id(new_level_vertex))
    {
        Msg("! [DA_PORT] отряд [%d]: точка [%.2f][%.2f][%.2f] вне навигационного графа — перенос "
            "отменён",
            ID, VPUSH(position));
        return;
    }

    GameGraph::_GRAPH_ID new_graph_vertex = ai().cross_table().vertex(new_level_vertex).game_vertex_id();
    o_Position = position;
    m_tNodeID = new_level_vertex;
    if (m_tGraphID != new_graph_vertex)
    {
        alife().graph().change(this, m_tGraphID, new_graph_vertex);
    }

    // m_tGraphID				= new_graph_vertex;
}

void CSE_ALifeOnlineOfflineGroup::on_failed_switch_online()
{
    MEMBERS::const_iterator I = m_members.begin();
    MEMBERS::const_iterator E = m_members.end();
    for (; I != E; ++I)
    {
        // [DA_PORT] Боец из сохранения ещё не связан с объектом (см. STATE_Read) — пропускаем.
        // Метка жизни здесь по той же причине, что и в switch_offline: указатель на
        // освобождённого бойца не ноль, а мусор, и проверка на ноль его пропускает.
        if ((*I).second && (*I).second->da_object_alive())
            (*I).second->clear_client_data();
    }
}
