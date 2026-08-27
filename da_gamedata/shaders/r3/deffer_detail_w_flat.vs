#include "common.h"

uniform float4 		consts; // {1/quant,1/quant,diffusescale,ambient}
// [DA_PORT] Fade band for the grass shadow: x = where it starts, y = where it ends, metres
// from the camera. STRICTLY ZERO outside the sun shadow pass, and zero means "do nothing".
uniform float4 		grass_sfade;
// World position of the CAMERA. Handed over separately on purpose: in the sun shadow pass
// m_WV belongs to the SUN, so a view-space distance there measures the wrong thing.
uniform float4 		grass_sfade_eye;
// [DA_PORT] Terrain-matched variation: x = strength, y = 1/patch size, z = base boost.
uniform float4 		grass_tint;
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
	// [DA_PORT] KOLEBANIE PO MESTNOSTI. Podrobno - u ps_r__grass_tint v dvizhke.
	//
	// Shum schitaetsya ot MIROVOI pozicii kustika (m0.w/m1.w/m2.w), a ne ot ekrannoi:
	// uzor stoit na meste pri dvizhenii kamery, kak nastoyashchaya nerovnost pochvy.
	// Ekrannyi shum ehal by za golovoi i chitalsya by gryazyu na stekle.
	//
	// Menyaem HEMI, a ne cvet: hemi uzhe techet v pikselnyi shader, i pravka zhivet celikom
	// zdes. Pikselnyi shader travy obshchii s drugoi geometriei - lezt tuda znachilo by
	// zadet i ee.
	//
	// Usilenie u OSNOVANIYA (grass_tint.z): vnizu stebel beret svoistva pochvy, k verhushke
	// ostaetsya soboi - tak styk s zemlei perestaet chitatsya liniei.
	float da_hemi = c0.w;
	[branch] if ( grass_tint.x > 0.001f )
	{
		const float2 wxz = float2( m0.w, m2.w ) * grass_tint.y;
		const float2 c   = floor( wxz );
		// [DA_PORT] frac() zdes NEDOSTUPNA: v etom shadere est peremennaya s takim zhe imenem,
		// i ona perekryvaet vstroennuyu funkciyu. Schitaem drobnuyu chast vruchnuyu.
		const float2 f   = smoothstep( 0.0f, 1.0f, wxz - c );
		float4 h   = sin( float4(
			dot( c + float2(0,0), float2(127.1f, 311.7f) ),
			dot( c + float2(1,0), float2(127.1f, 311.7f) ),
			dot( c + float2(0,1), float2(127.1f, 311.7f) ),
			dot( c + float2(1,1), float2(127.1f, 311.7f) ) ) ) * 43758.5453f;
		h = h - floor( h );
		const float n = lerp( lerp(h.x,h.y,f.x), lerp(h.z,h.w,f.x), f.y ) * 2.0f - 1.0f;

		const float up = saturate( v.pos.y * 2.0f );
		const float k  = lerp( 1.0f + grass_tint.z, 1.0f, up );
		da_hemi = saturate( da_hemi * ( 1.0f + n * grass_tint.x * k ) );
	}

	O.position	= float4	(Pe, 		da_hemi		);

	return O;
}
FXVS;
