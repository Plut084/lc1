static const float pi = 3.14159265359;

// Returns BRDF only. The caller applies radiance, N dot L and visibility.
float3 evaluate_pbr(float3 base_color, float metallic, float roughness,
                    float3 normal, float3 view, float3 light)
{
    float n_dot_v = saturate(dot(normal, view));
    float n_dot_l = saturate(dot(normal, light));
    float3 half_sum = view + light;
    float half_squared = dot(half_sum, half_sum);
    if (n_dot_v <= 0.0 || n_dot_l <= 0.0 || half_squared <= 1e-8)
        return float3(0);
    float3 halfway = half_sum * rsqrt(half_squared);
    float n_dot_h = saturate(dot(normal, halfway));
    float v_dot_h = saturate(dot(view, halfway));
    // Perceptual roughness is squared exactly once to obtain GGX alpha.
    float perceptual = clamp(roughness, 0.045, 1.0);
    float alpha = perceptual * perceptual;
    float alpha_squared = alpha * alpha;
    // |N x H|² avoids cancellation in 1 - (N dot H)² at sharp highlights.
    float3 n_cross_h = cross(normal, halfway);
    float denominator = dot(n_cross_h, n_cross_h) + alpha_squared * n_dot_h * n_dot_h;
    float distribution = alpha_squared / (pi * denominator * denominator);
    // Separable Smith-GGX G/(4*NdotV*NdotL), cancelled before evaluating.
    float view_mask = n_dot_v + sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_v * n_dot_v);
    float light_mask = n_dot_l + sqrt(alpha_squared + (1.0 - alpha_squared) * n_dot_l * n_dot_l);
    float visibility = 1.0 / (view_mask * light_mask);
    float3 f0 = lerp(float3(0.04), base_color, metallic);
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - v_dot_h, 5.0);
    float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * base_color / pi;
    return diffuse + distribution * visibility * fresnel;
}
