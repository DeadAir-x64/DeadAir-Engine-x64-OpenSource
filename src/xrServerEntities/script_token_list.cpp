////////////////////////////////////////////////////////////////////////////
//	Module 		: script_token_list.cpp
//	Created 	: 21.05.2004
//  Modified 	: 21.05.2004
//	Author		: Dmitriy Iassenev
//	Description : Script token list class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "script_token_list.h"
#include <algorithm>

CScriptTokenList::CScriptTokenList()
{
    clear();
}

CScriptTokenList::~CScriptTokenList() noexcept
{
    for (xr_token& token : m_token_list)
    {
        auto tokenName = const_cast<char*>(token.name);
        xr_free(tokenName);
    }
}

void CScriptTokenList::add(pcstr name, int id)
{
    VERIFY(token(name) == m_token_list.end() && token(id) == m_token_list.end());

    xr_token& back = m_token_list.back();
    VERIFY(back.name == nullptr && back.id == -1);
    back.name = xr_strdup(name);
    back.id = id;

    m_token_list.emplace_back(nullptr, -1);
}

void CScriptTokenList::remove(pcstr name)
{
    iterator I = token(name);
    const bool tokenFound = I != m_token_list.end();
    if (tokenFound)
    {
        auto tokenName = const_cast<char*>((*I).name);
        xr_delete(tokenName);
        m_token_list.erase(I);
    }
    else
    {
        Msg("! token with name [%s] not found while deleting it from CScriptTokenList", name);
    }
}

void CScriptTokenList::clear()
{
    m_token_list.clear();
    m_token_list.emplace_back(nullptr, -1);
}

int CScriptTokenList::id(pcstr name)
{
    iterator I = token(name);

    // [DA_PORT] Проверка вместо VERIFY: сюда приходят имена ИЗ СКРИПТОВ, то есть из данных мода.
    // Опечатка в конфиге не должна ронять игру разыменованием end(); -1 — общепринятый здесь
    // признак "нет такого", его вызывающая сторона уже умеет отличать.
    if (I == m_token_list.end())
    {
        Msg("! [DA_PORT] список токенов: имя [%s] не найдено", name);
        return -1;
    }

    return (*I).id;
}

pcstr CScriptTokenList::name(int id)
{
    iterator I = token(id);

    // [DA_PORT] Проверка вместо VERIFY — см. id() выше. Пустая строка вместо разыменования end().
    if (I == m_token_list.end())
    {
        Msg("! [DA_PORT] список токенов: номер [%d] не найден", id);
        return "";
    }

    return (*I).name;
}

CScriptTokenList::iterator CScriptTokenList::token(pcstr name)
{
    return std::find_if(m_token_list.begin(), m_token_list.end(), CTokenPredicateName(name));
}

CScriptTokenList::iterator CScriptTokenList::token(int id)
{
    return std::find_if(m_token_list.begin(), m_token_list.end(), CTokenPredicateID(id));
}
