#version 460
#extension GL_GOOGLE_include_directive : require
#include "vr/common/math.glsl"
#include "vr/render/bindless.glsl"
#include "vr/render/pbr.glsl"

layout(push_constant) uniform Geometry3DPushConstants {
    mat4 view_projection;
    vec4 light_direction_intensity;
    vec4 camera_position_reserved;
    vec4 material_uv_transform;
    uint material_flags;
    float alpha_cutoff;
    uint material_texture_slot;
    uint material_sampler_slot;
} pc;

layout(location = 0) in vec3 in_normal_world;
layout(location = 1) in vec4 in_albedo;
layout(location = 2) in vec4 in_material_params;
layout(location = 3) flat in uint in_instance_params;
layout(location = 4) in vec2 in_uv;
layout(location = 5) in vec3 in_world_position;
layout(location = 6) in float in_occlusion_strength;
layout(location = 7) flat in uint in_appearance_record_index;
layout(location = 8) in vec3 in_tangent_world;
layout(location = 9) in vec3 in_bitangent_world;

struct LightRecord3D {
    vec4 position_radius;
    vec4 color_intensity;
    vec4 direction_cone_outer;
    vec4 cone_source;
    float falloff_exponent;
    float volumetric_strength;
    uint light_type;
    uint channel_mask;
    uint flags;
    uint shadow_view_begin;
    uint shadow_meta;
    uint shadow_namespace_id;
};

struct ClusterHeaderPacked {
    uint offset;
    uint packed_count_flags;
};

struct ShadowViewRecord {
    mat4 view_matrix;
    mat4 projection_matrix;
    mat4 view_projection_matrix;
    vec4 split_bias;
    vec2 slope_texel;
    uvec2 namespace_component;
    uvec4 atlas_rect;
    uvec4 layer_view_cascade_flags;
};

struct MaterialGpuRecord {
    vec4 base_rgba;
    vec4 emissive_rgba;
    vec4 material_params;
    vec4 extras;
    uvec4 flags_u32;
    uvec4 textures0_u32;
    uvec4 textures1_u32;
};

layout(set = 2, binding = 0, std430) readonly buffer LightRecordBuffer {
    LightRecord3D light_records[];
};

layout(set = 2, binding = 1, std430) readonly buffer ClusterHeaderBuffer {
    ClusterHeaderPacked cluster_headers[];
};

layout(set = 2, binding = 2, std430) readonly buffer ClusterIndexBuffer {
    uint cluster_light_indices[];
};

layout(set = 2, binding = 3, std430) readonly buffer ShadowViewBuffer {
    ShadowViewRecord shadow_views[];
};

layout(set = 2, binding = 4, std140) uniform LightingParamsBuffer {
    vec4 camera_position_light_count;
    vec4 camera_forward_max_lights;
    uvec4 cluster_dims_reverse_z;
    vec4 near_far_scale_bias;
    vec4 framebuffer_shadow_views;
    uvec4 shadow_atlas_texture_sampler_slots;
} lighting_params;

layout(set = 2, binding = 7, std430) readonly buffer MaterialRecordBuffer {
    MaterialGpuRecord material_records[];
};

layout(set = 3, binding = 0, std140) uniform IblParamsBuffer {
    vec4 ibl_sh9[9];
    vec4 ibl_tint_intensity;
    vec4 ibl_rotation_max_lod_flags;
    uvec4 texture_sampler_slots;
} ibl_params;

layout(location = 0) out vec4 out_color;

const uint k_light_kind_global = 0u;
const uint k_light_kind_directional = 1u;
const uint k_light_kind_point = 2u;
const uint k_light_kind_spot = 3u;

const uint k_shadow_projection_directional = 0u;
const uint k_shadow_projection_spot = 1u;
const uint k_shadow_projection_point = 2u;

const uint k_invalid_shadow_view_begin = 0xFFFFFFFFu;
const uint k_shadow_view_flag_stabilize = 1u << 0u;
const uint k_shadow_view_flag_reverse_z = 1u << 1u;
const uint k_shadow_view_flag_filter_kernel_shift = 2u;
const uint k_shadow_view_flag_filter_kernel_mask = 0x3u << k_shadow_view_flag_filter_kernel_shift;
const uint k_shadow_filter_hard = 0u;
const uint k_shadow_filter_pcf3x3 = 1u;
const uint k_shadow_filter_pcf5x5 = 2u;
const uint k_invalid_material_record_index = 0xFFFFFFFFu;

const uint k_appearance_alpha_mode_shift = 3u;
const uint k_appearance_alpha_mode_mask = 0x3u << k_appearance_alpha_mode_shift;
const uint k_appearance_shading_model_shift = 5u;
const uint k_appearance_shading_model_mask = 0x3u << k_appearance_shading_model_shift;
const uint k_appearance_alpha_mode_opaque = 0u;
const uint k_appearance_alpha_mode_masked = 1u;
const uint k_appearance_shading_model_unlit = 0u;

const uint k_material_texture_presence_base_color = 1u << 0u;
const uint k_material_texture_presence_normal = 1u << 1u;
const uint k_material_texture_presence_metal_rough = 1u << 2u;
const uint k_material_texture_presence_occlusion = 1u << 3u;
const uint k_material_texture_presence_emissive = 1u << 4u;

uint unpack_shadow_view_count(uint shadow_meta_) {
    return shadow_meta_ & 0xFFFFu;
}

uint unpack_shadow_projection_kind(uint shadow_meta_) {
    return (shadow_meta_ >> 16u) & 0xFFu;
}

uint compute_cluster_z(float depth_distance) {
    const uint cluster_count_z = max(lighting_params.cluster_dims_reverse_z.z, 1u);
    if (cluster_count_z <= 1u) {
        return 0u;
    }

    float near_plane = max(1e-4, lighting_params.near_far_scale_bias.x);
    float far_plane = max(near_plane + 1e-4, lighting_params.near_far_scale_bias.y);
    float z_scale = max(1e-4, lighting_params.near_far_scale_bias.z);
    float z_bias = max(1e-4, lighting_params.near_far_scale_bias.w);

    float clamped_depth = clamp(depth_distance, near_plane, far_plane);
    float near_t = log2(near_plane * z_scale + z_bias);
    float far_t = log2(far_plane * z_scale + z_bias);
    float depth_t = log2(clamped_depth * z_scale + z_bias);
    float denom = max(1e-4, far_t - near_t);
    float normalized = vr_saturate((depth_t - near_t) / denom);
    if ((lighting_params.cluster_dims_reverse_z.w & 0x1u) != 0u) {
        normalized = 1.0 - normalized;
    }
    uint z_index = uint(normalized * float(cluster_count_z));
    return min(z_index, cluster_count_z - 1u);
}

uint compute_cluster_index(vec2 frag_coord, float depth_distance) {
    const uint cluster_count_x = max(lighting_params.cluster_dims_reverse_z.x, 1u);
    const uint cluster_count_y = max(lighting_params.cluster_dims_reverse_z.y, 1u);
    const uint cluster_count_z = max(lighting_params.cluster_dims_reverse_z.z, 1u);
    const float framebuffer_width = max(lighting_params.framebuffer_shadow_views.x, 1.0);
    const float framebuffer_height = max(lighting_params.framebuffer_shadow_views.y, 1.0);

    float norm_x = vr_saturate(frag_coord.x / framebuffer_width);
    float norm_y = vr_saturate(frag_coord.y / framebuffer_height);
    uint x_index = min(uint(norm_x * float(cluster_count_x)), cluster_count_x - 1u);
    uint y_index = min(uint(norm_y * float(cluster_count_y)), cluster_count_y - 1u);
    uint z_index = compute_cluster_z(depth_distance);
    return z_index * (cluster_count_x * cluster_count_y) + y_index * cluster_count_x + x_index;
}

uint unpack_shadow_filter_kernel(uint flags_) {
    return (flags_ & k_shadow_view_flag_filter_kernel_mask) >> k_shadow_view_flag_filter_kernel_shift;
}

int resolve_shadow_kernel_radius(uint flags_) {
    uint filter_kernel = unpack_shadow_filter_kernel(flags_);
    if (filter_kernel == k_shadow_filter_pcf5x5) {
        return 2;
    }
    if (filter_kernel == k_shadow_filter_hard) {
        return 0;
    }
    return 1;
}

float compute_shadow_receiver_bias(ShadowViewRecord view_record_,
                                   float n_dot_l_) {
    float slope_scaled_bias = max(view_record_.slope_texel.x, 0.0);
    float texel_world_size = max(view_record_.slope_texel.y, 0.0);
    float grazing_factor = 1.0 - clamp(n_dot_l_, 0.0, 1.0);
    return max(0.0, slope_scaled_bias * texel_world_size * grazing_factor * 0.25);
}

float sample_shadow_view_pcf(uint view_index_,
                             vec3 world_position_,
                             float n_dot_l_) {
    uint max_shadow_views = uint(max(0.0, lighting_params.framebuffer_shadow_views.z));
    if (view_index_ >= max_shadow_views) {
        return 1.0;
    }

    ShadowViewRecord view_record = shadow_views[view_index_];
    vec4 clip = view_record.view_projection_matrix * vec4(world_position_, 1.0);
    if (abs(clip.w) <= 1e-6) {
        return 1.0;
    }

    vec3 ndc = clip.xyz / clip.w;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
        return 1.0;
    }

    float depth_value = ndc.z;
    if (depth_value <= 0.0 || depth_value >= 1.0) {
        return 1.0;
    }
    ivec3 atlas_size =
        textureSize(g_Textures2DArray[nonuniformEXT(lighting_params.shadow_atlas_texture_sampler_slots.x)], 0);
    vec2 atlas_size_f = vec2(max(atlas_size.x, 1), max(atlas_size.y, 1));
    vec2 rect_origin = vec2(view_record.atlas_rect.x, view_record.atlas_rect.y);
    vec2 rect_extent = vec2(max(view_record.atlas_rect.z, 1u), max(view_record.atlas_rect.w, 1u));
    vec2 atlas_uv = (rect_origin + uv * rect_extent) / atlas_size_f;
    vec2 rect_min_uv = rect_origin / atlas_size_f;
    vec2 rect_max_uv = (rect_origin + rect_extent - vec2(1.0, 1.0)) / atlas_size_f;
    float layer = float(view_record.layer_view_cascade_flags.x);

    vec2 texel_size = 1.0 / atlas_size_f;
    int kernel_radius = resolve_shadow_kernel_radius(view_record.layer_view_cascade_flags.w);
    vec2 border_guard = texel_size * (float(kernel_radius) + 0.5);
    if (atlas_uv.x <= rect_min_uv.x + border_guard.x ||
        atlas_uv.y <= rect_min_uv.y + border_guard.y ||
        atlas_uv.x >= rect_max_uv.x - border_guard.x ||
        atlas_uv.y >= rect_max_uv.y - border_guard.y) {
        return 1.0;
    }

    float normal_bias = max(view_record.split_bias.w, 0.0);
    float receiver_bias = compute_shadow_receiver_bias(view_record, n_dot_l_);
    float combined_bias = max(0.0, normal_bias + receiver_bias);
    bool reverse_z = (view_record.layer_view_cascade_flags.w & k_shadow_view_flag_reverse_z) != 0u;

    if (kernel_radius == 0) {
        float stored_depth =
            SampleTexture2DArray(lighting_params.shadow_atlas_texture_sampler_slots.x,
                                 lighting_params.shadow_atlas_texture_sampler_slots.y,
                                 vec3(atlas_uv, layer)).r;
        if (!reverse_z) {
            return (depth_value - combined_bias > stored_depth) ? 0.0 : 1.0;
        }
        return (depth_value + combined_bias < stored_depth) ? 0.0 : 1.0;
    }

    float shadow_sum = 0.0;
    float sample_count = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            if (abs(x) > kernel_radius || abs(y) > kernel_radius) {
                continue;
            }
            vec2 tap_uv = atlas_uv + vec2(float(x), float(y)) * texel_size;
            if (tap_uv.x < rect_min_uv.x || tap_uv.y < rect_min_uv.y ||
                tap_uv.x > rect_max_uv.x || tap_uv.y > rect_max_uv.y) {
                shadow_sum += 1.0;
                sample_count += 1.0;
                continue;
            }
            float stored_depth =
                SampleTexture2DArray(lighting_params.shadow_atlas_texture_sampler_slots.x,
                                     lighting_params.shadow_atlas_texture_sampler_slots.y,
                                     vec3(tap_uv, layer)).r;
            float visible = 1.0;
            if (!reverse_z) {
                if (depth_value - combined_bias > stored_depth) {
                    visible = 0.0;
                }
            } else {
                if (depth_value + combined_bias < stored_depth) {
                    visible = 0.0;
                }
            }
            shadow_sum += visible;
            sample_count += 1.0;
        }
    }
    return (sample_count > 0.0) ? (shadow_sum / sample_count) : 1.0;
}

int pick_directional_shadow_view(uint view_begin_, uint view_count_, float depth_distance_) {
    uint max_shadow_views = uint(max(0.0, lighting_params.framebuffer_shadow_views.z));
    if (view_count_ == 0u || view_begin_ >= max_shadow_views) {
        return -1;
    }

    uint safe_count = min(view_count_, max_shadow_views - view_begin_);
    int best_index = int(view_begin_ + safe_count - 1u);
    for (uint i = 0u; i < safe_count; ++i) {
        uint view_index = view_begin_ + i;
        ShadowViewRecord view_record = shadow_views[view_index];
        if (depth_distance_ >= view_record.split_bias.x &&
            depth_distance_ <= view_record.split_bias.y) {
            return int(view_index);
        }
    }
    return best_index;
}

int pick_spot_shadow_view(uint view_begin_, uint view_count_) {
    uint max_shadow_views = uint(max(0.0, lighting_params.framebuffer_shadow_views.z));
    if (view_count_ == 0u || view_begin_ >= max_shadow_views) {
        return -1;
    }
    return int(view_begin_);
}

uint pick_point_face_index(vec3 light_to_fragment_) {
    vec3 abs_direction = abs(light_to_fragment_);
    if (abs_direction.x >= abs_direction.y && abs_direction.x >= abs_direction.z) {
        return light_to_fragment_.x >= 0.0 ? 0u : 1u;
    }
    if (abs_direction.y >= abs_direction.x && abs_direction.y >= abs_direction.z) {
        return light_to_fragment_.y >= 0.0 ? 2u : 3u;
    }
    return light_to_fragment_.z >= 0.0 ? 4u : 5u;
}

int pick_point_shadow_view(uint view_begin_, uint view_count_, vec3 light_to_fragment_) {
    uint max_shadow_views = uint(max(0.0, lighting_params.framebuffer_shadow_views.z));
    if (view_count_ == 0u || view_begin_ >= max_shadow_views) {
        return -1;
    }
    uint safe_count = min(view_count_, max_shadow_views - view_begin_);
    if (safe_count == 1u) {
        return int(view_begin_);
    }
    uint face_index = pick_point_face_index(light_to_fragment_);
    if (face_index >= safe_count) {
        face_index = 0u;
    }
    return int(view_begin_ + face_index);
}

float evaluate_shadow_factor(LightRecord3D light_record_,
                             vec3 world_position_,
                             vec3 normal_world_,
                             vec3 light_dir_,
                             float depth_distance_) {
    bool cast_shadow = (light_record_.flags & 0x1u) != 0u;
    uint shadow_view_begin = light_record_.shadow_view_begin;
    uint shadow_view_count = unpack_shadow_view_count(light_record_.shadow_meta);
    uint shadow_projection_kind = unpack_shadow_projection_kind(light_record_.shadow_meta);
    if (!cast_shadow || shadow_view_begin == k_invalid_shadow_view_begin || shadow_view_count == 0u) {
        return 1.0;
    }

    int shadow_view_index = -1;
    if (shadow_projection_kind == k_shadow_projection_directional) {
        shadow_view_index = pick_directional_shadow_view(shadow_view_begin,
                                                         shadow_view_count,
                                                         depth_distance_);
    } else if (shadow_projection_kind == k_shadow_projection_spot) {
        shadow_view_index = pick_spot_shadow_view(shadow_view_begin, shadow_view_count);
    } else if (shadow_projection_kind == k_shadow_projection_point) {
        vec3 light_to_fragment = world_position_ - light_record_.position_radius.xyz;
        shadow_view_index = pick_point_shadow_view(shadow_view_begin,
                                                   shadow_view_count,
                                                   light_to_fragment);
    }

    if (shadow_view_index < 0) {
        return 1.0;
    }
    float n_dot_l = max(dot(normalize(normal_world_), normalize(light_dir_)), 0.0);
    return sample_shadow_view_pcf(uint(shadow_view_index), world_position_, n_dot_l);
}

vec3 rotate_environment_direction(vec3 direction_) {
    float sin_y = ibl_params.ibl_rotation_max_lod_flags.x;
    float cos_y = ibl_params.ibl_rotation_max_lod_flags.y;
    vec3 rotated = vec3(
        cos_y * direction_.x + sin_y * direction_.z,
        direction_.y,
        -sin_y * direction_.x + cos_y * direction_.z);
    float length_sq = dot(rotated, rotated);
    if (length_sq <= 1e-6) {
        return direction_;
    }
    return rotated * inversesqrt(length_sq);
}

vec3 evaluate_sh9_irradiance(vec3 normal_) {
    vec3 n = rotate_environment_direction(normalize(normal_));
    float x = n.x;
    float y = n.y;
    float z = n.z;

    float basis[9];
    basis[0] = 0.282095;
    basis[1] = 0.488603 * y;
    basis[2] = 0.488603 * z;
    basis[3] = 0.488603 * x;
    basis[4] = 1.092548 * x * y;
    basis[5] = 1.092548 * y * z;
    basis[6] = 0.315392 * (3.0 * z * z - 1.0);
    basis[7] = 1.092548 * x * z;
    basis[8] = 0.546274 * (x * x - y * y);

    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < 9; ++i) {
        irradiance += ibl_params.ibl_sh9[i].rgb * basis[i];
    }
    return max(irradiance, vec3(0.0));
}

struct DecodedGeometryMaterial {
    MaterialSample material;
    bool alpha_test_enabled;
    float alpha_cutoff;
    bool unlit;
};

bool has_material_record() {
    return in_appearance_record_index != k_invalid_material_record_index &&
           in_appearance_record_index < uint(material_records.length());
}

bool material_texture_present(uint presence_mask_, uint flag_) {
    return (presence_mask_ & flag_) != 0u;
}

uint material_alpha_mode(MaterialGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_alpha_mode_shift) & 0x3u;
}

uint material_shading_model(MaterialGpuRecord record_) {
    return (record_.flags_u32.x >> k_appearance_shading_model_shift) & 0x3u;
}

bool has_valid_tangent_basis() {
    return dot(in_tangent_world, in_tangent_world) > 1e-6 &&
           dot(in_bitangent_world, in_bitangent_world) > 1e-6;
}

DecodedGeometryMaterial decode_fallback_geometry_material(vec3 normal_world_) {
    DecodedGeometryMaterial decoded;
    float occlusion_strength = clamp(in_occlusion_strength, 0.0, 1.0);
    decoded.material.base_color = in_albedo.rgb;
    decoded.material.alpha = in_albedo.a;
    decoded.material.metallic = clamp(in_material_params.x, 0.0, 1.0);
    decoded.material.roughness = clamp(in_material_params.y, 0.04, 1.0);
    decoded.material.normal_scale = max(in_material_params.z, 0.0);
    decoded.material.occlusion = occlusion_strength;
    decoded.material.emissive = vec3(0.0);
    decoded.material.normal_world = normalize(normal_world_);
    decoded.alpha_test_enabled = (pc.material_flags & 0x1u) != 0u;
    decoded.alpha_cutoff = clamp(pc.alpha_cutoff, 0.0, 1.0);
    decoded.unlit = ((in_instance_params >> 7u) & 0x1u) == 0u;

    vec2 fallback_uv = in_uv * pc.material_uv_transform.xy + pc.material_uv_transform.zw;
    vec4 fallback_albedo =
        SampleTexture2D(pc.material_texture_slot,
                        pc.material_sampler_slot,
                        fallback_uv);
    decoded.material.base_color *= fallback_albedo.rgb;
    decoded.material.alpha *= fallback_albedo.a;
    return decoded;
}

DecodedGeometryMaterial decode_appearance_geometry_material(vec3 normal_world_,
                                                            MaterialGpuRecord material_record) {
    DecodedGeometryMaterial decoded;
    uint presence_mask = material_record.textures1_u32.w;
    uint sampler_slot = material_record.textures1_u32.y;
    vec4 base_factor = material_record.base_rgba;
    vec4 emissive_factor = material_record.emissive_rgba;
    vec4 material_params = material_record.material_params;
    vec4 extras = material_record.extras;
    float occlusion_strength = clamp(material_params.w, 0.0, 1.0);

    vec4 base_sample = vec4(1.0);
    if (material_texture_present(presence_mask, k_material_texture_presence_base_color)) {
        base_sample = SampleTexture2D(material_record.textures0_u32.x,
                                      sampler_slot,
                                      in_uv);
    }

    decoded.material.base_color = base_sample.rgb * base_factor.rgb;
    decoded.material.alpha = base_sample.a * base_factor.a * clamp(extras.z, 0.0, 1.0);
    decoded.material.metallic = clamp(material_params.x, 0.0, 1.0);
    decoded.material.roughness = clamp(material_params.y, 0.04, 1.0);
    decoded.material.normal_scale = max(material_params.z, 0.0);
    decoded.material.occlusion = occlusion_strength;
    decoded.material.normal_world = normalize(normal_world_);

    vec3 orm_sample = vec3(1.0);
    if (material_texture_present(presence_mask, k_material_texture_presence_metal_rough)) {
        orm_sample = SampleTexture2D(material_record.textures0_u32.z,
                                     sampler_slot,
                                     in_uv).rgb;
        decoded.material.roughness =
            clamp(orm_sample.g * decoded.material.roughness, 0.04, 1.0);
        decoded.material.metallic =
            clamp(orm_sample.b * decoded.material.metallic, 0.0, 1.0);
    }

    float occlusion_value = 1.0;
    if (material_texture_present(presence_mask, k_material_texture_presence_occlusion)) {
        occlusion_value = SampleTexture2D(material_record.textures0_u32.w,
                                          sampler_slot,
                                          in_uv).r;
    } else if (material_texture_present(presence_mask, k_material_texture_presence_metal_rough)) {
        occlusion_value = orm_sample.r;
    }
    decoded.material.occlusion = mix(1.0, occlusion_value, occlusion_strength);

    if (material_texture_present(presence_mask, k_material_texture_presence_normal) &&
        has_valid_tangent_basis()) {
        vec3 tangent_normal =
            SampleTexture2D(material_record.textures0_u32.y,
                            sampler_slot,
                            in_uv).xyz * 2.0 - 1.0;
        tangent_normal.xy *= decoded.material.normal_scale;
        tangent_normal = normalize(tangent_normal);

        vec3 tangent_world = normalize(in_tangent_world);
        vec3 bitangent_world = normalize(in_bitangent_world);
        mat3 tbn = mat3(tangent_world, bitangent_world, normalize(normal_world_));
        decoded.material.normal_world = normalize(tbn * tangent_normal);
    }

    vec3 emissive_sample = vec3(1.0);
    if (material_texture_present(presence_mask, k_material_texture_presence_emissive)) {
        emissive_sample =
            SampleTexture2D(material_record.textures1_u32.x,
                            sampler_slot,
                            in_uv).rgb;
    }
    decoded.material.emissive = emissive_sample * emissive_factor.rgb * max(extras.y, 0.0);
    decoded.alpha_test_enabled =
        material_alpha_mode(material_record) == k_appearance_alpha_mode_masked;
    decoded.alpha_cutoff = clamp(extras.x, 0.0, 1.0);
    decoded.unlit = material_shading_model(material_record) == k_appearance_shading_model_unlit;
    return decoded;
}

DecodedGeometryMaterial decode_geometry_material(vec3 normal_world_) {
    if (!has_material_record()) {
        return decode_fallback_geometry_material(normal_world_);
    }

    MaterialGpuRecord material_record = material_records[in_appearance_record_index];
    return decode_appearance_geometry_material(normal_world_, material_record);
}

vec3 evaluate_ibl(MaterialSample material_,
                  vec3 view_dir_) {
    float ibl_intensity = max(ibl_params.ibl_tint_intensity.w, 0.0);
    if (ibl_intensity <= 1e-6) {
        return vec3(0.0);
    }

    vec3 tint = ibl_params.ibl_tint_intensity.rgb;
    vec3 diffuse_irradiance = evaluate_sh9_irradiance(material_.normal_world);

    vec3 reflection_dir = rotate_environment_direction(reflect(-view_dir_, material_.normal_world));
    float max_specular_lod = max(ibl_params.ibl_rotation_max_lod_flags.z, 0.0);
    vec3 prefiltered_specular =
        SampleTextureCubeLod(ibl_params.texture_sampler_slots.x,
                             ibl_params.texture_sampler_slots.w,
                             reflection_dir,
                             material_.roughness * max_specular_lod).rgb;
    float n_dot_v = max(dot(material_.normal_world, view_dir_), 0.0);
    vec2 brdf =
        SampleTexture2D(ibl_params.texture_sampler_slots.y,
                        ibl_params.texture_sampler_slots.w,
                        vec2(n_dot_v, material_.roughness)).rg;

    return EvaluatePbrIblFromTerms(material_,
                                   view_dir_,
                                   diffuse_irradiance,
                                   prefiltered_specular,
                                   brdf) * tint * ibl_intensity;
}

vec3 evaluate_light(LightRecord3D light_record_,
                    vec3 world_position_,
                    MaterialSample material_,
                    vec3 view_dir_,
                    float depth_distance_) {
    vec3 light_color = light_record_.color_intensity.rgb;
    float intensity = max(light_record_.color_intensity.a, 0.0);
    float radius = max(light_record_.position_radius.w, 1e-4);
    uint light_kind = light_record_.light_type;

    vec3 light_dir = vec3(0.0, 0.0, 1.0);
    float attenuation = 1.0;

    if (light_kind == k_light_kind_directional || light_kind == k_light_kind_global) {
        light_dir = normalize(-light_record_.direction_cone_outer.xyz);
    } else {
        vec3 to_light = light_record_.position_radius.xyz - world_position_;
        float distance_to_light = length(to_light);
        if (distance_to_light <= 1e-6) {
            return vec3(0.0);
        }

        light_dir = to_light / distance_to_light;
        float distance_ratio = vr_saturate(distance_to_light / radius);
        attenuation = vr_saturate(1.0 - pow(distance_ratio, max(light_record_.falloff_exponent, 1.0)));

        if (light_kind == k_light_kind_spot) {
            vec3 spot_dir = normalize(light_record_.direction_cone_outer.xyz);
            float cone_outer = clamp(light_record_.direction_cone_outer.w, -1.0, 1.0);
            float cone_inner = clamp(light_record_.cone_source.x, -1.0, 1.0);
            float cone_cos = dot(-light_dir, spot_dir);
            float cone_denom = max(1e-4, cone_inner - cone_outer);
            float cone_t = vr_saturate((cone_cos - cone_outer) / cone_denom);
            attenuation *= cone_t;
        }
    }

    float shadow_factor = evaluate_shadow_factor(light_record_,
                                                 world_position_,
                                                 material_.normal_world,
                                                 light_dir,
                                                 depth_distance_);
    vec3 light_radiance = light_color * (intensity * attenuation * shadow_factor);
    return EvaluatePbrDirect(material_,
                             view_dir_,
                             light_dir,
                             light_radiance);
}

void main() {
    vec3 normal_world = normalize(in_normal_world);

    DecodedGeometryMaterial material_state = decode_geometry_material(normal_world);
    MaterialSample material = material_state.material;
    if (material_state.alpha_test_enabled && material.alpha < material_state.alpha_cutoff) {
        discard;
    }

    if (material_state.unlit) {
        out_color = vec4(material.base_color + material.emissive, material.alpha);
        return;
    }

    vec3 camera_position = lighting_params.camera_position_light_count.xyz;
    vec3 camera_forward = normalize(lighting_params.camera_forward_max_lights.xyz);
    vec3 to_fragment = in_world_position - camera_position;
    float depth_distance = max(0.0, dot(to_fragment, camera_forward));
    vec3 view_dir = normalize(camera_position - in_world_position);

    vec3 direct_accum = vec3(0.0);

    uint cluster_index = compute_cluster_index(gl_FragCoord.xy, depth_distance);
    uint header_count = uint(cluster_headers.length());
    if (cluster_index < header_count) {
        ClusterHeaderPacked header = cluster_headers[cluster_index];
        uint light_count = header.packed_count_flags & 0xFFFFu;
        uint light_offset = header.offset;
        uint max_fragment_lights = uint(max(1.0, lighting_params.camera_forward_max_lights.w));
        uint max_light_records = uint(light_records.length());
        uint max_cluster_indices = uint(cluster_light_indices.length());

        uint processed = 0u;
        for (uint i = 0u; i < light_count && processed < max_fragment_lights; ++i) {
            uint cluster_light_index = light_offset + i;
            if (cluster_light_index >= max_cluster_indices) {
                break;
            }
            uint light_index = cluster_light_indices[cluster_light_index];
            if (light_index >= max_light_records) {
                continue;
            }

            LightRecord3D light_record = light_records[light_index];
            direct_accum += evaluate_light(light_record,
                                           in_world_position,
                                           material,
                                           view_dir,
                                           depth_distance);
            ++processed;
        }
    } else {
        vec3 fallback_light_dir = normalize(pc.light_direction_intensity.xyz);
        float fallback_intensity = max(pc.light_direction_intensity.w, 0.0);
        direct_accum = EvaluatePbrDirect(material,
                                         view_dir,
                                         -fallback_light_dir,
                                         vec3(fallback_intensity));
    }

    vec3 ibl_accum = evaluate_ibl(material, view_dir);
    vec3 lit_color = direct_accum + ibl_accum * material.occlusion + material.emissive;
    out_color = vec4(lit_color, material.alpha);
}
