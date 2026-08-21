#pragma once

#include "ui_defs.h"
#include "ui_debug.h"
#include "ui_focus.h"
#include "FontManager/FontManager.h"

#include "xrEngine/pure.h"
#include "xrEngine/device.h"

#include "xrCommon/xr_vector.h"
#include "xrCommon/xr_stack.h"

class CUICursor;
class CUIGameCustom;

class XRUICORE_API UICore : public CDeviceResetNotifier, public CUIResetNotifier
{
    C2DFrustum m_2DFrustum;
    C2DFrustum m_2DFrustumPP;
    C2DFrustum m_FrustumLIT;

    bool m_bPostprocess;

    CFontManager* m_pFontManager;
    CUICursor* m_pUICursor;
    CUIDebugger m_debugger;
    CUIFocusSystem m_focusSystem;

    Fvector2 m_pp_scale_;
    Fvector2 m_scale_;
    Fvector2* m_current_scale;

    // [DA_PORT] Сдвиг начала UI-координат в пикселях экрана. Нулевой на обычных экранах; ненулевой,
    // когда интерфейс ужат в центр (pillarbox на сверхшироких) или отодвинут от краёв (safe zone).
    // Пара к m_scale_: отдельная для постпроцесса — полноэкранные эффекты сдвигать нельзя.
    Fvector2 m_ui_offset_;
    Fvector2 m_pp_offset_;
    Fvector2* m_current_offset;

public:
    xr_stack<Frect> m_Scissors;

    UICore();
    ~UICore();
    void ReadTextureInfo();
    CFontManager& Font() { return *m_pFontManager; }
    CUICursor& GetUICursor() { return *m_pUICursor; }
    auto& Focus() { return m_focusSystem; }
    auto& Debugger() { return m_debugger; }
    IC float ClientToScreenScaledX(float left) const { return left * m_current_scale->x + m_current_offset->x; };
    IC float ClientToScreenScaledY(float top) const { return top * m_current_scale->y + m_current_offset->y; };
    void ClientToScreenScaled(Fvector2& dest, float left, float top) const;
    void ClientToScreenScaled(Fvector2& src_and_dest) const;
    void ClientToScreenScaledWidth(float& src_and_dest) const;
    void ClientToScreenScaledHeight(float& src_and_dest) const;
    void AlignPixel(float& src_and_dest) const;

    const C2DFrustum& ScreenFrustum() const { return (m_bPostprocess) ? m_2DFrustumPP : m_2DFrustum; }
    C2DFrustum& ScreenFrustumLIT() { return m_FrustumLIT; }
    void PushScissor(const Frect& r, bool overlapped = false);
    void PopScissor();

    void pp_start();
    void pp_stop();
    void RenderFont();

    virtual void OnDeviceReset();
    void OnUIReset() override;
    static bool is_widescreen();

    // [DA_PORT] Сверхширокий экран: 21:9 и шире. Порог 1.8 взят у Monolith и OGSR — там он тот же,
    // и совпадение важно: по нему обе стороны решают, брать ли набор разметки `_21`, а разметку эту
    // рисует сообщество. Разойдись мы в пороге — чужой готовый файл брался бы не на тех экранах.
    static bool is_ultra_widescreen();
    static float get_current_kx();
    static shared_str get_xml_name(pcstr path, pcstr fn);

    // [DA_PORT] Размер и положение прямоугольника экрана (в пикселях), в который укладывается
    // интерфейс: весь экран на обычных соотношениях, центральная область 16:9 на сверхшироких
    // (ui_pillarbox) минус отступ безопасной зоны (ui_safe_zone, процентов с каждой стороны).
    static void GetUILayoutMetrics(float& w, float& h, float& ox, float& oy);
    // Пересчитать m_scale_/m_ui_offset_, если со старта кадра поменялось разрешение или крутилки.
    // Дёргается из рендера курсора (он на seqRender каждый кадр): так ползунки меню подгонки
    // действуют сразу, без vid_restart.
    void UpdateLayout();

    IUIRender::ePointType m_currentPointType;
};

XRUICORE_API extern CUICursor& GetUICursor();
XRUICORE_API extern UICore& UI();
