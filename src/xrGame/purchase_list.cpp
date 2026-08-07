////////////////////////////////////////////////////////////////////////////
//	Module 		: purchase_list.cpp
//	Created 	: 12.01.2006
//  Modified 	: 12.01.2006
//	Author		: Dmitriy Iassenev
//	Description : purchase list class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "purchase_list.h"
#include "InventoryOwner.h"
#include "GameObject.h"
#include "xrAICore/Navigation/ai_object_location.h"
#include "Level.h"

static float min_deficit_factor = .3f;

// [DA_PORT] Счётчики закупки: заполняются вложенным process(), печатаются в конце верхнего.
namespace
{
u32 da_spawned_total = 0;
u32 da_sections_total = 0;
} // namespace

void CPurchaseList::process(CInifile& ini_file, LPCSTR section, CInventoryOwner& owner)
{
    owner.sell_useless_items();

    m_deficits.clear();

    const CGameObject& game_object = smart_cast<const CGameObject&>(owner);

    // [DA_PORT] Отчёт о закупке товара торговцем — по жалобе «у торговца пусто».
    //
    // Разобрать это со стороны было нечем: скриптовая часть отрабатывает молча (печать `CTime
    // update` идёт лишь в момент пополнения, то есть раз в игровые сутки), а движковая не говорила
    // ничего. Отличить «пополнение не звали» от «звали, но не создалось» стало невозможно.
    //
    // Здесь видно и то, и другое: строка есть — значит дошло до закупки, число созданных предметов
    // говорит, отработала ли она. ⚠️ Первой строкой этой функции идёт sell_useless_items(), которая
    // УНИЧТОЖАЕТ у торговца всё, кроме оружия в слоте и своей ПДА. Если после неё создастся ноль,
    // торговец останется пустым до следующих суток — и снаружи это выглядит как «товар пропал».
    da_spawned_total = 0;
    da_sections_total = 0;
    CInifile::Sect& S = ini_file.r_section(section);
    auto I = S.Data.cbegin();
    auto E = S.Data.cend();
    for (; I != E; ++I)
    {
        if (I->second.empty())
            continue;

        if (!pSettings->section_exist(I->first))
            continue;

        //VERIFY3(I->second.size(), "PurchaseList : cannot handle lines in section without values", section);

        string256 temp0, temp1;
        //THROW3(_GetItemCount(I->second.c_str()) == 2, "Invalid parameters in section", section);

		cpcstr count = _GetItem(I->second.c_str(), 0, temp0);
		cpcstr prob = _GetItemCount(I->second.c_str()) >= 2 ? _GetItem(I->second.c_str(), 1, temp1) : "1.0f";

        ++da_sections_total;
        process(game_object, I->first, atoi(count), (float)atof(prob));
    }

    Msg("~ [DA_TRADE] закупка: торговец [%s], список [%s] — строк учтено %u, предметов создано %u",
        game_object.cName().c_str(), section, da_sections_total, da_spawned_total);
}

void CPurchaseList::process(
    const CGameObject& owner, const shared_str& name, const u32& count, const float& probability)
{
    VERIFY3(count, "Invalid count for section in the purchase list", name.c_str());
    VERIFY3(!fis_zero(probability, EPS_S), "Invalid probability for section in the purchase list", name.c_str());

    const Fvector& position = owner.Position();
    const u32& level_vertex_id = owner.ai_location().level_vertex_id();
    const ALife::_OBJECT_ID& id = owner.ID();
    CRandom random((u32)(CPU::QPC() & u32(-1)));
    u32 j = 0;
    for (u32 i = 0; i < count; ++i)
    {
        if (random.randF() > probability)
            continue;

        ++j;
        ++da_spawned_total;
        Level().spawn_item(name.c_str(), position, level_vertex_id, id, false);
    }

    VERIFY3(m_deficits.find(name) == m_deficits.end(), "Duplicate section in the purchase list", name.c_str());
    m_deficits.emplace(name, (float)count * probability / _max((float)j, min_deficit_factor));
}
