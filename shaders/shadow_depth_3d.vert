#version 460

layout(location = 0) in vec3 in_position;
layout(location = 12) in vec3 in_morph0_position_delta;
layout(location = 14) in vec3 in_morph1_position_delta;

layout(push_constant) uniform ShadowPushConstants {
    mat4 view_projection;
    float world_affine[12];
    vec4 morph_weights;
} pc;

vec3 apply_morph_position(vec3 local_position) {
    return local_position +
           in_morph0_position_delta * pc.morph_weights.x +
           in_morph1_position_delta * pc.morph_weights.y;
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
    vec3 morphed_position = apply_morph_position(in_position);
    vec4 world_position = vec4(apply_affine_world(morphed_position), 1.0);
    gl_Position = pc.view_projection * world_position;
    gl_PointSize = 1.0;
}
