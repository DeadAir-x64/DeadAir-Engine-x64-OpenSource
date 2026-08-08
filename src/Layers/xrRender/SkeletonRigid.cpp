//---------------------------------------------------------------------------
#include "stdafx.h"
#pragma hdrstop

#include "SkeletonCustom.h"

namespace xray::render::RENDER_NAMESPACE
{
extern int psSkeletonUpdate;

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s);
#endif

// [DA_PORT] Замер общего замка расчёта костей. Пока da_bones_dump в нуле, не стоит ничего.
//
// Зачем. Расчёт костей ВСЕХ моделей сериализован одним глобальным замком UCalc_Mutex
// (SkeletonCustom.h). У соседей это место расшито, но прежде чем повторять чужую правку, надо
// узнать, есть ли что расшивать именно у нас.
//
// Считать надо ЧЕТЫРЕ разные вещи, и путать их нельзя:
//   - ожидание на замке   — цена сериализации, ровно её и снимет расшивка;
//   - работа под замком   — цена самого расчёта; расшивка её НЕ убирает, только раскладывает по ядрам;
//   - сколько потоков     — если поток один, расшивать нечего вообще: мешать некому;
//   - выходы без пересчёта — ранний выход стоит сразу за OnCalculateBones: кости НЕ пересчитываются,
//                           но дорожки анимации прокручиваются. ⛔ Сначала эта строка называлась
//                           «захваты впустую» и предлагала перенести проверку перед замок. Это было
//                           неверно: работа там есть, UpdateTracks обязан отработать каждый вызов.
//
// ⚠️ Ожидание меряем через TryEnter: на свободном замке не платим вовсе, таймер заводится только
// когда мы и правда встали в очередь. Иначе прибор мерил бы сам себя.
//
// ⚠️ Кадры считаем здесь же, по смене Device.dwFrame, а НЕ из кадрового обработчика рендера:
// на скрытом рабочем столе кадры не презентуются, CRender::Calculate не идёт, и прибор молчал бы
// именно на стенде. Расчёт костей идёт от игровой логики и не зависит от того, рисуем ли мы.
//
// Всё состояние трогается ТОЛЬКО под захваченным замком, поэтому атомики не нужны.
namespace
{
constexpr u32 DA_BONES_MAX_THREADS = 16;

struct da_bones_thread_row
{
    std::thread::id id{};
    u64 calls{};
    u64 wait_ns{};
};

da_bones_thread_row g_rows[DA_BONES_MAX_THREADS];
u32 g_rows_used = 0;

u64 g_calls = 0;
u64 g_contended = 0;
u64 g_wait_ns = 0;
u64 g_held_ns = 0;
u64 g_idle_locks = 0;

u32 g_frames = 0;
u32 g_last_frame = 0;
bool g_armed = false;

void da_bones_reset()
{
    for (auto& r : g_rows)
        r = da_bones_thread_row();
    g_rows_used = 0;
    g_calls = g_contended = g_wait_ns = g_held_ns = g_idle_locks = 0;
    g_frames = 0;
    g_last_frame = Device.dwFrame;
}

void da_bones_report()
{
    if (!g_frames)
    {
        Msg("~ [DA_BONES] кадров не набралось - замер пуст");
        return;
    }

    const double f = double(g_frames);
    Msg("~ [DA_BONES] за %u кадров: вызовов %llu (%.1f за кадр), с ожиданием %llu (%.1f%%)", g_frames, g_calls,
        double(g_calls) / f, g_contended, g_calls ? 100.0 * double(g_contended) / double(g_calls) : 0.0);
    Msg("~ [DA_BONES]   ожидание на замке %6.3f мс/кадр   работа под замком %6.3f мс/кадр",
        double(g_wait_ns) / 1e6 / f, double(g_held_ns) / 1e6 / f);
    Msg("~ [DA_BONES]   без пересчёта костей %llu (%.1f за кадр) - только прокрутка дорожек анимации",
        g_idle_locks, double(g_idle_locks) / f);
    Msg("~ [DA_BONES]   потоков: %u%s", g_rows_used,
        g_rows_used > 1 ? "" : " - ВСЁ В ОДИН ПОТОК, расшивать нечего");
    for (u32 i = 0; i < g_rows_used; ++i)
        Msg("~ [DA_BONES]     поток %u: вызовов %llu (%.1f за кадр), ожидание %6.3f мс/кадр", i + 1,
            g_rows[i].calls, double(g_rows[i].calls) / f, double(g_rows[i].wait_ns) / 1e6 / f);
}

// Строка потока. Зовётся только под захваченным замком.
da_bones_thread_row* da_bones_row()
{
    const std::thread::id me = std::this_thread::get_id();
    for (u32 i = 0; i < g_rows_used; ++i)
        if (g_rows[i].id == me)
            return &g_rows[i];

    if (g_rows_used >= DA_BONES_MAX_THREADS)
        return nullptr;

    g_rows[g_rows_used].id = me;
    return &g_rows[g_rows_used++];
}

// Замок с измерением. Пока прибор выключен, ведёт себя ровно как UCalc_mtlock.
struct da_bones_lock
{
    CTimer held;
    bool measured;

    da_bones_lock() : measured(ps_da_bones_dump > 0)
    {
        if (!measured)
        {
            UCalc_Mutex.Enter();
            return;
        }

        u64 wait_ns = 0;
        if (!UCalc_Mutex.TryEnter())
        {
            CTimer wait;
            wait.Start();
            UCalc_Mutex.Enter();
            wait_ns = wait.GetElapsed_ns();
        }

        // Дальше - под замком, поэтому обычные переменные.
        if (!g_armed)
        {
            da_bones_reset();
            g_armed = true;
        }

        if (Device.dwFrame != g_last_frame) // новый кадр
        {
            g_last_frame = Device.dwFrame;
            ++g_frames;
            if (--ps_da_bones_dump <= 0)
            {
                ps_da_bones_dump = 0;
                da_bones_report();
                g_armed = false;
                measured = false; // этот вызов уже вне замера
                held.Start();
                return;
            }
        }

        ++g_calls;
        g_wait_ns += wait_ns;
        if (wait_ns)
            ++g_contended;

        if (da_bones_thread_row* row = da_bones_row())
        {
            ++row->calls;
            row->wait_ns += wait_ns;
        }

        held.Start();
    }

    ~da_bones_lock()
    {
        if (measured)
            g_held_ns += held.GetElapsed_ns();
        UCalc_Mutex.Leave();
    }
};
} // namespace

void CKinematics::CalculateBones(BOOL bForceExact)
{
    ZoneScoped;

    // early out.
    // check if the info is still relevant
    // skip all the computations - assume nothing changes in a small period of time :)
    if (Device.dwTimeGlobal == UCalc_Time)
        return; // early out for "fast" update
    da_bones_lock lock; // [DA_PORT] был UCalc_mtlock; поведение то же, плюс замер
    OnCalculateBones();
    if (!bForceExact && (Device.dwTimeGlobal < (UCalc_Time + UCalc_Interval)))
    {
        // [DA_PORT] Замок взят, работы не будет. См. разбор у da_bones_lock.
        if (g_armed)
            ++g_idle_locks;
        return; // early out for "slow" update
    }
    if (Update_Visibility)
        Visibility_Update();

    _DBG_SINGLE_USE_MARKER;
    // here we have either:
    //	1:	timeout elapsed
    //	2:	exact computation required
    UCalc_Time = Device.dwTimeGlobal;

// exact computation
// Calculate bones
#ifdef DEBUG
    RImplementation.BasicStats.Animation.Begin();
#endif

    Bone_Calculate(bones->at(iRoot), &Fidentity);
#ifdef DEBUG
    check_kinematics(this, dbg_name.c_str());
    RImplementation.BasicStats.Animation.End();
#endif
    VERIFY(LL_GetBonesVisible() != 0);
    // Calculate BOXes/Spheres if needed
    UCalc_Visibox++;
    if (UCalc_Visibox >= psSkeletonUpdate)
    {
        ZoneScopedN("Skeleton update");

        // mark
        UCalc_Visibox = -(::Random.randI(psSkeletonUpdate - 1));

        // the update itself
        Fbox Box;
        Box.invalidate();
        for (u32 b = 0; b < bones->size(); b++)
        {
            if (!LL_GetBoneVisible(u16(b)))
                continue;
            Fobb& obb = (*bones)[b]->obb;
            Fmatrix& Mbone = bone_instances[b].mTransform;
            Fmatrix Mbox;
            obb.xform_get(Mbox);
            Fmatrix X;
            X.mul_43(Mbone, Mbox);
            Fvector& S = obb.m_halfsize;

            Fvector P, A;
            A.set(-S.x, -S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, -S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, -S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, -S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(-S.x, S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, S.y, S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
            A.set(S.x, S.y, -S.z);
            X.transform_tiny(P, A);
            Box.modify(P);
        }
        if (bones->size())
        {
            // previous frame we have updated box - update sphere
            vis.box.vMin = (Box.vMin);
            vis.box.vMax = (Box.vMax);
            vis.box.getsphere(vis.sphere.P, vis.sphere.R);
        }
#ifdef DEBUG
        // Validate
        VERIFY3(_valid(vis.box.vMin) && _valid(vis.box.vMax), "Invalid bones-xform in model", dbg_name.c_str());
        if (vis.sphere.R > 1000.f)
        {
            for (u16 ii = 0; ii < LL_BoneCount(); ++ii)
            {
                Fmatrix tr;
                tr = LL_GetTransform(ii);
                Log("bone ", LL_BoneName_dbg(ii));
                Log("bone_matrix", tr);
            }
            Log("end-------");
        }
        VERIFY3(vis.sphere.R < 1000.f, "Invalid bones-xform in model", dbg_name.c_str());
#endif
    }

    //
    if (Update_Callback)
        Update_Callback(this);
}

#ifdef DEBUG
void check_kinematics(CKinematics* _k, LPCSTR s)
{
    CKinematics* K = _k;
    Fmatrix& MrootBone = K->LL_GetBoneInstance(K->LL_GetBoneRoot()).mTransform;
    if (MrootBone.c.y > 10000)
    {
        Msg("all bones transform:--------[%s]", s);

        for (u16 ii = 0; ii < K->LL_BoneCount(); ++ii)
        {
            Fmatrix tr;

            tr = K->LL_GetTransform(ii);
            Log("bone ", K->LL_BoneName_dbg(ii));
            Log("bone_matrix", tr);
        }
        Log("end-------");
        VERIFY3(0, "check_kinematics failed for ", s);
    }
}
#endif

// Добавить скриптовое смещение для кости --#SM+#--
void CKinematics::LL_AddTransformToBone(KinematicsABT::additional_bone_transform& offset)
{
    m_bones_offsets.push_back(offset);
}

// Обнулить скриптовое смещение для конкретной кости или всех сразу (bone_id = BI_NONE) --#SM+#--
void CKinematics::LL_ClearAdditionalTransform(u16 bone_id)
{
    if (bone_id == BI_NONE)
    {
        m_bones_offsets.clear();
        return;
    }

    BONE_TRANSFORM_VECTOR_IT it = m_bones_offsets.begin();
    while (it != m_bones_offsets.end())
    {
        if (it->m_bone_id == bone_id)
        {
            it = m_bones_offsets.erase(it);
        }
        else
            ++it;
    }
}

void CKinematics::BuildBoneMatrix(
    const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask /*= (1<<0)*/)
{
    bi.mTransform.mul_43(*parent, bd->bind_transform);
    CalculateBonesAdditionalTransforms(bd, bi, parent, channel_mask); //--#SM+#--
}

// Добавляем константные смещения к нужным костям --#SM+#--
void CKinematics::CalculateBonesAdditionalTransforms(
    const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask /* = (1<<0)*/)
{
    // bi.mTransform.c - содержит смещение относительно первой кости модели\центра сцены (0, 0, 0)
    for (auto& it : m_bones_offsets)
    {
        if (it.m_bone_id == bd->GetSelfID())
        {
            const Fvector vOldPos = bi.mTransform.c;
            bi.mTransform.mulB_43(it.m_transform); // Rotation
            bi.mTransform.c.add(vOldPos, it.m_transform.c); // Translation
        }
    }
}

void CKinematics::CLBone(const CBoneData* bd, CBoneInstance& bi, const Fmatrix* parent, u8 channel_mask /*= (1<<0)*/)
{
    ZoneScoped;

    u16 SelfID = bd->GetSelfID();

    if (LL_GetBoneVisible(SelfID))
    {
        if (bi.callback_overwrite())
        {
            if (bi.callback())
                bi.callback()(&bi);
        }
        else
        {
            BuildBoneMatrix(bd, bi, parent, channel_mask);
#ifndef MASTER_GOLD
            R_ASSERT2(_valid(bi.mTransform), "anim kils bone matrix");
#endif // #ifndef MASTER_GOLD
            if (bi.callback())
            {
                bi.callback()(&bi);
#ifndef MASTER_GOLD
                R_ASSERT2(_valid(bi.mTransform), make_string("callback kils bone matrix bone: %s ", bd->name.c_str()));
#endif // #ifndef MASTER_GOLD
            }
        }
        bi.mRenderTransform.mul_43(bi.mTransform, bd->m2b_transform);
    }
}

void CKinematics::Bone_GetAnimPos(Fmatrix& pos, u16 id, u8 mask_channel, bool ignore_callbacks)
{
    ZoneScoped;

    R_ASSERT(id < LL_BoneCount());
    CBoneInstance bi = LL_GetBoneInstance(id);
    BoneChain_Calculate(&LL_GetData(id), bi, mask_channel, ignore_callbacks);
#ifndef MASTER_GOLD
    R_ASSERT(_valid(bi.mTransform));
#endif
    pos.set(bi.mTransform);
}

void CKinematics::Bone_Calculate(CBoneData* bd, Fmatrix* parent)
{
    ZoneScoped;

    u16 SelfID = bd->GetSelfID();
    CBoneInstance& BONE_INST = LL_GetBoneInstance(SelfID);
    CLBone(bd, BONE_INST, parent, u8(-1));
    // Calculate children
    for (xr_vector<CBoneData*>::iterator C = bd->children.begin(); C != bd->children.end(); ++C)
        Bone_Calculate(*C, &BONE_INST.mTransform);
}

void CKinematics::BoneChain_Calculate(const CBoneData* bd, CBoneInstance& bi, u8 mask_channel, bool ignore_callbacks)
{
    ZoneScoped;

    u16 SelfID = bd->GetSelfID();
    // CBlendInstance& BLEND_INST	= LL_GetBlendInstance(SelfID);
    // CBlendInstance::BlendSVec &Blend = BLEND_INST.blend_vector();
    // ignore callbacks
    BoneCallback bc = bi.callback();
    BOOL ow = bi.callback_overwrite();
    if (ignore_callbacks)
    {
        bi.set_callback(bi.callback_type(), nullptr, bi.callback_param(), 0);
    }
    if (SelfID == LL_GetBoneRoot())
    {
        CLBone(bd, bi, &Fidentity, mask_channel);
        // restore callback
        bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
        return;
    }
    u16 ParentID = bd->GetParentID();
    R_ASSERT(ParentID != BI_NONE);
    CBoneData* ParrentDT = &LL_GetData(ParentID);
    CBoneInstance parrent_bi = LL_GetBoneInstance(ParentID);
    BoneChain_Calculate(ParrentDT, parrent_bi, mask_channel, ignore_callbacks);
    CLBone(bd, bi, &parrent_bi.mTransform, mask_channel);
    // restore callback
    bi.set_callback(bi.callback_type(), bc, bi.callback_param(), ow);
}
} // namespace xray::render::RENDER_NAMESPACE
