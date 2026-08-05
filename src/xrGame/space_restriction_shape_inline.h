////////////////////////////////////////////////////////////////////////////
//	Module 		: space_restriction_shape_inline.h
//	Created 	: 17.08.2004
//  Modified 	: 27.08.2004
//	Author		: Dmitriy Iassenev
//	Description : Space restriction shape inline functions
////////////////////////////////////////////////////////////////////////////

#pragma once

IC Fvector CSpaceRestrictionShape::position(const CCF_Shape::shape_def& data) const
{
    switch (data.type)
    {
    case 0: return (data.data.sphere.P);
    case 1: return (data.data.box.c);
    default: NODEFAULT;
    }
#ifdef DEBUG
    return (Fvector().set(0.f, 0.f, 0.f));
#endif
}

IC float CSpaceRestrictionShape::radius(const CCF_Shape::shape_def& data) const
{
    switch (data.type)
    {
    case 0: return (data.data.sphere.R);
    case 1:
        return (
            Fbox().set(Fvector().set(-.5f, -.5f, -.5f), Fvector().set(.5f, .5f, .5f)).xform(data.data.box).getradius());
    default: NODEFAULT;
    }
#ifdef DEBUG
    return (0.f);
#endif
}

// [DA_PORT] Граница строится при первом обращении, а не в конструкторе.
//
// build_border обходит все вершины графа ИИ внутри габарита формы и на каждой дважды проверяет
// вхождение. Замер: 603 мс из 607 на всю регистрацию ограничителей, по 0.62 мс на штуку при 976
// ограничителях на уровне. Платили за все, а нужны единицы -- в большинство зон никто не заходит.
//
// Механизм лени в базе уже есть и им пользуется композиция: CSpaceRestrictionAbstract::border()
// сам зовёт initialize(), если объект не готов. Форма его просто игнорировала, объявляя себя
// готовой в конструкторе. Прямых обращений к m_border в обход border() нет, а inside() считает по
// геометрии формы и границу не трогает -- поэтому перенос безопасен.
IC CSpaceRestrictionShape::CSpaceRestrictionShape(CSpaceRestrictor* space_restrictor, bool default_restrictor)
{
    m_default = default_restrictor;
    m_initialized = false;

    VERIFY(space_restrictor);
    m_restrictor = space_restrictor;
}


IC bool CSpaceRestrictionShape::shape() const { return (true); }
IC bool CSpaceRestrictionShape::default_restrictor() const { return (m_default); }
