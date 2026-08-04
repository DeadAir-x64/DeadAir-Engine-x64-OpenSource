#include "pch_script.h"
#include "ai_space.h"
#include "object_factory.h"
#include "ai/monsters/ai_monster_squad_manager.h"

#include "entity_alive.h"
#include "ui/UIInventoryUtilities.h"
#include "ui/UIXmlInit.h"
#include "xrUICore/XML//UITextureMaster.h"

#include "InfoPortion.h"
#include "PhraseDialog.h"
#include "GameTask.h"
#include "encyclopedia_article.h"

#include "character_info.h"
#include "specific_character.h"
#include "character_community.h"
#include "monster_community.h"
#include "character_rank.h"
#include "character_reputation.h"

#include "xrEngine/profiler.h"

#include "sound_collection_storage.h"
#include "relation_registry.h"
#include "script_properties_list_helper.h"

extern CScriptPropertiesListHelper* g_property_list_helper;

typedef xr_vector<std::pair<shared_str, int>> STORY_PAIRS;
extern STORY_PAIRS story_ids;
extern STORY_PAIRS spawn_story_ids;

extern void release_smart_cast_stats();
extern void InitHudSoundSettings();

#include "xrEngine/IGame_Persistent.h"
void da_warmup_character_cache(); // [DA_PORT] определена ниже, разбор там же

void init_game_globals()
{
    ZoneScoped;

    InitHudSoundSettings();
    if (!GEnv.isDedicatedServer)
    {
        CInfoPortion::InitInternal(ShadowOfChernobylMode || ClearSkyMode, true);
        CEncyclopediaArticle::InitInternal(ShadowOfChernobylMode, true);
        CPhraseDialog::InitInternal();
    };
    CCharacterInfo::InitInternal();
    CSpecificCharacter::InitInternal();
    CHARACTER_COMMUNITY::InitInternal();
    CHARACTER_RANK::InitInternal();
    CHARACTER_REPUTATION::InitInternal();
    MONSTER_COMMUNITY::InitInternal();

    da_warmup_character_cache();
}

// [DA_PORT] Прогрев кеша описаний персонажей.
//
// Первый за сессию спавн человека стоил 10-11 мс — весь остальной спавн в сумме давал 0.5 мс, а
// из 84 спавнов за прогон дорогим был РОВНО ОДИН, самый первый. Разбор по этапам довёл до
// CSE_ALifeHumanAbstract::on_register -> specific_character(): когда профиль задан ШАБЛОНОМ, а не
// конкретным идентификатором, подбор перебирает ВСЕХ специфических персонажей игры и грузит
// каждого из XML. Кто оказался первым — тот и заплатил за всех.
//
// Данные складываются в общую таблицу с auto_delete = false, то есть переживают временный объект
// и живут до clean_game_globals. Значит достаточно один раз пройтись по списку здесь: эта функция
// выполняется на старте приложения отдельной задачей, до главного меню, где лишние миллисекунды
// никому не мешают. В игре после этого остаётся только чтение.
//
// Индексы (InitInternal) построены выше и уже разобрали XML на узлы — тут только доступ к данным
// по каждому идентификатору, без повторного открытия файлов.
void da_warmup_character_cache()
{
    CTimer timer;
    timer.Start();

    int characters = 0;
    for (int i = 0, e = CSpecificCharacter::GetMaxIndex(); i <= e; ++i)
    {
        const shared_str id = CSpecificCharacter::IndexToId(i);
        if (!id.size())
            continue;

        CSpecificCharacter character;
        character.Load(id);
        ++characters;
    }

    int profiles = 0;
    for (int i = 0, e = CCharacterInfo::GetMaxIndex(); i <= e; ++i)
    {
        const shared_str id = CCharacterInfo::IndexToId(i);
        if (!id.size())
            continue;

        CCharacterInfo info;
        info.Load(id);
        ++profiles;
    }

    Msg("* [DA_PORT] Кеш описаний персонажей прогрет: %d персонажей, %d профилей за %.0f мс",
        characters, profiles, timer.GetElapsed_sec() * 1000.f);
}

void clean_game_globals()
{
    xr_delete(g_property_list_helper);

    // destroy ai space
    xr_delete(g_ai_space);
    // destroy object factory
    xr_delete(g_object_factory);
    // destroy monster squad global var
    xr_delete(g_monster_squad);

    story_ids.clear();
    spawn_story_ids.clear();

    if (!GEnv.isDedicatedServer)
    {
        CInfoPortion::DeleteSharedData();
        CInfoPortion::DeleteIdToIndexData();

        CEncyclopediaArticle::DeleteSharedData();
        CEncyclopediaArticle::DeleteIdToIndexData();

        CPhraseDialog::DeleteSharedData();
        CPhraseDialog::DeleteIdToIndexData();
    }
    CCharacterInfo::DeleteSharedData();
    CCharacterInfo::DeleteIdToIndexData();

    CSpecificCharacter::DeleteSharedData();
    CSpecificCharacter::DeleteIdToIndexData();

    CHARACTER_COMMUNITY::DeleteIdToIndexData();
    CHARACTER_RANK::DeleteIdToIndexData();
    CHARACTER_REPUTATION::DeleteIdToIndexData();
    MONSTER_COMMUNITY::DeleteIdToIndexData();

    // static shader for blood
    CEntityAlive::UnloadBloodyWallmarks();
    CEntityAlive::UnloadFireParticles();

    // Очищение таблицы идентификаторов рангов и отношений сталкеров
    InventoryUtilities::ClearCharacterInfoStrings();

    xr_delete(g_sound_collection_storage);

#ifdef DEBUG
    xr_delete(g_profiler);
    release_smart_cast_stats();
#endif

    RELATION_REGISTRY::clear_relation_registry();
}
