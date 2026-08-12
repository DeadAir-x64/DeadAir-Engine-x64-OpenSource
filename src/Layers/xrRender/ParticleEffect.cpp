#include "stdafx.h"
#pragma hdrstop
#include "ParticleEffect.h"

#include "xrCore/Threading/ParallelFor.hpp"

#ifndef _EDITOR
#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K) || defined(XR_ARCHITECTURE_PPC64)
#include <xmmintrin.h>
#elif defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64)
#include "sse2neon/sse2neon.h"
#elif defined(XR_ARCHITECTURE_RISCV)
#include "sse2rvv/sse2rvv.h"
#else
#error Add your platform here
#endif
#endif

extern ENGINE_API float psHUD_FOV;
extern ENGINE_API float g_hud_fov_current; // [DA_PORT] nearwall

namespace xray::render::RENDER_NAMESPACE
{
using namespace PAPI;
using namespace PS;

const u32 PS::uDT_STEP = 33;
const float PS::fDT_STEP = float(uDT_STEP) / 1000.f;

#ifdef XR_COMPILER_MSVC
#pragma warning(disable : 4701) // " potentially uninitialized local variable" (magnitude_sse does initialize it)
#endif

static void ApplyTexgen(CBackend& cmd_list, const Fmatrix& mVP)
{
    Fmatrix mTexgen;

#if defined(USE_DX11)
    Fmatrix mTexelAdjust =
    {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };
#elif defined(USE_OGL)
    Fmatrix mTexelAdjust =
    {
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    };
#else
#   error No graphics API selected or enabled!
#endif

    mTexgen.mul(mTexelAdjust, mVP);
    cmd_list.set_c("mVPTexgen", mTexgen);
}

void PS::OnEffectParticleBirth(void* owner, u32, PAPI::Particle& m, u32)
{
    CParticleEffect* PE = static_cast<CParticleEffect*>(owner);
    VERIFY(PE);
    CPEDef* PED = PE->GetDefinition();
    if (PED)
    {
        if (PED->m_Flags.is(CPEDef::dfRandomFrame))
            m.frame = (u16)iFloor(Random.randI(PED->m_Frame.m_iFrameCount) * 255.f);
        if (PED->m_Flags.is(CPEDef::dfAnimated) && PED->m_Flags.is(CPEDef::dfRandomPlayback) && Random.randI(2))
            m.flags.set(Particle::ANIMATE_CCW, TRUE);
    }
}
void PS::OnEffectParticleDead(void*, u32, PAPI::Particle&, u32)
{
    //	CPEDef* PE = static_cast<CPEDef*>(owner);
}
//------------------------------------------------------------------------------
// class CParticleEffect
//------------------------------------------------------------------------------
CParticleEffect::CParticleEffect()
{
    m_HandleEffect = ParticleManager()->CreateEffect(1);
    VERIFY(m_HandleEffect >= 0);
    m_HandleActionList = ParticleManager()->CreateActionList();
    VERIFY(m_HandleActionList >= 0);
    m_RT_Flags.zero();
    m_Def = nullptr;
    m_fElapsedLimit = 0.f;
    m_MemDT = 0;
    m_InitialPosition.set(0, 0, 0);
    m_DestroyCallback = nullptr;
    m_CollisionCallback = nullptr;
    m_XFORM.identity();
}
CParticleEffect::~CParticleEffect()
{
    // Log					("--- destroy PE");
    OnDeviceDestroy();
    ParticleManager()->DestroyEffect(m_HandleEffect);
    ParticleManager()->DestroyActionList(m_HandleActionList);
}

void CParticleEffect::Play()
{
    m_RT_Flags.set(flRT_DefferedStop, FALSE);
    m_RT_Flags.set(flRT_Playing, TRUE);
    ParticleManager()->PlayEffect(m_HandleEffect, m_HandleActionList);
}
void CParticleEffect::Stop(BOOL bDefferedStop)
{
    ParticleManager()->StopEffect(m_HandleEffect, m_HandleActionList, bDefferedStop);
    if (bDefferedStop)
    {
        m_RT_Flags.set(flRT_DefferedStop, TRUE);
    }
    else
    {
        m_RT_Flags.set(flRT_Playing, FALSE);
    }
}
void CParticleEffect::RefreshShader()
{
    OnDeviceDestroy();
    OnDeviceCreate();
}

void CParticleEffect::UpdateParent(const Fmatrix& m, const Fvector& velocity, BOOL bXFORM)
{
    m_RT_Flags.set(flRT_XFORM, bXFORM);
    if (bXFORM)
        m_XFORM.set(m);
    else
    {
        m_InitialPosition = m.c;
        ParticleManager()->Transform(m_HandleActionList, m, velocity);
    }
}

void CParticleEffect::OnFrame(u32 frame_dt)
{
    ZoneScoped;

    if (m_Def && m_RT_Flags.is(flRT_Playing))
    {
        m_MemDT += frame_dt;

        int StepCount = 0;
        if (m_MemDT >= static_cast<s32>(uDT_STEP))
        {
            // allow maximum of three steps (99ms) to avoid slowdown after loading
            // it will really skip updates at less than 10fps, which is unplayable
            StepCount = m_MemDT / uDT_STEP;
            m_MemDT = m_MemDT % uDT_STEP;
            clamp(StepCount, 0, 3);
        }

        for (; StepCount; StepCount--)
        {
            if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
            {
                if (!m_RT_Flags.is(flRT_DefferedStop))
                {
                    m_fElapsedLimit -= fDT_STEP;
                    if (m_fElapsedLimit < 0.f)
                    {
                        m_fElapsedLimit = m_Def->m_fTimeLimit;
                        Stop(true);
                        break;
                    }
                }
            }
            ParticleManager()->Update(m_HandleEffect, m_HandleActionList, fDT_STEP);

            PAPI::Particle* particles;
            u32 p_cnt;
            ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

            // our actions
            if (m_Def->m_Flags.is(CPEDef::dfFramed | CPEDef::dfAnimated))
                m_Def->ExecuteAnimate(particles, p_cnt, fDT_STEP);
            if (m_Def->m_Flags.is(CPEDef::dfCollision))
                m_Def->ExecuteCollision(particles, p_cnt, fDT_STEP, this, m_CollisionCallback);

            //-move action
            if (p_cnt)
            {
                vis.box.invalidate();
                float p_size = 0.f;
                for (u32 i = 0; i < p_cnt; i++)
                {
                    Particle& m = particles[i];
                    vis.box.modify((Fvector&)m.pos);
                    if (m.size.x > p_size)
                        p_size = m.size.x;
                    if (m.size.y > p_size)
                        p_size = m.size.y;
                    if (m.size.z > p_size)
                        p_size = m.size.z;
                }
                vis.box.grow(p_size);
                vis.box.getsphere(vis.sphere.P, vis.sphere.R);
            }
            if (m_RT_Flags.is(flRT_DefferedStop) && (0 == p_cnt))
            {
                m_RT_Flags.set(flRT_Playing | flRT_DefferedStop, FALSE);
                break;
            }
        }
    }
    else
    {
        vis.box.set(m_InitialPosition, m_InitialPosition);
        vis.box.grow(EPS_L);
        vis.box.getsphere(vis.sphere.P, vis.sphere.R);
    }
}

BOOL CParticleEffect::Compile(CPEDef* def)
{
    m_Def = def;
    if (m_Def)
    {
        // refresh shader
        RefreshShader();

        // append actions
        IReader F(m_Def->m_Actions.pointer(), m_Def->m_Actions.size());
        ParticleManager()->LoadActions(m_HandleActionList, F);
        ParticleManager()->SetMaxParticles(m_HandleEffect, m_Def->m_MaxParticles);
        ParticleManager()->SetCallback(m_HandleEffect, OnEffectParticleBirth, OnEffectParticleDead, this, 0);
        // time limit
        if (m_Def->m_Flags.is(CPEDef::dfTimeLimit))
            m_fElapsedLimit = m_Def->m_fTimeLimit;
    }
    if (def)
        shader = def->m_CachedShader;
    return TRUE;
}

void CParticleEffect::SetBirthDeadCB(PAPI::OnBirthParticleCB bc, PAPI::OnDeadParticleCB dc, void* owner, u32 p) const
{
    ParticleManager()->SetCallback(m_HandleEffect, bc, dc, owner, p);
}

u32 CParticleEffect::ParticlesCount() { return ParticleManager()->GetParticlesCount(m_HandleEffect); }
//------------------------------------------------------------------------------
// Render
//------------------------------------------------------------------------------
void CParticleEffect::Copy(dxRender_Visual*) { FATAL("Can't duplicate particle system - NOT IMPLEMENTED"); }
void CParticleEffect::OnDeviceCreate()
{
    if (m_Def)
    {
        if (m_Def->m_Flags.is(CPEDef::dfSprite))
        {
            geom.create(FVF::F_LIT, RImplementation.Vertex.Buffer(), RImplementation.QuadIB);
            if (m_Def)
                shader = m_Def->m_CachedShader;
        }
    }
}

void CParticleEffect::OnDeviceDestroy()
{
    if (m_Def)
    {
        if (m_Def->m_Flags.is(CPEDef::dfSprite))
        {
            geom.destroy();
            shader.destroy();
        }
    }
}

IC void FillSprite_fpu(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos, const Fvector2& lt,
    const Fvector2& rb, float r1, float r2, u32 clr, float sina, float cosa)
{
    ZoneScoped;

    Fvector Vr, Vt;

    Vr.x = T.x * r1 * sina + R.x * r1 * cosa;
    Vr.y = T.y * r1 * sina + R.y * r1 * cosa;
    Vr.z = T.z * r1 * sina + R.z * r1 * cosa;

    Vt.x = T.x * r2 * cosa - R.x * r2 * sina;
    Vt.y = T.y * r2 * cosa - R.y * r2 * sina;
    Vt.z = T.z * r2 * cosa - R.z * r2 * sina;

    Fvector a, b, c, d;

    a.sub(Vt, Vr);
    b.add(Vt, Vr);

    c.invert(a);
    d.invert(b);

    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y);
    pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y);
    pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y);
    pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y);
    pv++;
}

IC void FillSprite_fpu(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt, const Fvector2& rb,
    float r1, float r2, u32 clr, float sina, float cosa)
{
    ZoneScoped;

    const Fvector& T = dir;

    Fvector R;
    R.crossproduct(T, Device.vCameraDirection).normalize_safe();

    Fvector Vr, Vt;

    Vr.x = T.x * r1 * sina + R.x * r1 * cosa;
    Vr.y = T.y * r1 * sina + R.y * r1 * cosa;
    Vr.z = T.z * r1 * sina + R.z * r1 * cosa;

    Vt.x = T.x * r2 * cosa - R.x * r2 * sina;
    Vt.y = T.y * r2 * cosa - R.y * r2 * sina;
    Vt.z = T.z * r2 * cosa - R.z * r2 * sina;

    Fvector a, b, c, d;

    a.sub(Vt, Vr);
    b.add(Vt, Vr);

    c.invert(a);
    d.invert(b);

    pv->set(d.x + pos.x, d.y + pos.y, d.z + pos.z, clr, lt.x, rb.y);
    pv++;
    pv->set(a.x + pos.x, a.y + pos.y, a.z + pos.z, clr, lt.x, lt.y);
    pv++;
    pv->set(c.x + pos.x, c.y + pos.y, c.z + pos.z, clr, rb.x, rb.y);
    pv++;
    pv->set(b.x + pos.x, b.y + pos.y, b.z + pos.z, clr, rb.x, lt.y);
    pv++;
}

#ifndef _EDITOR
//----------------------------------------------------
Lock m_sprite_section;

#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K) || defined(XR_ARCHITECTURE_PPC64)
IC void FillSprite(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos, const Fvector2& lt,
    const Fvector2& rb, float r1, float r2, u32 clr, float sina, float cosa)
{
    ZoneScoped;

    m_sprite_section.Enter();

    __m128 Vr, Vt, T_, R_, _pos, _zz, _sa, _ca, a, b, c, d;

    _sa = _mm_set1_ps(sina);
    _ca = _mm_set1_ps(cosa);

    T_ = _mm_load_ss((float*)&T.x);
    T_ = _mm_loadh_pi(T_, (__m64*)&T.y);

    R_ = _mm_load_ss((float*)&R.x);
    R_ = _mm_loadh_pi(R_, (__m64*)&R.y);

    _pos = _mm_load_ss((float*)&pos.x);
    _pos = _mm_loadh_pi(_pos, (__m64*)&pos.y);

    _zz = _mm_setzero_ps();

    Vr = _mm_mul_ps(_mm_set1_ps(r1), _mm_add_ps(_mm_mul_ps(T_, _sa), _mm_mul_ps(R_, _ca)));
    Vt = _mm_mul_ps(_mm_set1_ps(r2), _mm_sub_ps(_mm_mul_ps(T_, _ca), _mm_mul_ps(R_, _sa)));

    a = _mm_sub_ps(Vt, Vr);
    b = _mm_add_ps(Vt, Vr);
    c = _mm_sub_ps(_zz, a);
    d = _mm_sub_ps(_zz, b);

    a = _mm_add_ps(a, _pos);
    d = _mm_add_ps(d, _pos);
    b = _mm_add_ps(b, _pos);
    c = _mm_add_ps(c, _pos);

    _mm_store_ss((float*)&pv->p.x, d);
    _mm_storeh_pi((__m64*)&pv->p.y, d);
    pv->color = clr;
    pv->t.set(lt.x, rb.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, a);
    _mm_storeh_pi((__m64*)&pv->p.y, a);
    pv->color = clr;
    pv->t.set(lt.x, lt.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, c);
    _mm_storeh_pi((__m64*)&pv->p.y, c);
    pv->color = clr;
    pv->t.set(rb.x, rb.y);
    pv++;

    _mm_store_ss((float*)&pv->p.x, b);
    _mm_storeh_pi((__m64*)&pv->p.y, b);
    pv->color = clr;
    pv->t.set(rb.x, lt.y);
    pv++;
    m_sprite_section.Leave();
}

IC void FillSprite(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt, const Fvector2& rb,
    float r1, float r2, u32 clr, float sina, float cosa)
{
    ZoneScoped;

    const Fvector& T = dir;
    Fvector R;

    // R.crossproduct(T,Device.vCameraDirection).normalize_safe();

    __m128 _t, _t1, _t2, _r, _r1, _r2;

    // crossproduct

    _t = _mm_load_ss((float*)&T.x);
    _t = _mm_loadh_pi(_t, (__m64*)&T.y);

    _r = _mm_load_ss((float*)&Device.vCameraDirection.x);
    _r = _mm_loadh_pi(_r, (__m64*)&Device.vCameraDirection.y);

    _t1 = _mm_shuffle_ps(_t, _t, _MM_SHUFFLE(0, 3, 1, 2));
    _t2 = _mm_shuffle_ps(_t, _t, _MM_SHUFFLE(2, 0, 1, 3));

    _r1 = _mm_shuffle_ps(_r, _r, _MM_SHUFFLE(2, 0, 1, 3));
    _r2 = _mm_shuffle_ps(_r, _r, _MM_SHUFFLE(0, 3, 1, 2));

    _t1 = _mm_mul_ps(_t1, _r1);
    _t2 = _mm_mul_ps(_t2, _r2);

    _t1 = _mm_sub_ps(_t1, _t2); // z | y | 0 | x

    // normalize_safe

    _t2 = _mm_mul_ps(_t1, _t1); // zz | yy | 00 | xx
    _r1 = _mm_movehl_ps(_t2, _t2); // zz | yy | zz | yy
    _t2 = _mm_add_ss(_t2, _r1); // zz | yy | 00 | xx + yy
    _r1 = _mm_shuffle_ps(_r1, _r1, _MM_SHUFFLE(1, 1, 1, 1)); // zz | zz | zz | zz
    _t2 = _mm_add_ss(_t2, _r1); // zz | yy | 00 | xx + yy + zz

    _r1 = _mm_set_ss(std::numeric_limits<float>::min());

    if (_mm_comigt_ss(_t2, _r1))
    {
        _t2 = _mm_rsqrt_ss(_t2);
        _t2 = _mm_shuffle_ps(_t2, _t2, _MM_SHUFFLE(0, 0, 0, 0));
        _t1 = _mm_mul_ps(_t1, _t2);
    }

    _mm_store_ss((float*)&R.x, _t1);
    _mm_storeh_pi((__m64*)&R.y, _t1);

    FillSprite(pv, T, R, pos, lt, rb, r1, r2, clr, sina, cosa);
}
#elif defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64)
ICF void FillSprite(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos, const Fvector2& lt,
    const Fvector2& rb, float r1, float r2, u32 clr, float sina, float cosa)
{
    FillSprite_fpu(pv, T, R, pos, lt, rb, r1, r2, clr, sina, cosa);
}
ICF void FillSprite(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt, const Fvector2& rb,
    float r1, float r2, u32 clr, float sina, float cosa)
{
    FillSprite_fpu(pv, pos, dir, lt, rb, r1, r2, clr, sina, cosa);
}
#else
#error Specify your platform explicitly
#endif // defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K)

#if defined(XR_ARCHITECTURE_X86) || defined(XR_ARCHITECTURE_X64) || defined(XR_ARCHITECTURE_E2K) || defined(XR_ARCHITECTURE_PPC64)
ICF void magnitude_sse(Fvector& vec, float& res) // XXX: move this to Fvector class
{
    __m128 tv, tu;

    tv = _mm_load_ss((float*)&vec.x); // tv = 0 | 0 | 0 | x
    tv = _mm_loadh_pi(tv, (__m64*)&vec.y); // tv = z | y | 0 | x
    tv = _mm_mul_ps(tv, tv); // tv = zz | yy | 0 | xx
    tu = _mm_movehl_ps(tv, tv); // tu = zz | yy | zz | yy
    tv = _mm_add_ss(tv, tu); // tv = zz | yy | 0 | xx + yy
    tu = _mm_shuffle_ps(tu, tu, _MM_SHUFFLE(1, 1, 1, 1)); // tu = zz | zz | zz | zz
    tv = _mm_add_ss(tv, tu); // tv = zz | yy | 0 | xx + yy + zz
    tv = _mm_sqrt_ss(tv); // tv = zz | yy | 0 | sqrt( xx + yy + zz )
    _mm_store_ss((float*)&res, tv);
}
#elif defined(XR_ARCHITECTURE_ARM) || defined(XR_ARCHITECTURE_ARM64)
ICF void magnitude_sse(Fvector& vec, float& res)
{
    res = vec.magnitude();
}
#else
#error Specify your platform explicitly
#endif

void CParticleEffect::ParticleRenderStream(FVF::LIT* pv, u32 count, PAPI::Particle * particles)
{
    float sina = 0.0f, cosa = 0.0f;
    // Xottab_DUTY: changed angle to be float instead of DWORD
    // But it must be 0xFFFFFFFF or otherwise some particles won't play
    float angle = float(0xFFFFFFFF); // XXX: check if we can replace with flt_max

    const auto renderParticles = [&, this](const TaskRange<u32>& range)
    {
        for (u32 i = range.begin(); i != range.end(); ++i)
        {
            PAPI::Particle& m = particles[i];
            Fvector2 lt, rb;
            lt.set(0.f, 0.f);
            rb.set(1.f, 1.f);

            _mm_prefetch((char*)&particles[i + 1], _MM_HINT_NTA);

            if (angle != m.rot.x)
            {
                angle = m.rot.x;
                sina = sinf(angle);
                cosa = cosf(angle);
            }

            _mm_prefetch(64 + (char*)&particles[i + 1], _MM_HINT_NTA);

            if (m_Def->m_Flags.is(CPEDef::dfFramed))
                m_Def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);

            float r_x = m.size.x * 0.5f;
            float r_y = m.size.y * 0.5f;
            float speed = 0.f;
            bool speed_calculated = false;

            if (m_Def->m_Flags.is(CPEDef::dfVelocityScale))
            {
                magnitude_sse(m.vel, speed);
                speed_calculated = true;
                r_x += speed * m_Def->m_VelocityScale.x;
                r_y += speed * m_Def->m_VelocityScale.y;
            }

            if (m_Def->m_Flags.is(CPEDef::dfAlignToPath))
            {
                if (!speed_calculated)
                    magnitude_sse(m.vel, speed);

                if ((speed < EPS_S) && m_Def->m_Flags.is(CPEDef::dfWorldAlign))
                {
                    Fmatrix M;
                    M.setXYZ(m_Def->m_APDefaultRotation);
                    if (m_RT_Flags.is(CParticleEffect::flRT_XFORM))
                    {
                        Fvector p;
                        m_XFORM.transform_tiny(p, m.pos);
                        M.mulA_43(m_XFORM);
                        FillSprite(pv, M.k, M.i, p, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                    else
                    {
                        FillSprite(pv, M.k, M.i, m.pos, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                }
                else if ((speed >= EPS_S) && m_Def->m_Flags.is(CPEDef::dfFaceAlign))
                {
                    Fmatrix M;
                    M.identity();
                    M.k.div(m.vel, speed);
                    M.j.set(0, 1, 0);
                    if (_abs(M.j.dotproduct(M.k)) > .99f)
                        M.j.set(0, 0, 1);
                    M.i.crossproduct(M.j, M.k);
                    M.i.normalize();
                    M.j.crossproduct(M.k, M.i);
                    M.j.normalize();
                    if (m_RT_Flags.is(CParticleEffect::flRT_XFORM))
                    {
                        Fvector p;
                        m_XFORM.transform_tiny(p, m.pos);
                        M.mulA_43(m_XFORM);
                        FillSprite(pv, M.j, M.i, p, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                    else
                    {
                        FillSprite(pv, M.j, M.i, m.pos, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                }
                else
                {
                    Fvector dir;
                    if (speed >= EPS_S)
                        dir.div(m.vel, speed);
                    else
                        dir.setHP(-m_Def->m_APDefaultRotation.y, -m_Def->m_APDefaultRotation.x);
                    if (m_RT_Flags.is(CParticleEffect::flRT_XFORM))
                    {
                        Fvector p, d;
                        m_XFORM.transform_tiny(p, m.pos);
                        m_XFORM.transform_dir(d, dir);
                        FillSprite(pv, p, d, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                    else
                    {
                        FillSprite(pv, m.pos, dir, lt, rb, r_x, r_y, m.color, sina, cosa);
                    }
                }
            }
            else
            {
                if (m_RT_Flags.is(CParticleEffect::flRT_XFORM))
                {
                    Fvector p;
                    m_XFORM.transform_tiny(p, m.pos);
                    FillSprite(pv, Device.vCameraTop, Device.vCameraRight, p, lt, rb, r_x, r_y, m.color, sina, cosa);
                }
                else
                {
                    FillSprite(pv, Device.vCameraTop, Device.vCameraRight, m.pos, lt, rb, r_x, r_y, m.color, sina, cosa);
                }
            }
        }
    };
    // XXX: it turned out that singlethreaded code works way faster
    // But on processors with small caches it may work slower, profiling needed
    //if (count > (TaskScheduler->GetWorkersCount() * 64))
    //    xr_parallel_for(TaskRange<u32>(0, count), renderParticles);
    //else
    {
        renderParticles(TaskRange<u32>(0, count));
    }
}

// [DA_PORT] Разбор стоимости частиц по трём частям: da_particle_prof N.
//
// Повод: замер назвал прямой проход самым расточительным местом кадра -- 509 вызовов отрисовки на
// 11 тысяч треугольников, 0.79 мс процессора и НОЛЬ на видеокарте, и 412 из них дают эффекты
// частиц. Напрашивается склейка вызовов, но у каждого эффекта здесь ЕЩЁ и своя пара Lock/Unlock
// динамического буфера, а это обращение к драйверу. Что именно стоит денег -- вызовы отрисовки или
// блокировки буфера -- по числу вызовов не видно, а правки у этих двух версий разные: в первом
// случае надо сливать отрисовку, во втором достаточно одной блокировки на весь проход.
//
// Части считаются раздельно, чтобы ответ не пришлось угадывать.
int ps_da_particle_prof = 0;

// [DA_PORT] Порог отрисовки эффектов частиц, МЕТРЫ (0 = без порога).
//
// Замер на Юпитере: 602 эффекта в кадре, ближайший в 122 метрах, дальний в 638. Ни одного рядом с
// игроком -- то есть четыре тысячи частиц собираются процессором каждый кадр для облачков в
// несколько пикселей. Порог 150 м снимал бы две трети эффектов и семь десятых частиц.
//
// В движке такое отсечение уже есть, но закрыто #ifdef USE_OGL и до ветки DX11 не доходит. Взять
// его как есть было нельзя по двум причинам. Первая: порог там жёстко зашит (100 м * psVisDistance),
// а psVisDistance -- это дальность видимости мира, к частицам отношения не имеющая; связывать их
// значило бы менять поведение частиц при смене настройки, к которой игрок их не относит. Вторая:
// точка отсчёта там m_InitialPosition, а это поле присваивается ТОЛЬКО в ветке bXFORM == FALSE
// (см. UpdateParent) -- у эффектов с собственным преобразованием там остаётся ноль из конструктора,
// и расстояние считалось бы до начала координат карты. В нашей сцене таких эффектов не оказалось
// ни одного, так что вживую это не проявилось бы, но закладывать грабли незачем.
//
// Здесь отсчёт от центра облака частиц: vis.sphere.P строится по их фактическим положениям
// (CParticleEffect::OnFrame), а для преобразованных эффектов переводится в мировые координаты.
//
// Значение по умолчанию выбрано по замеру на Юпитере, порог сравнивался с выключенным:
//
//   порог выкл.:  585 эффектов, 3500 частиц, 0.72 мс
//   порог 200 м:  434 эффекта,  2427 частиц, 0.53 мс  (-26%)
//   порог 150 м:  снимает ещё примерно вдвое больше, дальний эффект остаётся в пределах порога
//
// ⚠️ Порог обрывает эффект резко, затухания у границы нет: если на 150 метрах пропадание дальнего
// дыма станет заметно глазом, чинить надо не порогом, а плавным гашением -- порог тогда можно будет
// опустить и ниже. Число живёт в консоли (r__particle_dist), поднять или выключить можно на месте.
int ps_da_particle_dist = 150;

u32 g_da_pp_skipped = 0; // сколько эффектов отсёк порог -- чтобы выигрыш был виден в том же отчёте
double g_da_pp_lock = 0.0;  // Lock + Unlock динамического буфера
double g_da_pp_fill = 0.0;  // сборка спрайтов в память
double g_da_pp_draw = 0.0;  // установка состояний и сам вызов отрисовки
u32 g_da_pp_calls = 0;      // сколько эффектов дошло до отрисовки
u32 g_da_pp_parts = 0;      // сколько в них частиц суммарно

// [DA_PORT] Разбивка по расстоянию до камеры -- чтобы ЗАРАНЕЕ знать цену отсечения.
//
// В движке отсечение частиц по расстоянию уже написано (100 м * psVisDistance), но закрыто
// #ifdef USE_OGL и до ветки DX11 не доходит. Прежде чем его включать -- а оно меняет картинку,
// далёкие дымы просто исчезнут -- надо знать, сколько работы оно снимет. Порог тут не применяется,
// только считается: правка от замера отделена намеренно.
u32 g_da_pp_far[4] = {};    // эффектов дальше 50 / 100 / 150 / 200 м
u32 g_da_pp_far_p[4] = {};  // и частиц в них

// [DA_PORT] Крайние значения и доля преобразованных эффектов.
//
// Зачем: прибор намерил «сто процентов эффектов дальше ста метров», и такая доля выглядит как
// поломка отсчёта, а не как расстановка. Проверка отсчёта ничего не изменила -- вторая версия дала
// ТО ЖЕ САМОЕ. Отличить «так и есть» от «врут обе точки» по одной доле нельзя, поэтому здесь
// крайние значения: ближайший эффект ровно за порогом был бы подписью ошибки, а заметно дальше
// порога -- признаком того, что доля настоящая.
//
// Ответ: ближайший 122 м, дальний 638 м, преобразованных ноль из 602. То есть доля была верной с
// самого начала, а объяснение через m_InitialPosition (оно и правда присваивается только в одной
// ветке UpdateParent) к этой сцене не относилось -- ни один эффект по той ветке не шёл. Замер был
// прав, неправа была догадка о нём.
float g_da_pp_dmin = 1e9f;
float g_da_pp_dmax = 0.f;
u32 g_da_pp_xform = 0;      // из них с собственным преобразованием

void CParticleEffect::Render(CBackend& cmd_list, float, bool use_fast_geo)
{
#ifdef _GPA_ENABLED
    TAL_SCOPED_TASK_NAMED("CParticleEffect::Render()");
#endif // _GPA_ENABLED

    // [DA_PORT] Порог по расстоянию, см. ps_da_particle_dist. Стоит здесь, до всего остального:
    // отсечённый эффект не должен стоить ни выборки частиц, ни блокировки буфера, ни отрисовки.
    if (ps_da_particle_dist > 0)
    {
        Fvector da_pos;
        if (m_RT_Flags.is(flRT_XFORM))
            m_XFORM.transform_tiny(da_pos, vis.sphere.P);
        else
            da_pos = vis.sphere.P;

        const float lim = float(ps_da_particle_dist);
        if (Device.vCameraPosition.distance_to_sqr(da_pos) > lim * lim)
        {
            if (ps_da_particle_prof > 0)
                ++g_da_pp_skipped;
            return;
        }
    }

#ifdef USE_OGL
    // Due to the big impact on performance
    // [DA_PORT] Порог выше заменяет это отсечение и в ветке OpenGL: там оно считало расстояние от
    // m_InitialPosition, верного лишь при bXFORM == FALSE, и привязывало частицы к rs_vis_distance.
    const float distSQ = Device.vCameraPosition.distance_to_sqr(m_InitialPosition) + EPS;
    if (ps_da_particle_dist <= 0 && distSQ > _sqr(100.f*psVisDistance))
        return;
#endif

    u32 dwOffset, dwCount;
    // Get a pointer to the particles in gp memory
    PAPI::Particle* particles;
    u32 p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

    if (p_cnt > 0)
    {
        if (m_Def && m_Def->m_Flags.is(CPEDef::dfSprite))
        {
            // [DA_PORT] Приборы включаются только по команде: CTimer на каждый эффект при
            // выключенном замере сам стал бы тем, что мы измеряем.
            const bool prof = ps_da_particle_prof > 0;
            CTimer t;
            if (prof)
                t.Start();

            FVF::LIT* pv_start = (FVF::LIT*)RImplementation.Vertex.Lock(p_cnt * 4 * 4, geom->vb_stride, dwOffset);

            if (prof)
            {
                g_da_pp_lock += t.GetElapsed_sec() * 1000.0;
                t.Start();
            }

            ParticleRenderStream(pv_start, p_cnt, particles);

            dwCount = p_cnt << 2;

            if (prof)
            {
                g_da_pp_fill += t.GetElapsed_sec() * 1000.0;
                t.Start();
            }

            RImplementation.Vertex.Unlock(dwCount, geom->vb_stride);

            if (prof)
            {
                g_da_pp_lock += t.GetElapsed_sec() * 1000.0;
                t.Start();
                ++g_da_pp_calls;
                g_da_pp_parts += p_cnt;

                // [DA_PORT] Отсчёт от центра облака частиц: vis.sphere.P строится по их фактическим
                // положениям (OnFrame), а для преобразованных эффектов переводится в мир.
                //
                // 🪤 Не от m_InitialPosition -- оно присваивается только в ветке bXFORM == FALSE
                // (UpdateParent), и у эффектов с собственным преобразованием остаётся нулём из
                // конструктора. В измеренной сцене таких не нашлось ни одного, так что разницы в
                // числах не вышло, но опираться на поле, верное лишь для части случаев, незачем --
                // и по той же причине отсечение из ветки OpenGL нельзя брать как есть.
                Fvector pos;
                if (m_RT_Flags.is(CParticleEffect::flRT_XFORM))
                {
                    m_XFORM.transform_tiny(pos, vis.sphere.P);
                    ++g_da_pp_xform;
                }
                else
                    pos = vis.sphere.P;
                const float dist = Device.vCameraPosition.distance_to(pos);
                if (dist < g_da_pp_dmin)
                    g_da_pp_dmin = dist;
                if (dist > g_da_pp_dmax)
                    g_da_pp_dmax = dist;
                static const float thr[4] = { 50.f, 100.f, 150.f, 200.f };
                for (int b = 0; b < 4; ++b)
                {
                    if (dist > thr[b])
                    {
                        ++g_da_pp_far[b];
                        g_da_pp_far_p[b] += p_cnt;
                    }
                }
            }
            if (dwCount)
            {
#ifndef _EDITOR
                Fmatrix Pold = Device.mProject;
                Fmatrix FTold = Device.mFullTransform;
                if (GetHudMode())
                {
                    Device.mProject.build_projection(deg2rad(g_hud_fov_current * Device.fFOV), Device.fASPECT, HUD_VIEWPORT_NEAR,
                        g_pGamePersistent->Environment().CurrentEnv.far_plane);

                    Device.mFullTransform.mul(Device.mProject, Device.mView);
                    cmd_list.set_xform_project(Device.mProject);
                    RImplementation.rmNear(cmd_list);
                    ApplyTexgen(cmd_list, Device.mFullTransform);
                }
#endif

                cmd_list.set_xform_world(Fidentity);
                cmd_list.set_Geometry(geom);

                cmd_list.set_CullMode(m_Def->m_Flags.is(CPEDef::dfCulling) ?
                        (m_Def->m_Flags.is(CPEDef::dfCullCCW) ? CULL_CCW : CULL_CW) :
                        CULL_NONE);
                cmd_list.Render(D3DPT_TRIANGLELIST, dwOffset, 0, dwCount, 0, dwCount / 2);
                cmd_list.set_CullMode(CULL_CCW);
#ifndef _EDITOR
                if (GetHudMode())
                {
                    RImplementation.rmNormal(cmd_list);
                    Device.mProject = Pold;
                    Device.mFullTransform = FTold;
                    cmd_list.set_xform_project(Device.mProject);
                    ApplyTexgen(cmd_list, Device.mFullTransform);
                }
#endif
            }
            if (prof)
                g_da_pp_draw += t.GetElapsed_sec() * 1000.0;
        }
    }
}

#else // _EDITOR

//----------------------------------------------------
IC void FillSprite(FVF::LIT*& pv, const Fvector& T, const Fvector& R, const Fvector& pos, const Fvector2& lt,
    const Fvector2& rb, float r1, float r2, u32 clr, float angle)
{
    FillSprite_fpu(pv, T, R, pos, lt, rb, r1, r2, clr, _sin(angle), _cos(angle));
}

IC void FillSprite(FVF::LIT*& pv, const Fvector& pos, const Fvector& dir, const Fvector2& lt, const Fvector2& rb,
    float r1, float r2, u32 clr, float angle)
{
    FillSprite_fpu(pv, pos, dir, lt, rb, r1, r2, clr, _sin(angle), _cos(angle));
}

void CParticleEffect::Render(float, bool)
{
    u32 dwOffset, dwCount;
    // Get a pointer to the particles in gp memory
    PAPI::Particle* particles;
    u32 p_cnt;
    ParticleManager()->GetParticles(m_HandleEffect, particles, p_cnt);

    if (p_cnt > 0)
    {
        if (m_Def && m_Def->m_Flags.is(CPEDef::dfSprite))
        {
            FVF::LIT* pv_start = (FVF::LIT*)RImplementation.Vertex.Lock(p_cnt * 4 * 4, geom->vb_stride, dwOffset);
            FVF::LIT* pv = pv_start;

            for (u32 i = 0; i < p_cnt; i++)
            {
                PAPI::Particle& m = particles[i];

                Fvector2 lt, rb;
                lt.set(0.f, 0.f);
                rb.set(1.f, 1.f);
                if (m_Def->m_Flags.is(CPEDef::dfFramed))
                    m_Def->m_Frame.CalculateTC(iFloor(float(m.frame) / 255.f), lt, rb);
                float r_x = m.size.x * 0.5f;
                float r_y = m.size.y * 0.5f;
                if (m_Def->m_Flags.is(CPEDef::dfVelocityScale))
                {
                    float speed = m.vel.magnitude();
                    r_x += speed * m_Def->m_VelocityScale.x;
                    r_y += speed * m_Def->m_VelocityScale.y;
                }
                if (m_Def->m_Flags.is(CPEDef::dfAlignToPath))
                {
                    float speed = m.vel.magnitude();
                    if ((speed < EPS_S) && m_Def->m_Flags.is(CPEDef::dfWorldAlign))
                    {
                        Fmatrix M;
                        M.setXYZ(m_Def->m_APDefaultRotation);
                        if (m_RT_Flags.is(flRT_XFORM))
                        {
                            Fvector p;
                            m_XFORM.transform_tiny(p, m.pos);
                            M.mulA_43(m_XFORM);
                            FillSprite(pv, M.k, M.i, p, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                        else
                        {
                            FillSprite(pv, M.k, M.i, m.pos, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    }
                    else if ((speed >= EPS_S) && m_Def->m_Flags.is(CPEDef::dfFaceAlign))
                    {
                        Fmatrix M;
                        M.identity();
                        M.k.div(m.vel, speed);
                        M.j.set(0, 1, 0);
                        if (_abs(M.j.dotproduct(M.k)) > .99f)
                            M.j.set(0, 0, 1);
                        M.i.crossproduct(M.j, M.k);
                        M.i.normalize();
                        M.j.crossproduct(M.k, M.i);
                        M.j.normalize();
                        if (m_RT_Flags.is(flRT_XFORM))
                        {
                            Fvector p;
                            m_XFORM.transform_tiny(p, m.pos);
                            M.mulA_43(m_XFORM);
                            FillSprite(pv, M.j, M.i, p, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                        else
                        {
                            FillSprite(pv, M.j, M.i, m.pos, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    }
                    else
                    {
                        Fvector dir;
                        if (speed >= EPS_S)
                            dir.div(m.vel, speed);
                        else
                            dir.setHP(-m_Def->m_APDefaultRotation.y, -m_Def->m_APDefaultRotation.x);
                        if (m_RT_Flags.is(flRT_XFORM))
                        {
                            Fvector p, d;
                            m_XFORM.transform_tiny(p, m.pos);
                            m_XFORM.transform_dir(d, dir);
                            FillSprite(pv, p, d, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                        else
                        {
                            FillSprite(pv, m.pos, dir, lt, rb, r_x, r_y, m.color, m.rot.x);
                        }
                    }
                }
                else
                {
                    if (m_RT_Flags.is(flRT_XFORM))
                    {
                        Fvector p;
                        m_XFORM.transform_tiny(p, m.pos);
                        FillSprite(pv, Device.vCameraTop, Device.vCameraRight, p, lt, rb, r_x, r_y, m.color, m.rot.x);
                    }
                    else
                    {
                        FillSprite(
                            pv, Device.vCameraTop, Device.vCameraRight, m.pos, lt, rb, r_x, r_y, m.color, m.rot.x);
                    }
                }
            }
            dwCount = u32(pv - pv_start);
            RImplementation.Vertex.Unlock(dwCount, geom->vb_stride);
            if (dwCount)
            {
#ifndef _EDITOR
                Fmatrix Pold = Device.mProject;
                Fmatrix FTold = Device.mFullTransform;
                if (GetHudMode())
                {
                    Device.mProject.build_projection(deg2rad(g_hud_fov_current * Device.fFOV), Device.fASPECT, HUD_VIEWPORT_NEAR,
                        g_pGamePersistent->Environment().CurrentEnv.far_plane);

                    Device.mFullTransform.mul(Device.mProject, Device.mView);
                    RCache.set_xform_project(Device.mProject);
                    RImplementation.rmNear();
                    ApplyTexgen(Device.mFullTransform);
                }
#endif

                RCache.set_xform_world(Fidentity);
                RCache.set_Geometry(geom);

                RCache.set_CullMode(m_Def->m_Flags.is(CPEDef::dfCulling) ?
                        (m_Def->m_Flags.is(CPEDef::dfCullCCW) ? CULL_CCW : CULL_CW) :
                        CULL_NONE);
                RCache.Render(D3DPT_TRIANGLELIST, dwOffset, 0, dwCount, 0, dwCount / 2);
                RCache.set_CullMode(CULL_CCW);
#ifndef _EDITOR
                if (GetHudMode())
                {
                    RImplementation.rmNormal();
                    Device.mProject = Pold;
                    Device.mFullTransform = FTold;
                    RCache.set_xform_project(Device.mProject);
                    ApplyTexgen(Device.mFullTransform);
                }
#endif
            }
        }
    }
}

#endif // _EDITOR
} // namespace xray::render::RENDER_NAMESPACE
