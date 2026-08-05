// CameraManager.cpp: implementation of the CCameraManager class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "IGame_Level.h"
#include "IGame_Persistent.h"

#include "Environment.h"
#include "CameraBase.h"
#include "CameraManager.h"
#include "Effector.h"
#include "EffectorPP.h"

#include "GameFont.h"
#include "Render.h"

// [DA_PORT] TAA: defined in xr_ioc_cmd.cpp / device.cpp — see the jitter block in ApplyDevice below.
extern ENGINE_API int ps_r__taa;
extern ENGINE_API int ps_r__taa_jitter;
extern ENGINE_API bool g_da_jitter_suppress; // [DA_PORT] см. xr_ioc_cmd.cpp
ENGINE_API int ps_da_jitter_log = 0; // [DA_PORT] покадровый замер, см. ниже
extern ENGINE_API int ps_r__fsr2;
// [DA_PORT] "is a temporal upscaler reconstructing this frame" - one list, see xr_ioc_cmd.cpp.
extern ENGINE_API bool da_upscaler_active();
extern ENGINE_API Fvector2 g_da_taa_jitter;
extern ENGINE_API Fvector2 g_da_fsr2_jitter_px;

float psCamInert = 0.f;
float psCamSlideInert = 0.25f;

SPPInfo pp_identity;
SPPInfo pp_zero;

CCameraManager::CCameraManager(bool bApplyOnUpdate)
{
#ifdef DEBUG
    dbg_upd_frame = 0;
#endif

    m_bAutoApply = bApplyOnUpdate;

    pp_identity.blur = 0;
    pp_identity.gray = 0;
    pp_identity.duality.h = 0;
    pp_identity.duality.v = 0;
    pp_identity.noise.intensity = 0;
    pp_identity.noise.grain = 1.0f;
    pp_identity.noise.fps = 30;
    pp_identity.color_base.set(.5f, .5f, .5f);
    pp_identity.color_gray.set(.333f, .333f, .333f);
    pp_identity.color_add.set(0, 0, 0);

    pp_zero.blur = pp_zero.gray = pp_zero.duality.h = pp_zero.duality.v = 0.0f;
    pp_zero.noise.intensity = 0;
    pp_zero.noise.grain = 0.0f;
    pp_zero.noise.fps = 0.0f;
    pp_zero.color_base.set(0, 0, 0);
    pp_zero.color_gray.set(0, 0, 0);
    pp_zero.color_add.set(0, 0, 0);

    pp_affected = pp_identity;
}

CCameraManager::~CCameraManager()
{
    for (auto it = m_EffectorsCam.begin(); it != m_EffectorsCam.end(); ++it)
        xr_delete(*it);

    for (auto it = m_EffectorsPP.begin(); it != m_EffectorsPP.end(); ++it)
        xr_delete(*it);
}

CEffectorCam* CCameraManager::GetCamEffector(ECamEffectorType type)
{
    for (auto it = m_EffectorsCam.begin(); it != m_EffectorsCam.end(); ++it)
        if ((*it)->eType == type)
        {
            return *it;
        }
    return 0;
}

CEffectorCam* CCameraManager::AddCamEffector(CEffectorCam* ef)
{
    m_EffectorsCam_added_deffered.push_back(ef);
    return m_EffectorsCam_added_deffered.back();
}

void CCameraManager::UpdateDeffered()
{
    for (auto& effector : m_EffectorsCam_added_deffered)
    {
        RemoveCamEffector(effector->eType);

        if (effector->AbsolutePositioning())
            m_EffectorsCam.push_front(effector);
        else
            m_EffectorsCam.push_back(effector);
    }

    m_EffectorsCam_added_deffered.clear();
}

void CCameraManager::RemoveCamEffector(ECamEffectorType type)
{
    for (auto it = m_EffectorsCam.begin(); it != m_EffectorsCam.end(); ++it)
        if ((*it)->eType == type)
        {
            OnEffectorReleased(*it);
            m_EffectorsCam.erase(it);
            return;
        }
}

CEffectorPP* CCameraManager::GetPPEffector(EEffectorPPType type)
{
    for (auto& effector : m_EffectorsPP)
        if (effector->Type() == type)
            return effector;
    return nullptr;
}

ECamEffectorType CCameraManager::RequestCamEffectorId()
{
    ECamEffectorType index = (ECamEffectorType)effCustomEffectorStartID;
    for (; GetCamEffector(index); index = (ECamEffectorType)(index + 1))
    {
        ;
    }
    return index;
}

EEffectorPPType CCameraManager::RequestPPEffectorId()
{
    EEffectorPPType index = (EEffectorPPType)effCustomEffectorStartID;
    for (; GetPPEffector(index); index = (EEffectorPPType)(index + 1))
    {
        ;
    }
    return index;
}

CEffectorPP* CCameraManager::AddPPEffector(CEffectorPP* ef)
{
    RemovePPEffector(ef->Type());
    m_EffectorsPP.push_back(ef);
    return m_EffectorsPP.back();
}

void CCameraManager::RemovePPEffector(EEffectorPPType type)
{
    for (auto it = m_EffectorsPP.begin(); it != m_EffectorsPP.end(); ++it)
        if ((*it)->Type() == type)
        {
            if ((*it)->FreeOnRemove())
            {
                OnEffectorReleased(*it);
                // xr_delete (*it);
            }
            m_EffectorsPP.erase(it);
            return;
        }
}

void CCameraManager::OnEffectorReleased(SBaseEffector* e)
{
    if (!e->m_on_b_remove_callback.empty())
        e->m_on_b_remove_callback();

    xr_delete(e);
}

void CCameraManager::UpdateFromCamera(const CCameraBase* C)
{
    Update(C->vPosition, C->vDirection, C->vNormal, C->f_fov, C->f_aspect,
        g_pGamePersistent->Environment().CurrentEnv.far_plane, C->m_Flags.flags);
}

void CCameraManager::Update(const Fvector& P, const Fvector& D, const Fvector& N, float fFOV_Dest, float fASPECT_Dest,
    float fFAR_Dest, u32 flags)
{
    ZoneScoped;
#ifdef DEBUG
    if (!Device.Paused())
    {
        VERIFY(dbg_upd_frame != Device.dwFrame); // already updated !!!
        dbg_upd_frame = Device.dwFrame;
    }
#endif // DEBUG
    // camera
    if (flags & CCameraBase::flPositionRigid)
        m_cam_info.p.set(P);
    else
        m_cam_info.p.inertion(P, psCamInert);
    if (flags & CCameraBase::flDirectionRigid)
    {
        m_cam_info.d.set(D);
        m_cam_info.n.set(N);
    }
    else
    {
        m_cam_info.d.inertion(D, psCamInert);
        m_cam_info.n.inertion(N, psCamInert);
    }

    // Normalize
    m_cam_info.d.normalize();
    m_cam_info.n.normalize();
    m_cam_info.r.crossproduct(m_cam_info.n, m_cam_info.d);
    m_cam_info.n.crossproduct(m_cam_info.d, m_cam_info.r);

    float aspect = Device.fHeight_2 / Device.fWidth_2;
    float src = 10 * Device.fTimeDelta;
    clamp(src, 0.f, 1.f);
    float dst = 1 - src;
    m_cam_info.fFov = m_cam_info.fFov * dst + fFOV_Dest * src;
    m_cam_info.fNear = VIEWPORT_NEAR;
    m_cam_info.fFar = m_cam_info.fFar * dst + fFAR_Dest * src;
    m_cam_info.fAspect = m_cam_info.fAspect * dst + (fASPECT_Dest * aspect) * src;
    m_cam_info.dont_apply = false;

    UpdateCamEffectors();

    UpdatePPEffectors();

    if (!m_cam_info.dont_apply && m_bAutoApply)
        ApplyDevice();

    UpdateDeffered();
}

bool CCameraManager::ProcessCameraEffector(CEffectorCam* eff)
{
    // Do NOT delete effector here! It's unsafe because:
    // 1. Leads to failed iterators in UpdateCamEffectors
    // 2. Child classes with overrided ProcessCameraEffector would be surprised if eff becames invalid pointer
    // The best way - return 'false' when the effector should be deleted, and delete it in ProcessCameraEffector

    bool res = false;
    if (eff->Valid() && eff->ProcessCam(m_cam_info))
    {
        res = true;
    }
    else if (eff->AllowProcessingIfInvalid())
    {
        eff->ProcessIfInvalid(m_cam_info);
    }
    return res;
}

void CCameraManager::UpdateCamEffectors()
{
    if (m_EffectorsCam.empty())
        return;

    auto r_it = m_EffectorsCam.rbegin();
    while (r_it != m_EffectorsCam.rend())
    {
        if (ProcessCameraEffector(*r_it))
            ++r_it;
        else
        {
            // Dereferencing reverse iterator returns previous element of the list, r_it.base() returns current element
            // So, we should use base()-1 iterator to delete just processed element. 'Previous' element would be
            // automatically changed after deletion, so r_it would dereferencing to another value, no need to change it
            OnEffectorReleased(*r_it);
            auto r_to_del = r_it.base();
            m_EffectorsCam.erase(--r_to_del);
        }
    }

    m_cam_info.d.normalize();
    m_cam_info.n.normalize();
    m_cam_info.r.crossproduct(m_cam_info.n, m_cam_info.d);
    m_cam_info.n.crossproduct(m_cam_info.d, m_cam_info.r);
}

void CCameraManager::UpdatePPEffectors()
{
    pp_affected.validate("before applying pp");

    int _count = 0;
    if (m_EffectorsPP.size())
    {
        bool b = false;
        pp_affected = pp_identity;
        for (int i = m_EffectorsPP.size() - 1; i >= 0; --i)
        {
            CEffectorPP* eff = m_EffectorsPP[i];
            SPPInfo l_PPInf = pp_zero;
            if (eff->Valid() && eff->Process(l_PPInf))
            {
                ++_count;
                if (!b)
                {
                    pp_affected.add(l_PPInf);
                    pp_affected.sub(pp_identity);
                    pp_affected.validate("in cycle");
                }
                if (!eff->bOverlap)
                {
                    b = true;
                    pp_affected = l_PPInf;
                }
            }
            else
                RemovePPEffector(eff->Type());
        }
        if (0 == _count)
            pp_affected = pp_identity;
        else
            pp_affected.normalize();
    }
    else
    {
        pp_affected = pp_identity;
    }

    if (!positive(pp_affected.noise.grain))
        pp_affected.noise.grain = pp_identity.noise.grain;

    pp_affected.validate("after applying pp");
}

void CCameraManager::ApplyDevice()
{
    ZoneScoped;
    // Device params
    Device.mView.build_camera_dir(m_cam_info.p, m_cam_info.d, m_cam_info.n);

    Device.vCameraPosition.set(m_cam_info.p);
    Device.vCameraDirection.set(m_cam_info.d);
    Device.vCameraTop.set(m_cam_info.n);
    Device.vCameraRight.set(m_cam_info.r);

    // projection
    Device.fFOV = m_cam_info.fFov;
    Device.fASPECT = m_cam_info.fAspect;
    Device.mProject.build_projection(deg2rad(m_cam_info.fFov), m_cam_info.fAspect, m_cam_info.fNear, m_cam_info.fFar);

    // Apply offset required for Nvidia Ansel
    Device.mProject._31 = -m_cam_info.offsetX;
    Device.mProject._32 = -m_cam_info.offsetY;

    // [DA_PORT] TAA projection jitter. Reprojecting the previous frame only removes temporal noise; the
    // actual anti-aliasing comes from moving the sample point around inside the pixel and letting the
    // history average those samples together. Halton(2,3) is the usual sequence for it — 8 phases spread
    // evenly over the pixel with no clumping. _31/_32 shift the projection in NDC, which is exactly what
    // Ansel above uses them for, so this rides on an offset the engine already supports.
    g_da_taa_jitter.set(0.f, 0.f);
    // [DA_PORT] Пиксельный джиттер сбрасывается ЗДЕСЬ ЖЕ. Раньше он только присваивался внутри
    // условия ниже и никогда не обнулялся: страховкой служил ноль в cl_taa_jitter под «без
    // апскейлера». Теперь сдвиг выдаётся всегда, поэтому не сброшенное значение осталось бы
    // висеть на геометрии после выключения и TAA, и апскейлера.
    g_da_fsr2_jitter_px.set(0.f, 0.f);

    // Not while the menu is up. Since the paused scene is now drawn behind the menu, the menu itself
    // ends up inside the temporal history — and with the projection shifting by a sub-pixel every frame
    // the static text smears into stripes as the history accumulates. The scene is frozen anyway, so
    // there is nothing for the jitter to resolve here.
    const bool menu_up = g_pGamePersistent && g_pGamePersistent->m_pMainMenu && g_pGamePersistent->m_pMainMenu->IsActive();

    // [DA_PORT] Every temporal upscaler needs the jitter just as much as our own temporal AA does — it
    // is what gives them sub-pixel samples to reconstruct from, and each is told the exact offset every
    // frame. So the jitter follows any consumer being active, not just r__taa.
    //
    // This read "ps_r__fsr2" while FSR 3 and XeSS were added beside it, so selecting either produced no
    // jitter at all: the upscaler was handed an offset of zero and the same sub-pixel positions every
    // frame, leaving it nothing to reconstruct from. Fine detail at a distance could then never resolve
    // however long the camera stood still, which is what "objects in the distance smear" turned out to
    // be. Now behind da_upscaler_active(), so the next upscaler cannot repeat it.
    if ((ps_r__taa || da_upscaler_active()) && ps_r__taa_jitter && !g_da_jitter_suppress && !menu_up && Device.dwRenderWidth &&
        Device.dwRenderHeight)
    {
        // [DA_PORT] Generated exactly the way FSR 2 specifies, because it has to undo this offset and
        // will only do so correctly if both sides agree on the sequence AND on the sign convention.
        // Feeding it our own 8-phase Halton left part of the jitter uncompensated and the whole picture
        // shook — and no combination of signs on the hand-off fixed it, because the mismatch was in the
        // sequence itself.
        //
        // The phase count grows with the upscaling ratio: reconstructing more output pixels from each
        // rendered one needs proportionally more sub-pixel positions to sample. AMD's formula is
        // 8 * (display/render)^2, so at 70% it is about 16 phases rather than 8.
        const float ratio = float(Device.dwWidth) / float(Device.dwRenderWidth);
        const int phase_count = std::max(8, int(8.f * ratio * ratio));
        const int index = int(Device.dwFrame % u32(phase_count)) + 1;

        // Halton, computed rather than tabulated: the phase count is no longer a fixed 8.
        auto halton = [](int i, int base)
        {
            float f = 1.f, r = 0.f;
            while (i > 0)
            {
                f /= float(base);
                r += f * float(i % base);
                i /= base;
            }
            return r;
        };

        // In PIXELS, centred on the pixel — this is the form FSR 2 is handed.
        g_da_fsr2_jitter_px.set(halton(index, 2) - 0.5f, halton(index, 3) - 0.5f);

        // The same offset expressed for the projection matrix, where the whole target spans 2.
        // Fixed, deliberately: the projection and the value handed to FSR 2 must describe the SAME
        // shift, so the sign experiment belongs on the hand-off (r__fsr2_jitter_sign, applied in
        // phase_fsr2) and not here. Flipping it here changed what was applied while leaving what
        // was reported untouched, which pulled the two apart instead of aligning them.
        g_da_taa_jitter.set(2.f * g_da_fsr2_jitter_px.x / float(Device.dwRenderWidth),
            2.f * g_da_fsr2_jitter_px.y / float(Device.dwRenderHeight));

        // [DA_PORT] Джиттер НЕ идёт в матрицу проекции ни в одном режиме — его накладывают сами
        // шейдеры сцены, из константы m_taa_jitter (см. cl_taa_jitter в r2.cpp).
        //
        // Раньше через матрицу шла наша собственная темпоралка, а апскейлеры — через шейдеры, и это
        // разошлось в двух местах сразу. Матрица правит ещё и каскады теней, частицы и HUD, которым
        // сдвиг никто не компенсирует. А главное: снятие сдвига в gbuffer_load_data сделано ровно на
        // m_taa_jitter, а под нашей темпоралкой эта константа была нулевой — «поправка обращается в
        // ничто», как и написано там в комментарии. Картинка при этом сдвинута матрицей, позиция
        // восстанавливается из несдвинутой экранной координаты, история в da_taa.ps ложится мимо на
        // величину джиттера — и кадр ездит влево-вправо с частотой кадров. На низком FPS это видно
        // как рывки, на высоком сливается в мыло.
        //
        // Теперь путь один и тот же для TAA и для апскейлеров — тот, который заведомо не качает.
        // Плата: под TAA тени, частицы и HUD больше не дрожат, то есть сглаживаются слабее. Это
        // ровно то поведение, которое у апскейлеров и так работает.
    }

    // [DA_PORT] Покадровый замер джиттера: da_jitter_log <кадров>.
    //
    // Нужен потому, что «трясёт» — это наблюдение, а не число. Сам сдвиг обязан быть мелким и
    // ровным: доли пикселя, меняющиеся по Halton. Если он таким и окажется, виноват не он, а
    // потребитель — и дальше искать надо там, а не крутить знаки здесь.
    //
    // Вместе со сдвигом печатается всё, от чего он зависит, и заодно постэффект: у аномалий свой
    // .ppe, а зерно шума и двоение — ровно те величины, которые ломают накопление кадров под
    // апскейлером и выглядят как тряска.
    //
    // И сама камера — позиция, направление, угол обзора. Дрожащая камера и дрожащий разбор кадра
    // с экрана неотличимы, но лечатся в разных местах: первая — в игре, второй — в рендере.
    // Разделяет их только замер: у сдвига размах заведомо в доли пикселя, у камеры покадровая
    // разница на стоящем игроке обязана быть нулевой.
    //
    // Время кадра — третья величина, без которой первые две не разводятся. Неровный ход кадров
    // джиттер превращает в видимую дрожь: каждый кадр берётся из своей точки внутри пикселя, а
    // держится на экране разное время, и усредниться они не успевают. С выключенным сдвигом та
    // же неровность глазу незаметна — все кадры сняты из одной точки. Отсюда и наблюдение
    // «r__taa_jitter 0 лечит»: оно указывает не на сдвиг, а на того, кто портит ход кадров.
    //
    // ЧЕМ КОНЧИЛОСЬ. Замером снято обвинение с самого сдвига: на сейве внутри электрической
    // аномалии он ровно такой, каким обязан быть — ±0.44 пикселя по Halton, постпроцесс
    // единичный (зерно 1, шум и двоение по нулям), разрешение не плавает.
    //
    // Виноват оказался не сдвиг, а то, ЧЕЙ кадр показывают: зона включает цветокоррекцию,
    // постобработка переключается на второй вариант шейдера (`E[u_need_CM() ? 4 : 0]`), а в нём
    // нашего порта не было вовсе — выход апскейлера выбрасывался, на экран растягивалась сырая
    // сцена вместе с этим самым сдвигом. См. postprocess_cm.ps и 12_FAQ_ROADMAP.md.
    //
    // Урок на будущее для этой пробы: она честно доказала, что сдвиг исправен, и этого хватило,
    // чтобы перестать крутить знаки и искать потребителя. Но «сдвиг исправен» не значит «дефект
    // не в нём проявляется» — половина пути прошла впустую, пока я искал того, кто сдвиг
    // ПОРТИТ, вместо того кто его НЕ СНИМАЕТ.
    if (ps_da_jitter_log > 0)
    {
        --ps_da_jitter_log;
        Msg("~ [DA_JIT] кадр %u | сдвиг пикс %+.4f %+.4f | клип %+.6f %+.6f | рендер %ux%u -> %ux%u "
            "| taa %d апскейлер %d подавлен %d меню %d | зерно %.3f шум %.3f двоение %.3f %.3f "
            "| размытие %.3f | камера %.4f %.4f %.4f | взгляд %.5f %.5f %.5f | обзор %.3f "
            "| кадр %.2f мс",
            Device.dwFrame, g_da_fsr2_jitter_px.x, g_da_fsr2_jitter_px.y, g_da_taa_jitter.x,
            g_da_taa_jitter.y, Device.dwRenderWidth, Device.dwRenderHeight, Device.dwWidth,
            Device.dwHeight, ps_r__taa, da_upscaler_active() ? 1 : 0, g_da_jitter_suppress ? 1 : 0,
            menu_up ? 1 : 0, pp_affected.noise.grain, pp_affected.noise.intensity,
            pp_affected.duality.h, pp_affected.duality.v, pp_affected.blur, m_cam_info.p.x,
            m_cam_info.p.y, m_cam_info.p.z, m_cam_info.d.x, m_cam_info.d.y, m_cam_info.d.z,
            m_cam_info.fFov, Device.fTimeDelta * 1000.f);
        if (ps_da_jitter_log == 0)
            Msg("~ [DA_JIT] ---- done ----");
    }

    if (g_pGamePersistent && g_pGamePersistent->m_pMainMenu->IsActive())
        ResetPP();
    else
    {
        pp_affected.validate("apply device");
        // postprocess
        clamp(pp_affected.noise.grain, EPS_L, 1000.0f);
        GEnv.Render->SetPostProcessParams(pp_affected);
    }
}

void CCameraManager::ResetPP()
{
    SPPInfo params = pp_identity;
    params.cm_influence = 0.0f;
    params.cm_interpolate = 1.0f;
    params.cm_tex1 = "";
    params.cm_tex2 = "";
    GEnv.Render->SetPostProcessParams(params);
}

void CCameraManager::Dump()
{
    Fmatrix mInvCamera;
    mInvCamera.invert(Device.mView);

    const Fvector right{ mInvCamera._11, mInvCamera._12, mInvCamera._13 };
    const Fvector normal{ mInvCamera._21, mInvCamera._22, mInvCamera._23 };
    const Fvector direction{ mInvCamera._31, mInvCamera._32, mInvCamera._33 };
    const Fvector position{ mInvCamera._41, mInvCamera._42, mInvCamera._43 };

    Log("CCameraManager::Dump::vPosition = ", position);
    Log("CCameraManager::Dump::vDirection = ", direction);
    Log("CCameraManager::Dump::vNormal = ", normal);
    Log("CCameraManager::Dump::vRight = ", right);
}
