#ifndef SLOAD_H
#define SLOAD_H

#include "common.h"

#define GLOSS_MUL 2//.1f

#ifdef	MSAA_ALPHATEST_DX10_1
#if MSAA_SAMPLES == 2
static const float2 MSAAOffsets[2] = { float2(4,4), float2(-4,-4) };
#endif
#if MSAA_SAMPLES == 4
static const float2 MSAAOffsets[4] = { float2(-2,-6), float2(6,-2), float2(-6,2), float2(2,6) };
#endif
#if MSAA_SAMPLES == 8
static const float2 MSAAOffsets[8] = { float2(1,-3), float2(-1,3), float2(5,1), float2(-3,-5), 
								               float2(-5,5), float2(-7,-1), float2(3,7), float2(7,-7) };
#endif
#endif	//	MSAA_ALPHATEST_DX10_1

//////////////////////////////////////////////////////////////////////////////////////////
// Bumped surface loader                //
//////////////////////////////////////////////////////////////////////////////////////////
struct	surface_bumped
{
	float4	base;
	float3	normal;
	float	gloss;
	float	height;

};

// [DA_PORT] ---- Detail-bump stability under a temporal upscaler ---------------------------------
// x = gloss damping, y = normal damping. Both zero by default, and at zero every line below collapses
// to a multiply by one, i.e. this file behaves exactly as the mod's original. Driven from the console
// by r__detail_gloss_fix / r__detail_normal_fix; see xr_ioc_cmd.cpp for the full diagnosis.
uniform float4 da_detail_fix;

// How fast the detail normal changes from one pixel to the next. Where this is large the tilt is finer
// than the sampling grid: the specular answer then depends on exactly where inside the texel the sample
// landed, the jitter moves that point every frame, and the upscaler locks the resulting noise into a
// standing pattern. Measured in SCREEN space on purpose - it needs no knowledge of how the detail map is
// encoded, and it still sees the frequency at grazing angles, where anisotropic filtering keeps alive
// what mip selection alone would have removed.
float da_detail_instability( float3 dN )
{
	return length( ddx(dN) ) + length( ddy(dN) );
}

float4 tbase( float2 tc )
{
   return	s_base.Sample( smp_base, tc);
}

#if defined(ALLOW_STEEPPARALLAX) && defined(USE_STEEPPARALLAX)

static const float fParallaxStartFade = 8.0f;
static const float fParallaxStopFade = 12.0f;

void UpdateTC( inout p_bumped I)
{
	if (I.position.z < fParallaxStopFade)
	{
		const float maxSamples = 25;
		const float minSamples = 5;
		const float fParallaxOffset = -0.013;

		float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
									 I.M1.y, I.M2.y, I.M3.y,
									 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);

		eye = normalize(eye);
		
		//	Calculate number of steps
		float nNumSteps = lerp( maxSamples, minSamples, eye.z );

		float	fStepSize			= 1.0 / nNumSteps;
		float2	vDelta				= eye.xy * fParallaxOffset*1.2;
		float2	vTexOffsetPerStep	= fStepSize * vDelta;

		//	Prepare start data for cycle
		float2	vTexCurrentOffset	= I.tcdh;
		float	fCurrHeight			= 0.0;
		float	fCurrentBound		= 1.0;

		for( int i=0; i<nNumSteps; ++i )
		{
			if (fCurrHeight < fCurrentBound)
			{	
				vTexCurrentOffset += vTexOffsetPerStep;		
				fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
				fCurrentBound -= fStepSize;
			}
		}

/*
		[unroll(25)]	//	Doesn't work with [loop]
		for( ;fCurrHeight < fCurrentBound; fCurrentBound -= fStepSize )
		{
			vTexCurrentOffset += vTexOffsetPerStep;		
			fCurrHeight = s_bumpX.SampleLevel( smp_base, vTexCurrentOffset.xy, 0 ).a; 
		}
*/
		//	Reconstruct previouse step's data
		vTexCurrentOffset -= vTexOffsetPerStep;
		float fPrevHeight = s_bumpX.Sample( smp_base, float3(vTexCurrentOffset.xy,0) ).a;

		//	Smooth tc position between current and previouse step
		float	fDelta2 = ((fCurrentBound + fStepSize) - fPrevHeight);
		float	fDelta1 = (fCurrentBound - fCurrHeight);
		float	fParallaxAmount = (fCurrentBound * fDelta2 - (fCurrentBound + fStepSize) * fDelta1 ) / ( fDelta2 - fDelta1 );
		float	fParallaxFade 	= smoothstep(fParallaxStopFade, fParallaxStartFade, I.position.z);
		float2	vParallaxOffset = vDelta * ((1- fParallaxAmount )*fParallaxFade);
		float2	vTexCoord = I.tcdh + vParallaxOffset;
	
		//	Output the result
		I.tcdh = vTexCoord;

#if defined(USE_TDETAIL) && defined(USE_STEEPPARALLAX)
		I.tcdbump = vTexCoord * dt_params;
#endif
	}

}

#elif	defined(USE_PARALLAX) || defined(USE_STEEPPARALLAX)

void UpdateTC( inout p_bumped I)
{
	float3	 eye = mul (float3x3(I.M1.x, I.M2.x, I.M3.x,
								 I.M1.y, I.M2.y, I.M3.y,
								 I.M1.z, I.M2.z, I.M3.z), -I.position.xyz);
								 
	float	height	= s_bumpX.Sample( smp_base, I.tcdh).w;	//
			//height  /= 2;
			//height  *= 0.8;
			height	= height*(parallax.x) + (parallax.y);	//
	float2	new_tc  = I.tcdh + height * normalize(eye);	//

	//	Output the result
	I.tcdh	= new_tc;
}

#else	//	USE_PARALLAX

void UpdateTC( inout p_bumped I)
{
	;
}

#endif	//	USE_PARALLAX

surface_bumped sload_i( p_bumped I)
{
	surface_bumped	S;
   
	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzy + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.z;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
	// [DA_PORT] Damped where the detail bump has gone finer than a pixel - see the top of this file.
	float3 da_dN		= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
	float  da_inst		= da_detail_instability( da_dN );
	float  da_wn		= 1.0h - saturate(da_inst * da_detail_fix.y);	// weight on the normal
	float  da_wg		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, so which half is guilty can be settled without first calibrating a scale.
	if (da_detail_fix.z > 3.5h && da_detail_fix.z < 4.5h)	da_wn = 0.0h;	// 4: no detail in the normal
	if (da_detail_fix.z > 4.5h && da_detail_fix.z < 5.5h)	da_wg = 0.0h;	// 5: no detail in the gloss
	// Neutral for the gloss is ONE, not zero: NDetail.x sits around .5 and GLOSS_MUL is 2, so the pair
	// multiplies to unity on average. Scaling the product by the weight would have driven gloss to zero
	// and flattened the metal instead of merely steadying it.
	S.gloss				= S.gloss * lerp( 1.0h, NDetail.x * GLOSS_MUL, da_wg );
	S.normal			+= da_dN * da_wn;
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

	// [DA_PORT] r__detail_debug 1 and 2 paint the weight itself rather than the surface, so its magnitude
	// can be read off the screen instead of guessed; 3 paints every pixel this branch touches at all,
	// which is the one reading that separates "the number is too small" from "this code never runs here".
	// Written last, so the detail tint above cannot colour it.
	if (da_detail_fix.z > 2.5h && da_detail_fix.z < 3.5h)
		S.base.rgb		= float3( 1.0h, 0.0h, 0.0h );
	else if (da_detail_fix.z > 1.5h && da_detail_fix.z < 2.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.x );
	// Bounded on BOTH sides. It was not, and modes 4/5/8/9 - the ones that answer which half is guilty -
	// fell into this branch and painted the surface with a weight of zero, i.e. black. Their whole point
	// is to change behaviour and paint NOTHING, so the artefact can be judged on the real surface.
	else if (da_detail_fix.z > 0.5h && da_detail_fix.z < 1.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.y );

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	// [DA_PORT] This is the branch metal props actually take (verified in game with the debug modes
	// below). Note what it does: a COLOURED detail texture goes straight into the albedo with a factor of
	// two. Once that texture is finer than a pixel the tint differs from frame to frame as the jitter
	// moves the sample, and the upscaler settles the difference into standing coloured mottling - which
	// is why the artefact reads as iridescence rather than the whitish sparkle a specular problem gives.
	float  da_inst		= da_detail_instability( detail.rgb );
	float  da_wa		= 1.0h - saturate(da_inst * da_detail_fix.w);	// weight on the albedo tint
	float  da_wg2		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, to settle which half is guilty without first calibrating a scale.
	if (da_detail_fix.z > 7.5h && da_detail_fix.z < 8.5h)	da_wa  = 0.0h;	// 8: no detail in the albedo
	if (da_detail_fix.z > 8.5h)								da_wg2 = 0.0h;	// 9: no detail in the gloss
	// Neutral is ONE for both: an average detail texel sits near .5 and both are scaled by 2, so full
	// damping means "as if this surface had no detail map", not "black" and not "matte".
	S.base.rgb			= S.base.rgb * lerp( 1.0h, detail.rgb * 2, da_wa );
	S.gloss				= S.gloss * lerp( 1.0h, detail.w * GLOSS_MUL, da_wg2 );
	// [DA_PORT] r__detail_debug 7: detail WITHOUT a bump map. Blue here and red under mode 3 are mutually
	// exclusive, so the pair says which of the two detail paths a surface actually takes.
	if (da_detail_fix.z > 6.5h && da_detail_fix.z < 7.5h)
		S.base.rgb		= float3( 0.0h, 0.0h, 1.0h );
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload_i( p_bumped I, float2 pixeloffset )
{
	surface_bumped	S;
   
   // apply offset
#ifdef	MSAA_ALPHATEST_DX10_1
   I.tcdh.xy += pixeloffset.x * ddx(I.tcdh.xy) + pixeloffset.y * ddy(I.tcdh.xy);
#endif

	UpdateTC(I);	//	All kinds of parallax are applied here.

	float4 	Nu	= s_bump.Sample( smp_base, I.tcdh );		// IN:	normal.gloss
	float4 	NuE	= s_bumpX.Sample( smp_base, I.tcdh);	// IN:	normal_error.height

	S.base		= tbase(I.tcdh);				//	IN:  rgb.a
	S.normal	= Nu.wzyx + (NuE.xyz - 1.0h);	//	(Nu.wzyx - .5h) + (E-.5)
	S.gloss		= Nu.x*Nu.x;					//	S.gloss = Nu.x*Nu.x;
	S.height	= NuE.z;
	//S.height	= 0;

#ifdef        USE_TDETAIL
#ifdef        USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
#if ( (!defined(ALLOW_STEEPPARALLAX) ) && defined(USE_STEEPPARALLAX) )
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
#endif

	float4 NDetail		= s_detailBump.Sample( smp_base, I.tcdbump);
	float4 NDetailX		= s_detailBumpX.Sample( smp_base, I.tcdbump);
	// [DA_PORT] Damped where the detail bump has gone finer than a pixel - see the top of this file.
	float3 da_dN		= NDetail.wzy + NDetailX.xyz - 1.0h; //	(Nu.wzyx - .5h) + (E-.5)
	float  da_inst		= da_detail_instability( da_dN );
	float  da_wn		= 1.0h - saturate(da_inst * da_detail_fix.y);	// weight on the normal
	float  da_wg		= 1.0h - saturate(da_inst * da_detail_fix.x);	// weight on the gloss
	// Hard overrides, so which half is guilty can be settled without first calibrating a scale.
	if (da_detail_fix.z > 3.5h && da_detail_fix.z < 4.5h)	da_wn = 0.0h;	// 4: no detail in the normal
	if (da_detail_fix.z > 4.5h && da_detail_fix.z < 5.5h)	da_wg = 0.0h;	// 5: no detail in the gloss
	// Neutral for the gloss is ONE, not zero: NDetail.x sits around .5 and GLOSS_MUL is 2, so the pair
	// multiplies to unity on average. Scaling the product by the weight would have driven gloss to zero
	// and flattened the metal instead of merely steadying it.
	S.gloss				= S.gloss * lerp( 1.0h, NDetail.x * GLOSS_MUL, da_wg );
	S.normal			+= da_dN * da_wn;
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;

	// [DA_PORT] r__detail_debug 1 and 2 paint the weight itself rather than the surface, so its magnitude
	// can be read off the screen instead of guessed; 3 paints every pixel this branch touches at all,
	// which is the one reading that separates "the number is too small" from "this code never runs here".
	// Written last, so the detail tint above cannot colour it.
	if (da_detail_fix.z > 2.5h && da_detail_fix.z < 3.5h)
		S.base.rgb		= float3( 1.0h, 0.0h, 0.0h );
	else if (da_detail_fix.z > 1.5h && da_detail_fix.z < 2.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.x );
	// Bounded on BOTH sides. It was not, and modes 4/5/8/9 - the ones that answer which half is guilty -
	// fell into this branch and painted the surface with a weight of zero, i.e. black. Their whole point
	// is to change behaviour and paint NOTHING, so the artefact can be judged on the real surface.
	else if (da_detail_fix.z > 0.5h && da_detail_fix.z < 1.5h)
		S.base.rgb		= saturate( da_inst * da_detail_fix.y );

//	S.base.rgb			= float3(1,0,0);
#else        //	USE_TDETAIL_BUMP
#ifdef MSAA_ALPHATEST_DX10_1
   I.tcdbump.xy += pixeloffset.x * ddx(I.tcdbump.xy) + pixeloffset.y * ddy(I.tcdbump.xy);
#endif
	float4 detail		= s_detail.Sample( smp_base, I.tcdbump);
	S.base.rgb			= S.base.rgb * detail.rgb * 2;
	S.gloss				= S.gloss * detail.w * GLOSS_MUL;
#endif        //	USE_TDETAIL_BUMP
#endif

	return S;
}

surface_bumped sload ( p_bumped I)
{
      surface_bumped      S   = sload_i	(I);
		S.normal.z			*=	0.3;		//. make bump twice as contrast (fake, remove me if possible)
	// [DA_PORT] r__detail_debug 6: paint EVERY bumped surface, whether or not it has a detail map. This
	// is the control for mode 3 - if a surface turns green here but never red there, this file is live
	// on it and the detail-bump branch simply is not the path it takes.
	if (da_detail_fix.z > 5.5h && da_detail_fix.z < 6.5h)
		S.base.rgb		= float3( 0.0h, 1.0h, 0.0h );

#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

surface_bumped sload ( p_bumped I, float2 pixeloffset )
{
      surface_bumped      S   = sload_i	(I, pixeloffset );
		S.normal.z			*=	0.3;		//. make bump twice as contrast (fake, remove me if possible)
#ifdef	GBUFFER_OPTIMIZATION
	   S.height = 0;
#endif	//	GBUFFER_OPTIMIZATION
      return              S;
}

#endif
