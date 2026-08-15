////////////////////////////////////////////////////////////////////////////
//  Module      : alife_simulator_script.cpp
//  Created     : 25.12.2002
//  Modified    : 13.05.2004
//  Author      : Dmitriy Iassenev
//  Description : ALife Simulator script export
////////////////////////////////////////////////////////////////////////////

#include "pch_script.h"

#include "alife_simulator.h"
#include "ai_space.h"
#include "alife_object_registry.h"
#include "alife_story_registry.h"
#include "xrScriptEngine/script_engine.hpp"
#include "xrScriptEngine/da_lua_singleton.hpp"
#include "xrServer_Objects_ALife_Monsters.h"
#include "restriction_space.h"
#include "alife_graph_registry.h"
#include "alife_spawn_registry.h"
#include "alife_registry_container.h"
#include "xrServer.h"
#include "Level.h"

#include "xrNetServer/NET_Messages.h"

typedef xr_vector<std::pair<shared_str, int>> STORY_PAIRS;
typedef STORY_PAIRS SPAWN_STORY_PAIRS;
LPCSTR _INVALID_STORY_ID = "INVALID_STORY_ID";
LPCSTR _INVALID_SPAWN_STORY_ID = "INVALID_SPAWN_STORY_ID";
STORY_PAIRS story_ids;
SPAWN_STORY_PAIRS spawn_story_ids;

CALifeSimulator* alife() { return (const_cast<CALifeSimulator*>(ai().get_alife())); }

// DA: одна обёртка вместо новой на каждый вызов — см. da_lua_singleton.hpp.
// Симулятор пересоздаётся при загрузке сейва, поэтому кэш «на всю игру» был бы ссылкой на
// освобождённую память; здесь подмена ловится сравнением указателей внутри шаблона.
static da_lua_singleton<CALifeSimulator> s_da_alife;

static int da_lua_alife(lua_State* L)
{
    return s_da_alife.push(L, alife(), "alife");
}

bool valid_object_id(const CALifeSimulator* self, ALife::_OBJECT_ID object_id)
{
    VERIFY(self);
    return (object_id != 0xffff);
}

// [DA_PORT] Сторож висячих выдач: ловит момент, когда реестр отдаёт скрипту УЖЕ УДАЛЁННЫЙ объект.
//
// Зачем нужен именно такой прибор. Падение при массовой уборке выглядит как «исполнение по адресу
// 0000000000000000» без машинного стека: скрипт получил указатель на освобождённую память и позвал
// метод, а таблицы методов там больше нет. По самому падению виновника не найти — оно происходит
// уже внутри чужого кода, за много шагов от того, кто взял плохой указатель.
//
// Поэтому ловим НЕ падение, а выдачу. Адреса освобождаемых объектов запоминаем, и если реестр отдаёт
// такой адрес наружу — печатаем номер, имя и стек Lua. Стек назовёт скрипт и строку, то есть ровно
// то, чего не даёт разбор краша.
//
// Ложных срабатываний быть не должно: аллокатор охотно отдаёт освобождённую память следующему, кто
// попросит, поэтому при регистрации нового объекта его адрес из списка вычёркивается (da_watch_reused).
namespace
{
xr_unordered_map<const void*, u16> s_da_released;
}

int ps_da_dangling_watch = 0;

void da_watch_released(const void* object, u16 id)
{
    if (!ps_da_dangling_watch || !object)
        return;

    // Список не должен расти бесконечно: за долгую игру через него проходят десятки тысяч трупов.
    // Точность от сброса не страдает — интересует ближайшее прошлое, а не вся сессия.
    if (s_da_released.size() > 8192)
        s_da_released.clear();

    s_da_released[object] = id;

    // [DA_PORT] Самопроверка: молчание прибора и отсутствие события неотличимы, пока прибор не
    // отчитался, что он вообще работает. Раз в 500 удалений говорим, сколько адресов под присмотром —
    // тогда «висячих выдач ноль» означает именно ноль, а не мёртвый сторож.
    static u32 s_da_watched = 0;
    if (((++s_da_watched) % 500) == 0)
        Msg("~ [DA_DANGLE] сторож жив: под присмотром %u адресов, всего запомнено %u",
            u32(s_da_released.size()), s_da_watched);
}

void da_watch_reused(const void* object)
{
    if (s_da_released.empty() || !object)
        return;

    s_da_released.erase(object);
}

namespace
{
CSE_ALifeDynamicObject* da_check_dangling(CSE_ALifeDynamicObject* object, ALife::_OBJECT_ID asked_id)
{
    if (!ps_da_dangling_watch || !object || s_da_released.empty())
        return object;

    const auto it = s_da_released.find(object);
    if (it == s_da_released.end())
        return object;

    // Ни одного метода объекта здесь не вызываем — он мёртв, любое обращение к полям недостоверно.
    Msg("! [DA_DANGLE] реестр отдал скрипту УДАЛЁННЫЙ объект: спрошен id [%d], запись была id [%d], адрес [%p]",
        asked_id, it->second, (const void*)object);
    FlushLog();

    if (g_da_lua_stack_printer)
        g_da_lua_stack_printer();

    FlushLog();
    return object;
}
} // namespace

CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, ALife::_OBJECT_ID object_id)
{
    VERIFY(self);
    if (!valid_object_id(self, object_id))
    {
        GEnv.ScriptEngine->script_log(LuaMessageType::Error,"! alife():object(id): invalid id[%u] specified", object_id);
        return nullptr;
    }
    return da_check_dangling(self->objects().object(object_id, true), object_id);
}

CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, pcstr name)
{
    VERIFY(self);

    for (CALifeObjectRegistry::OBJECT_REGISTRY::const_iterator it = self->objects().objects().begin();
         it != self->objects().objects().end(); ++it)
    {
        CSE_ALifeDynamicObject* obj = it->second;
        if (xr_strcmp(obj->name_replace(), name) == 0)
            return (it->second);
    }

    return nullptr;
}

CSE_ALifeDynamicObject* alife_object(const CALifeSimulator* self, ALife::_OBJECT_ID id, bool no_assert)
{
    VERIFY(self);
    return da_check_dangling(self->objects().object(id, no_assert), id);
}

CSE_ALifeDynamicObject* alife_story_object(const CALifeSimulator* self, ALife::_STORY_ID id)
{
    return (self->story_objects().object(id, true));
}

template <typename _id_type>
void generate_story_ids(STORY_PAIRS& result, _id_type INVALID_ID, LPCSTR section_name, LPCSTR INVALID_ID_STRING,
    LPCSTR invalid_id_description, LPCSTR invalid_id_redefinition, LPCSTR duplicated_id_description)
{
    result.clear();

    const CInifile* Ini = pGameIni;

    LPCSTR N, V;
    u32 k = 0;
    R_ASSERT(Ini->section_exist(section_name));

    result.reserve(Ini->line_count(section_name) + 1);
    while (Ini->r_line(section_name, k, &N, &V))
    {
        const shared_str& temp = Ini->r_string_wb(section_name, N);

        R_ASSERT3(!strchr(temp.c_str(), ' '), invalid_id_description, temp.c_str());
        R_ASSERT2(xr_strcmp(temp.c_str(), INVALID_ID_STRING), invalid_id_redefinition);

        for (const auto& story : result)
            R_ASSERT3(story.first != temp, duplicated_id_description, temp.c_str());

        result.emplace_back(temp.c_str(), atoi(N));
        ++k;
    }

    result.emplace_back(INVALID_ID_STRING, INVALID_ID);
}

void kill_entity0(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, const GameGraph::_GRAPH_ID& game_vertex_id)
{
    alife->kill_entity(monster, game_vertex_id, 0);
}

void kill_entity1(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster)
{
    alife->kill_entity(monster, monster->m_tGraphID, 0);
}

void add_in_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->add_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeIn);
}

void add_out_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->add_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeOut);
}

void remove_in_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->remove_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeIn);
}

void remove_out_restriction(CALifeSimulator* alife, CSE_ALifeMonsterAbstract* monster, ALife::_OBJECT_ID id)
{
    alife->remove_restriction(monster->ID, id, RestrictionSpace::eRestrictorTypeOut);
}

u32 get_level_id(CALifeSimulator* self) { return (self->graph().level().level_id()); }
CSE_ALifeDynamicObject* CALifeSimulator__create(CALifeSimulator* self, ALife::_SPAWN_ID spawn_id)
{
    const CALifeSpawnRegistry::SPAWN_GRAPH::CVertex* vertex = ai().alife().spawns().spawns().vertex(spawn_id);
    // [DA_PORT] Reachable from any script. THROW2 does fire in Release, but it fires by
    // throwing, nothing on the MinGW path catches it, and the reason it assembled is never
    // printed - the player just gets "Unexpected application termination". Report the bad
    // id and hand nil back to Lua, which then fails at the calling script line.
    if (!vertex)
    {
        Msg("! [DA] alife():create - invalid spawn id [%d]", spawn_id);
        return nullptr;
    }

    CSE_ALifeDynamicObject* spawn = smart_cast<CSE_ALifeDynamicObject*>(&vertex->data()->object());
    if (!spawn)
    {
        Msg("! [DA] alife():create - spawn id [%d] is not a dynamic object", spawn_id);
        return nullptr;
    }

    CSE_ALifeDynamicObject* object;
    self->create(object, spawn, spawn_id);

    return (object);
}

CSE_Abstract* CALifeSimulator__spawn_item(CALifeSimulator* self, LPCSTR section, const Fvector& position,
    u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id)
{
    THROW(self);
    return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, ALife::_OBJECT_ID(-1)));
}

CSE_Abstract* CALifeSimulator__spawn_item2(CALifeSimulator* self, LPCSTR section, const Fvector& position,
    u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent)
{
    if (id_parent == ALife::_OBJECT_ID(-1))
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    CSE_ALifeDynamicObject* object = ai().alife().objects().object(id_parent, true);
    if (!object)
    {
        Msg("! invalid parent id [%d] specified", id_parent);
        return (0);
    }

    if (!object->m_bOnline)
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    NET_Packet packet;
    packet.w_begin(M_SPAWN);
    packet.w_stringZ(section);

    CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);
    if (!item) // [DA_PORT] spawn_item now rejects bad section/vertex instead of crashing
        return nullptr;
    item->Spawn_Write(packet, FALSE);
    self->server().FreeID(item->ID, 0);
    F_entity_Destroy(item);

    ClientID clientID;
    clientID.set(0xffff);

    u16 dummy;
    packet.r_begin(dummy);
    VERIFY(dummy == M_SPAWN);
    return (self->server().Process_spawn(packet, clientID));
}

//Alundaio: Allows to call alife():register(se_obj) manually afterward so that packet editing can be done safely when spawning object with a parent
CSE_Abstract* CALifeSimulator__spawn_item3(CALifeSimulator* self, pcstr section, const Fvector& position,
                                           u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id,
                                           ALife::_OBJECT_ID id_parent, bool reg = true)
{
    if (reg)
        return CALifeSimulator__spawn_item2(self, section, position, level_vertex_id, game_vertex_id, id_parent);

    if (id_parent == ALife::_OBJECT_ID(-1))
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    const auto object = ai().alife().objects().object(id_parent, true);
    if (!object)
    {
        GEnv.ScriptEngine->script_log(LuaMessageType::Error, "! invalid parent id [%u] specified", id_parent);
        return nullptr;
    }

    if (!object->m_bOnline)
        return (self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent));

    CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);

    return item; // may be nullptr now (bad section/vertex) - Lua gets nil instead of a crash
}

CSE_Abstract* CALifeSimulator__spawn_ammo(CALifeSimulator* self, LPCSTR section, const Fvector& position,
    u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent, int ammo_to_spawn)
{
    //	if (id_parent == ALife::_OBJECT_ID(-1))
    //		return (self->spawn_item(section,position,level_vertex_id,game_vertex_id,id_parent));
    CSE_ALifeDynamicObject* object = 0;
    if (id_parent != ALife::_OBJECT_ID(-1))
    {
        object = ai().alife().objects().object(id_parent, true);
        if (!object)
        {
            GEnv.ScriptEngine->script_log(LuaMessageType::Error, "! invalid parent id [%u] specified", id_parent);
            return (0);
        }
    }

    if (!object || !object->m_bOnline)
    {
        CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent);
        if (!item) // [DA_PORT] bad section/vertex rejected inside - give Lua nil, don't THROW
            return (0);

        CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(item);
        THROW(ammo);
        THROW(ammo->m_boxSize >= ammo_to_spawn);
        ammo->a_elapsed = (u16)ammo_to_spawn;

        return (item);
    }

    NET_Packet packet;
    packet.w_begin(M_SPAWN);
    packet.w_stringZ(section);

    CSE_Abstract* item = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);
    if (!item) // [DA_PORT] bad section/vertex rejected inside - give Lua nil, don't THROW
        return (0);

    CSE_ALifeItemAmmo* ammo = smart_cast<CSE_ALifeItemAmmo*>(item);
    THROW(ammo);
    THROW(ammo->m_boxSize >= ammo_to_spawn);
    ammo->a_elapsed = (u16)ammo_to_spawn;

    item->Spawn_Write(packet, FALSE);
    self->server().FreeID(item->ID, 0);
    F_entity_Destroy(item);

    ClientID clientID;
    clientID.set(0xffff);

    u16 dummy;
    packet.r_begin(dummy);
    VERIFY(dummy == M_SPAWN);
    return (self->server().Process_spawn(packet, clientID));
}

ALife::_SPAWN_ID CALifeSimulator__spawn_id(CALifeSimulator* self, ALife::_SPAWN_STORY_ID spawn_story_id)
{
    return (((const CALifeSimulator*)self)->spawns().spawn_id(spawn_story_id));
}

void CALifeSimulator__release(CALifeSimulator* self, CSE_Abstract* object, bool)
{
    VERIFY(self);
    //	self->release						(object,true);

    // [DA_PORT] Здесь стоял `THROW(object);` и строкой ниже — недостижимый `if (!object) return;`.
    // Две проверки, которые вместе не проверяют ничего: THROW бросает, а ловить некому, и до второй
    // строки управление не доходит никогда.
    //
    // Ноль сюда приходит законно: скрипты мода удаляют объекты пачками (вороны, отряды), и движок к
    // этому моменту мог убрать тот же объект сам. Это норма пути, а не ошибка вызывающего.
    if (!object)
    {
        extern ENGINE_API int ps_da_alife_release_log;
        if (ps_da_alife_release_log)
            Msg("~ [DA_REL] скрипт просит освободить НОЛЬ — объект уже убран движком, пропускаем");
        return;
    }

    // [DA_PORT] Отметка пути: сюда приходит ТОЛЬКО скрипт. Движок зовёт release напрямую, минуя эту
    // обёртку, и по следу их иначе не различить — а разница решает, где искать обрыв.
    {
        extern ENGINE_API int ps_da_alife_release_log;
        if (ps_da_alife_release_log)
            Msg("~ [DA_REL] СКРИПТ просит освободить id [%d] (онлайн: %s)", object->ID,
                smart_cast<CSE_ALifeObject*>(object) && smart_cast<CSE_ALifeObject*>(object)->m_bOnline
                    ? "да" : "нет");
    }

    CSE_ALifeObject* alife_object = smart_cast<CSE_ALifeObject*>(object);

    // [DA_PORT] Здесь стоял второй `THROW(alife_object);`, а следующей строкой — разыменование без
    // проверки. То есть при неудачном приведении выбор был между броском, который никто не ловит, и
    // обращением по нулю. Приведение не обязано удаваться: серверный объект может не быть
    // CSE_ALifeObject, и это не ошибка вызывающего.
    if (!alife_object)
    {
        extern ENGINE_API int ps_da_alife_release_log;
        if (ps_da_alife_release_log)
            Msg("~ [DA_REL] id [%d] не является объектом ALife — освобождать нечего, пропускаем",
                object->ID);
        return;
    }

    if (!alife_object->m_bOnline)
    {
        // [DA_PORT] Номер запоминаем ДО удаления: после него читать поля объекта уже нельзя.
        const u16 da_released_id = object->ID;

        self->release(object, true);

        extern ENGINE_API int ps_da_alife_release_log;
        if (ps_da_alife_release_log)
            Msg("~ [DA_REL] возврат в скрипт после id [%d]", da_released_id);
        return;
    }

    // [DA_PORT] Онлайн-объект: пакет шлём только если есть КОМУ его исполнить.
    //
    // Приём взят из второго порта (Dead Air Refined, alife_simulator_script.cpp). Ниже —
    // единственное, что делает скриптовый release с онлайновым объектом: посылает GE_DESTROY и
    // уходит. Если клиентской части уже нет или она помечена на уничтожение, пакет исполнять некому:
    // на сервере он обернётся «ge_destroy: not found on server», а номер при этом всё равно уйдёт в
    // общий пул и достанется новому объекту. Так и получаются висячие записи в реестрах, из-за
    // которых потом падает планировщик ALife.
    //
    // Корень этим не лечится — скриптовый release для онлайн-объекта по-прежнему ничего не
    // уничтожает, — но мусора становится заметно меньше: повторные и запоздалые вызовы отсекаются
    // здесь, а не превращаются в пакет.
    IGameObject* online_object = Level().Objects.net_Find(object->ID);
    if (!online_object || online_object->getDestroy())
    {
        extern ENGINE_API int ps_da_alife_release_log;
        if (ps_da_alife_release_log)
            Msg("~ [DA_REL] id [%d]: клиентской части %s — GE_DESTROY не шлём", object->ID,
                online_object ? "уже уничтожается" : "нет");
        return;
    }

    // awful hack, for stohe only
    NET_Packet packet;
    packet.w_begin(M_EVENT);
    packet.w_u32(Level().timeServer());
    packet.w_u16(GE_DESTROY);
    packet.w_u16(object->ID);
    Level().Send(packet, net_flags(TRUE, TRUE));
}

LPCSTR get_level_name(const CALifeSimulator* self, int level_id)
{
    LPCSTR result = ai().game_graph().header().level((GameGraph::_LEVEL_ID)level_id).name().c_str();
    return (result);
}

CSE_ALifeCreatureActor* get_actor(const CALifeSimulator* self)
{
    THROW(self);
    return (self->graph().actor());
}

KNOWN_INFO_VECTOR* registry(const CALifeSimulator* self, const ALife::_OBJECT_ID& id)
{
    THROW(self);
    return (self->registry(info_portions).object(id, true));
}

bool has_info(const CALifeSimulator* self, const ALife::_OBJECT_ID& id, LPCSTR info_id)
{
    const KNOWN_INFO_VECTOR* known_info = registry(self, id);
    if (!known_info)
        return (false);

    if (std::find_if(known_info->begin(), known_info->end(), CFindByIDPred(info_id)) == known_info->end())
        return (false);

    return (true);
}

bool dont_has_info(const CALifeSimulator* self, const ALife::_OBJECT_ID& id, LPCSTR info_id)
{
    THROW(self);
    // absurdly, but only because of scriptwriters needs
    return (!has_info(self, id, info_id));
}

//Alundaio: teleport object
void teleport_object(CALifeSimulator* alife, ALife::_OBJECT_ID id, GameGraph::_GRAPH_ID game_vertex_id, u32 level_vertex_id, const Fvector& position)
{
    alife->teleport_object(id, game_vertex_id, level_vertex_id, position);
}

void IterateInfo(const CALifeSimulator* alife, const ALife::_OBJECT_ID& id, const luabind::functor<void>& functor)
{
    const auto known_info = registry(alife, id);
    if (!known_info)
        return;

    for (const auto& it : *known_info)
        functor(id, it.info_id);
}

CSE_Abstract* reprocess_spawn(CALifeSimulator* self, CSE_Abstract* object)
{
    NET_Packet packet;
    packet.w_begin(M_SPAWN);
    packet.w_stringZ(object->s_name);

    object->Spawn_Write(packet, FALSE);
    self->server().FreeID(object->ID, 0);
    F_entity_Destroy(object);

    ClientID clientID;
    clientID.set(0xffff);

    u16 dummy;
    packet.r_begin(dummy);

    return self->server().Process_spawn(packet, clientID);
}

CSE_Abstract* try_to_clone_object(CALifeSimulator* self, CSE_Abstract* object, pcstr section, const Fvector& position,
                                  u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent,
                                  bool bRegister = true)
{
    CSE_ALifeItemWeaponMagazined* wpnmag = smart_cast<CSE_ALifeItemWeaponMagazined*>(object);
    if (!wpnmag)
        return nullptr;

    CSE_Abstract* absClone = self->spawn_item(section, position, level_vertex_id, game_vertex_id, id_parent, false);
    if (!absClone)
        return nullptr;

    CSE_ALifeItemWeaponMagazined * clone = smart_cast<CSE_ALifeItemWeaponMagazined*>(absClone);
    if (!clone)
    {
        // [DA_PORT] Заготовку за собой убираем (приём из Dead Air Refined). Здесь стоял голый
        // return: объект уже создан и НОМЕР ЗА НИМ ЗАНЯТ, а уйти он должен был в клон, которого не
        // получилось. Утечка тихая — ни ошибки, ни следа, только номер, который больше никому не
        // достанется... до тех пор, пока не достанется: это ровно тот механизм, из которого у нас
        // растут переиспользованные номера и висячие записи в реестрах ALife.
        self->server().FreeID(absClone->ID, 0);
        F_entity_Destroy(absClone);
        return nullptr;
    }

    clone->wpn_flags = wpnmag->wpn_flags;
    clone->m_addon_flags = wpnmag->m_addon_flags;
    clone->m_fCondition = wpnmag->m_fCondition;
    clone->ammo_type = wpnmag->ammo_type;
    clone->m_upgrades = wpnmag->m_upgrades;
    clone->a_elapsed = wpnmag->a_elapsed;
    clone->a_current = wpnmag->a_current;
    // [DA_PORT] Маска поломок переносится вместе с износом.
    //
    // Её здесь не было, и это давало жалобу «снял обвес -- все поломки исчезли, остался только
    // процент изношенности». Формулировка точная: m_fCondition (износ) в списке выше есть, а
    // condition_type (поломки) не было.
    //
    // Почему это вообще всплыло на снятии обвеса: в моде обвес не снимается, а ПОДМЕНЯЕТСЯ ствол.
    // dxr_scopes.script зовёт clone_weapon(старый, базовая_секция), затем release старого --
    // wpn_scar_ac10632 id[25724] превращается в wpn_scar id[31252]. Всё, чего клон не скопировал,
    // умирает вместе со старым объектом. То же и при установке прицела, в обратную сторону.
    //
    // Путь искался четырьмя приборами: изменение маски внутри объекта, запись в серверный объект,
    // чтение подсказкой и само снятие. Первые три молчали именно потому, что объект каждый раз
    // НОВЫЙ: у него и старое, и новое значение -- ноль, менять нечего.
    clone->condition_type = wpnmag->condition_type;

    return bRegister ? reprocess_spawn(self, absClone) : absClone;
}
CSE_Abstract* try_to_clone_object(CALifeSimulator* self, CSE_Abstract* object, pcstr section, const Fvector& position,
    u32 level_vertex_id, GameGraph::_GRAPH_ID game_vertex_id, ALife::_OBJECT_ID id_parent)
{
    return try_to_clone_object(self, object, section, position, level_vertex_id, game_vertex_id, id_parent, true);
}

void set_objects_per_update(CALifeSimulator* self, u32 count)
{
    self->objects_per_update(count);
}

void set_process_time(CALifeSimulator* self, int micro)
{
    self->set_process_time(micro);
}

xr_vector<u16>& get_children(const CALifeSimulator* self, CSE_Abstract* object)
{
    VERIFY(self);
    return object->children;
}
//-Alundaio

void iterate_objects(const CALifeSimulator* self, luabind::functor<bool> functor)
{
    THROW(self);
    for (const auto& it : self->objects().objects())
    {
        CSE_ALifeDynamicObject* obj = it.second;
        if (functor(obj))
            return;
    }
}

void set_start_position(Fvector& pos)
{
    g_start_position = pos;
}
void set_start_game_vertex_id(int id)
{
    g_start_game_vertex_id = id;
}

void CALifeSimulator::script_register(lua_State* luaState)
{
    using namespace luabind;
    using namespace luabind::policy;

    s_da_alife.reset();

    module(luaState)
    [
        class_<CALifeSimulator>("alife_simulator")
            .def("valid_object_id", &valid_object_id)
            .def("level_id", &get_level_id)
            .def("level_name", &get_level_name)
            .def("object",
                (CSE_ALifeDynamicObject * (*)(const CALifeSimulator*, ALife::_OBJECT_ID))(alife_object))
            .def("object", (CSE_ALifeDynamicObject * (*)(const CALifeSimulator*, ALife::_OBJECT_ID, bool))(
                               alife_object))
            .def("object", (CSE_ALifeDynamicObject *(*) (const CALifeSimulator*, pcstr))(alife_object))
            .def("story_object", (CSE_ALifeDynamicObject * (*)(const CALifeSimulator*, ALife::_STORY_ID))(
                                     alife_story_object))
            .def("set_switch_online",
                (void (CALifeSimulator::*)(ALife::_OBJECT_ID, bool))(&CALifeSimulator::set_switch_online))
            .def("set_switch_offline",
                (void (CALifeSimulator::*)(ALife::_OBJECT_ID, bool))(&CALifeSimulator::set_switch_offline))
            .def("set_interactive",
                (void (CALifeSimulator::*)(ALife::_OBJECT_ID, bool))(&CALifeSimulator::set_interactive))
            .def("kill_entity", &CALifeSimulator::kill_entity)

            .def("kill_entity", &kill_entity0)
            .def("kill_entity", &kill_entity1)
            .def("add_in_restriction", &add_in_restriction)
            .def("add_out_restriction", &add_out_restriction)
            .def("remove_in_restriction", &remove_in_restriction)
            .def("remove_out_restriction", &remove_out_restriction)
            .def("remove_all_restrictions", &CALifeSimulator::remove_all_restrictions)
            .def("create", &CALifeSimulator__create)
            .def("create", &CALifeSimulator__spawn_item2)
            .def("create", &CALifeSimulator__spawn_item)
            .def("create", &CALifeSimulator__spawn_item3)
            .def("create_ammo", &CALifeSimulator__spawn_ammo)
            .def("release", &CALifeSimulator__release)
            .def("spawn_id", &CALifeSimulator__spawn_id)
            .def("actor", &get_actor)
            .def("has_info", &has_info)
            .def("dont_has_info", &dont_has_info)
            .def("switch_distance", (float (CALifeSimulator::*)())(&CALifeSimulator::switch_distance))
            .def("switch_distance", (void (CALifeSimulator::*)(float))
               (&CALifeSimulator::set_switch_distance))
            .def("set_switch_distance", (void (CALifeSimulator::*)(float))
               (&CALifeSimulator::set_switch_distance)) //Alundaio: renamed to set_switch_distance from switch_distance
            //Alundaio: extend alife simulator exports
            .def("teleport_object", &CALifeSimulator::teleport_object)
            .def("iterate_objects", &iterate_objects)
            .def("iterate_info", &IterateInfo)
            .def("clone_weapon", (CSE_Abstract* (*)(CALifeSimulator*, CSE_Abstract*, pcstr, const Fvector&, u32,
                GameGraph::_GRAPH_ID, ALife::_OBJECT_ID))&try_to_clone_object)
            .def("clone_weapon", (CSE_Abstract* (*)(CALifeSimulator*, CSE_Abstract*, pcstr, const Fvector&, u32,
                GameGraph::_GRAPH_ID, ALife::_OBJECT_ID, bool))&try_to_clone_object)
            .def("register", &reprocess_spawn)
            .def("set_objects_per_update", &set_objects_per_update)
            .def("set_process_time", &CALifeSimulator::set_process_time)
            .def("get_children", &get_children, return_stl_iterator()),
            //Alundaio: END

        def("alife", &alife),
        def("set_start_position", &set_start_position),
        def("set_start_game_vertex_id", &set_start_game_vertex_id)
    ];

    // DA: перекрываем биндинг luabind своей функцией с кэшем обёртки (см. da_lua_alife выше)
    lua_pushcfunction(luaState, &da_lua_alife);
    lua_setglobal(luaState, "alife");

    class CALifeSimulatorExporter1
    {
    };

    {
        if (story_ids.empty())
            generate_story_ids(story_ids, INVALID_STORY_ID, "story_ids", "INVALID_STORY_ID",
                "Invalid story id description (contains spaces)!", "INVALID_STORY_ID redifinition!",
                "Duplicated story id description!");

        luabind::class_<CALifeSimulatorExporter1> instance("story_ids");

        STORY_PAIRS::const_iterator I = story_ids.begin();
        STORY_PAIRS::const_iterator E = story_ids.end();
        for (; I != E; ++I)
            instance.enum_("_story_ids")[luabind::value((*I).first.c_str(), (*I).second)];

        luabind::module(luaState)[instance];
    }

    class CALifeSimulatorExporter2
    {
    };

    {
        if (spawn_story_ids.empty())
            generate_story_ids(spawn_story_ids, INVALID_SPAWN_STORY_ID, "spawn_story_ids", "INVALID_SPAWN_STORY_ID",
                "Invalid spawn story id description (contains spaces)!", "INVALID_SPAWN_STORY_ID redifinition!",
                "Duplicated spawn story id description!");

        luabind::class_<CALifeSimulatorExporter2> instance("spawn_story_ids");

        SPAWN_STORY_PAIRS::const_iterator I = spawn_story_ids.begin();
        SPAWN_STORY_PAIRS::const_iterator E = spawn_story_ids.end();
        for (; I != E; ++I)
            instance.enum_("_spawn_story_ids")[luabind::value((*I).first.c_str(), (*I).second)];

        luabind::module(luaState)[instance];
    }
}

#if 0 // def DEBUG
struct dummy {
    int count;
    lua_State* state;
    int ref;
};

void CALifeSimulator::validate			()
{
	typedef CALifeSpawnRegistry::SPAWN_GRAPH::const_vertex_iterator	const_vertex_iterator;
	const_vertex_iterator		I = spawns().spawns().vertices().begin();
	const_vertex_iterator		E = spawns().spawns().vertices().end();
	for ( ; I != E; ++I) {
		luabind::wrap_base		*base = smart_cast<luabind::wrap_base*>(&(*I).second->data()->object());
		if (!base)
			continue;

		if (!base->m_self.m_impl)
			continue;

		dummy					*_dummy = (dummy*)((void*)base->m_self.m_impl);
		lua_State				**_state = &_dummy->state;
		VERIFY2					(
			base->m_self.state(),
			make_string(
				"0x%08x name[%s] name_replace[%s]",
				*(int*)&_state,
				(*I).second->data()->object().name(),
				(*I).second->data()->object().name_replace()
			)
		);
	}
}
#endif // DEBUG
