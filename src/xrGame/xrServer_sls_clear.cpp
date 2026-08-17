#include "StdAfx.h"
#include "game_sv_single.h"
#include "alife_simulator.h"
#include "xrServer_Objects.h"
#include "xrServer.h"
#include "xrMessages.h"
#include "ai_space.h"
#include "xrNetServer/NET_Messages.h"

void xrServer::Perform_destroy(CSE_Abstract* object, u32 mode)
{
    R_ASSERT(object);
    R_ASSERT(object->ID_Parent == 0xffff);

#ifdef DEBUG
#ifdef SLOW_VERIFY_ENTITIES
    verify_entities();
#endif
#endif

    while (!object->children.empty())
    {
        const u16 child_id = object->children.back();
        CSE_Abstract* child = game->get_entity_from_eid(child_id);

        // [DA_PORT] Пропавший потомок больше не валит игру (задача #74).
        //
        // ЧТО БЫЛО. Здесь стоял живой R_ASSERT2 — не VERIFY, а именно R_ASSERT2, то есть он
        // работает и в релизе. Если объект держит в списке номер потомка, которого уже нет в карте
        // сущностей, уборка обрывалась фаталом «child registered but not found». Происходит это на
        // ВЫХОДЕ из игры, когда пользователю уже нечего терять, но выглядит как вылет.
        //
        // Найдено прогоном da_grenade_test: выдача предметов родителем-актёром плюс расстановка
        // сталкеров, затем выход — «child registered but not found [55588]».
        //
        // ПОЧЕМУ ПРОПУСК, А НЕ ФАТАЛ. Мы разбираем мир на части, и уже уничтоженный потомок — это в
        // точности то состояние, к которому мы и стремимся. Разрушать процесс из-за того, что
        // работа частично сделана заранее, смысла нет: остальные потомки от этого не уберутся.
        //
        // ⛔ Запись ОБЯЗАТЕЛЬНО снимается. Цикл крутится, пока список не опустеет, а вычёркивает из
        // него обычно Perform_reject — для отсутствующего потомка он не отработает, и простое
        // «пропустить» дало бы вечный цикл вместо вылета. Это было бы хуже: вылет хотя бы виден.
        if (!child)
        {
            static u32 reported = 0;
            if (reported < 8)
            {
                ++reported;
                // ⚠️ Номер владельца печатается ВСЕГДА, а имя — только если оно есть. У актёра
                // name_replace() пуст, и первая версия сообщения выглядела как «числится за []»:
                // сказать, что запись снята, и не сказать чья — значит отдать половину подсказки.
                pcstr owner = object->name_replace();
                if (!owner || !*owner)
                    owner = object->name();

                Msg("! [DA] уборка: потомок [%u] числится за [%s] (номер %u), но уже не существует "
                    "— запись снята%s",
                    u32(child_id), (owner && *owner) ? owner : "без имени", u32(object->ID),
                    (reported == 8) ? " (дальнейшие сообщения подавлены)" : "");
            }
            object->children.pop_back();
            continue;
        }
        //		Msg					("SLS-CLEAR : REJECT  [%s][%s] FROM
        //[%s][%s]",child->name(),child->name_replace(),object->name(),object->name_replace());
        Perform_reject(child, object, 2 * NET_Latency);
#ifdef DEBUG
#ifdef SLOW_VERIFY_ENTITIES
        verify_entities();
#endif
#endif
        Perform_destroy(child, mode);
    }

    //	Msg						("SLS-CLEAR : DESTROY [%s][%s]",object->name(),object->name_replace());
    u16 object_id = object->ID;
    entity_Destroy(object);

#ifdef DEBUG
#ifdef SLOW_VERIFY_ENTITIES
    verify_entities();
#endif
#endif

    NET_Packet P;
    P.w_begin(M_EVENT);
    P.w_u32(Device.dwTimeGlobal - 2 * NET_Latency);
    P.w_u16(GE_DESTROY);
    P.w_u16(object_id);
    SendBroadcast(BroadcastCID, P, mode);
}

void xrServer::SLS_Clear()
{
#if 0
	Msg									("SLS-CLEAR : %d objects");
	xrS_entities::const_iterator		I = entities.begin();
	xrS_entities::const_iterator		E = entities.end();
	for ( ; I != E; ++I)
		Msg								("entity to destroy : [%d][%s][%s]",(*I).second->ID,(*I).second->name(),(*I).second->name_replace());
#endif

    u32 mode = net_flags(TRUE, TRUE);
    while (!entities.empty())
    {
        bool found = false;
        xrS_entities::const_iterator I = entities.begin();
        xrS_entities::const_iterator E = entities.end();
        for (; I != E; ++I)
        {
            if ((*I).second->ID_Parent != 0xffff)
                continue;
            found = true;
            Perform_destroy((*I).second, mode);
            break;
        }
        if (!found) // R_ASSERT(found);
        {
            I = entities.begin();
            E = entities.end();
            for (; I != E; ++I)
            {
                if (I->second)
                    Msg("! ERROR: can't destroy object [%d][%s] with parent [%d]", I->second->ID,
                        I->second->s_name.size() ? I->second->s_name.c_str() : "unknown", I->second->ID_Parent);
                else
                    Msg("! ERROR: can't destroy entity [%d][?] with parent[?]", I->first);
            }
            Msg("! ERROR: FATAL: can't delete all entities !");
            entities.clear();
        }
    }
}
