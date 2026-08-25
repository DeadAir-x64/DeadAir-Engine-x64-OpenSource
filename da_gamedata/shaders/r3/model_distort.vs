#include "common.h"
#include "skin.h"

struct vf
{
  float2 tc0	: TEXCOORD0;    // base
  float4 c0		: COLOR0;    // color
  float4 hpos	: SV_Position;
};

vf _main (v_model v)
{
  vf     o;

  o.hpos     	= mul      (m_WVP, v.P);    // xform, input in world coords
	// [DA_PORT] Подпиксельный сдвиг, как в deffer_model_flat.vs.
	//
	// Под апскейлером ВСЯ геометрия кадра рисуется смещённой на долю пикселя, и из этих смещений
	// набирается разрешение. Шейдер, который сдвиг не применяет, ложится мимо остальной сцены: у
	// следов и наложенных отметин это не только пила по краю, но и расхождение с поверхностью, на
	// которую они нанесены, — они дрожат относительно неё каждый кадр.
	//
	// Без апскейлера m_taa_jitter обнуляется каждый кадр (CameraManager.cpp), поэтому строка
	// ничего не меняет.
	o.hpos.xy += m_taa_jitter.xy * o.hpos.w;
  o.tc0    	= v.tc.xy;          // copy tc

  // calculate fade
	float3  dir_v   = normalize    (mul(m_WV,v.P));
	float3  norm_v  = normalize    (mul(m_WV,v.N));
  float   fade   = 1-abs      (dot(dir_v,norm_v));
  o.c0    = fade;

  return o;
}

/////////////////////////////////////////////////////////////////////////
#ifdef   SKIN_NONE
vf  main(v_model v)     { return _main(v);     }
#endif

#ifdef   SKIN_0
vf  main(v_model_skinned_0 v)   { return _main(skinning_0(v)); }
#endif

#ifdef  SKIN_1
vf  main(v_model_skinned_1 v)   { return _main(skinning_1(v)); }
#endif

#ifdef  SKIN_2
vf  main(v_model_skinned_2 v)   { return _main(skinning_2(v)); }
#endif

#ifdef  SKIN_3
vf  main(v_model_skinned_3 v)   { return _main(skinning_3(v)); }
#endif

#ifdef  SKIN_4
vf  main(v_model_skinned_4 v)   { return _main(skinning_4(v)); }
#endif