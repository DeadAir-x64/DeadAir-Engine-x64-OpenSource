////////////////////////////////////////////////////////////////////////////
//	Module 		: material_manager.cpp
//	Created 	: 27.12.2003
//  Modified 	: 27.12.2003
//	Author		: Dmitriy Iassenev
//	Description : Material manager
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "material_manager.h"

// [DA_PORT] Есть ли лужа в этой точке мира (xrEngine, xr_ioc_cmd.cpp).
extern ENGINE_API bool da_puddle_at(const Fvector& p);
extern ENGINE_API float g_da_rain_wetness;
extern ENGINE_API int ps_r__puddles_debug;
#include "alife_space.h"
#include "PHMovementControl.h"
#include "entity_alive.h"
#include "CharacterPhysicsSupport.h"
#include "Include/xrRender/Kinematics.h"
#include "Level.h"

CMaterialManager::CMaterialManager(IGameObject* object, CPHMovementControl* movement_control)
{
    VERIFY(object);
    m_object = object;

    VERIFY(movement_control);
    m_movement_control = movement_control;

    m_my_material_idx = GAMEMTL_NONE_IDX;
    m_run_mode = false;
}

CMaterialManager::~CMaterialManager() {}
#ifdef DEBUG
BOOL debug_character_material_load = FALSE;
#endif

void CMaterialManager::Load(LPCSTR section)
{
    R_ASSERT3(
        pSettings->line_exist(section, "material"), "Material not found in the section ", m_object->cNameSect().c_str());
    m_my_material_idx = GMLib.GetMaterialIdx(pSettings->r_string(section, "material"));

#ifdef DEBUG
    if (debug_character_material_load)
    {
        CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(m_object);
        if (entity_alive)
        {
            VERIFY(GAMEMTL_NONE_IDX != m_my_material_idx);
            SGameMtl* m = GMLib.GetMaterialByIdx(m_my_material_idx);

            // [DA_PORT] Живая проверка вместо VERIFY. GetMaterialByIdx по НАШЕЙ ЖЕ защите (задача
            // #37/#47) отдаёт ноль при негодном номере, а строка ниже читает у него имя — то есть
            // сообщение, написанное для отладки, роняло бы игру ровно в том случае, который оно и
            // должно было описать.
            if (m)
                Msg("(CMaterialManager::Load(LPCSTR section)) material: %s loaded for %s, from section: %s ",
                    m->m_Name.c_str(), entity_alive->cName().c_str(), section);
            else
                Msg("! [DA] материал по номеру %u не найден для [%s], секция [%s]", u32(m_my_material_idx),
                    entity_alive->cName().c_str(), section);
        }
    }
#endif
}

void CMaterialManager::reinit()
{
    m_last_material_idx = GMLib.GetMaterialIdx("default");
    m_step_id = 0;
    m_run_mode = false;

    CEntityAlive* entity_alive = smart_cast<CEntityAlive*>(m_object);
    if (entity_alive)
    {
        // VERIFY( entity_alive->character_physics_support()->movement()->CharacterExist() );
        entity_alive->character_physics_support()->movement()->SetPLastMaterialIDX(&m_last_material_idx);
        entity_alive->character_physics_support()->movement()->SetMaterial(m_my_material_idx);
#ifdef DEBUG
        if (debug_character_material_load)
        {
            VERIFY(GAMEMTL_NONE_IDX != m_my_material_idx);
            SGameMtl* m = GMLib.GetMaterialByIdx(m_my_material_idx);
            VERIFY(m);
            Msg("(CMaterialManager::reinit) material: %s loaded for %s ", m->m_Name.c_str(),
                entity_alive->cName().c_str());
        }
#endif
    }
}


// [DA_PORT] Земля под ногами для звука шага. Шаги играет CStepManager через get_current_pair(), а не
// CMaterialManager::update — тот в этой сборке не зовётся ниоткуда вовсе. Первая версия правки жила
// именно в update, и молчала не потому, что что-то не сработало, а потому, что не выполнялась.
u16 CMaterialManager::da_ground_material_idx()
{
    // ⚠️ ЧЕРЕЗ GetMaterial(), а не GetMaterialIdx(). У второй внутри VERIFY, а VERIFY в релизе вырезан:
    // если имени в списке нет, она молча возвращает индекс ЗА КОНЦОМ списка, и следующий же поиск пары
    // падает уже настоящим R_ASSERT — что и случилось при первой попытке. GetMaterial честно отдаёт
    // nullptr, и тогда мы просто ничего не подменяем.
    static bool  s_probed = false;
    static u16   s_water_idx = GAMEMTL_NONE_IDX;
    static bool  s_have_water = false;
    if (!s_probed)
    {
        s_probed = true;
        // GetMaterialID отдаёт GAMEMTL_NONE_ID для ненайденного имени — в отличие от GetMaterialIdx,
        // которая в релизе вернула бы индекс за концом списка (VERIFY внутри вырезан) и уронила игру
        // на следующем же поиске пары. Так и произошло с первой версией этой правки.
        // ⚠️ ДВА слэша. С одним `\w` — не escape-последовательность, компилятор молча оставляет букву,
        // и ищется имя "materialswater". Именно так эта правка и «не работала» с первого раза.
        const u32 water_id = GMLib.GetMaterialID("materials\\water");
        if (GAMEMTL_NONE_ID != water_id)
        {
            s_water_idx = GMLib.GetMaterialIdx((int)water_id);
            s_have_water = true;
        }
        Msg("* [DA_PORT] шаги по лужам: материал воды %s (индекс %u)",
            s_have_water ? "найден" : "НЕ НАЙДЕН, плеска не будет", (u32)s_water_idx);
    }

    if (!s_have_water)
        return m_last_material_idx;

    // [DA_PORT] Под крышей плеска быть не должно. Картинка это уже учитывает — там гейтом служит
    // доступ солнца из лайтмапы, — но da_puddle_at про лайтмапу ничего не знает: это движковая
    // функция, у неё есть только координаты. Поэтому здесь свой признак: луч вверх. Упёрся в
    // геометрию — значит над головой крыша, и лужи под ногами нет.
    //
    // Один луч на шаг, не на кадр: дорого не бывает.
    const Fvector foot = m_object->Position();
    const bool noise_hit = da_puddle_at(foot);
    bool roof = false;
    if (noise_hit)
    {
        collide::rq_result rq;
        Fvector up, from = foot;
        up.set(0.f, 1.f, 0.f);
        from.y += 0.5f;
        roof = !!Level().ObjectSpace.RayPick(from, up, 20.f, collide::rqtStatic, rq, m_object);
    }
    const bool in_puddle = noise_hit && !roof;
    const bool have_pair = (nullptr != GMLib.GetMaterialPairByIndices(m_my_material_idx, s_water_idx));

    // [DA_PORT] Лог пишется ПО СМЕНЕ состояния и только для игрока, а не по таймеру и не для каждого
    // сталкера: так он живёт постоянно, не засоряя лог, и отвечает на единственный вопрос, который по
    // картинке не проверить, - совпадает ли слышимое с видимым. Отдельно показан признак крыши: без
    // него «плеск в помещении» и «плеск на сухом месте» в логе выглядели бы одинаково.
    if (Level().CurrentControlEntity() == m_object)
    {
        static int last_state = -1;
        const int state = in_puddle ? 1 : 0;
        if (state != last_state)
        {
            last_state = state;
            Msg("* [DA_PORT] под ногами %s: шум %d, крыша %d, влажность %.2f, пара с водой %d",
                in_puddle ? "ЛУЖА" : "сухо", noise_hit ? 1 : 0, roof ? 1 : 0, g_da_rain_wetness,
                have_pair ? 1 : 0);
        }
    }

    if (ps_r__puddles_debug)
    {
        static u32 last_log = 0;
        if (Device.dwTimeGlobal - last_log > 1000)
        {
            last_log = Device.dwTimeGlobal;
            Msg("* [DA_PORT] шаги: влажность %.2f, в луже %d, крыша %d, пара с водой есть %d, земля %u",
                g_da_rain_wetness, in_puddle ? 1 : 0, roof ? 1 : 0, have_pair ? 1 : 0,
                (u32)m_last_material_idx);
        }
    }

    return (in_puddle && have_pair) ? s_water_idx : m_last_material_idx;
}

void CMaterialManager::reload(LPCSTR section) {}
void CMaterialManager::update(float time_delta, float volume, float step_time, bool standing)
{
    VERIFY(GAMEMTL_NONE_IDX != m_my_material_idx);
    VERIFY(GAMEMTL_NONE_IDX != m_last_material_idx);
    Fvector position = m_object->Position();

    // [DA_PORT] Шаги по лужам. Земля под ногами считается водой, если в этой точке есть лужа — тогда
    // движок сам возьмёт водяную пару материалов с её звуками шагов, и плеск получается без единого
    // нового ассета: в gamemtl.xr для воды уже прописаны n_water_1..4.
    //
    // Признак лужи считается ТЕМ ЖЕ шумом, что рисует картинку (da_puddle_at в xr_ioc_cmd.cpp): иначе
    // звук и вид разойдутся, и игрок будет слышать плеск на сухом месте.
    SGameMtlPair* mtl_pair = GMLib.GetMaterialPairByIndices(m_my_material_idx, m_last_material_idx);
    VERIFY3(mtl_pair, "Undefined material pair: ", GMLib.GetMaterialByIdx(m_last_material_idx)->m_Name.c_str());
    // [DA_PORT] Пара материалов из gamemtl.xr может быть не определена (все прочие вызовы
    // GetMaterialPairByIndices это проверяют, и наш же have_pair выше — тоже). VERIFY3 в релизе
    // вырезан, а дальше идёт mtl_pair->StepSounds: без гварда null-разыменование (recall-audit)
    if (!mtl_pair)
        return;
    if (m_movement_control->CharacterExist())
    {
        position.y += m_movement_control->FootRadius();
    }

    // ref_sound step
    if (!standing)
    {
        if (m_time_to_step < 0)
        {
            auto& snd_array = mtl_pair->StepSounds;

            if (m_run_mode && mtl_pair->BreakingSounds.size() > 0)
                snd_array = mtl_pair->BreakingSounds;

            if (snd_array.size() > 0)
            {
                m_step_id = ::Random.randI(0, snd_array.size());
                m_time_to_step = step_time;

                m_step_sound[m_step_id] = snd_array[m_step_id];
                m_step_sound[m_step_id].play_at_pos(m_object, position);
            }
        }
        m_time_to_step -= time_delta;
    }
    else
        m_time_to_step = 0;

    for (int i = 0; i < 4; i++)
        if (m_step_sound[i]._feedback())
        {
            m_step_sound[i].set_position(position);
            m_step_sound[i].set_volume(1.f * volume);
        }
}

void CMaterialManager::set_run_mode(bool run_mode) { m_run_mode = run_mode; }
