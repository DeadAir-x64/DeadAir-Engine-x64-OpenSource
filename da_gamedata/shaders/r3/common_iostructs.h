#ifndef	common_iostructs_h_included
#define	common_iostructs_h_included

////////////////////////////////////////////////////////////////
//	This file contains io structs:
//	v_name	:	input for vertex shader.
//	v2p_name:	output for vertex shader.
//	p_name	:	input for pixel shader.
////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////
//	TL0uv
struct	v_TL0uv_positiont
{
	float4	P		: POSITIONT;
	float4	Color	: COLOR; 
};

struct	v_TL0uv
{
	float4	P		: POSITION;
	float4	Color	: COLOR; 
};

struct	v2p_TL0uv
{
	float4	Color	: COLOR;
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_TL0uv
{
	float4	Color	: COLOR;
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

////////////////////////////////////////////////////////////////
//	TL
struct	v_TL_positiont
{
	float4	P		: POSITIONT;
	float2	Tex0	: TEXCOORD0;
	float4	Color	: COLOR; 
};

struct	v_TL
{
	float4	P		: POSITION;
	float2	Tex0	: TEXCOORD0;
	float4	Color	: COLOR; 
};

struct	v2p_TL
{
	float2 	Tex0	: TEXCOORD0;
	float4	Color	: COLOR;
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_TL
{
	float2 	Tex0	: TEXCOORD0;
	float4	Color	: COLOR;
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

////////////////////////////////////////////////////////////////
//	TL2uv
struct	v_TL2uv
{
	float4	P		: POSITIONT;
	float2	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float4	Color	: COLOR; 
};

struct	v2p_TL2uv
{
	float2 	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float4	Color	: COLOR;
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_TL2uv
{
	float2 	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float4	Color	: COLOR;
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};
////////////////////////////////////////////////////////////////
//	postpr
struct	v_postpr
{
	float4	P		: POSITIONT;
	float2 	Tex0	: TEXCOORD0;	// base1 (duality)	
	float2	Tex1	: TEXCOORD1;	// base2 (duality)
	float2	Tex2	: TEXCOORD2;	// base  (noise)
	float4	Color	: COLOR0;		// multiplier, color.w = noise_amount
	float4	Gray	: COLOR1;		// (.3,.3,.3.,amount)
};

struct	v2p_postpr
{
	float2 	Tex0	: TEXCOORD0;	// base1 (duality)	
	float2	Tex1	: TEXCOORD1;	// base2 (duality)
	float2	Tex2	: TEXCOORD2;	// base  (noise)
	float4	Color	: COLOR0;		// multiplier, color.w = noise_amount
	float4	Gray	: COLOR1;		// (.3,.3,.3.,amount)
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_postpr
{
	float2 	Tex0	: TEXCOORD0;	// base1 (duality)	
	float2	Tex1	: TEXCOORD1;	// base2 (duality)
	float2	Tex2	: TEXCOORD2;	// base  (noise)
	float4	Color	: COLOR0;		// multiplier, color.w = noise_amount
	float4	Gray	: COLOR1;		// (.3,.3,.3.,amount)
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};
////////////////////////////////////////////////////////////////
//	build	(bloom_build)
struct	v_build
{
	float4	P		: POSITIONT;
	float2	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float2 	Tex2	: TEXCOORD2;
	float2	Tex3	: TEXCOORD3;
};

struct	v2p_build
{
	float2 	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float2 	Tex2	: TEXCOORD2;
	float2	Tex3	: TEXCOORD3;
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_build
{
	float2 	Tex0	: TEXCOORD0;
	float2	Tex1	: TEXCOORD1;
	float2 	Tex2	: TEXCOORD2;
	float2	Tex3	: TEXCOORD3;
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};
////////////////////////////////////////////////////////////////
//	filter	(bloom_filter)
struct	v_filter
{
	float4	P		: POSITIONT;
	float4 	Tex0	: TEXCOORD0;
	float4	Tex1	: TEXCOORD1;
	float4 	Tex2	: TEXCOORD2;
	float4	Tex3	: TEXCOORD3;
	float4 	Tex4	: TEXCOORD4;
	float4	Tex5	: TEXCOORD5;
	float4 	Tex6	: TEXCOORD6;
	float4	Tex7	: TEXCOORD7;
};

struct	v2p_filter
{
	float4 	Tex0	: TEXCOORD0;
	float4	Tex1	: TEXCOORD1;
	float4 	Tex2	: TEXCOORD2;
	float4	Tex3	: TEXCOORD3;
	float4 	Tex4	: TEXCOORD4;
	float4	Tex5	: TEXCOORD5;
	float4 	Tex6	: TEXCOORD6;
	float4	Tex7	: TEXCOORD7;
	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_filter
{
	float4 	Tex0	: TEXCOORD0;
	float4	Tex1	: TEXCOORD1;
	float4 	Tex2	: TEXCOORD2;
	float4	Tex3	: TEXCOORD3;
	float4 	Tex4	: TEXCOORD4;
	float4	Tex5	: TEXCOORD5;
	float4 	Tex6	: TEXCOORD6;
	float4	Tex7	: TEXCOORD7;
//	float4 	HPos	: SV_Position;	// Clip-space position 	(for rasterization)
};

////////////////////////////////////////////////////////////////
//	aa_AA
struct	v_aa_AA
{
	float4 P		:POSITIONT;
	float2 	Tex0	:TEXCOORD0;
	float2	Tex1	:TEXCOORD1;
	float2 	Tex2	:TEXCOORD2;
	float2	Tex3	:TEXCOORD3;
	float2	Tex4	:TEXCOORD4;
	float4	Tex5	:TEXCOORD5;
	float4	Tex6	:TEXCOORD6;
};

struct	v2p_aa_AA
{
	float2 	Tex0	:TEXCOORD0;
	float2	Tex1	:TEXCOORD1;
	float2 	Tex2	:TEXCOORD2;
	float2	Tex3	:TEXCOORD3;
	float2	Tex4	:TEXCOORD4;
	float4	Tex5	:TEXCOORD5;
	float4	Tex6	:TEXCOORD6;
	float4 	HPos	:SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_aa_AA
{
	float2 	Tex0	:TEXCOORD0;
	float2	Tex1	:TEXCOORD1;
	float2 	Tex2	:TEXCOORD2;
	float2	Tex3	:TEXCOORD3;
	float2	Tex4	:TEXCOORD4;
	float4	Tex5	:TEXCOORD5;
	float4	Tex6	:TEXCOORD6;
//	float4 	HPos	:SV_Position;	// Clip-space position 	(for rasterization)
};

struct	p_aa_AA_sun
{
	float2 	tc		:TEXCOORD0;
	float2	unused	:TEXCOORD1;
	float2 	LT		:TEXCOORD2;
	float2	RT		:TEXCOORD3;
	float2	LB		:TEXCOORD4;
	float2	RB		:TEXCOORD5;
//	float4 	HPos	:SV_Position;	// Clip-space position 	(for rasterization)
};

////////////////////////////////////////////////////////////////
//	dumb
struct 	v_dumb
{
	float4	P		:POSITION;	// Clip-space position 	(for rasterization)
};

struct 	v2p_dumb
{
	float4	HPos	:SV_Position;	// Clip-space position 	(for rasterization)
};

////////////////////////////////////////////////////////////////
//	Volume
struct 	v2p_volume
{
	float4 	tc		:TEXCOORD0;
#ifdef 	USE_SJITTER
	float4 	tcJ		:TEXCOORD1;
#endif
	float4 	hpos	:SV_Position;	// Clip-space position 	(for rasterization)
};
struct 	p_volume
{
	float4 	tc		:TEXCOORD0;
#ifdef 	USE_SJITTER
	float4 	tcJ		:TEXCOORD1;
#endif
//	float4 	hpos	:SV_Position;	// Clip-space position 	(for rasterization)
};
////////////////////////////////////////////////////////////////
//	Static
struct         v_static
{
	float4	Nh		:NORMAL;	// (nx,ny,nz,hemi occlusion)
	float4	T		:TANGENT;	// tangent
	float4	B		:BINORMAL;	// binormal
	int2	tc		:TEXCOORD0;	// (u,v)
#ifdef	USE_LM_HEMI
	int2	lmh		:TEXCOORD1;	// (lmu,lmv)
#endif
//	float4	color	:COLOR0;	// (r,g,b,dir-occlusion)	//	Swizzle before use!!!
	float4	P		:POSITION;	// (float,float,float,1)
};

struct	v_static_color
{
	float4	Nh		:NORMAL;	// (nx,ny,nz,hemi occlusion)
	float4	T		:TANGENT;	// tangent
	float4	B		:BINORMAL;	// binormal
	int2	tc		:TEXCOORD0;	// (u,v)
#ifdef	USE_LM_HEMI
	int2	lmh		:TEXCOORD1;	// (lmu,lmv)
#endif
	float4	color	:COLOR0;	// (r,g,b,dir-occlusion)	//	Swizzle before use!!!
	float4	P		:POSITION;	// (float,float,float,1)
};

////////////////////////////////////////////////////////////////
//	defer
// [DA_PORT] Previous frame's world-view-projection, supplied by the engine (binder_wvp_old in r2.cpp).
// Declared as a loose global rather than added to the dynamic_transforms cbuffer on purpose: that
// buffer's layout is shared with the engine side, and growing it would change the offsets of matrices
// every vertex shader already relies on.
// [DA_PORT] Sub-pixel jitter, applied by the shader rather than baked into the projection.
// Zero unless a temporal upscaler is running - see cl_taa_jitter in r2.cpp.
//
// Объявлена ВНЕ блока DA_VELOCITY намеренно. Раньше она жила внутри него — вместе с матрицами для
// векторов движения, — и была видна только шейдерам G-буфера. Но сдвиг нужен и там, где по глубине
// восстанавливают позицию (gbuffer_load_data в common_functions.h): эти шейдеры собираются без
// DA_VELOCITY, и обращение к константе ломало им сборку, а движок молча подставлял заглушку.
// Неиспользованная константа ничего не стоит: компилятор её выбрасывает.
uniform float4		m_taa_jitter;

#ifdef DA_VELOCITY
uniform float4x4	m_WVP_old;
// [DA_PORT] Same pair without any world part, for geometry already in world space (trees).
uniform float4x4	m_VP_nojit_ws;
uniform float4x4	m_VP_old_ws;
// Current frame WITHOUT the TAA jitter - motion vectors must be measured between two
// un-jittered positions, otherwise the jitter itself shows up as motion.
uniform float4x4	m_VP_nojit;
#endif

// [DA_PORT] DA_VELOCITY adds a motion-vector output to the G-buffer: where each pixel was on the
// previous frame, in NDC units. This is the input every temporal upscaler needs (FSR 2, XeSS, DLSS)
// and it also lets temporal AA stop guessing at moving objects.
//
// Gated on the option so that with it off this file is byte-identical to the mod's original: the
// structure is shared by every shader that writes into the G-buffer, and changing it unconditionally
// would mean recompiling all of them for a feature that may be switched off.
//
// NB the slot differs between the two layouts — with GBUFFER_OPTIMIZATION the normal is packed into
// position, so there is no separate Ne target and velocity moves up one.
#ifndef GBUFFER_OPTIMIZATION
struct                  f_deffer
{
	float4	position: SV_Target0;        // px,py,pz, m-id
	float4	Ne		  : SV_Target1;        // nx,ny,nz, hemi
	float4	C		  : SV_Target2;        // r, g, b,  gloss
#ifdef DA_VELOCITY
	float2	velocity: SV_Target3;        // [DA_PORT] screen-space motion, NDC units
#endif
#ifdef EXTEND_F_DEFFER
   uint     mask    : SV_COVERAGE;
#endif
};
#else
struct                  f_deffer
{
	float4	position: SV_Target0;        // xy=encoded normal, z = pz, w = encoded(m-id,hemi)
	float4	C		  : SV_Target1;        // r, g, b,  gloss
#ifdef DA_VELOCITY
	float2	velocity: SV_Target2;        // [DA_PORT] screen-space motion, NDC units
	// [DA_PORT] Reactive mask. 1 means "do not trust the temporal history for this pixel". Defaulted to
	// 0 inside pack_gbuffer, so every shader gets a sane value without being touched; only the ones that
	// genuinely need it raise the flag afterwards.
	float	reactive: SV_Target3;
#endif
#ifdef EXTEND_F_DEFFER
   uint     mask    : SV_COVERAGE;
#endif
};
#endif

struct					gbuffer_data
{
	float3  P; // position.( mtl or sun )
	float   mtl; // material id
	float3  N; // normal
	float   hemi; // AO
	float3  C;
	float   gloss;
};

////////////////////////////////////////////////////////////////
//	Defer bumped
struct v2p_bumped
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4	tcdh	: TEXCOORD0;	// Texture coordinates,         w=sun_occlusion
#else
	float2	tcdh	: TEXCOORD0;	// Texture coordinates
#endif
	float4	position: TEXCOORD1;	// position + hemi
	float3	M1		: TEXCOORD2;	// nmap 2 eye - 1
	float3	M2		: TEXCOORD3;	// nmap 2 eye - 2
	float3	M3		: TEXCOORD4;	// nmap 2 eye - 3
#ifdef USE_TDETAIL
	float2	tcdbump	: TEXCOORD5;	// d-bump
#endif
#ifdef USE_LM_HEMI
		float2	lmh	: TEXCOORD6;	// lm-hemi
#endif
	// [DA_PORT] see v2p_flat — same purpose, different free slots (0..6 are taken here).
#ifdef DA_VELOCITY
	float4	hpos_curr : TEXCOORD7;
	float4	hpos_old  : TEXCOORD8;
#endif
	float4	hpos	: SV_Position;
};

struct p_bumped
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4	tcdh	: TEXCOORD0;	// Texture coordinates,         w=sun_occlusion
#else
	float2	tcdh	: TEXCOORD0;	// Texture coordinates
#endif
	float4	position: TEXCOORD1;	// position + hemi
	float3	M1		: TEXCOORD2;	// nmap 2 eye - 1
	float3	M2		: TEXCOORD3;	// nmap 2 eye - 2
	float3	M3		: TEXCOORD4;	// nmap 2 eye - 3
#ifdef USE_TDETAIL
	float2	tcdbump	: TEXCOORD5;	// d-bump
#endif
#ifdef USE_LM_HEMI
		float2	lmh	: TEXCOORD6;	// lm-hemi
#endif
	// [DA_PORT] mirrors v2p_bumped.
#ifdef DA_VELOCITY
	float4	hpos_curr : TEXCOORD7;
	float4	hpos_old  : TEXCOORD8;
#endif
};
////////////////////////////////////////////////////////////////
//	Defer flat
struct	v2p_flat
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4	tcdh	: TEXCOORD0;	// Texture coordinates,         w=sun_occlusion
#else
	float2	tcdh	: TEXCOORD0;	// Texture coordinates
#endif
	float4	position: TEXCOORD1;	// position + hemi
	float3	N		: TEXCOORD2;	// Eye-space normal        (for lighting)
#ifdef USE_TDETAIL
	float2	tcdbump	: TEXCOORD3;	// d-bump
#endif
#ifdef USE_LM_HEMI
	float2	lmh		: TEXCOORD4;	// lm-hemi
#endif
	// [DA_PORT] Clip-space position for this frame and the previous one, for motion vectors. hpos below
	// cannot serve: by the time the pixel shader sees SV_Position it has already been divided by w and
	// turned into screen coordinates, so the perspective divide has to be carried out by hand from these.
	// Slots 5 and 6 are free in every combination of the options above.
#ifdef DA_VELOCITY
	float4	hpos_curr : TEXCOORD5;
	float4	hpos_old  : TEXCOORD6;
#endif
	float4	hpos	: SV_Position;
};

struct	p_flat
{
#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	float4	tcdh	: TEXCOORD0;	// Texture coordinates,         w=sun_occlusion
#else
	float2	tcdh	: TEXCOORD0;	// Texture coordinates
#endif
	float4	position: TEXCOORD1;	// position + hemi
	float3	N		: TEXCOORD2;	// Eye-space normal        (for lighting)
#ifdef USE_TDETAIL
	float2	tcdbump	: TEXCOORD3;	// d-bump
#endif
#ifdef USE_LM_HEMI
	float2	lmh		: TEXCOORD4;	// lm-hemi
#endif
	// [DA_PORT] Must mirror v2p_flat exactly — these are the same interpolators on the receiving side.
#ifdef DA_VELOCITY
	float4	hpos_curr : TEXCOORD5;
	float4	hpos_old  : TEXCOORD6;
#endif
};

////////////////////////////////////////////////////////////////
//	Shadow
struct	v_shadow_direct_aref
{
	float4	P		: POSITION;		// (float,float,float,1)
	int4	tc		: TEXCOORD0;	// (u,v,frac,???)
};

struct	v_shadow_direct
{
	float4	P		: POSITION;		// (float,float,float,1)
};


struct	v2p_shadow_direct_aref
{
	float2	tc0		: TEXCOORD1;	// Diffuse map for aref
	float4	hpos	: SV_Position;	// Clip-space position         (for rasterization)
};

struct	v2p_shadow_direct
{
	float4	hpos	: SV_Position;		// Clip-space position         (for rasterization)
};

struct	p_shadow_direct_aref
{
	float2	tc0		: TEXCOORD1;	// Diffuse map for aref
};

////////////////////////////////////////////////////////////////
//	Model
struct	v_model
{
	float4	P		: POSITION;		// (float,float,float,1)
	float3	N		: NORMAL;		// (nx,ny,nz)
	float3	T		: TANGENT;		// (nx,ny,nz)
	float3	B		: BINORMAL;		// (nx,ny,nz)
	float2	tc		: TEXCOORD0;	// (u,v)
};

////////////////////////////////////////////////////////////////
//	Tree
struct	v_tree
{
	float4	P		: POSITION;		// (float,float,float,1)
	float4	Nh		: NORMAL;		// (nx,ny,nz)
	float3	T		: TANGENT;		// tangent
	float3	B		: BINORMAL;		// binormal
	int4	tc		: TEXCOORD0;	// (u,v,frac,???)
};

////////////////////////////////////////////////////////////////
//	Details
struct        v_detail                    
{
        float4      pos                : POSITION;                // (float,float,float,1)
        int4        misc        : TEXCOORD0;        // (u(Q),v(Q),frac,matrix-id)
};

#endif	//	common_iostructs_h_included