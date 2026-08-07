////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object_registry.cpp
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife object registry
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "alife_object_registry.h"
#include "ai_debug.h"
#include "xrServerEntities/xrMessages.h"

CALifeObjectRegistry::CALifeObjectRegistry(LPCSTR section) {}
CALifeObjectRegistry::~CALifeObjectRegistry()
{
    OBJECT_REGISTRY::iterator const B = m_objects.begin();
    OBJECT_REGISTRY::iterator I = B;
    OBJECT_REGISTRY::iterator const E = m_objects.end();
    for (; I != E; ++I)
        (*I).second->on_unregister();

    for (I = B; I != E; ++I)
        xr_delete((*I).second);
}

void CALifeObjectRegistry::save(IWriter& memory_stream, CSE_ALifeDynamicObject* object, u32& object_count)
{
    ++object_count;

    NET_Packet tNetPacket;
    // Spawn
    object->Spawn_Write(tNetPacket, TRUE);
    memory_stream.w_u16(u16(tNetPacket.B.count));
    memory_stream.w(tNetPacket.B.data, tNetPacket.B.count);

    // Update
    tNetPacket.w_begin(M_UPDATE);
    object->UPDATE_Write(tNetPacket);

    memory_stream.w_u16(u16(tNetPacket.B.count));
    memory_stream.w(tNetPacket.B.data, tNetPacket.B.count);

    ALife::OBJECT_VECTOR::const_iterator I = object->children.begin();
    ALife::OBJECT_VECTOR::const_iterator E = object->children.end();
    for (; I != E; ++I)
    {
        CSE_ALifeDynamicObject* child = this->object(*I, true);
        if (!child)
            continue;

        if (!child->can_save())
            continue;

        save(memory_stream, child, object_count);
    }
}

void CALifeObjectRegistry::save(IWriter& memory_stream)
{
    Msg("* Saving objects...");
    memory_stream.open_chunk(OBJECT_CHUNK_DATA);

    u32 position = memory_stream.tell();
    memory_stream.w_u32(u32(-1));

    u32 object_count = 0;
    OBJECT_REGISTRY::iterator I = m_objects.begin();
    OBJECT_REGISTRY::iterator E = m_objects.end();
    for (; I != E; ++I)
    {
        if (!(*I).second->can_save())
            continue;

        if ((*I).second->redundant())
            continue;

        if ((*I).second->ID_Parent != 0xffff)
            continue;

        save(memory_stream, (*I).second, object_count);
    }

    u32 last_position = memory_stream.tell();
    memory_stream.seek(position);
    memory_stream.w_u32(object_count);
    memory_stream.seek(last_position);

    memory_stream.close_chunk();

    Msg("* %d objects are successfully saved", object_count);
}

CSE_ALifeDynamicObject* CALifeObjectRegistry::get_object(IReader& file_stream)
{
    NET_Packet tNetPacket;
    u16 u_id;
    // Spawn
    tNetPacket.B.count = file_stream.r_u16();
    file_stream.r(tNetPacket.B.data, tNetPacket.B.count);
    tNetPacket.r_begin(u_id);
    R_ASSERT2(M_SPAWN == u_id, "Invalid packet ID (!= M_SPAWN)");

    string64 s_name;
    tNetPacket.r_stringZ(s_name);
#ifdef DEBUG
    if (psAI_Flags.test(aiALife))
    {
        Msg("Loading object %s [%d]b", s_name, tNetPacket.B.count);
    }
#endif
    // create entity
    CSE_Abstract* tpSE_Abstract = F_entity_Create(s_name);
    R_ASSERT2(tpSE_Abstract, "Can't create entity.");
    CSE_ALifeDynamicObject* tpALifeDynamicObject = smart_cast<CSE_ALifeDynamicObject*>(tpSE_Abstract);
    R_ASSERT2(tpALifeDynamicObject, "Non-ALife object in the saved game!");
    tpALifeDynamicObject->Spawn_Read(tNetPacket);

    // Update
    tNetPacket.B.count = file_stream.r_u16();
    file_stream.r(tNetPacket.B.data, tNetPacket.B.count);
    tNetPacket.r_begin(u_id);
    R_ASSERT2(M_UPDATE == u_id, "Invalid packet ID (!= M_UPDATE)");
    tpALifeDynamicObject->UPDATE_Read(tNetPacket);

    return (tpALifeDynamicObject);
}

void CALifeObjectRegistry::load(IReader& file_stream)
{
    Msg("* Loading objects...");
    R_ASSERT2(file_stream.find_chunk(OBJECT_CHUNK_DATA), "Can't find chunk OBJECT_CHUNK_DATA!");

    m_objects.clear();

    u32 count = file_stream.r_u32();
    CSE_ALifeDynamicObject** objects = (CSE_ALifeDynamicObject**)xr_alloca(count * sizeof(CSE_ALifeDynamicObject*));

    CSE_ALifeDynamicObject** I = objects;
    CSE_ALifeDynamicObject** E = objects + count;
    for (; I != E; ++I)
    {
        *I = get_object(file_stream);
        add(*I);
    }

    Msg("* %d objects are successfully loaded", count);
}

// [DA_PORT] ---- Санитар реестра ALife -------------------------------------------------------------
//
// Метка жизни (см. xrServer_Object_Base.h) не даёт УПАСТЬ на висячей записи, но саму запись не
// убирает: она остаётся в реестре и будет попадаться снова и снова. Санитар — вторая половина
// решения: он проходит реестр, выкидывает записи, чьи объекты уже разрушены, и рассказывает об
// этом поимённо.
//
// Зачем рассказывать. Метка лечит СИМПТОМ. Причина — тот, кто удаляет объект, не сняв его с
// реестра, — остаётся жить, и найти его можно только по следу: номер, секция, как часто. Молчаливая
// защита неотличима от неработающей, а по этим строкам видно и то, что защита сработала, и то, где
// искать источник.
namespace
{
u32 g_da_stale_hits = 0;   // сколько раз реестр отдавал бы мёртвый объект
u32 g_da_stale_swept = 0;  // сколько записей вычистил санитар
} // namespace

// Вызывается из CALifeObjectRegistry::object при попытке выдать разрушенный объект.
void da_registry_report_stale(u16 id, const void* object)
{
    ++g_da_stale_hits;

    // Первые случаи печатаем все, дальше — редко: если источник живой, строк будет много, и
    // заливать ими лог смысла нет, число в итоговом отчёте скажет больше.
    if (g_da_stale_hits <= 10 || (g_da_stale_hits % 200) == 0)
        Msg("~ [DA_SANITAR] реестр придержал МЁРТВЫЙ объект: номер [%d], адрес [%p], случай #%u", id,
            object, g_da_stale_hits);
}

// Проход по реестру: выкинуть записи с погашенной меткой.
//
// Ходим по копии ключей: remove правит ту самую карту, по которой идём, а обход с изменением —
// отдельный способ уронить игру, который мы уже проходили в другом месте.
// Проход по реестру: выкинуть записи с погашенной меткой. Метод класса — снаружи карта закрыта.
//
// Ходим по копии ключей: remove правит ту самую карту, по которой идём, а обход с изменением —
// отдельный способ уронить игру, который мы уже проходили в другом месте.
void CALifeObjectRegistry::da_sanitize()
{
    xr_vector<ALife::_OBJECT_ID> dead;

    for (const auto& pair : m_objects)
    {
        const CSE_ALifeDynamicObject* object = pair.second;
        if (!object || !object->da_object_alive())
            dead.push_back(pair.first);
    }

    if (dead.empty())
        return;

    for (const ALife::_OBJECT_ID id : dead)
    {
        // Секцию НЕ читаем: объект мёртв, обращение к его полям недостоверно. Номера хватает.
        remove(id, true);
        ++g_da_stale_swept;
    }

    Msg("! [DA_SANITAR] вычищено мёртвых записей: %u (всего за сессию %u, придержано выдач %u)",
        u32(dead.size()), g_da_stale_swept, g_da_stale_hits);
}

// Итог по требованию — команда da_alife_sanitize (см. console_commands.cpp).
void CALifeObjectRegistry::da_sanitize_report()
{
    Msg("~ [DA_SANITAR] выдач придержано: %u, записей вычищено: %u", g_da_stale_hits,
        g_da_stale_swept);
    da_sanitize();
}
