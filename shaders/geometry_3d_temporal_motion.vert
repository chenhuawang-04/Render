#version 460

layout(location = 0) in vec3 in_position;
layout(location = 12) in vec3 in_normal;
layout(location = 17) in uvec4 in_joint_indices;
layout(location = 18) in vec4 in_joint_weights;

layout(location = 1) in vec4 in_current_world_row0;
layout(location = 2) in vec4 in_current_world_row1;
layout(location = 3) in vec4 in_current_world_row2;
layout(location = 4) in vec4 in_current_world_row3;
layout(location = 13) in vec4 in_current_deform_param0;
layout(location = 14) in vec4 in_current_deform_param1;

layout(location = 5) in vec4 in_previous_world_row0;
layout(location = 6) in vec4 in_previous_world_row1;
layout(location = 7) in vec4 in_previous_world_row2;
layout(location = 8) in vec4 in_previous_world_row3;
layout(location = 15) in vec4 in_previous_deform_param0;
layout(location = 16) in vec4 in_previous_deform_param1;
layout(location = 9) in float in_confidence_seed;
layout(location = 10) in uint in_component_index;
layout(location = 11) in uint in_previous_skeletal_component_index;

layout(push_constant) uniform Geometry3DTemporalMotionPushConstants {
    mat4 current_view_projection;
    mat4 current_clip_to_previous_clip;
    float current_jitter_uv_x;
    float current_jitter_uv_y;
    float previous_jitter_uv_x;
    float previous_jitter_uv_y;
    uint flags;
    uint reserved0;
    uint reserved1;
    uint reserved2;
} pc;

struct SkeletalComponentGpu {
    uint matrix_offset;
    uint joint_count;
    uint flags;
    uint reserved0;
};

layout(std430, set = 0, binding = 0) readonly buffer CurrentSkeletalComponentBuffer {
    SkeletalComponentGpu current_skeletal_components[];
};

layout(std430, set = 0, binding = 1) readonly buffer CurrentSkeletalMatrixBuffer {
    mat4 current_skeletal_matrices[];
};

layout(std430, set = 0, binding = 2) readonly buffer PreviousSkeletalComponentBuffer {
    SkeletalComponentGpu previous_skeletal_components[];
};

layout(std430, set = 0, binding = 3) readonly buffer PreviousSkeletalMatrixBuffer {
    mat4 previous_skeletal_matrices[];
};

layout(location = 0) out vec4 out_current_clip;
layout(location = 1) out vec4 out_previous_clip;
layout(location = 2) flat out float out_confidence_seed;

mat4 assemble_matrix(vec4 row0, vec4 row1, vec4 row2, vec4 row3) {
    return mat4(row0, row1, row2, row3);
}

uint current_skeletal_component_count() {
    return uint(current_skeletal_components.length());
}

uint current_skeletal_matrix_count() {
    return uint(current_skeletal_matrices.length());
}

uint previous_skeletal_component_count() {
    return uint(previous_skeletal_components.length());
}

uint previous_skeletal_matrix_count() {
    return uint(previous_skeletal_matrices.length());
}

bool current_skeletal_enabled() {
    return in_component_index < current_skeletal_component_count() &&
           (current_skeletal_components[in_component_index].flags & 1u) != 0u;
}

bool previous_skeletal_enabled() {
    return in_previous_skeletal_component_index <
               previous_skeletal_component_count() &&
           (previous_skeletal_components[in_previous_skeletal_component_index]
                .flags &
            1u) != 0u;
}

mat4 fetch_current_joint_matrix(uint joint_index) {
    if (!current_skeletal_enabled()) {
        return mat4(1.0);
    }

    SkeletalComponentGpu component = current_skeletal_components[in_component_index];
    uint matrix_count = current_skeletal_matrix_count();
    if (joint_index >= component.joint_count ||
        component.matrix_offset + joint_index >= matrix_count) {
        return mat4(1.0);
    }
    return current_skeletal_matrices[component.matrix_offset + joint_index];
}

mat4 fetch_previous_joint_matrix(uint joint_index) {
    if (!previous_skeletal_enabled()) {
        return mat4(1.0);
    }

    SkeletalComponentGpu component =
        previous_skeletal_components[in_previous_skeletal_component_index];
    uint matrix_count = previous_skeletal_matrix_count();
    if (joint_index >= component.joint_count ||
        component.matrix_offset + joint_index >= matrix_count) {
        return mat4(1.0);
    }
    return previous_skeletal_matrices[component.matrix_offset + joint_index];
}

vec3 apply_vertex_deform(vec3 local_position,
                         vec3 local_normal,
                         vec4 deform_param0,
                         vec4 deform_param1) {
    if (all(equal(deform_param0, vec4(0.0))) &&
        all(equal(deform_param1, vec4(0.0)))) {
        return local_position;
    }

    vec3 axis = vec3(deform_param0.y, deform_param0.z, deform_param0.w);
    float axis_length = length(axis);
    if (axis_length <= 1e-6) {
        axis = local_normal;
        axis_length = length(axis);
    }
    axis = (axis_length > 1e-6) ? (axis / axis_length) : vec3(0.0, 0.0, 1.0);

    float amplitude = deform_param0.x;
    float frequency = max(abs(deform_param1.y), 1e-4);
    float phase = deform_param1.x;
    float bias = deform_param1.z;
    float normal_offset = deform_param1.w;
    float wave = sin(dot(local_position, axis) * frequency + phase) * amplitude + bias;
    return local_position + axis * wave + local_normal * normal_offset;
}

vec3 apply_current_skinning_position(vec3 local_position) {
    if (!current_skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_position;
    }

    vec4 skinned = vec4(0.0);
    skinned += fetch_current_joint_matrix(in_joint_indices.x) * vec4(local_position, 1.0) *
               in_joint_weights.x;
    skinned += fetch_current_joint_matrix(in_joint_indices.y) * vec4(local_position, 1.0) *
               in_joint_weights.y;
    skinned += fetch_current_joint_matrix(in_joint_indices.z) * vec4(local_position, 1.0) *
               in_joint_weights.z;
    skinned += fetch_current_joint_matrix(in_joint_indices.w) * vec4(local_position, 1.0) *
               in_joint_weights.w;
    return skinned.xyz;
}

vec3 apply_previous_skinning_position(vec3 local_position) {
    if (!previous_skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_position;
    }

    vec4 skinned = vec4(0.0);
    skinned += fetch_previous_joint_matrix(in_joint_indices.x) * vec4(local_position, 1.0) *
               in_joint_weights.x;
    skinned += fetch_previous_joint_matrix(in_joint_indices.y) * vec4(local_position, 1.0) *
               in_joint_weights.y;
    skinned += fetch_previous_joint_matrix(in_joint_indices.z) * vec4(local_position, 1.0) *
               in_joint_weights.z;
    skinned += fetch_previous_joint_matrix(in_joint_indices.w) * vec4(local_position, 1.0) *
               in_joint_weights.w;
    return skinned.xyz;
}

vec3 apply_current_skinning_normal(vec3 local_normal) {
    if (!current_skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_normal;
    }

    vec3 skinned = vec3(0.0);
    skinned += mat3(fetch_current_joint_matrix(in_joint_indices.x)) * local_normal *
               in_joint_weights.x;
    skinned += mat3(fetch_current_joint_matrix(in_joint_indices.y)) * local_normal *
               in_joint_weights.y;
    skinned += mat3(fetch_current_joint_matrix(in_joint_indices.z)) * local_normal *
               in_joint_weights.z;
    skinned += mat3(fetch_current_joint_matrix(in_joint_indices.w)) * local_normal *
               in_joint_weights.w;
    float skinned_length = length(skinned);
    return (skinned_length > 1e-6) ? (skinned / skinned_length) : local_normal;
}

vec3 apply_previous_skinning_normal(vec3 local_normal) {
    if (!previous_skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_normal;
    }

    vec3 skinned = vec3(0.0);
    skinned += mat3(fetch_previous_joint_matrix(in_joint_indices.x)) * local_normal *
               in_joint_weights.x;
    skinned += mat3(fetch_previous_joint_matrix(in_joint_indices.y)) * local_normal *
               in_joint_weights.y;
    skinned += mat3(fetch_previous_joint_matrix(in_joint_indices.z)) * local_normal *
               in_joint_weights.z;
    skinned += mat3(fetch_previous_joint_matrix(in_joint_indices.w)) * local_normal *
               in_joint_weights.w;
    float skinned_length = length(skinned);
    return (skinned_length > 1e-6) ? (skinned / skinned_length) : local_normal;
}

void main() {
    const mat4 current_world = assemble_matrix(in_current_world_row0,
                                               in_current_world_row1,
                                               in_current_world_row2,
                                               in_current_world_row3);
    const mat4 previous_world = assemble_matrix(in_previous_world_row0,
                                                in_previous_world_row1,
                                                in_previous_world_row2,
                                                in_previous_world_row3);

    const vec3 current_local_normal = apply_current_skinning_normal(in_normal);
    const vec3 previous_local_normal = apply_previous_skinning_normal(in_normal);
    const vec3 current_local_position = apply_vertex_deform(
        apply_current_skinning_position(in_position),
        current_local_normal,
        in_current_deform_param0,
        in_current_deform_param1);
    const vec3 previous_local_position = apply_vertex_deform(
        apply_previous_skinning_position(in_position),
        previous_local_normal,
        in_previous_deform_param0,
        in_previous_deform_param1);
    const vec4 current_unjittered_clip =
        pc.current_view_projection * current_world * vec4(current_local_position, 1.0);
    const vec4 previous_unjittered_clip =
        pc.current_clip_to_previous_clip *
        (pc.current_view_projection * previous_world *
         vec4(previous_local_position, 1.0));

    const vec2 current_jitter_ndc =
        vec2(pc.current_jitter_uv_x, pc.current_jitter_uv_y) * 2.0;
    const vec2 previous_jitter_ndc =
        vec2(pc.previous_jitter_uv_x, pc.previous_jitter_uv_y) * 2.0;

    out_current_clip = current_unjittered_clip;
    out_current_clip.xy += out_current_clip.w * current_jitter_ndc;
    out_previous_clip = previous_unjittered_clip;
    out_previous_clip.xy += out_previous_clip.w * previous_jitter_ndc;
    out_confidence_seed = in_confidence_seed;
    gl_Position = out_current_clip;
}
