#include "common.h"

uniform float4 		consts; // {1/quant,1/quant,diffusescale,ambient}
// [DA_PORT] Fade band for the grass shadow: x = where it starts, y = where it ends, metres
// from the camera. STRICTLY ZERO outside the sun shadow pass, and zero means "do nothing".
uniform float4 		grass_sfade;
// World position of the CAMERA. Handed over separately on purpose: in the sun shadow pass
// m_WV belongs to the SUN, so a view-space distance there measures the wrong thing.
uniform float4 		grass_sfade_eye;
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

	// [DA_PORT] Grass shadow fade band. Zero = untouched, exactly as before.
	//
	// The shadow of the grass is cut at a radius around the camera, and the cut edge reads as a
	// cone travelling with the player. Industry answer is to fade before the cull, not at it
	// (PerInstanceFadeAmount in Unreal). We fade by HEIGHT, not opacity: the blade lies down, its
	// shadow shortens and vanishes. Opacity would need dithered alpha test - per pixel work.
	//
	// The height contribution is the y element of each row (m0.y, m1.y, m2.y) - the same three
	// numbers r__grass_fade_flat scales on the CPU side. Distance is taken from the instance
	// ORIGIN (m0.w, m1.w, m2.w), so a blade never shrinks unevenly along its own height.
	[branch] if ( grass_sfade.y > 0.001f )
	{
		const float3 wp = float3( m0.w, m1.w, m2.w );
		const float  d  = distance( wp, grass_sfade_eye.xyz );
		const float  k  = saturate( ( grass_sfade.y - d ) /
		                            max( grass_sfade.y - grass_sfade.x, 0.001f ) );
		m0.y *= k; m1.y *= k; m2.y *= k;
	}

	// Transform pos to world coords
	float4 	pos;
 	pos.x 		= dot	(m0, v.pos);
 	pos.y 		= dot	(m1, v.pos);
 	pos.z 		= dot	(m2, v.pos);
	pos.w 		= 1;

	// Normal in world coords
	float3 	norm;	
		norm.x 	= pos.x - m0.w	;
		norm.y 	= pos.y - m1.w	+ .75f;	// avoid zero
		norm.z	= pos.z - m2.w	;

	// Final out
	float4	Pp 	= mul		(m_WVP,	pos				);
	O.hpos 		= Pp;

	// [DA_PORT] Motion vectors - see deffer_detail_w_flat.vs for the note on wind.
#ifdef DA_VELOCITY
	O.hpos_curr	= mul( m_VP_nojit, pos );
	O.hpos_old	= mul( m_WVP_old, pos );
#endif
#ifdef DA_VELOCITY
	// [DA_PORT] Jitter applied here, after the positions the motion vectors are built from,
	// so those stay clean. Zero unless FSR 2 is running.
	O.hpos.xy += m_taa_jitter.xy * O.hpos.w;
#endif
	O.N 		= mul		(m_WV,  normalize(norm)	);
	float3	Pe	= mul		(m_WV,  pos				);
	O.tcdh 		= float4	((v.misc * consts).xyyy	);

# if defined(USE_R2_STATIC_SUN)
	O.tcdh.w	= c0.x;								// (,,,dir-occlusion)
# endif

	O.position	= float4	(Pe, 		c0.w		);

	return O;
}
FXVS;
