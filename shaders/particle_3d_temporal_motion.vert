#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec2 in_size;
layout(location = 2) in float in_rotation_radians;
layout(location = 3) in float in_stretch_factor;
layout(location = 4) in vec4 in_color;
layout(location = 5) in vec3 in_velocity;
layout(location = 6) in uint in_texture_slot;
layout(location = 7) in uint in_sampler_slot;

layout(push_constant) uniform Particle3DTemporalMotionPushConstants {
    mat4 view_projection;
    vec4 camera_right;
    vec4 camera_up;
    float delta_time_s;
    float responsive_velocity_begin_pixels;
    float responsive_velocity_end_pixels;
    uint params;
    uint target_width;
    uint target_height;
    uint reserved0;
    uint reserved1;
} pc;

layout(location = 0) flat out vec2 out_local_motion_uv;
layout(location = 1) flat out float out_confidence_seed;

vec2 corner01_for_vertex(uint vertex_index) {
    switch (vertex_index) {
    case 0u: return vec2(0.0, 0.0);
    case 1u: return vec2(1.0, 0.0);
    case 2u: return vec2(1.0, 1.0);
    case 3u: return vec2(1.0, 1.0);
    case 4u: return vec2(0.0, 1.0);
    default: return vec2(0.0, 0.0);
    }
}

vec3 safe_normalize(vec3 value, vec3 fallback_value) {
    float len_sq = dot(value, value);
    if (len_sq <= 1e-10) {
        return fallback_value;
    }
    return value * inversesqrt(len_sq);
}

void main() {
    const uint render_mode = pc.params & 0xFFu;
    const uint facing_mode = (pc.params >> 8u) & 0xFFu;

    const vec2 corner01 = corner01_for_vertex(uint(gl_VertexIndex));
    const vec2 corner_signed = corner01 - vec2(0.5, 0.5);

    vec3 basis_right = safe_normalize(pc.camera_right.xyz, vec3(1.0, 0.0, 0.0));
    vec3 basis_up = safe_normalize(pc.camera_up.xyz, vec3(0.0, 1.0, 0.0));
    vec3 view_normal = safe_normalize(cross(basis_up, basis_right),
                                      vec3(0.0, 0.0, -1.0));

    if (render_mode == 0u || render_mode == 2u || render_mode == 3u) {
        if (facing_mode == 1u) {
            vec3 velocity_plane = in_velocity - view_normal * dot(in_velocity, view_normal);
            basis_right = safe_normalize(velocity_plane, basis_right);
            basis_up = safe_normalize(cross(view_normal, basis_right), basis_up);
        }
    }

    const float stretch_factor = max(in_stretch_factor, 1.0);
    const vec2 scaled_size = vec2(in_size.x * stretch_factor, in_size.y);
    vec2 local = corner_signed * scaled_size;

    const float s = sin(in_rotation_radians);
    const float c = cos(in_rotation_radians);
    local = vec2(c * local.x - s * local.y,
                 s * local.x + c * local.y);

    const vec3 world = in_position +
                       basis_right * local.x +
                       basis_up * local.y;
    gl_Position = pc.view_projection * vec4(world, 1.0);

    const vec4 current_center_clip = pc.view_projection * vec4(in_position, 1.0);
    const vec4 previous_center_clip = pc.view_projection *
                                      vec4(in_position - in_velocity * max(pc.delta_time_s, 0.0),
                                           1.0);
    const float current_w = abs(current_center_clip.w);
    const float previous_w = abs(previous_center_clip.w);
    if (current_w <= 1.0e-6 || previous_w <= 1.0e-6) {
        out_local_motion_uv = vec2(0.0);
        out_confidence_seed = 0.0;
        return;
    }

    const vec2 current_uv = current_center_clip.xy / current_center_clip.w * 0.5 + 0.5;
    const vec2 previous_uv = previous_center_clip.xy / previous_center_clip.w * 0.5 + 0.5;
    const bool previous_in_bounds =
        previous_uv.x >= 0.0 && previous_uv.x <= 1.0 &&
        previous_uv.y >= 0.0 && previous_uv.y <= 1.0;
    out_local_motion_uv = current_uv - previous_uv;

    const vec2 target_size =
        vec2(float(max(pc.target_width, 1u)),
             float(max(pc.target_height, 1u)));
    const float motion_pixels = length(out_local_motion_uv * target_size);
    const float responsive =
        smoothstep(pc.responsive_velocity_begin_pixels,
                   pc.responsive_velocity_end_pixels,
                   motion_pixels);
    const float alpha_confidence = mix(0.35, 1.0, clamp(in_color.a, 0.0, 1.0));
    out_confidence_seed = previous_in_bounds
        ? clamp((1.0 - responsive) * alpha_confidence, 0.0, 1.0)
        : 0.0;
}
