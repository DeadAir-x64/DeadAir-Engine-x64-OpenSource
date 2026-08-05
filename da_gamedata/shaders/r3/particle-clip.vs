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
	float4 hpos	: SV_Position;
};

v2p main (vv v)
{
	v2p 		o;

	o.hpos 		= mul	(m_WVP, v.P);		// xform, input in world coords
	o.hpos.z	= abs	(o.hpos.z);
	o.hpos.w	= abs	(o.hpos.w);

	// [DA_PORT] Тот же сдвиг, что у мировой геометрии.
	//
	// Частицы рисуются поверх уже СМЕЩЁННОЙ сцены. Пока сдвига здесь не было, они стояли в своём
	// экранном пространстве, а мир — в другом, и расхождение менялось каждый кадр вместе с
	// последовательностью Halton. В скоплении искр это читается как тряска всей картинки: так
	// проявилось при входе в электрическую аномалию.
	//
	// Безусловно, без #ifdef: m_taa_jitter объявлена вне условий и равна нулю, пока временного
	// апскейлера нет.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
	o.tc		= v.tc;				// copy tc
	o.c		= v.c;				// copy color

	return o;
}
