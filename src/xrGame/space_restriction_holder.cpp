////////////////////////////////////////////////////////////////////////////
//	Module 		: space_restriction_holder.cpp
//	Created 	: 17.08.2004
//  Modified 	: 27.08.2004
//	Author		: Dmitriy Iassenev
//	Description : Space restriction holder
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "space_restriction_holder.h"
#include "Common/object_broker.h"
#include "space_restrictor.h"
#include "space_restriction_bridge.h"
#include "space_restriction_shape.h"
#include "space_restriction_composition.h"
#include "restriction_space.h"

CSpaceRestrictionHolder::~CSpaceRestrictionHolder() { clear(); }
void CSpaceRestrictionHolder::clear()
{
    std::lock_guard lock(space_restriction_lock());

#ifndef XR_COMPILER_GCC // At least GCC call destructor of members at call parent destructor
    delete_data(m_restrictions);
#endif
    m_default_out_restrictions = "";
    m_default_in_restrictions = "";
}

shared_str CSpaceRestrictionHolder::normalize_string(shared_str space_restrictors)
{
    u32 n = xr_strlen(space_restrictors);
    if (!n)
        return ("");

    // 1. parse the string, copying to temp buffer with leading zeroes, storing pointers in vector
    pstr* strings = (pstr*)xr_alloca(MAX_RESTRICTION_PER_TYPE_COUNT * sizeof(pstr));
    pstr* string_current = strings;

    pstr temp_string = (pstr)xr_alloca((n + 1) * sizeof(char));
    LPCSTR I = space_restrictors.c_str();
    pstr i = temp_string, j = i;
    for (; *I; ++I, ++i)
    {
        if (*I != ',')
        {
            *i = *I;
            continue;
        }

        *i = 0;
        // [DA_PORT] §4 audit: space_restrictors — строка из конфига; при числе рестрикторов больше лимита
        // string_current уходил за стек-массив strings[] (VERIFY снят в релизе = порча стека). Обрубаем.
        if (u32(string_current - strings) >= MAX_RESTRICTION_PER_TYPE_COUNT)
        {
            Msg("! [DA] рестрикторов в секции больше %u — лишние отброшены", (u32)MAX_RESTRICTION_PER_TYPE_COUNT);
            break;
        }
        VERIFY(u32(string_current - strings) < MAX_RESTRICTION_PER_TYPE_COUNT);
        *string_current = j;
        ++string_current;
        j = i + 1;
    }
    if (string_current == strings)
        return (space_restrictors);

    *i = 0;
    // [DA_PORT] §4 audit: тот же лимит для последнего элемента — не писать за границей strings[].
    if (u32(string_current - strings) < MAX_RESTRICTION_PER_TYPE_COUNT)
    {
        *string_current = j;
        ++string_current;
    }

    // 2. sort the vector (svector???)
    std::sort(strings, string_current, pred_str());

    // 3. copy back to another temp string, based on sorted vector
    pstr result_string = (pstr)xr_alloca((n + 1) * sizeof(char));
    pstr pointer = result_string;
    {
        pstr* I = strings;
        pstr* E = string_current;
        for (; I != E; ++I)
        {
            for (pstr i = *I; *i; ++i, ++pointer)
                *pointer = *i;

            *pointer = ',';
            ++pointer;
        }
    }
    *(pointer - 1) = 0;

    // 4. finally, dock shared_str
    return (result_string);
}

SpaceRestrictionHolder::CBaseRestrictionPtr CSpaceRestrictionHolder::restriction(shared_str space_restrictors)
{
    std::lock_guard lock(space_restriction_lock());

    if (!xr_strlen(space_restrictors))
        return (0);

    space_restrictors = normalize_string(space_restrictors);

    RESTRICTIONS::const_iterator I = m_restrictions.find(space_restrictors);
    if (I != m_restrictions.end())
        return ((*I).second);

    collect_garbage();

    CSpaceRestrictionBase* composition = xr_new<CSpaceRestrictionComposition>(this, space_restrictors);
    CSpaceRestrictionBridge* bridge = xr_new<CSpaceRestrictionBridge>(composition);
    m_restrictions.insert(std::make_pair(space_restrictors, bridge));
    return (bridge);
}

// [DA_PORT] Регистрация ограничителя — 580 мс из 604 на всю их часть спавна. Внутри три разных
// дела, и какое из них дорого, по одной сумме не видно. Считаем порознь; печатает da_spawn_dump.
//
// Отдельно считаем, сколько регистраций попадает в ветку списка по умолчанию: там строка со всеми
// именами разбирается в массив на MAX_RESTRICTION_PER_TYPE_COUNT (128) указателей, а проверка
// границы стоит под VERIFY, которого в релизе нет.
float g_da_ms_reg_default = 0.f;
float g_da_ms_reg_shape = 0.f;
float g_da_ms_reg_insert = 0.f;
u32 g_da_reg_default_count = 0;
u32 g_da_reg_names_max = 0;

void CSpaceRestrictionHolder::register_restrictor(
    CSpaceRestrictor* space_restrictor, const RestrictionSpace::ERestrictorTypes& restrictor_type)
{
    std::lock_guard lock(space_restriction_lock());

    string4096 m_temp_string;
    CTimer da_reg;
    da_reg.Start();
    shared_str space_restrictors = space_restrictor->cName();
    if (restrictor_type != RestrictionSpace::eDefaultRestrictorTypeNone)
    {
        shared_str *temp = 0, temp1;
        if (restrictor_type == RestrictionSpace::eDefaultRestrictorTypeOut)
            temp = &m_default_out_restrictions;
        else if (restrictor_type == RestrictionSpace::eDefaultRestrictorTypeIn)
            temp = &m_default_in_restrictions;
        else
            NODEFAULT;
        temp1 = *temp;

        if (xr_strlen(*temp) && xr_strlen(space_restrictors))
            strconcat(sizeof(m_temp_string), m_temp_string, (*temp).c_str(), ",", space_restrictors.c_str());
        else
            strconcat(sizeof(m_temp_string), m_temp_string, (*temp).c_str(), space_restrictors.c_str());

        *temp = normalize_string(m_temp_string);

        if (xr_strcmp(*temp, temp1))
            on_default_restrictions_changed();

        ++g_da_reg_default_count;
        const u32 da_names = _GetItemCount((*temp).c_str());
        if (da_names > g_da_reg_names_max)
            g_da_reg_names_max = da_names;
    }

    g_da_ms_reg_default += da_reg.GetElapsed_sec() * 1000.f;
    da_reg.Start();

    CSpaceRestrictionShape* shape =
        xr_new<CSpaceRestrictionShape>(space_restrictor, restrictor_type != RestrictionSpace::eDefaultRestrictorTypeNone);

    g_da_ms_reg_shape += da_reg.GetElapsed_sec() * 1000.f;
    da_reg.Start();

    RESTRICTIONS::iterator I = m_restrictions.find(space_restrictors);
    if (I == m_restrictions.end())
    {
        CSpaceRestrictionBridge* bridge = xr_new<CSpaceRestrictionBridge>(shape);
        m_restrictions.insert(std::make_pair(space_restrictors, bridge));
        g_da_ms_reg_insert += da_reg.GetElapsed_sec() * 1000.f;
        return;
    }

    (*I).second->change_implementation(shape);

    g_da_ms_reg_insert += da_reg.GetElapsed_sec() * 1000.f;
}

bool try_remove_string(shared_str& search_string, const shared_str& string_to_search)
{
    bool found = false;
    string256 temp;
    string4096 temp1;
    *temp1 = 0;
    for (int i = 0, j = 0, n = _GetItemCount(search_string.c_str()); i < n; ++i, ++j)
    {
        if (xr_strcmp(string_to_search, _GetItem(search_string.c_str(), i, temp)))
        {
            if (j)
                xr_strcat(temp1, ",");
            xr_strcat(temp1, temp);
            continue;
        }

        found = true;
        --j;
    }

    if (!found)
        return (false);

    search_string = temp1;
    return (true);
}

void CSpaceRestrictionHolder::unregister_restrictor(CSpaceRestrictor* space_restrictor)
{
    std::lock_guard lock(space_restriction_lock());

    shared_str restrictor_id = space_restrictor->cName();
    RESTRICTIONS::iterator I = m_restrictions.find(restrictor_id);

    // [DA_PORT] Проверка вместо VERIFY: ниже разыменование и erase, оба по end() при
    // отсутствии записи. Рестриктор мог быть снят раньше — на смене уровня это обычное дело.
    if (I == m_restrictions.end())
        return;


    CSpaceRestrictionBridge* bridge = (*I).second;
    m_restrictions.erase(I);

    if (try_remove_string(m_default_out_restrictions, restrictor_id))
        on_default_restrictions_changed();
    else
    {
        if (try_remove_string(m_default_in_restrictions, restrictor_id))
            on_default_restrictions_changed();
    }

    CSpaceRestrictionBase* composition = xr_new<CSpaceRestrictionComposition>(this, restrictor_id);
    bridge->change_implementation(composition);
    m_restrictions.insert(std::make_pair(restrictor_id, bridge));

    collect_garbage();
}

IC void CSpaceRestrictionHolder::collect_garbage()
{
    RESTRICTIONS::iterator I = m_restrictions.begin(), J;
    RESTRICTIONS::iterator E = m_restrictions.end();
    for (; I != E;)
    {
        if (!(*I).second->shape() && (*I).second->released() &&
            (Device.dwTimeGlobal >= (*I).second->m_last_time_dec + TIME_TO_REMOVE_GARBAGE))
        {
            J = I;
            ++I;
            xr_delete((*J).second);
            m_restrictions.erase(J);
        }
        else
            ++I;
    }
}
