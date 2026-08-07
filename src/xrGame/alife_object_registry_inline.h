////////////////////////////////////////////////////////////////////////////
//	Module 		: alife_object_registry_штдшту.h
//	Created 	: 15.01.2003
//  Modified 	: 12.05.2004
//	Author		: Dmitriy Iassenev
//	Description : ALife object registry inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC void CALifeObjectRegistry::add(CSE_ALifeDynamicObject* object)
{
    const OBJECT_REGISTRY::const_iterator I = objects().find(object->ID);
    if (I != objects().end())
    {
        // [DA_PORT] Столкновение номеров: под этим номером в реестре УЖЕ кто-то лежит.
        //
        // Штатная реакция — THROW2 с текстом «The specified object is already presented», и она
        // никуда не годится по трём причинам сразу. Текст обманывает (THROW2 срабатывает при ЛОЖНОМ
        // условии, то есть сообщение означает обратное — там ДРУГОЙ объект). Ни номера, ни
        // участников в нём нет. И главное: он валит игру там, где её можно спасти.
        //
        // Спасать есть от чего. Занявший номер объект, как правило, УЖЕ МЁРТВ — память освобождена,
        // а из реестра его не вычеркнули. Проверено дорого: первая версия этой диагностики печатала
        // секцию и имя обеих сторон и падала на чтении имени, потому что читала по висячему
        // указателю. Поэтому здесь НЕ ВЫЗЫВАЕТСЯ ни один метод старого объекта — только номер и
        // адрес, оба безопасны.
        //
        // Откуда берётся мёртвая запись: xrServer::entity_Destroy возвращает номер в общий пул
        // ВСЕГДА (m_tID_Generator.vfFreeID), даже когда сам объект остаётся жить под управлением
        // ALife. Номер уходит новому объекту, а старая запись остаётся висеть.
        // Имя берём ТОЛЬКО у нового объекта: он жив и создан только что. У старого не вызываем
        // ничего — он, как правило, уже мёртв, и первая версия этой диагностики падала именно на
        // чтении его имени.
        Msg("! [DA_PORT] реестр ALife: номер [%d] занят, запись [%p] заменяется на [%p] — новый: %s [%s]",
            object->ID, (void*)(*I).second, (void*)object, object->name_replace(), object->s_name.c_str());

        // Запись заменяем ВСЕГДА, даже когда указатели совпали.
        //
        // Совпадение адреса не означает «тот же объект»: аллокатор отдаёт освобождённую память
        // следующему, кто попросит, и в логе это видно прямо — один адрес регистрируется под
        // номерами 33628, 33629, 33630 подряд. Пропустить такую регистрацию значило бы оставить
        // НОВЫЙ объект вне реестра, а это хуже устаревшей записи: его потом никто не найдёт.
        //
        // Если же это действительно повторная регистрация одного объекта, замена безвредна — на
        // место старой пары ляжет точно такая же.
        m_objects.erase(object->ID);
    }

    m_objects.insert(std::make_pair(object->ID, object));
}

IC void CALifeObjectRegistry::remove(const ALife::_OBJECT_ID& id, bool no_assert)
{
    OBJECT_REGISTRY::iterator I = m_objects.find(id);
    if (I == m_objects.end())
    {
        THROW2(no_assert, "The specified object hasn't been found in the Object Registry!");
        return;
    }

    m_objects.erase(I);
}

// [DA_PORT] Метка жизни проверяется ЗДЕСЬ, в единственной точке выдачи объекта из реестра.
//
// Через эту функцию проходят все обращения к объектам ALife — их около восьмидесяти по коду. Ставить
// проверку в каждой означало бы восемьдесят мест, где её однажды забудут; здесь она одна.
//
// Висячая запись в реестре — не выдумка: сегодня мы четырежды падали в
// CSE_ALifeOnlineOfflineGroup::commander_id() именно потому, что реестр честно отдал указатель на
// объект, чья память уже освобождена. Проверить это иначе нечем — см. разбор в xrServer_Object_Base.h.
IC CSE_ALifeDynamicObject* CALifeObjectRegistry::object(const ALife::_OBJECT_ID& id, bool no_assert) const
{
    START_PROFILE("ALife/objects::object")
    OBJECT_REGISTRY::const_iterator I = objects().find(id);

    if (objects().end() == I)
    {
#ifdef DEBUG
        if (!no_assert)
            Msg("There is no object with id %d!", id);
#endif
        THROW2(no_assert, "Specified object hasn't been found in the object registry!");
        return (0);
    }

    CSE_ALifeDynamicObject* object = (*I).second;

    // [DA_PORT] Запись есть, но объект по ней может быть уже разрушен — тогда её как бы нет.
    // Возвращаем ноль: вызывающие к нулю готовы (это штатный ответ «такого объекта нет»), а к
    // мусору не готов никто. Считаем такие случаи — молчаливая защита неотличима от неработающей.
    if (object && !object->da_object_alive())
    {
        extern void da_registry_report_stale(u16 id, const void* object);
        da_registry_report_stale(id, object);
        return (0);
    }

    return (object);
    STOP_PROFILE
}

IC const CALifeObjectRegistry::OBJECT_REGISTRY& CALifeObjectRegistry::objects() const { return (m_objects); }
IC CALifeObjectRegistry::OBJECT_REGISTRY& CALifeObjectRegistry::objects() { return (m_objects); }
