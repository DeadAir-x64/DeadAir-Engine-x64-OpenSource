#include "common.h"
#include "shared\waterconfig.h"
#include "shared\watermove.h"

struct	v_vert
{
	float4 	P		: POSITION;		// (float,float,float,1)
	float4	N		: NORMAL;		// (nx,ny,nz,hemi occlusion)
	float4 	T		: TANGENT;
	float4 	B		: BINORMAL;
	float4	color	: COLOR0;		// (r,g,b,dir-occlusion)
//	float2 	uv		: TEXCOORD0;	// (u0,v0)
	int2 	uv		: TEXCOORD0;	// (u0,v0)
};

struct vf
{
	float2 tbase	: TEXCOORD0;
	float2 tdist0	: TEXCOORD1;
	float2 tdist1	: TEXCOORD2;
#ifdef	USE_SOFT_WATER
#ifdef	NEED_SOFT_WATER
	float4	tctexgen: TEXCOORD3;
#endif	//	USE_SOFT_WATER
#endif	//	NEED_SOFT_WATER	
	float4 hpos		: SV_Position;
};

uniform float4x4	m_texgen;

vf main (v_vert v)
{
	v.N		=	unpack_D3DCOLOR(v.N);
	v.T		=	unpack_D3DCOLOR(v.T);
	v.B		=	unpack_D3DCOLOR(v.B);
	v.color	=	unpack_D3DCOLOR(v.color);
	vf 		o;

	float4 	P 	= v.P;
	float3 	N 	= unpack_normal		(v.N);
		P 	= watermove		(P);

	o.tbase		= unpack_tc_base	(v.uv,v.T.w,v.B.w);		// copy tc
	o.tdist0	= watermove_tc 		(o.tbase*W_DISTORT_BASE_TILE_0, P.xz, W_DISTORT_AMP_0);
	o.tdist1	= watermove_tc 		(o.tbase*W_DISTORT_BASE_TILE_1, P.xz, W_DISTORT_AMP_1);
	o.hpos 		= mul			(m_VP, P);
	// [DA_PORT] Джиттер накладываем здесь, как это делает вся остальная геометрия
	// (см. O.hpos.xy += m_taa_jitter.xy * O.hpos.w в deffer_base_flat.vs и соседях).
	//
	// Без этой строки вода была ЕДИНСТВЕННОЙ поверхностью, которая рисовалась в несмещённом
	// экранном пространстве, пока весь остальной кадр смещался. Сдвиг меняется каждый кадр по
	// последовательности Halton, поэтому вода «стояла» относительно дрожащего дна — на экране
	// это читалось мерцанием, которое видно только под апскейлером и только сверху, где сквозь
	// воду видно дно.
	//
	// ⚠️ Это НЕ та правка, что запрещена в common_functions.h. Там запрещено ВЫЧИТАТЬ джиттер из
	// координаты выборки G-буфера: весь отложенный проход живёт в одном смещённом пространстве, и
	// поправка выдёргивала позицию из общего согласия. Здесь наоборот — вода в это согласие
	// возвращается, получая тот же сдвиг, что и все.
	//
	// Проверяется так же, как проверялась та: r__taa_jitter 0 должен убирать разницу.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
			// xform, input in world coords

//	Igor: for additional depth dest
#ifdef	USE_SOFT_WATER
#ifdef	NEED_SOFT_WATER
	o.tctexgen = mul( m_texgen, P);
	float3	Pe	= mul		(m_V,  P);
	o.tctexgen.z = Pe.z;
#endif	//	USE_SOFT_WATER
#endif	//	NEED_SOFT_WATER

	return o;
}
