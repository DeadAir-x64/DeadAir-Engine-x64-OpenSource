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
    //
    // ⛔ Первая версия этой правки делала здесь `return`, и это было хуже болезни: выход уносил
    // с собой снятие с учёта в пространственной базе и в планировщике, то есть менял «лишний
    // erase» на ДВА висячих указателя в глобальных реестрах. Проверка обязана прикрывать только
    // сам erase, а не остаток деструктора.
    if (it != g_pGamePersistent->ps_active.end())
        g_pGamePersistent->ps_active.erase(it);

    // [DA_PORT] Снятие с очереди уничтожения вместо утверждения о том, что нас там нет.
    //
    // Было `VERIFY(it2 == ps_destroy.end())` — в релизе пусто. А очередь хранит СЫРЫЕ указатели и
    // ничем не защищена от повторной постановки: PSI_destroy() просто делает push_back. Один и тот
    // же экземпляр попадает туда дважды (разбор у самой PSI_destroy), первый проход его удаляет, а
    // на втором `psi->Locked()` — ВИРТУАЛЬНЫЙ вызов по освобождённой памяти, следом второе
    // освобождение. Тот же класс, что очередь уничтожения объектов в CObjectList::Unload.
    //
    // Корень закрыт в PSI_destroy; здесь второй конец, на случай удаления мимо очереди —
    // destroy_particles() сносит экземпляры прямо из ps_active.
    auto& queue = g_pGamePersistent->ps_destroy;
    const auto stale = std::remove(queue.begin(), queue.end(), this);
    if (stale != queue.end())
    {
        queue.erase(stale, queue.end());
        Msg("~ [DA] частица удалена, минуя очередь уничтожения — запись снята, висячего указателя не осталось");
    }

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
    // [DA_PORT] Корень «двойного уничтожения частиц»: постановка в очередь без проверки.
    //
    // Очередь ps_destroy — вектор сырых указателей, и push_back здесь безусловен. Между тем
    // просьб об уничтожении приходит НЕСКОЛЬКО, из независимых мест:
    //   - shedule_Update по истечении времени жизни — и вот он-то m_bDead проверяет;
    //   - CParticlesObject::Destroy();
    //   - ~CScriptParticles(), когда сборщик мусора Lua добирается до объекта скрипта.
    // Автор движка прикрыл флагом ОДНОГО вызывающего из трёх — это и есть подпись дефекта.
    //
    // Что происходит без проверки: экземпляр лежит в очереди дважды, первый проход
    // IGame_Persistent::OnFrame его удаляет, второй берёт тот же адрес и зовёт `psi->Locked()` —
    // виртуальный вызов по освобождённой памяти, то есть чтение таблицы виртуальных функций у
    // покойника. Дальше PSI_internal_delete освобождает второй раз. Стек при этом указывает на
    // движок частиц, а виноват тот, кто попросил уничтожение вторым.
    //
    // Флаг для этого годится: m_bDead выставляется только здесь и нигде не сбрасывается.
    if (m_bDead)
        return;

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
