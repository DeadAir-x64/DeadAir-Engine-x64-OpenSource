// [DA_PORT] Вершинный шейдер прохода СВЕЧЕНИЯ - копия shadow_direct_model.vs плюс джиттер.
//
// Свечение (лампа, экран, светящаяся палочка в руке) рисуется отдельным проходом после G-буфера, и
// в исходном дереве шейдеров этот проход берёт вершинный шейдер у КАРТЫ ТЕНЕЙ - shadow_direct_model.
// Тому джиттер противопоказан: карта теней рисуется из точки источника, и сдвиг на доли пикселя даёт
// мерцание теней (наступали, см. историю правок).
//
// Но в основном кадре так нельзя. Вся геометрия сдвигается на джиттер прямо в вершинном шейдере
// (deffer_model_flat.vs, строка O.hpos.xy += m_taa_jitter.xy * O.hpos.w), а свечение того же самого
// предмета рисовалось БЕЗ сдвига. Каждый кадр непрозрачная часть уезжала на свои доли пикселя, а
// свечение оставалось на месте - и апскейлер, снимая сдвиг со всего кадра, промахивался ровно по
// свечению. Это и есть пила по кромке светящейся палочки и мерцание ламп в помещениях.
//
// Поэтому проход свечения получает СВОЙ вершинный шейдер: тот же самый, но со сдвигом. Карта теней
// продолжает пользоваться прежним и остаётся нетронутой.
//
// Строка сдвига скопирована из deffer_model_flat.vs дословно - знаки и множитель там уже проверены
// в игре, и подбирать их заново значило бы заново наступить на те же грабли.

#include "common.h"
#include "skin.h"

//////////////////////////////////////////////////////////////////////////////////////////
// Vertex
v2p_shadow_direct _main( v_model	I )
{
	v2p_shadow_direct	O ;
	float4 	hpos 	= mul( m_WVP, I.P );

	O.hpos 			= hpos;

	// [DA_PORT] Ровно тот же сдвиг, что и у непрозрачной части этого же предмета.
	O.hpos.xy 		+= m_taa_jitter.xy * O.hpos.w;
#ifndef USE_HWSMAP
	O.depth 		= O.hpos.z;
#endif
 	return			O ;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
v2p_shadow_direct 	main(v_model v) 			{ return _main(v); }
#endif

#ifdef 	SKIN_0
v2p_shadow_direct 	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
v2p_shadow_direct 	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
v2p_shadow_direct 	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
v2p_shadow_direct 	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
v2p_shadow_direct 	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif

FXVS;
