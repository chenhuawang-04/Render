#version 460

layout(location = 0) in vec2 in_uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform AtmospherePushConstants {
    vec4 camera_right_scale_x;
    vec4 camera_up_scale_y;
    vec4 camera_forward_reserved;
    vec4 zenith_exposure;
    vec4 horizon_intensity;
    vec4 ground_density;
    vec4 tint_mie;
    vec4 sun_direction_rayleigh;
} pc;

float saturate(float value_) {
    return clamp(value_, 0.0, 1.0);
}

void main() {
    vec2 ndc = vec2(in_uv.x * 2.0 - 1.0,
                    1.0 - in_uv.y * 2.0);
    vec3 ray_direction = normalize(pc.camera_forward_reserved.xyz +
                                   pc.camera_right_scale_x.xyz * (ndc.x * pc.camera_right_scale_x.w) +
                                   pc.camera_up_scale_y.xyz * (ndc.y * pc.camera_up_scale_y.w));
    vec3 sun_direction = normalize(pc.sun_direction_rayleigh.xyz);

    float view_up = saturate(ray_direction.y);
    float view_down = saturate(-ray_direction.y);
    float density = max(pc.ground_density.w, 0.05);
    float mie = max(pc.tint_mie.w, 0.05);
    float rayleigh = max(pc.sun_direction_rayleigh.w, 0.05);

    float upper_curve = pow(view_up, mix(0.45, 2.2, saturate(rayleigh * 0.2)));
    float lower_curve = pow(view_down, mix(0.55, 1.7, saturate(mie * 0.2)));
    vec3 sky_color = mix(pc.horizon_intensity.rgb, pc.zenith_exposure.rgb, upper_curve);
    vec3 ground_color = mix(pc.horizon_intensity.rgb, pc.ground_density.rgb, lower_curve);
    vec3 base_color = (ray_direction.y >= 0.0) ? sky_color : ground_color;

    float horizon_glow = exp2(-abs(ray_direction.y) * mix(10.0, 2.5, saturate(density * 0.15)));
    base_color = mix(base_color,
                     pc.horizon_intensity.rgb,
                     saturate(horizon_glow * 0.10 * density));

    float sun_amount = saturate(dot(ray_direction, sun_direction));
    float sun_core = pow(sun_amount, mix(384.0, 2048.0, saturate(mie * 0.1)));
    float sun_haze = pow(sun_amount, mix(6.0, 40.0, saturate(density * 0.1)));
    vec3 sun_color = mix(pc.horizon_intensity.rgb,
                         vec3(1.0, 0.97, 0.92),
                         0.65);
    sun_color *= (0.5 + 0.5 * saturate(rayleigh * 0.2));

    vec3 color = base_color;
    color += sun_color * (sun_haze * density * 0.20 + sun_core * 5.0);
    color *= pc.tint_mie.rgb;
    color *= max(pc.zenith_exposure.w * pc.horizon_intensity.w, 0.0);

    out_color = vec4(color, 1.0);
}
