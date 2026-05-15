struct DecodedAppearance3D {
    AppearanceSample3D appearance;
    bool alpha_test_enabled;
    float alpha_cutoff;
    bool unlit;
};

bool appearance_record_has_texture(uint presence_mask_, uint flag_) {
    return (presence_mask_ & flag_) != 0u;
}

uint appearance_record_alpha_mode(AppearanceGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_alpha_mode_shift) & 0x3u;
}

uint appearance_record_shading_model(AppearanceGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_shading_model_shift) & 0x3u;
}

bool appearance_has_valid_tangent_basis(vec3 tangent_world_, vec3 bitangent_world_) {
    return dot(tangent_world_, tangent_world_) > 1e-6 &&
           dot(bitangent_world_, bitangent_world_) > 1e-6;
}

DecodedAppearance3D decode_appearance_record_3d(AppearanceGpuRecord appearance_record_,
                                                vec2 sample_uv_,
                                                vec3 normal_world_,
                                                vec3 tangent_world_,
                                                vec3 bitangent_world_) {
    DecodedAppearance3D decoded;
    uint presence_mask = appearance_record_.textures1_u32.w;
    uint sampler_slot = appearance_record_.textures1_u32.y;
    vec4 base_factor = appearance_record_.base_rgba;
    vec4 emissive_factor = appearance_record_.emissive_rgba;
    vec4 appearance_params = appearance_record_.appearance_params;
    vec4 extras = appearance_record_.extras;
    float occlusion_strength = clamp(appearance_params.w, 0.0, 1.0);

    vec4 base_sample = vec4(1.0);
    if (appearance_record_has_texture(presence_mask, k_appearance_texture_presence_base_color)) {
        base_sample = SampleTexture2D(appearance_record_.textures0_u32.x,
                                      sampler_slot,
                                      sample_uv_);
    }

    decoded.appearance.base_color = base_sample.rgb * base_factor.rgb;
    decoded.appearance.alpha = base_sample.a * base_factor.a * clamp(extras.z, 0.0, 1.0);
    decoded.appearance.metallic = clamp(appearance_params.x, 0.0, 1.0);
    decoded.appearance.roughness = clamp(appearance_params.y, 0.04, 1.0);
    decoded.appearance.normal_scale = max(appearance_params.z, 0.0);
    decoded.appearance.occlusion = occlusion_strength;
    decoded.appearance.normal_world = normalize(normal_world_);

    vec3 orm_sample = vec3(1.0);
    if (appearance_record_has_texture(presence_mask, k_appearance_texture_presence_metal_rough)) {
        orm_sample = SampleTexture2D(appearance_record_.textures0_u32.z,
                                     sampler_slot,
                                     sample_uv_).rgb;
        decoded.appearance.roughness =
            clamp(orm_sample.g * decoded.appearance.roughness, 0.04, 1.0);
        decoded.appearance.metallic =
            clamp(orm_sample.b * decoded.appearance.metallic, 0.0, 1.0);
    }

    float occlusion_value = 1.0;
    if (appearance_record_has_texture(presence_mask, k_appearance_texture_presence_occlusion)) {
        occlusion_value = SampleTexture2D(appearance_record_.textures0_u32.w,
                                          sampler_slot,
                                          sample_uv_).r;
    } else if (appearance_record_has_texture(presence_mask,
                                             k_appearance_texture_presence_metal_rough)) {
        occlusion_value = orm_sample.r;
    }
    decoded.appearance.occlusion = mix(1.0, occlusion_value, occlusion_strength);

    if (appearance_record_has_texture(presence_mask, k_appearance_texture_presence_normal) &&
        appearance_has_valid_tangent_basis(tangent_world_, bitangent_world_)) {
        vec3 tangent_normal =
            SampleTexture2D(appearance_record_.textures0_u32.y,
                            sampler_slot,
                            sample_uv_).xyz * 2.0 - 1.0;
        tangent_normal.xy *= decoded.appearance.normal_scale;
        tangent_normal = normalize(tangent_normal);

        vec3 tangent_world = normalize(tangent_world_);
        vec3 bitangent_world = normalize(bitangent_world_);
        mat3 tbn = mat3(tangent_world, bitangent_world, normalize(normal_world_));
        decoded.appearance.normal_world = normalize(tbn * tangent_normal);
    }

    vec3 emissive_sample = vec3(1.0);
    if (appearance_record_has_texture(presence_mask, k_appearance_texture_presence_emissive)) {
        emissive_sample =
            SampleTexture2D(appearance_record_.textures1_u32.x,
                            sampler_slot,
                            sample_uv_).rgb;
    }
    decoded.appearance.emissive = emissive_sample * emissive_factor.rgb * max(extras.x, 0.0);
    decoded.alpha_test_enabled =
        appearance_record_alpha_mode(appearance_record_) == k_appearance_alpha_mode_masked;
    decoded.alpha_cutoff = clamp(extras.y, 0.0, 1.0);
    decoded.unlit =
        appearance_record_shading_model(appearance_record_) == k_appearance_shading_model_unlit;
    return decoded;
}
