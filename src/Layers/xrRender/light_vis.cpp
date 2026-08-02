#include "stdafx.h"
#include "Layers/xrRender/light.h"
#include "xrCDB/Intersect.hpp"

namespace xray::render::RENDER_NAMESPACE
{
const u32 delay_small_min = 1;
const u32 delay_small_max = 3;
const u32 delay_large_min = 10;
const u32 delay_large_max = 20;
const u32 cullfragments = 4;

void light::vis_prepare(CBackend& cmd_list)
{
    if (int(indirect_photons) != ps_r2_GI_photons)
        gi_generate();

    //	. test is sheduled for future	= keep old result
    //	. test time comes :)
    //		. camera inside light volume	= visible,	shedule for 'small' interval
    //		. perform testing				= ???,		pending

    u32 frame = Device.dwFrame;
    if (frame < vis.frame2test)
        return;

    float safe_area = VIEWPORT_NEAR;
    {
        float a0 = deg2rad(Device.fFOV * Device.fASPECT / 2.f);
        float a1 = deg2rad(Device.fFOV / 2.f);
        float x0 = VIEWPORT_NEAR / _cos(a0);
        float x1 = VIEWPORT_NEAR / _cos(a1);
        float c = _sqrt(x0 * x0 + x1 * x1);
        safe_area = _max(_max(VIEWPORT_NEAR, _max(x0, x1)), c);
    }

    // Msg	("sc[%f,%f,%f]/c[%f,%f,%f] - sr[%f]/r[%f]",VPUSH(spatial.center),VPUSH(position),spatial.radius,range);
    // Msg	("dist:%f, sa:%f",Device.vCameraPosition.distance_to(spatial.center),safe_area);
    bool skiptest = false;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_DONT_TEST_UNSHADOWED) && !flags.bShadow)
        skiptest = true;
    if (ps_r2_ls_flags.test(R2FLAG_EXP_DONT_TEST_SHADOWED) && flags.bShadow)
        skiptest = true;

    if (skiptest || Device.vCameraPosition.distance_to(spatial.sphere.P) <= (spatial.sphere.R * 1.01f + safe_area))
    { // small error
        vis.visible = true;
        vis.pending = false;
        vis.frame2test = frame + ::Random.randI(delay_small_min, delay_small_max);
        return;
    }

    // testing
    vis.pending = true;
    xform_calc();
    cmd_list.set_xform_world(m_xform);
    vis.query_order = RImplementation.occq_begin(vis.query_id);
    //	Hack: Igor. Light is visible if it's frutum is visible. (Only for volumetric)
    //	Hope it won't slow down too much since there's not too much volumetric lights
    //	TODO: sort for performance improvement if this technique hurts
    if ((flags.type == IRender_Light::SPOT) && flags.bShadow && flags.bVolumetric)
        cmd_list.set_Stencil(FALSE);
    else
        cmd_list.set_Stencil(TRUE, D3DCMP_LESSEQUAL, 0x01, 0xff, 0x00);
    RImplementation.Target->draw_volume(cmd_list, this);
    RImplementation.occq_end(vis.query_id);
}

void light::vis_update()
{
    //	. not pending	->>> return (early out)
    //	. test-result:	visible:
    //		. shedule for 'large' interval
    //	. test-result:	invisible:
    //		. shedule for 'next-frame' interval

    if (!vis.pending)
        return;

    const u32 frame = Device.dwFrame;
    const auto fragments = RImplementation.occq_get(vis.query_id);
    // Log					("",fragments);

    // [DA_PORT] Гистерезис вместо мгновенного приговора: лампа гаснет только после нескольких
    // отрицательных проверок подряд, а загорается от первой же положительной.
    //
    // Что было. Ответ занимает считанные фрагменты: лампу видно через дверной проём десятком
    // пикселей, порог жёсткий (`cullfragments`), и она то проходит его, то нет. При этом «видно»
    // назначает следующую проверку через 10-20 кадров, а «не видно» - через один: источник гаснет
    // мгновенно и загорается через десяток кадров. На базе, где ламп много и все они за косяками и
    // решётками, это выглядело как нескончаемое мигание света.
    //
    // Обратная сторона порога не менее важна: с апскейлером кадр рисуется в 60-70% разрешения, и
    // площадь того же силуэта в ПИКСЕЛЯХ падает вдвое - те же лампы оказываются к порогу вдвое ближе.
    //
    // Односторонний счётчик, а не «два порога»: число фрагментов дрожит слишком сильно, чтобы
    // сравнивать его с чем-то ещё, а «трижды подряд не видно» - устойчивый признак того, что лампа
    // действительно скрылась. Задержка расплаты - три проверки по одному кадру, на глаз незаметна.
    //
    // Выключить проверку целиком (`r2_exp_donttest_shad on`) мигание тоже убирает, но выходом не
    // является: в игре при этом часть ламп стоит ТЁМНЫМИ, пока не подойдёшь ближе. Механизм этого
    // побочного эффекта не выяснен - к слоту теневой карты он отношения не имеет, `s_finalclip`
    // считает совсем другое (пустую сцену со стороны источника). Здесь важно лишь то, что флаг
    // лечит симптом и приносит свой; поэтому проверка остаётся, а чинится её устойчивость.
    const bool seen = (fragments > cullfragments);
    vis.pending = false;

    if (seen)
    {
        vis.miss_streak = 0;
        vis.visible = true;
        vis.frame2test = frame + ::Random.randI(delay_large_min, delay_large_max);
        return;
    }

    if (vis.miss_streak < 255)
        ++vis.miss_streak;

    // Пока промахов меньше трёх - лампа считается видимой и проверяется снова на следующем кадре.
    if (vis.miss_streak < 3)
    {
        vis.frame2test = frame + 1;
        return;
    }

    vis.visible = false;
    vis.frame2test = frame + 1;
}
} // namespace xray::render::RENDER_NAMESPACE
