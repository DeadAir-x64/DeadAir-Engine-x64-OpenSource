#include "common.h"
#include "skin.h"

struct 	vf
{
	float2 tc0	: TEXCOORD0;		// base
	float3 tc1	: TEXCOORD1;		// environment
	float3 c0	: COLOR0;		// color
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

vf 	_main (v_model v)
{
	vf 		o;

	float4 	pos 	= v.P;
	float3  pos_w 	= mul			(m_W, pos);
	float3 	norm_w 	= normalize 		(mul(m_W,v.N));

	o.hpos 		= mul			(m_WVP, pos);		// xform, input in world coords

	// [DA_PORT] Джиттер мировой геометрии.
	//
	// Весь отложенный проход живёт в ОДНОМ смещённом экранном пространстве: буфер глубины пишет
	// смещённая геометрия, свет сравнивается с ним же, выборка G-буфера идёт по той же координате.
	// Шейдер без сдвига выпадал из этого согласия — под апскейлером его край оставался
	// ступенчатым, накапливать между кадрами было нечего.
	//
	// Безусловно, без #ifdef: m_taa_jitter объявлена вне условий и равна нулю, пока временного
	// апскейлера нет (см. common_iostructs.h и cl_taa_jitter в r2.cpp). Здесь нет выхода скоростей,
	// поэтому портить нечего — в шейдерах с векторами движения сдвиг ставится ПОСЛЕ них.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
	o.tc0		= v.tc.xy;					// copy tc
	o.tc1		= calc_reflection	(pos_w, norm_w);
	o.c0 		= calc_model_lq_lighting(norm_w);
	o.fog 		= saturate(calc_fogging 		(float4(pos_w,1)) );	// fog, input in world coords

	return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef 	SKIN_NONE
vf	main(v_model v) 		{ return _main(v); 		}
#endif

#ifdef 	SKIN_0
vf	main(v_model_skinned_0 v) 	{ return _main(skinning_0(v)); }
#endif

#ifdef	SKIN_1
vf	main(v_model_skinned_1 v) 	{ return _main(skinning_1(v)); }
#endif

#ifdef	SKIN_2
vf	main(v_model_skinned_2 v) 	{ return _main(skinning_2(v)); }
#endif

#ifdef	SKIN_3
vf	main(v_model_skinned_3 v) 	{ return _main(skinning_3(v)); }
#endif

#ifdef	SKIN_4
vf	main(v_model_skinned_4 v) 	{ return _main(skinning_4(v)); }
#endif