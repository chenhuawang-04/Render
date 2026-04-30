#ifndef VR_COMMON_MATH_GLSL
#define VR_COMMON_MATH_GLSL

float vr_saturate(float value_) {
    return clamp(value_, 0.0, 1.0);
}

#endif // VR_COMMON_MATH_GLSL

