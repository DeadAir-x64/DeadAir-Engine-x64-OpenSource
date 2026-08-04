#include "common.h"

struct vf
{
	float2 tc0	: TEXCOORD0;
	float3 c0	: COLOR0;		// c0=all lighting
	float  fog	: FOG;
	float4 hpos	: SV_Position;
};

/*
struct	v_vert
{
	float4 	P		: POSITION;		// (float,float,float,1)
	float4	N		: NORMAL;		// (nx,ny,nz,hemi occlusion)
	float4 	T		: TANGENT;
	float4 	B		: BINORMAL;
	float4	color	: COLOR0;		// (r,g,b,dir-occlusion)
	float2 	uv		: TEXCOORD0;	// (u0,v0)
};
struct         v_static                {
        float4      P                	: POSITION;                        // (float,float,float,1)
        float4      Nh                	: NORMAL;                // (nx,ny,nz,hemi occlusion)
        float4      T                 	: TANGENT;                // tangent
        float4      B                 	: BINORMAL;                // binormal
        float2      tc                	: TEXCOORD0;        // (u,v)
        float2      lmh                	: TEXCOORD1;        // (lmu,lmv)
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
                float4                color                : COLOR0;                        // (r,g,b,dir-occlusion)
#endif
};
*/

vf main (v_static_color v)
{
	vf 		o;

	float3 	N 	= unpack_normal		(v.Nh);
	o.hpos 		= mul				(m_VP, v.P);			// xform, input in world coords

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
	o.tc0		= unpack_tc_base	(v.tc,v.T.w,v.B.w);		// copy tc
//	o.tc0		= unpack_tc_base	(v.tc);				// copy tc

	float3 	L_rgb 	= v.color.zyx;						// precalculated RGB lighting
	float3 	L_hemi 	= v_hemi(N)*v.Nh.w;					// hemisphere
	float3 	L_sun 	= v_sun(N)*v.color.w;					// sun
	float3 	L_final	= L_rgb + L_hemi + L_sun + L_ambient;

	o.c0		= L_final;
	o.fog 		= saturate(calc_fogging 		(v.P));			// fog, input in world coords

	return o;
}
