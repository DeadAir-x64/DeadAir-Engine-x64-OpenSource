////////////////////////////////////////////////////////////////////////////
//	Module 		: material_manager.h
//	Created 	: 27.12.2003
//  Modified 	: 27.12.2003
//	Author		: Dmitriy Iassenev
//	Description : Material manager
////////////////////////////////////////////////////////////////////////////

#pragma once

#include "xrMaterialSystem/GameMtlLib.h"
#include "PHMovementControl.h"
class CPHMovementControl;

class CMaterialManager
{
private:
    bool m_run_mode;
    float m_time_to_step;
    u32 m_step_id;
    u16 m_my_material_idx;
    ref_sound m_step_sound[4];
    IGameObject* m_object;
    CPHMovementControl* m_movement_control;

protected:
    u16 m_last_material_idx;

public:
    CMaterialManager(IGameObject* object, CPHMovementControl* movement_control);
    virtual ~CMaterialManager();
    virtual void Load(LPCSTR section);
    virtual void reinit();
    virtual void reload(LPCSTR section);
    virtual void set_run_mode(bool run_mode);
    virtual void update(float time_delta, float volume, float step_time, bool standing);
    IC u16 last_material_idx() const;
    IC u16 self_material_idx() const;
    IC SGameMtlPair* get_current_pair();

    // [DA_PORT] Пара БЕЗ подмены на воду: нужна для частиц. Звук в луже должен быть водяным, а вот
    // частицы — нет: у водяного материала это круги-всплески, и на мелкой луже они выглядят так, будто
    // игрок бредёт по озеру.
    IC SGameMtlPair* get_current_pair_ground();

    // [DA_PORT] Какой материал считать землёй под ногами: обычно тот, что под персонажем, но в луже —
    // вода. Отдельным методом, потому что вызов идёт из inline-заголовка, а обращаться оттуда к
    // движковым ручкам значит тащить их объявления во все включающие файлы.
    u16 da_ground_material_idx();
};

#include "material_manager_inline.h"
