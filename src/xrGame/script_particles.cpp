////////////////////////////////////////////////////////////////////////////
//	Module 		: script_sound.cpp
//	Created 	: 06.02.2004
//  Modified 	: 06.02.2004
//	Author		: Dmitriy Iassenev
//	Description : XRay Script sound class
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "script_particles.h"
#include "xrEngine/ObjectAnimator.h"

CScriptParticlesCustom::CScriptParticlesCustom(CScriptParticles* owner, LPCSTR caParticlesName)
    : CParticlesObject(caParticlesName, FALSE, true)
{
    //	CScriptParticlesCustom* self = this;
    //	Msg							("CScriptParticlesCustom: 0x%08x",*(int*)&self);
    m_owner = owner;
    m_animator = 0;
}

// XRCORE_API		fastdelegate::FastDelegate< void () >	g_verify_stalkers;

CScriptParticlesCustom::~CScriptParticlesCustom()
{
    //	CScriptParticlesCustom* self = this;
    //	Msg							("~CScriptParticlesCustom: 0x%08x",*(int*)&self);
    //	if ( g_verify_stalkers )
    //		g_verify_stalkers		();

    xr_delete(m_animator);

    //	if ( g_verify_stalkers )
    //		g_verify_stalkers		();
}

void CScriptParticlesCustom::PSI_internal_delete()
{
    if (m_owner)
        m_owner->m_particles = NULL;
    CParticlesObject::PSI_internal_delete();
}

void CScriptParticlesCustom::PSI_destroy()
{
    if (m_owner)
        m_owner->m_particles = NULL;
    CParticlesObject::PSI_destroy();
}

void CScriptParticlesCustom::shedule_Update(u32 _dt)
{
    CParticlesObject::shedule_Update(_dt);
    if (m_animator)
    {
        float dt = float(_dt) / 1000.f;
        Fvector prev_pos = m_animator->XFORM().c;
        m_animator->Update(dt);
        Fvector vel;
        vel.sub(m_animator->XFORM().c, prev_pos).div(dt);
        UpdateParent(m_animator->XFORM(), vel);
    }
}
void CScriptParticlesCustom::LoadPath(LPCSTR caPathName)
{
    if (!m_animator)
        m_animator = xr_new<CObjectAnimator>();
    if ((0 == m_animator->Name()) || (0 != xr_strcmp(m_animator->Name(), caPathName)))
    {
        m_animator->Clear();
        m_animator->Load(caPathName);
    }
}
void CScriptParticlesCustom::StartPath(bool looped)
{
    VERIFY(m_animator);
    m_animator->Play(looped);
}
void CScriptParticlesCustom::PausePath(bool val)
{
    VERIFY(m_animator);
    m_animator->Pause(val);
}

void CScriptParticlesCustom::StopPath()
{
    VERIFY(m_animator);
    m_animator->Stop();
}

void CScriptParticlesCustom::remove_owner()
{
    R_ASSERT(m_owner);
    m_owner = 0;
}

CScriptParticles::CScriptParticles(LPCSTR caParticlesName)
{
    m_particles = xr_new<CScriptParticlesCustom>(this, caParticlesName);
    m_transform.identity();
}

CScriptParticles::~CScriptParticles()
{
    if (m_particles)
    {
        // destroy particles
        m_particles->remove_owner();
        m_particles->PSI_destroy();
        m_particles = 0;
    }
}

// [DA_PORT] Живые проверки вместо VERIFY во ВСЁМ классе, а часть методов ниже не имела вообще
// никакой (SetDirection, SetOrientation, StartPath, StopPath, PausePath).
//
// Почему это не «защита от невозможного»: объект particles живёт в Lua, а сама система частиц —
// в движке, и она удаляет себя САМА, когда доиграла. При этом PSI_destroy/PSI_internal_delete
// выше обнуляют m_particles у владельца. То есть после `p:play()` неповторяющегося эффекта любой
// следующий вызов из скрипта — `p:stop()`, `p:move_to()`, `p:playing()` — приходит на пустой
// указатель. Скрипту неоткуда узнать момент, когда это произошло: методы, отвечающего на вопрос
// «жив ли ещё эффект», у него нет — playing() сам разыменовывает то, что проверяет.
//
// Молчим намеренно: обращение к доигравшему эффекту — обычный порядок вещей в скриптах мода, а
// не ошибка, и сообщение на каждый такой вызов залило бы лог.
void CScriptParticles::Play()
{
    if (!m_particles)
        return;
    m_particles->Play(false);
}

void CScriptParticles::PlayAtPos(const Fvector& position)
{
    //m_particles->play_at_pos(position);
    m_transform.translate_over(position);
    if (!m_particles)
        return;
    m_particles->UpdateParent(m_transform, zero_vel);
    m_particles->Play(false);
    m_particles->UpdateParent(m_transform, zero_vel);
}

void CScriptParticles::Stop()
{
    if (!m_particles)
        return;
    m_particles->Stop(FALSE);
}

void CScriptParticles::StopDeferred()
{
    if (!m_particles)
        return;
    m_particles->Stop(TRUE);
}

void CScriptParticles::MoveTo(const Fvector& pos, const Fvector& vel)
{
    //Fmatrix XF;
    //XF.translate(pos);
    m_transform.translate_over(pos);

    //m_particles->UpdateParent(XF, vel);
    // Положение запоминаем в любом случае: эффект могут запустить повторно, и он должен появиться
    // там, куда его успел передвинуть скрипт.
    if (!m_particles)
        return;
    m_particles->UpdateParent(m_transform, vel);
}

void CScriptParticles::SetDirection(const Fvector& dir)
{
    Fmatrix matrix;
    matrix.identity();
    matrix.k.set(dir);
    Fvector::generate_orthonormal_basis_normalized(matrix.k, matrix.j, matrix.i);
    matrix.translate_over(m_transform.c);
    m_transform.set(matrix);
    if (!m_particles)
        return;
    m_particles->UpdateParent(matrix, zero_vel);
}

void CScriptParticles::SetOrientation(float yaw, float pitch, float roll)
{
    Fmatrix matrix;
    matrix.setHPB(yaw, pitch, roll); // ?????????? matrix.c
    matrix.translate_over(m_transform.c);
    m_transform.set(matrix);
    if (!m_particles)
        return;
    m_particles->UpdateParent(matrix, zero_vel);
}

bool CScriptParticles::IsPlaying() const
{
    // Ушедший эффект не играет — это честный ответ, а не заглушка.
    if (!m_particles)
        return false;
    return m_particles->IsPlaying();
}

bool CScriptParticles::IsLooped() const
{
    if (!m_particles)
        return false;
    return m_particles->IsLooped();
}

void CScriptParticles::LoadPath(LPCSTR caPathName)
{
    if (!m_particles)
        return;
    m_particles->LoadPath(caPathName);
}
void CScriptParticles::StartPath(bool looped)
{
    if (m_particles)
        m_particles->StartPath(looped);
}
void CScriptParticles::StopPath()
{
    if (m_particles)
        m_particles->StopPath();
}
void CScriptParticles::PausePath(bool val)
{
    if (m_particles)
        m_particles->PausePath(val);
}
