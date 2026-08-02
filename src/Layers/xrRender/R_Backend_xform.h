#pragma once

namespace xray::render::RENDER_NAMESPACE
{
class ECORE_API R_xforms
{
public:
    Fmatrix m_w; // Basic	- world
    Fmatrix m_invw; // derived	- world2local, cached
    Fmatrix m_v; // Basic	- view
    Fmatrix m_p; // Basic	- projection
    Fmatrix m_wv; // Derived	- world2view
    Fmatrix m_vp; // Derived	- view2projection
    Fmatrix m_wvp; // Derived	- world2view2projection

    // [DA_PORT] World matrix this object had on the PREVIOUS frame, for motion vectors. set_W keeps it
    // equal to the current one, which is the right answer for everything that does not move (the whole
    // static level, where it stays identity) and a safe fallback for anything that moves but does not
    // report its previous transform — such an object gets the camera's motion and none of its own,
    // instead of a garbage vector. Movers call set_W_old right after set_W to supply the real one.
    Fmatrix m_w_old;

    // [DA_PORT] The two motion-vector matrices, kept here rather than behind R_constant_setup binders.
    // Those binders run from set_Pass, i.e. ONCE per shader element - and the scene graph batches by
    // shader, setting the pass once and then drawing dozens of objects, changing only the world matrix.
    // So every object in a batch was handed whichever world matrix happened to be current when the pass
    // was set. Static geometry never noticed: its world matrix is identity throughout, so the stale
    // value was the right one by accident. Characters got a stranger's transform, which is what tore
    // them apart under the upscaler. Following m_wvp instead means they are recomputed per draw call.
    Fmatrix m_wvp_old;   // previous frame's world-view-projection for this object
    Fmatrix m_wvp_nojit; // current one, without the temporal jitter

    R_constant* c_wvp_old;
    R_constant* c_wvp_nojit;

    // [DA_PORT] Оружие и предметы в руках рисуются СВОЕЙ проекцией (узкий HUD-обзор,
    // `hud_transform_helper`), а обе матрицы движения строились из камеры СЦЕНЫ. Значит для всего, что
    // в руках, вектор движения считался между двумя разными проекциями — он огромен и не имеет смысла,
    // и апскейлер тянет историю не оттуда. На тусклом стволе это читается как лёгкая грязь по краю, а
    // на светящейся палочке — как мерцающая кайма (с неё и началось).
    //
    // Пока эти указатели не пусты, обе матрицы строятся из HUD-камеры. Ставит и снимает их
    // `hud_transform_helper`, ровно на время отрисовки HUD.
    const Fmatrix* m_da_prev_VP{ nullptr };  // прошлый кадр, той же проекцией
    const Fmatrix* m_da_nojit_VP{ nullptr }; // этот кадр, той же проекцией

    void da_set_VP_overrides(const Fmatrix* prev_VP, const Fmatrix* nojit_VP)
    {
        m_da_prev_VP = prev_VP;
        m_da_nojit_VP = nojit_VP;
    }

    R_constant* c_w;
    R_constant* c_invw;
    R_constant* c_v;
    R_constant* c_p;
    R_constant* c_wv;
    R_constant* c_vp;
    R_constant* c_wvp;

private:
    bool m_bInvWValid;

public:
    explicit R_xforms(CBackend& cmd_list_in);
    void unmap();
    void set_W(const Fmatrix& m);
    void set_W_old(const Fmatrix& m); // [DA_PORT] motion vectors
    void set_V(const Fmatrix& m);
    void set_P(const Fmatrix& m);
    const Fmatrix& get_W() { return m_w; }
    const Fmatrix& get_V() { return m_v; }
    const Fmatrix& get_P() { return m_p; }
    IC void set_c_w(R_constant* C);
    IC void set_c_invw(R_constant* C);
    IC void set_c_v(R_constant* C);
    IC void set_c_p(R_constant* C);
    IC void set_c_wv(R_constant* C);
    IC void set_c_vp(R_constant* C);
    IC void set_c_wvp(R_constant* C);
    IC void set_c_wvp_old(R_constant* C); // [DA_PORT]
    IC void set_c_wvp_nojit(R_constant* C); // [DA_PORT]

private:
    void apply_invw();

    CBackend& cmd_list;
};
} // namespace xray::render::RENDER_NAMESPACE
