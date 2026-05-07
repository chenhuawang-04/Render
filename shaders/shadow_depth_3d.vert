#version 460

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 12) in vec3 in_morph0_position_delta;
layout(location = 13) in vec3 in_morph0_normal_delta;
layout(location = 14) in vec3 in_morph1_position_delta;
layout(location = 15) in vec3 in_morph1_normal_delta;
layout(location = 17) in uvec4 in_joint_indices;
layout(location = 18) in vec4 in_joint_weights;

layout(push_constant) uniform ShadowPushConstants {
    mat4 view_projection;
    float world_affine[12];
    vec4 morph_weights;
} pc;

struct SkeletalComponentGpu {
    uint matrix_offset;
    uint joint_count;
    uint flags;
    uint reserved0;
};

layout(std430, set = 0, binding = 0) readonly buffer SkeletalComponentBuffer {
    SkeletalComponentGpu skeletal_components[];
};

layout(std430, set = 0, binding = 1) readonly buffer SkeletalMatrixBuffer {
    mat4 skeletal_matrices[];
};

struct ShadowDeformComponentGpu {
    vec4 deform_param0;
    vec4 deform_param1;
};

layout(std430, set = 0, binding = 2) readonly buffer ShadowDeformComponentBuffer {
    ShadowDeformComponentGpu shadow_deform_components[];
};

uint skeletal_component_count() {
    return uint(skeletal_components.length());
}

uint skeletal_matrix_count() {
    return uint(skeletal_matrices.length());
}

uint shadow_deform_component_count() {
    return uint(shadow_deform_components.length());
}

vec3 apply_morph_position(vec3 local_position) {
    return local_position +
           in_morph0_position_delta * pc.morph_weights.x +
           in_morph1_position_delta * pc.morph_weights.y;
}

vec3 apply_morph_normal(vec3 local_normal) {
    vec3 morphed_normal = local_normal +
                          in_morph0_normal_delta * pc.morph_weights.x +
                          in_morph1_normal_delta * pc.morph_weights.y;
    float normal_length = length(morphed_normal);
    return (normal_length > 1e-6) ? (morphed_normal / normal_length) : local_normal;
}

uint skeletal_component_index() {
    if (!(pc.morph_weights.z >= 0.0)) {
        return 0xFFFFFFFFu;
    }
    return uint(pc.morph_weights.z + 0.5);
}

bool has_valid_skeletal_component() {
    return skeletal_component_index() < skeletal_component_count();
}

bool skeletal_enabled() {
    uint component_index = skeletal_component_index();
    return component_index < skeletal_component_count() &&
           (skeletal_components[component_index].flags & 1u) != 0u;
}

mat4 fetch_joint_matrix(uint joint_index) {
    if (!has_valid_skeletal_component()) {
        return mat4(1.0);
    }
    SkeletalComponentGpu component = skeletal_components[skeletal_component_index()];
    uint matrix_count = skeletal_matrix_count();
    if ((component.flags & 1u) == 0u ||
        joint_index >= component.joint_count ||
        component.matrix_offset >= matrix_count) {
        return mat4(1.0);
    }
    uint matrix_index = component.matrix_offset + joint_index;
    if (matrix_index < component.matrix_offset || matrix_index >= matrix_count) {
        return mat4(1.0);
    }
    return skeletal_matrices[matrix_index];
}

vec3 apply_skinning_position(vec3 local_position) {
    if (!skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_position;
    }

    vec4 skinned = vec4(0.0);
    skinned += fetch_joint_matrix(in_joint_indices.x) * vec4(local_position, 1.0) * in_joint_weights.x;
    skinned += fetch_joint_matrix(in_joint_indices.y) * vec4(local_position, 1.0) * in_joint_weights.y;
    skinned += fetch_joint_matrix(in_joint_indices.z) * vec4(local_position, 1.0) * in_joint_weights.z;
    skinned += fetch_joint_matrix(in_joint_indices.w) * vec4(local_position, 1.0) * in_joint_weights.w;
    return skinned.xyz;
}

vec3 apply_skinning_normal(vec3 local_normal) {
    if (!skeletal_enabled() || dot(in_joint_weights, vec4(1.0)) <= 1e-6) {
        return local_normal;
    }

    vec3 skinned = vec3(0.0);
    skinned += mat3(fetch_joint_matrix(in_joint_indices.x)) * local_normal * in_joint_weights.x;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.y)) * local_normal * in_joint_weights.y;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.z)) * local_normal * in_joint_weights.z;
    skinned += mat3(fetch_joint_matrix(in_joint_indices.w)) * local_normal * in_joint_weights.w;
    float skinned_length = length(skinned);
    return (skinned_length > 1e-6) ? (skinned / skinned_length) : local_normal;
}

ShadowDeformComponentGpu fetch_deform_component() {
    uint component_index = skeletal_component_index();
    if (component_index >= shadow_deform_component_count()) {
        ShadowDeformComponentGpu identity_component;
        identity_component.deform_param0 = vec4(0.0);
        identity_component.deform_param1 = vec4(0.0);
        return identity_component;
    }
    return shadow_deform_components[component_index];
}

vec3 apply_vertex_deform(vec3 local_position, vec3 local_normal) {
    ShadowDeformComponentGpu deform = fetch_deform_component();
    if (all(equal(deform.deform_param0, vec4(0.0))) &&
        all(equal(deform.deform_param1, vec4(0.0)))) {
        return local_position;
    }

    vec3 axis = vec3(deform.deform_param0.y, deform.deform_param0.z, deform.deform_param0.w);
    float axis_length = length(axis);
    if (axis_length <= 1e-6) {
        axis = local_normal;
        axis_length = length(axis);
    }
    axis = (axis_length > 1e-6) ? (axis / axis_length) : vec3(0.0, 0.0, 1.0);

    float amplitude = deform.deform_param0.x;
    float frequency = max(abs(deform.deform_param1.y), 1e-4);
    float phase = deform.deform_param1.x;
    float bias = deform.deform_param1.z;
    float normal_offset = deform.deform_param1.w;

    float wave = sin(dot(local_position, axis) * frequency + phase) * amplitude + bias;
    return local_position + axis * wave + local_normal * normal_offset;
}

vec3 apply_affine_world(vec3 local_position) {
    return vec3(
        pc.world_affine[0] * local_position.x +
            pc.world_affine[3] * local_position.y +
            pc.world_affine[6] * local_position.z +
            pc.world_affine[9],
        pc.world_affine[1] * local_position.x +
            pc.world_affine[4] * local_position.y +
            pc.world_affine[7] * local_position.z +
            pc.world_affine[10],
        pc.world_affine[2] * local_position.x +
            pc.world_affine[5] * local_position.y +
            pc.world_affine[8] * local_position.z +
            pc.world_affine[11]);
}

void main() {
    vec3 morphed_normal = apply_morph_normal(in_normal);
    vec3 morphed_position = apply_morph_position(in_position);
    vec3 skinned_normal = apply_skinning_normal(morphed_normal);
    vec3 skinned_position = apply_skinning_position(morphed_position);
    vec3 local_position = apply_vertex_deform(skinned_position, skinned_normal);
    vec4 world_position = vec4(apply_affine_world(local_position), 1.0);
    gl_Position = pc.view_projection * world_position;
    gl_PointSize = 1.0;
}
