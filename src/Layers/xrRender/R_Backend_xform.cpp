#include "stdafx.h"
#pragma hdrstop

#include "R_Backend_xform.h"

// [DA_PORT] Camera matrices for motion vectors, owned by the engine. Declared outside the namespace:
// an extern written inside xray::render would look for the symbol in that namespace instead.
extern ENGINE_API Fmatrix g_da_taa_unjittered_VP;

namespace xray::render::RENDER_NAMESPACE
{
// Previous frame's view-projection, captured at the end of each frame (r2_R_render.cpp).
extern Fmatrix g_da_prev_VP;


void R_xforms::set_W(const Fmatrix& m)
{
    m_w.set(m);
    // [DA_PORT] Default the previous-frame world to the current one; movers overwrite it via set_W_old
    // immediately afterwards. Doing it here rather than leaving the last object's matrix behind matters:
    // these are set per draw call, and a stale value from an unrelated object would produce vectors
    // pointing at nothing.
    m_w_old.set(m);
    m_wv.mul_43(m_v, m_w);
    m_wvp.mul(m_p, m_wv);
    // [DA_PORT] Rebuilt for THIS object, on the same footing as m_wvp - see the header for why they
    // cannot be constant-setup binders. With m_w_old just defaulted to the current world, both describe
    // an object that has not moved of its own accord; a mover corrects the first one via set_W_old.
    m_wvp_old.mul(g_da_prev_VP, m_w_old);
    m_wvp_nojit.mul(g_da_taa_unjittered_VP, m_w);
    if (c_wvp_old)
        cmd_list.set_c(c_wvp_old, m_wvp_old);
    if (c_wvp_nojit)
        cmd_list.set_c(c_wvp_nojit, m_wvp_nojit);
    if (c_w)
        cmd_list.set_c(c_w, m_w);
    if (c_wv)
        cmd_list.set_c(c_wv, m_wv);
    if (c_wvp)
        cmd_list.set_c(c_wvp, m_wvp);
    m_bInvWValid = false;
    if (c_invw)
        apply_invw();
    cmd_list.set_xform(D3DTS_WORLD, m);
}
// [DA_PORT] Supplies the real previous-frame transform for something that moves. Always called right
// after set_W, which has already defaulted it, so only the one derived matrix needs redoing.
void R_xforms::set_W_old(const Fmatrix& m)
{
    m_w_old.set(m);
    m_wvp_old.mul(g_da_prev_VP, m_w_old);
    if (c_wvp_old)
        cmd_list.set_c(c_wvp_old, m_wvp_old);
}

void R_xforms::set_V(const Fmatrix& m)
{
    m_v.set(m);
    m_wv.mul_43(m_v, m_w);
    m_vp.mul(m_p, m_v);
    m_wvp.mul(m_p, m_wv);
    if (c_v)
        cmd_list.set_c(c_v, m_v);
    if (c_vp)
        cmd_list.set_c(c_vp, m_vp);
    if (c_wv)
        cmd_list.set_c(c_wv, m_wv);
    if (c_wvp)
        cmd_list.set_c(c_wvp, m_wvp);
    cmd_list.set_xform(D3DTS_VIEW, m);
}
void R_xforms::set_P(const Fmatrix& m)
{
    m_p.set(m);
    m_vp.mul(m_p, m_v);
    m_wvp.mul(m_p, m_wv);
    if (c_p)
        cmd_list.set_c(c_p, m_p);
    if (c_vp)
        cmd_list.set_c(c_vp, m_vp);
    if (c_wvp)
        cmd_list.set_c(c_wvp, m_wvp);
    // always setup projection - D3D relies on it to work correctly :(
    cmd_list.set_xform(D3DTS_PROJECTION, m);
}

void R_xforms::apply_invw()
{
    VERIFY(c_invw);

    if (!m_bInvWValid)
    {
        m_invw.invert_b(m_w);
        m_bInvWValid = true;
    }

    cmd_list.set_c(c_invw, m_invw);
}

void R_xforms::unmap()
{
    c_w = nullptr;
    c_invw = nullptr;
    c_v = nullptr;
    c_p = nullptr;
    c_wv = nullptr;
    c_vp = nullptr;
    c_wvp = nullptr;
    c_wvp_old = nullptr; // [DA_PORT]
    c_wvp_nojit = nullptr; // [DA_PORT]
}
R_xforms::R_xforms(CBackend& cmd_list_in)
    : cmd_list(cmd_list_in)
{
    unmap();
    m_w.identity();
    m_w_old.identity(); // [DA_PORT] motion vectors
    m_invw.identity();
    m_v.identity();
    m_p.identity();
    m_wv.identity();
    m_vp.identity();
    m_wvp.identity();
    m_wvp_old.identity(); // [DA_PORT]
    m_wvp_nojit.identity(); // [DA_PORT]
    m_bInvWValid = true;
}
} // namespace xray::render::RENDER_NAMESPACE
