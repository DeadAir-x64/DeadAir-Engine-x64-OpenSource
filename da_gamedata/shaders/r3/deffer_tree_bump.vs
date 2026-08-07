#include "common.h"
#include "tree_instance.h"	// [DA_PORT] пакетна€ отрисовка деревьев

uniform float3x4	m_xform		;
uniform float3x4	m_xform_v	;
uniform float4 		consts; 	// {1/quant,1/quant,???,???}
uniform float4 		c_scale,c_bias,wind,wave;
// [DA_PORT] Same two as of the previous frame, so the sway itself lands in the motion
// vectors. Rebuilding the previous position from the CURRENT, already-displaced vertex
// makes foliage claim it never moved, and the upscaler blinks on it.
uniform float4 		wind_old, wave_old;
uniform float2 		c_sun;		// x=*, y=+

v2p_bumped 	main 	(v_tree I, uint instance_id : SV_InstanceID)
{
	I.Nh	=	unpack_D3DCOLOR(I.Nh);
	I.T		=	unpack_D3DCOLOR(I.T);
	I.B		=	unpack_D3DCOLOR(I.B);

	// Transform to world coords

	// [DA_PORT] „то рисуем Ч одиночное дерево или экземпл€р из пачки. ѕачки может не быть даже в
	// инстансном шейдере (еЄ собирают не всегда), поэтому одиночный путь об€зан остатьс€ рабочим.
	float3x4 l_xform   = m_xform;
	float3x4 l_xform_v = m_xform_v;
	float4   l_c_scale = c_scale;
	float4   l_c_bias  = c_bias;
	float2   l_c_sun   = c_sun;
	if (tree_instance_control.x > 0.5f)
	{
		l_xform   = tree_instance_xform(instance_id);
		l_xform_v = tree_instance_xform_v(instance_id);
		l_c_scale = tree_instance_scale(instance_id);
		l_c_bias  = tree_instance_bias(instance_id);
		l_c_sun   = tree_instance_sun(instance_id);
	}

	float3 	pos		= mul			(l_xform, I.P);

	//
	float 	base 	= l_xform._24	;		// take base height from matrix
	float 	dp		= calc_cyclic  	(wave.w+dot(pos,(float3)wave));
	float 	H 		= pos.y - base	;		// height of vertex (scaled, rotated, etc.)
	float 	frac 	= I.tc.z*consts.x;		// fractional (or rigidity)
	float 	inten 	= H * dp;				// intensity
	float2 	result	= calc_xz_wave	(wind.xz*inten, frac);
#ifdef		USE_TREEWAVE
			result	= 0;
#endif
	float4 	w_pos 	= float4(pos.x+result.x, pos.y, pos.z+result.y, 1);
	float2 	tc 		= (I.tc * consts).xy;
	float 	hemi 	= I.Nh.w * l_c_scale.w + l_c_bias.w;
//	float 	hemi 	= I.Nh.w;

	// Eye-space pos/normal
	v2p_bumped 		O;
	float3	Pe		= mul		(m_V,  	w_pos		);
	O.tcdh 			= float4	(tc.xyyy			);
	O.hpos 			= mul		(m_VP,	w_pos		);

	// [DA_PORT] Motion vectors. The position is already in world space, so the plain view-projection
	// pair is what is needed - no world matrix, which trees never set anyway.
#ifdef DA_VELOCITY
	O.hpos_curr		= mul( m_VP_nojit_ws, w_pos );
	// [DA_PORT] The sway rebuilt with the PREVIOUS frame's wind, so the vector carries the
	// foliage's own movement and not just the camera's. Same arithmetic as above, only the
	// two wind constants differ.
	float 	dp_old 	= calc_cyclic  (wave_old.w+dot(pos,(float3)wave_old));
	float2 	res_old	= calc_xz_wave (wind_old.xz*(H*dp_old), frac);
	float4 	w_pos_old = float4(pos.x+res_old.x, pos.y, pos.z+res_old.y, 1);
	O.hpos_old		= mul( m_VP_old_ws, w_pos_old );
#endif
#ifdef DA_VELOCITY
	// [DA_PORT] Jitter applied here, after the positions the motion vectors are built from,
	// so those stay clean. Zero unless FSR 2 is running.
	O.hpos.xy += m_taa_jitter.xy * O.hpos.w;
#endif
	O.position		= float4	(Pe, 	hemi		);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float 	suno 	= I.Nh.w * l_c_sun.x + l_c_sun.y	;
	O.tcdh.w		= suno;					// (,,,dir-occlusion)
#endif

	// Calculate the 3x3 transform from tangent space to eye-space
	// TangentToEyeSpace = object2eye * tangent2object
	//		     = object2eye * transpose(object2tangent) (since the inverse of a rotation is its transpose)
	float3 	N 		= unpack_bx4(I.Nh);	// just scale (assume normal in the -.5f, .5f)
	float3 	T 		= unpack_bx4(I.T);	//
	float3 	B 		= unpack_bx4(I.B);	//
	float3x3 xform	= mul	((float3x3)l_xform_v, float3x3(
						T.x,B.x,N.x,
						T.y,B.y,N.y,
						T.z,B.z,N.z
					));

	// The pixel shader operates on the bump-map in [0..1] range
	// Remap this range in the matrix, anyway we are pixel-shader limited :)
	// ...... [ 2  0  0  0]
	// ...... [ 0  2  0  0]
	// ...... [ 0  0  2  0]
	// ...... [-1 -1 -1  1]
	// issue: strange, but it's slower :(
	// issue: interpolators? dp4? VS limited? black magic?

	// Feed this transform to pixel shader
	O.M1 			= xform[0];
	O.M2 			= xform[1];
	O.M3 			= xform[2];

#ifdef 	USE_TDETAIL
	O.tcdbump		= O.tcdh * dt_params;		// dt tc
#endif

	return O;
}
FXVS;
