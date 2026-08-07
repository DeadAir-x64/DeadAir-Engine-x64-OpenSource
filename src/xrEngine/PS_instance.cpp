//----------------------------------------------------
// file: TempObject.cpp
//----------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "PS_instance.h"
#include "IGame_Persistent.h"

CPS_Instance::CPS_Instance(bool destroy_on_game_load)
    : SpatialBase(g_pGamePersistent->SpatialSpace), m_destroy_on_game_load(destroy_on_game_load)
{
    g_pGamePersistent->ps_active.insert(this);
    renderable.pROS_Allowed = false;

    m_iLifeTime = int_max;
    m_bAutoRemove = true;
    m_bDead = false;
}
extern ENGINE_API bool g_bRendering;

//----------------------------------------------------
CPS_Instance::~CPS_Instance()
{
    VERIFY(!g_bRendering);
    auto it = g_pGamePersistent->ps_active.find(this);
    // [DA_PORT] Проверка вместо VERIFY: он исчезает в релизе, а erase(end()) — неопределённое
    // поведение, оно портит контейнер и рушится потом в стороне. Пропуск удаления безвреден:
    // элемента и так нет.
    if (it == g_pGamePersistent->ps_active.end())
        return;

    g_pGamePersistent->ps_active.erase(it);

    [[maybe_unused]] auto it2 = std::find(g_pGamePersistent->ps_destroy.begin(), g_pGamePersistent->ps_destroy.end(), this);
    VERIFY(it2 == g_pGamePersistent->ps_destroy.end());

    spatial_unregister();
    shedule_unregister();
}
//----------------------------------------------------
void CPS_Instance::shedule_Update(u32 dt)
{
    ZoneScoped;

    if (renderable.pROS)
        GEnv.Render->ros_destroy(renderable.pROS); //. particles doesn't need ROS

    ScheduledBase::shedule_Update(dt);
    m_iLifeTime -= dt;

    // remove???
    if (m_bDead)
        return;
    if (m_bAutoRemove && m_iLifeTime <= 0)
        PSI_destroy();
}
//----------------------------------------------------
void CPS_Instance::PSI_destroy()
{
    m_bDead = true;
    m_iLifeTime = 0;
    g_pGamePersistent->ps_destroy.push_back(this);
}
//----------------------------------------------------
void CPS_Instance::PSI_internal_delete()
{
    CPS_Instance* self = this;
    xr_delete(self);
}
