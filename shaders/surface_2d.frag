#version 460
#extension GL_GOOGLE_include_directive : require

#include "vr/common/math.glsl"
#include "vr/render/bindless.glsl"

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 2) flat in uint in_params;
layout(location = 3) flat in uint in_image_slot;
layout(location = 4) flat in uint in_sampler_slot;
layout(location = 5) in vec2 in_world_position;

layout(push_constant) uniform Surface2DPushConstants {
    vec4 viewport;
    uint params;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

struct LightRecord2D {
    vec4 position_radius_intensity;
    vec4 color_falloff;
    vec4 direction_cone;
    float source_height;
    uint light_type;
    uint channel_mask;
    uint flags;
    uint shadow_view_begin;
    uint shadow_meta;
    uint shadow_namespace_id;
    uint reserved0;
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

layout(set = 2, binding = 0, std430) readonly buffer LightRecordBuffer {
    LightRecord2D light_records[];
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

layout(set = 2, binding = 4) uniform sampler2DArray shadow_atlas_texture;

layout(set = 2, binding = 5, std140) uniform LightingParamsBuffer {
    vec4 world_to_ndc;
    vec4 light_counts;
    uvec4 tile_reverse;
    vec4 framebuffer_size;
} lighting_params;

layout(location = 0) out vec4 out_color;

const uint k_light_kind_global = 0u;
const uint k_light_kind_directional = 1u;
const uint k_light_kind_point = 2u;
const uint k_light_kind_spot = 3u;

const uint k_invalid_shadow_view_begin = 0xFFFFFFFFu;

uint unpack_shadow_view_count(uint shadow_meta_) {
    return shadow_meta_ & 0xFFFFu;
}

uint compute_cluster_index_2d(vec2 frag_coord_) {
    uint tile_count_x = max(lighting_params.tile_reverse.x, 1u);
    uint tile_count_y = max(lighting_params.tile_reverse.y, 1u);
    float framebuffer_width = max(pc.viewport.x, 1.0);
    float framebuffer_height = max(pc.viewport.y, 1.0);

    float norm_x = vr_saturate(frag_coord_.x / framebuffer_width);
    float norm_y = vr_saturate(frag_coord_.y / framebuffer_height);
    uint x_index = min(uint(norm_x * float(tile_count_x)), tile_count_x - 1u);
    uint y_index = min(uint(norm_y * float(tile_count_y)), tile_count_y - 1u);
    return y_index * tile_count_x + x_index;
}

float sample_shadow_view_pcf(uint view_index_, vec3 world_position_) {
    uint max_shadow_views = uint(max(0.0, lighting_params.light_counts.z));
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
    ivec3 atlas_size = textureSize(shadow_atlas_texture, 0);
    vec2 atlas_size_f = vec2(max(atlas_size.x, 1), max(atlas_size.y, 1));
    vec2 rect_origin = vec2(view_record.atlas_rect.x, view_record.atlas_rect.y);
    vec2 rect_extent = vec2(max(view_record.atlas_rect.z, 1u), max(view_record.atlas_rect.w, 1u));
    vec2 atlas_uv = (rect_origin + uv * rect_extent) / atlas_size_f;
    vec2 rect_min_uv = rect_origin / atlas_size_f;
    vec2 rect_max_uv = (rect_origin + rect_extent - vec2(1.0, 1.0)) / atlas_size_f;
    float layer = float(view_record.layer_view_cascade_flags.x);

    vec2 texel_size = 1.0 / atlas_size_f;
    vec2 border_guard = texel_size * 1.5;
    if (atlas_uv.x <= rect_min_uv.x + border_guard.x ||
        atlas_uv.y <= rect_min_uv.y + border_guard.y ||
        atlas_uv.x >= rect_max_uv.x - border_guard.x ||
        atlas_uv.y >= rect_max_uv.y - border_guard.y) {
        return 1.0;
    }

    float depth_bias = view_record.split_bias.z;
    float normal_bias = view_record.split_bias.w;
    float receiver_bias = max(0.0, view_record.slope_texel.y * 0.35);
    float combined_bias = max(0.0, depth_bias + normal_bias + receiver_bias);
    bool reverse_z = (view_record.layer_view_cascade_flags.w & (1u << 1u)) != 0u;

    float shadow_sum = 0.0;
    float sample_count = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 tap_uv = atlas_uv + vec2(float(x), float(y)) * texel_size;
            if (tap_uv.x < rect_min_uv.x || tap_uv.y < rect_min_uv.y ||
                tap_uv.x > rect_max_uv.x || tap_uv.y > rect_max_uv.y) {
                shadow_sum += 1.0;
                sample_count += 1.0;
                continue;
            }
            float stored_depth = texture(shadow_atlas_texture, vec3(tap_uv, layer)).r;
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

float evaluate_shadow_factor(LightRecord2D light_record_, vec3 world_position_) {
    uint flags = light_record_.flags;
    bool cast_shadow = (flags & 0x1u) != 0u;
    uint shadow_view_begin = light_record_.shadow_view_begin;
    uint shadow_view_count = unpack_shadow_view_count(light_record_.shadow_meta);
    if (!cast_shadow || shadow_view_begin == k_invalid_shadow_view_begin || shadow_view_count == 0u) {
        return 1.0;
    }

    return sample_shadow_view_pcf(shadow_view_begin, world_position_);
}

vec3 evaluate_light_contribution(LightRecord2D light_record_,
                                 vec3 world_position_,
                                 vec3 surface_normal_) {
    vec2 light_position_xy = light_record_.position_radius_intensity.xy;
    float radius = max(light_record_.position_radius_intensity.z, 1e-4);
    float intensity = max(light_record_.position_radius_intensity.w, 0.0);
    vec3 light_color = light_record_.color_falloff.rgb;
    float falloff_exponent = max(light_record_.color_falloff.w, 1.0);
    vec2 light_direction_xy = light_record_.direction_cone.xy;
    float cone_cos_outer = clamp(light_record_.direction_cone.z, -1.0, 1.0);
    float cone_cos_inner = clamp(light_record_.direction_cone.w, -1.0, 1.0);
    float source_height = max(abs(light_record_.source_height), 1e-3);
    uint light_kind = light_record_.light_type;
    uint light_flags = light_record_.flags;

    vec3 to_light = vec3(light_position_xy - world_position_.xy, source_height);
    vec3 light_dir = vec3(0.0, 0.0, 1.0);
    float attenuation = 1.0;

    if (light_kind == k_light_kind_global || light_kind == k_light_kind_directional) {
        vec2 dir_xy = vec2(-light_direction_xy.x, -light_direction_xy.y);
        float dir_len = length(dir_xy);
        if (dir_len <= 1e-6) {
            dir_xy = vec2(0.0, -1.0);
        } else {
            dir_xy /= dir_len;
        }
        light_dir = normalize(vec3(dir_xy, 1.0));
    } else {
        float light_distance = length(to_light);
        if (light_distance <= 1e-5) {
            return vec3(0.0);
        }
        light_dir = to_light / light_distance;
        float distance_ratio = vr_saturate(light_distance / radius);
        attenuation = vr_saturate(1.0 - pow(distance_ratio, falloff_exponent));

        if (light_kind == k_light_kind_spot) {
            vec2 spot_dir = light_direction_xy;
            float spot_len = length(spot_dir);
            if (spot_len <= 1e-6) {
                spot_dir = vec2(0.0, -1.0);
            } else {
                spot_dir /= spot_len;
            }

            vec2 to_fragment_xy = world_position_.xy - light_position_xy;
            float to_fragment_len = length(to_fragment_xy);
            if (to_fragment_len <= 1e-6) {
                return vec3(0.0);
            }
            to_fragment_xy /= to_fragment_len;
            float cone_cos = dot(to_fragment_xy, spot_dir);
            float cone_denom = max(1e-4, cone_cos_inner - cone_cos_outer);
            float cone_factor = vr_saturate((cone_cos - cone_cos_outer) / cone_denom);
            attenuation *= cone_factor;
        }
    }

    float n_dot_l = 1.0;
    bool affect_normals_only = (light_flags & (1u << 1u)) != 0u;
    if (affect_normals_only) {
        n_dot_l = max(dot(surface_normal_, light_dir), 0.0);
    }
    float shadow_factor = evaluate_shadow_factor(light_record_, world_position_);
    float lit = n_dot_l * intensity * attenuation * shadow_factor;
    return light_color * lit;
}

void main() {
    vec4 color = SampleTexture2D(in_image_slot, in_sampler_slot, in_uv) * in_color;
    const bool premultiplied = (in_params & 0x10u) != 0u;
    if (color.a <= 1e-5) {
        discard;
    }

    vec3 final_rgb = color.rgb;
    uint light_count = uint(max(0.0, lighting_params.light_counts.x));
    if (light_count > 0u) {
        vec3 world_position = vec3(in_world_position, 0.0);
        vec3 surface_normal = vec3(0.0, 0.0, 1.0);
        vec3 light_accum = vec3(0.0);
        uint processed = 0u;
        uint max_fragment_lights = uint(max(1.0, lighting_params.light_counts.y));
        uint max_light_records = uint(light_records.length());

        uint direct_light_limit = min(light_count, min(max_fragment_lights, max_light_records));
        if (direct_light_limit <= 32u) {
            for (uint i = 0u; i < direct_light_limit; ++i) {
                LightRecord2D light_record = light_records[i];
                light_accum += evaluate_light_contribution(light_record, world_position, surface_normal);
                ++processed;
            }
        } else {
            uint cluster_index = compute_cluster_index_2d(gl_FragCoord.xy);
            uint header_count = uint(cluster_headers.length());
            if (cluster_index < header_count) {
                ClusterHeaderPacked header = cluster_headers[cluster_index];
                uint cluster_light_count = header.packed_count_flags & 0xFFFFu;
                uint cluster_light_offset = header.offset;
                uint max_cluster_indices = uint(cluster_light_indices.length());

                for (uint i = 0u; i < cluster_light_count && processed < max_fragment_lights; ++i) {
                    uint cluster_light_index = cluster_light_offset + i;
                    if (cluster_light_index >= max_cluster_indices) {
                        break;
                    }
                    uint light_index = cluster_light_indices[cluster_light_index];
                    if (light_index >= max_light_records || light_index >= light_count) {
                        continue;
                    }

                    LightRecord2D light_record = light_records[light_index];
                    light_accum += evaluate_light_contribution(light_record, world_position, surface_normal);
                    ++processed;
                }
            }

            if (processed == 0u) {
                for (uint i = 0u; i < direct_light_limit; ++i) {
                    LightRecord2D light_record = light_records[i];
                    light_accum += evaluate_light_contribution(light_record, world_position, surface_normal);
                }
            }
        }

        float ambient = vr_saturate(lighting_params.light_counts.w);
        final_rgb *= (vec3(ambient) + light_accum);
    }

    vec4 output_color = vec4(final_rgb, color.a);
    if (premultiplied) {
        output_color.rgb *= output_color.a;
    }
    out_color = output_color;
}
