#include "common.h"
#include "da_vertex_pull.h"	// [DA_PORT] выборка вершин из шейдера

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
#define	v_in	v_static_color
#else
#define	v_in	v_static
#endif


v2p_flat main ( v_in I, uint da_vid : SV_VertexID, uint da_iid : SV_InstanceID )
{
	// [DA_PORT] Два пути в одном шейдере -- ровно как у пакетной отрисовки деревьев.
	//
	// da_pull_control.x == 0: всё как было, вершина приходит через разметку входа.
	// Иначе: вершина достаётся из общего буфера уровня по номеру объекта (SV_InstanceID) и
	// номеру вершины внутри него (SV_VertexID), а разметка входа игнорируется.
	//
	// Разметка и вершинный буфер при этом ОСТАЮТСЯ привязанными: рисовать с пустой разметкой,
	// когда шейдер объявляет входы, значит нарваться на молчаливый отброс вызова -- ровно тот
	// класс отказа, что уже дважды ломал нам картинку без единого сообщения.
	bool da_pull_skip = false;
	if (da_pull_control.x != 0)
	{
		uint da_count = asuint(da_pull_objects[da_iid]).z;
		da_pull_skip  = (da_vid >= da_count);	// хвост пачки: у объекта меньше вершин, чем у самого длинного

		da_pulled_vertex V = da_pull_fetch(da_iid, min(da_vid, da_count - 1));
		I.P		= V.P;
		I.Nh	= V.Nh;		// сырое: распакует общий код ниже, как и для разметки входа
		I.T		= V.T;
		I.B		= V.B;
		I.tc	= V.tc;
#ifdef	USE_LM_HEMI
		I.lmh	= V.lmh;
#endif
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
		I.color	= V.color;
#endif
	}

	// Распаковка ОДНА на оба пути: выборка отдаёт поля в том же виде, что и разметка входа.
	I.Nh			= unpack_D3DCOLOR(I.Nh);
	I.T				= unpack_D3DCOLOR(I.T);
	I.B				= unpack_D3DCOLOR(I.B);

	// Eye-space pos/normal
	v2p_flat 		O;
	float4	Pp 	= mul( m_WVP, I.P );
	O.hpos 		= Pp;

	// [DA_PORT] Motion vectors: the same vertex put through the previous frame's transform. Taken before
	// anything else touches hpos, and specifically before any jitter is applied — a temporal upscaler is
	// told the jitter separately and would otherwise count it twice.
#ifdef DA_VELOCITY
	O.hpos_curr	= mul( m_VP_nojit, I.P );
	O.hpos_old	= mul( m_WVP_old, I.P );
#endif
#ifdef DA_VELOCITY
	// [DA_PORT] Jitter applied here, after the positions the motion vectors are built from,
	// so those stay clean. Zero unless FSR 2 is running.
	O.hpos.xy += m_taa_jitter.xy * O.hpos.w;
#endif
	O.N 		= mul( (float3x3)m_WV, unpack_bx2(I.Nh) );
	float3	Pe	= mul( m_WV, I.P );

	float2	tc 	= unpack_tc_base( I.tc, I.T.w, I.B.w);	// copy tc
	O.tcdh		= float4( tc.xyyy );
	O.position	= float4( Pe, I.Nh.w );

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float 	s	= I.color.w	;							// (r,g,b,dir-occlusion)
	O.tcdh.w	= s;
#endif

#ifdef	USE_TDETAIL
	O.tcdbump	= O.tcdh * dt_params;					// dt tc
#endif

#ifdef	USE_LM_HEMI
	O.lmh 		= unpack_tc_lmap( I.lmh );
#endif

	// [DA_PORT] Хвост пачки уводим за ближнюю плоскость -- такой треугольник отсекается целиком.
	// Число индексов у объекта кратно трём, поэтому за границу уходят все три вершины сразу, и
	// половины треугольника на экране остаться не может.
	if (da_pull_skip)
		O.hpos = float4(0,0,-1,1);

	return	O;
}
FXVS;
