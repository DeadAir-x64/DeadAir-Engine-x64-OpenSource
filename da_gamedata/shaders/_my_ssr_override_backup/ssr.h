/*
    Screen Space Reflections — интеграция в water-шейдер (как в gl/water.ps): env = compute_ssr(pos_w, N, env).
    Реализация общеизвестной техники SSR (ray-march в мировом пространстве). Наш код, не копия чужих модов.
    Референс алгоритма: gl/ssr.h (X-Ray Oxygen, свободно). Входы: uniform m_VP, m_V, eye_position (common.h);
    samplers s_position (eye-space pos RT) и s_image (освещённый кадр) — объявлены в common.h.
*/
#ifndef SSR_H
#define SSR_H

#define SSR_EDGE_ATTENUATION 0.09
#ifndef SSR_SAMPLES
#  define SSR_SAMPLES 14
#endif
#ifndef SSR_DISTANCE
#  define SSR_DISTANCE 200.0
#endif

float RayAttenBorder(float2 pos, float value)
{
    float borderDist = min(1.0 - max(pos.x, pos.y), min(pos.x, pos.y));
    return saturate(borderDist > value ? 1.0 : borderDist / value);
}

float4 proj_to_screen(float4 proj)
{
    float4 screen = proj;
    screen.x = (proj.x + proj.w);
    screen.y = (proj.w - proj.y);
    screen.xy *= 0.5;
    return screen;
}

float is_sky(float depth) { return step(abs(depth - 10000.0), 0.001); }

// position — МИРОВАЯ позиция пикселя воды; normal — мировая нормаль; skybox — fallback (env-отражение)
float4 compute_ssr(float3 position, float3 normal, float3 skybox)
{
    float stepv = 1.0 / float(SSR_SAMPLES);
    float2 refl_tc = float2(0.0, 0.0);

    float3 v2point = normalize(position - eye_position);
    float3 vreflect = normalize(reflect(v2point, normalize(normal)));

    [loop]
    for (int i = 0; i < SSR_SAMPLES; i++)
    {
        float3 new_position = position + vreflect * stepv;

        float4 proj_position = mul(m_VP, float4(new_position, 1.0));
        float4 p2ss = proj_to_screen(proj_position);
        refl_tc.xy = p2ss.xy / p2ss.w;

        float hit_depth = tex2D(s_position, float2(refl_tc.x, -refl_tc.y)).z;
        hit_depth = lerp(hit_depth, 0.0, is_sky(hit_depth));

        float depth = mul(m_V, float4(position, 1.0)).z;

        if ((depth - hit_depth) > 0.0 || (hit_depth > SSR_DISTANCE))
            return float4(skybox.xyz, 0.0); // промах → небо (env), alpha 0 = «нет SSR»

        stepv = length(hit_depth - depth);
    }

    float edge = RayAttenBorder(refl_tc.xy, SSR_EDGE_ATTENUATION);
    float3 img = tex2D(s_image, float2(refl_tc.x, -refl_tc.y)).xyz;

    // ДИАГНОСТИКА: попадание луча = ЗЕЛЁНЫЙ (real-отражение вернём после подтверждения)
    return float4(0.0, 1.0, 0.0, 1.0);
    // return float4(lerp(skybox, img, edge), edge);
}
#endif // SSR_H
