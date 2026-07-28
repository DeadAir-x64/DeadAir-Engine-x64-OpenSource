#include "common.h"

struct vv
{
	float3 pos0	: POSITION0	;
	float3 pos1	: POSITION1	;
	float3 n0	: NORMAL0	;
	float3 n1	: NORMAL1	;
	float2 tc0	: TEXCOORD0	;
	float2 tc1	: TEXCOORD1	;
	float4 rgbh0	: TEXCOORD2;	// rgb.h
	float4 rgbh1	: TEXCOORD3;	// rgb.h
	float4 sun_af	: COLOR0;	// x=sun_0, y=sun_1, z=alpha, w=factor
};
struct vf
{
	float3	Pe	: TEXCOORD0	;
 	float2 	tc0	: TEXCOORD1	;	// base0
 	float2 	tc1	: TEXCOORD2	;	// base1
	float4 	af	: COLOR1	;	// alpha&factor
	// [DA_PORT] Motion vectors. Slots 3 and 4 are the first free ones here. Must mirror lod.ps exactly.
#ifdef DA_VELOCITY
	float4	hpos_curr	: TEXCOORD3;
	float4	hpos_old	: TEXCOORD4;
#endif
	float4 	hpos: SV_Position;
};

#define L_SCALE (2.0h*1.55h)
vf 	main	( vv I )
{
	vf 		o;

	I.sun_af.xyz	= I.sun_af.zyx;
	I.rgbh0.xyz		= I.rgbh0.zyx;
	I.rgbh1.xyz		= I.rgbh1.zyx;

	// lerp pos
	float 	factor 	= I.sun_af.w	;
	float4 	pos 	= float4	(lerp(I.pos0,I.pos1,factor),1);

	float 	h 	= lerp		(I.rgbh0.w,I.rgbh1.w,factor)		*L_SCALE;

	o.hpos 		= mul		(m_VP, 	pos);				// xform, input in world coords
	o.Pe		= mul		(m_V,	pos);

	// [DA_PORT] Motion vectors for the distant impostors, which had none at all.
	//
	// This is the far LOD every distant object collapses into - the trees on a horizon are drawn here,
	// not by the tree shaders. It wrote nothing into the velocity target, and pack_gbuffer leaves that
	// at zero, which does not mean "unknown" to an upscaler: it means "this pixel did not move on
	// screen". So while the player walked, the whole distance was reprojected as though pinned to the
	// screen, and it appeared to drift along with the camera - which is exactly how it was described.
	//
	// World-space matrices, because that is what this shader draws in: pos is already a world position
	// and o.hpos above is built with the plain m_VP, no object matrix anywhere. The LOD blend factor
	// changes between frames too, but the geometry itself does not move, so both ends use the same
	// blended position and the vector carries the camera's motion alone - which is all there is.
#ifdef DA_VELOCITY
	o.hpos_curr	= mul( m_VP_nojit_ws, pos );
	o.hpos_old	= mul( m_VP_old_ws,   pos );

	// And the jitter, which this shader never applied either. Under an upscaler the projection matrix
	// is left clean and every scene shader offsets itself by this constant; missing it meant the far
	// LOD was sampled at the same sub-pixel position every frame, so no amount of standing still could
	// resolve it. Applied after the two positions above, so those stay on the un-jittered grid.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
#endif

	// replicate TCs
	o.tc0		= I.tc0;						
	o.tc1		= I.tc1;						

	// calc normal & lighting
	o.af		= float4	(h,h,I.sun_af.z,factor);
	return o	;
}
FXVS;
