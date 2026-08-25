#include "common.h"

uniform float4 		consts; // {1/quant,1/quant,diffusescale,ambient}
uniform float4 		wave; 	// cx,cy,cz,tm
uniform float4 		dir2D; 
// [DA_PORT] Same pair one frame back, so the sway itself reaches the motion vectors. Rebuilding the
// previous position from the CURRENT, already-bent blade makes grass claim it never moved.
uniform float4 		wave_old, dir2D_old;
//uniform float4 		array	[200] : register(c12);
//tbuffer DetailsData
//{
	uniform float4 		array[61*4];
//}

v2p_flat 	main (v_detail v, uint instance_id : SV_InstanceID)
{
	v2p_flat 		O;
	// index
	// [DA_PORT] Номер экземпляра приходит от видеокарты, а не из вершины.
	//
	// Раньше в буфере лежала 61 копия одной травинки, и каждая вершина несла номер своей копии.
	// Теперь копия одна, а размножает её сам вызов отрисовки - буфер вершин меньше в 61 раз,
	// и та же горстка вершин читается из кэша.
	int 	i 	= int(instance_id) * 4;
	float4  m0 	= array[i+0];
	float4  m1 	= array[i+1];
	float4  m2 	= array[i+2];
	float4  c0 	= array[i+3];

	// Transform pos to world coords
	float4 	pos;
 	pos.x 		= dot	(m0, v.pos);
 	pos.y 		= dot	(m1, v.pos);
 	pos.z 		= dot	(m2, v.pos);
	pos.w 		= 1;

	// 
	float 	base 	= m1.w;
	float 	dp	= calc_cyclic   (dot(pos,wave));
	float 	H 	= pos.y - base;			// height of vertex (scaled)
	float 	frac 	= v.misc.z*consts.x;		// fractional
	float 	inten 	= H * dp;
	float2 	result	= calc_xz_wave	(dir2D.xz*inten,frac);
	// [DA_PORT] Keep the unbent position: the previous frame's bend is applied to it below.
	float4	pos_flat = pos;
	pos		= float4(pos.x+result.x, pos.y, pos.z+result.y, 1);

	// Normal in world coords
	float3 	norm;	//	= float3(0,1,0);
		norm.x 	= pos.x - m0.w	;
		norm.y 	= pos.y - m1.w	+ .75f;	// avoid zero
		norm.z	= pos.z - m2.w	;

	// Final out
	float4	Pp 	= mul		(m_WVP,	pos				);
	O.hpos 		= Pp;

	// [DA_PORT] Motion vectors. NB the wind sway is NOT undone here: the previous position is taken
	// with the CURRENT sway, so a blade's own swaying is missing from its vector while the camera
	// movement is correct. Grass is small and its sway is sub-pixel at any distance, so this is a
	// deliberate approximation; doing it properly means evaluating the wave twice, with last frame's
	// wave time.
#ifdef DA_VELOCITY
	O.hpos_curr	= mul( m_VP_nojit, pos );
	// [DA_PORT] The bend rebuilt with the previous frame's wind - same arithmetic, other constants.
	float 	dp_old	= calc_cyclic   (dot(pos_flat,wave_old));
	float2 	res_old	= calc_xz_wave  (dir2D_old.xz*((pos_flat.y-base)*dp_old), frac);
	float4	pos_old	= float4(pos_flat.x+res_old.x, pos_flat.y, pos_flat.z+res_old.y, 1);
	O.hpos_old	= mul( m_WVP_old, pos_old );
#endif
#ifdef DA_VELOCITY
	// [DA_PORT] Jitter applied here, after the positions the motion vectors are built from,
	// so those stay clean. Zero unless FSR 2 is running.
	O.hpos.xy += m_taa_jitter.xy * O.hpos.w;
#endif
	O.N 		= mul		(m_WV,  normalize(norm)	);
	float3	Pe	= mul		(m_WV,  pos				);
//	O.tcdh 		= float4	((v.misc * consts).xy	);
	O.tcdh 		= float4	((v.misc * consts).xyyy );

# if defined(USE_R2_STATIC_SUN)
	O.tcdh.w	= c0.x;								// (,,,dir-occlusion)
# endif
	O.position	= float4	(Pe, 		c0.w		);

	return O;
}
FXVS;
