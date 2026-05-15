const float k_pbr_pi = 3.14159265358979323846;

struct AppearanceSample3D {
    vec3 base_color;
    float alpha;

    float metallic;
    float roughness;
    float normal_scale;
    float occlusion;

    vec3 emissive;
    vec3 normal_world;
};

float D_GGX(float NoH, float roughness) {
    float perceptual_roughness = clamp(roughness, 0.04, 1.0);
    float alpha = perceptual_roughness * perceptual_roughness;
    float alpha2 = alpha * alpha;
    float denom = NoH * NoH * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(k_pbr_pi * denom * denom, 1e-6);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float roughness) {
    float perceptual_roughness = clamp(roughness, 0.04, 1.0);
    float alpha = perceptual_roughness * perceptual_roughness;
    float alpha2 = alpha * alpha;

    float lambda_v = NoL * sqrt(max((NoV - NoV * alpha2) * NoV + alpha2, 1e-6));
    float lambda_l = NoV * sqrt(max((NoL - NoL * alpha2) * NoL + alpha2, 1e-6));
    return 0.5 / max(lambda_v + lambda_l, 1e-6);
}

vec3 F_Schlick(vec3 F0, float VoH) {
    float clamped = clamp(1.0 - VoH, 0.0, 1.0);
    float factor = clamped * clamped * clamped * clamped * clamped;
    return F0 + (vec3(1.0) - F0) * factor;
}

vec3 ResolvePbrF0(AppearanceSample3D appearance_) {
    return mix(vec3(0.04), appearance_.base_color, appearance_.metallic);
}

vec3 ResolvePbrDiffuseColor(AppearanceSample3D appearance_) {
    return appearance_.base_color * (1.0 - appearance_.metallic);
}

vec3 EvaluatePbrIblFromTerms(AppearanceSample3D appearance_,
                             vec3 view_dir_,
                             vec3 diffuse_irradiance_,
                             vec3 prefiltered_specular_,
                             vec2 brdf_) {
    vec3 view_dir = normalize(view_dir_);
    vec3 normal_world = normalize(appearance_.normal_world);
    float n_dot_v = max(dot(normal_world, view_dir), 0.0);

    vec3 diffuse_ibl = diffuse_irradiance_ * ResolvePbrDiffuseColor(appearance_);
    vec3 fresnel = F_Schlick(ResolvePbrF0(appearance_), n_dot_v);
    vec3 specular_ibl = prefiltered_specular_ * (fresnel * brdf_.x + brdf_.y);
    return diffuse_ibl + specular_ibl;
}

vec3 EvaluatePbrDirect(AppearanceSample3D appearance_,
                       vec3 view_dir_,
                       vec3 light_dir_,
                       vec3 light_radiance_) {
    vec3 N = normalize(appearance_.normal_world);
    vec3 V = normalize(view_dir_);
    vec3 L = normalize(light_dir_);
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    if (NoL <= 0.0) {
        return vec3(0.0);
    }

    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 1e-4);
    float VoH = max(dot(V, H), 1e-4);

    vec3 F0 = ResolvePbrF0(appearance_);
    vec3 F = F_Schlick(F0, VoH);
    float D = D_GGX(NoH, appearance_.roughness);
    float Vis = V_SmithGGXCorrelated(NoV, NoL, appearance_.roughness);

    vec3 specular = D * Vis * F;
    vec3 kd = (vec3(1.0) - F) * (1.0 - appearance_.metallic);
    vec3 diffuse = kd * appearance_.base_color * (1.0 / k_pbr_pi);

    return (diffuse + specular) * light_radiance_ * NoL;
}
