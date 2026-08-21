#pragma once

class CUIWindow;
class CUIStatic;

class XRUICORE_API CUICursor : public pureRender, public CDeviceResetNotifier, public CUIResetNotifier
{
    Fvector2 vPos{};
    CUIStatic* m_static{};
    Fvector2 vPrevPos{};
    Fvector2 correction;
    // [DA_PORT] Начало рамки интерфейса в пикселях экрана (pillarbox/safe zone, см. ui_base.cpp).
    // Курсор живёт в UI-координатах 0..1024x768, поэтому перевод из пикселей мыши и обратно
    // обязан учитывать и масштаб рамки, и её сдвиг — иначе на сверхшироком экране курсор
    // «уедет» от рисуемого интерфейса.
    Fvector2 layout_offset{};
    bool bVisible{};
    bool m_bound_to_system_cursor{};

    void InitInternal();

public:
    CUICursor();
    ~CUICursor() override;

    void Show() { bVisible = true; }
    void Hide() { bVisible = false; }

    [[nodiscard]]
    bool IsVisible() const { return bVisible; }

    void OnRender() override;
    void OnDeviceReset() override;
    void OnUIReset() override;

    void WarpToWindow(const CUIWindow* wnd, bool center = false);
    void UpdateCursorPosition(Fvector2 pos);

    void SetUICursorPosition(Fvector2 pos);

    [[nodiscard]]
    Fvector2 GetCursorPosition() const;

    [[nodiscard]]
    Fvector2 GetCursorPositionDelta() const;
};
