#include "common.h"

struct vv
{
	float4 P	: POSITION;
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;
};

struct v2p
{
	float2 tc	: TEXCOORD0;
	float4 c	: COLOR0;

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	float4 tctexgen	: TEXCOORD1;
#endif	//	USE_SOFT_PARTICLES

	float4 hpos	: SV_Position;
	float  fog	: FOG;
};

uniform float4x4 	mVPTexgen;

v2p main (vv v)
{
	v2p 		o;

	o.hpos 		= mul	(m_WVP, v.P);		// xform, input in world coords

	// [DA_PORT] Тот же сдвиг, что у мировой геометрии.
	//
	// Этот шейдер собирают particles_xadd.s и particles_xdistort.s - аддитивные частицы и
	// искажение экрана, то есть искры и марево аномалий. Джиттера здесь не было, и частицы
	// стояли в НЕсмещённом экранном пространстве, пока весь мир под ними ездил на доли пикселя
	// каждый кадр. В скоплении искр расхождение читается как дрожание всей картинки - так это и
	// проявилось при входе в электрическую аномалию, и пропадало сразу на выходе.
	//
	// Промах, стоивший трёх заходов: правка ушла в particle-clip.vs и deffer_particle.vs, а искры
	// собираются НЕ на них. Имя вершинного шейдера задаёт .s-блендер первым аргументом begin(),
	// и проверять надо там, а не по похожести имён файлов.
	//
	// Сдвигается только hpos. tctexgen (мягкие частицы) считается отдельно от v.P и остаётся
	// несмещённым: он служит для выборки глубины, промах там ограничен половиной пикселя и
	// несопоставим с тем, что чинится здесь.
	//
	// Безусловно, без #ifdef: m_taa_jitter объявлена вне условий и равна нулю, пока временного
	// апскейлера нет.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
//	o.hpos 		= mul	(m_VP, v.P);		// xform, input in world coords
	o.tc		= v.tc;				// copy tc
	o.c			= unpack_D3DCOLOR(v.c);				// copy color

//	Igor: for additional depth dest
#ifdef	USE_SOFT_PARTICLES
	o.tctexgen 	= mul( mVPTexgen, v.P);
	o.tctexgen.z	= o.hpos.z;
#endif	//	USE_SOFT_PARTICLES

	o.fog 		= saturate(calc_fogging(v.P));	// skyloader: fog, input in world coords

	return o;
}
