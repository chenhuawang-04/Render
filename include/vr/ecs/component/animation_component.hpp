#pragma once

#include "vr/ecs/component/spatial_types.hpp"
#include "vr/ecs/component/text_component.hpp"
#include "vr/ecs/concept/dimension.hpp"

#include <cstdint>
#include <limits>
#include <type_traits>

namespace vr::ecs {

struct PropertyTrack final {};
struct MaterialTrack final {};
struct PathMotion final {};
struct CameraTrack final {};

struct Skeletal final {};
struct VertexDeform final {};
struct Morph final {};
struct FrameSequence final {};
struct PhysicsDriven final {};
struct ParametricGeometry final {};
struct ParticleSimulation final {};

enum class AnimationLoopMode : std::uint8_t {
    once = 0U,
    loop = 1U,
    ping_pong = 2U,
};

enum AnimationPlaybackStateFlags : std::uint8_t {
    animation_playing_flag = 1U << 0U,
    animation_reverse_flag = 1U << 1U,
    animation_completed_flag = 1U << 2U,
    animation_ping_pong_backward_flag = 1U << 3U,
    animation_auto_start_flag = 1U << 4U,
};

enum AnimationDirtyFlags : std::uint32_t {
    animation_dirty_playback_flag = 1U << 0U,
    animation_dirty_binding_flag = 1U << 1U,
    animation_dirty_sample_flag = 1U << 2U,
    animation_dirty_runtime_flag = 1U << 3U,
};

enum class AnimationTargetDomain : std::uint8_t {
    none = 0U,
    transform = 1U,
    camera = 2U,
    appearance = 3U,
    surface = 4U,
    text = 5U,
    custom = 6U,
};

enum class AnimationInterpolationMode : std::uint8_t {
    step = 0U,
    linear = 1U,
};

enum class AnimationValueEncoding : std::uint8_t {
    scalar = 0U,
    float2 = 1U,
    float3 = 2U,
    float4 = 3U,
    quaternion = 4U,
    color_rgba8 = 5U,
};

enum class PropertyTrackSemantic : std::uint16_t {
    none = 0U,
    transform_local_position = 1U,
    transform_local_rotation = 2U,
    transform_local_scale = 3U,
    camera_zoom = 16U,
    camera_orthographic_height = 17U,
    camera_vertical_fov = 18U,
    text_color = 32U,
    text_outline_color = 33U,
};

enum class MaterialTrackSemantic : std::uint16_t {
    none = 0U,
    surface_uv_rect = 1U,
    surface_uv_transform = 2U,
    surface_tint_color = 3U,
    surface_opacity = 4U,
    appearance_color = 16U,
    appearance_opacity = 17U,
    appearance_emissive_color = 18U,
    appearance_emissive_intensity = 19U,
};

enum class AnimationPathOrientationMode : std::uint8_t {
    none = 0U,
    sampled = 1U,
    tangent = 2U,
};

enum AnimationPathApplyFlags : std::uint8_t {
    animation_path_apply_position_flag = 1U << 0U,
    animation_path_apply_rotation_flag = 1U << 1U,
    animation_path_apply_scale_flag = 1U << 2U,
};

enum AnimationCameraTrackApplyFlags : std::uint16_t {
    animation_camera_apply_transform_position_flag = 1U << 0U,
    animation_camera_apply_transform_rotation_flag = 1U << 1U,
    animation_camera_apply_vertical_fov_flag = 1U << 2U,
    animation_camera_apply_orthographic_height_flag = 1U << 3U,
    animation_camera_apply_zoom_flag = 1U << 4U,
    animation_camera_apply_shake_offset_flag = 1U << 5U,
};

struct AnimationClipHandle final {
    std::uint32_t index;
    std::uint32_t generation;
};

struct AnimationPathHandle final {
    std::uint32_t index;
    std::uint32_t generation;
};

inline constexpr std::uint32_t invalid_animation_handle_index =
    (std::numeric_limits<std::uint32_t>::max)();
inline constexpr AnimationClipHandle invalid_animation_clip_handle{
    .index = invalid_animation_handle_index,
    .generation = 0U,
};
inline constexpr AnimationPathHandle invalid_animation_path_handle{
    .index = invalid_animation_handle_index,
    .generation = 0U,
};

struct AnimationTargetRef final {
    std::uint32_t entity_id;
    std::uint16_t slot;
    AnimationTargetDomain domain;
    std::uint8_t reserved0;
    std::uint32_t sub_index;
};

struct AnimationPlaybackState final {
    AnimationClipHandle clip_handle;
    float time_s;
    float duration_s;
    float speed;
    float weight;
    std::uint16_t layer;
    AnimationLoopMode loop_mode;
    std::uint8_t state_flags;
};

struct AnimationRuntimeState final {
    std::uint32_t revision_playback;
    std::uint32_t revision_binding;
    std::uint32_t sample_revision;
    std::uint32_t dirty_flags;
    std::uint32_t curve_hint_index;
    std::uint32_t reserved0;
    std::uint32_t reserved1;
    std::uint32_t reserved2;
};

struct PropertyTrackBinding final {
    AnimationTargetRef target;
    PropertyTrackSemantic semantic;
    AnimationValueEncoding value_encoding;
    std::uint16_t channel_mask;
    std::uint16_t reserved0;
    std::uint32_t binding_handle;
};

struct PropertyTrackSample final {
    Float4 value;
    Quaternion rotation_value;
    AnimationInterpolationMode interpolation_mode;
    AnimationValueEncoding value_encoding;
    std::uint16_t channel_mask;
    std::uint16_t reserved0;
};

struct MaterialTrackBinding final {
    AnimationTargetRef target;
    MaterialTrackSemantic semantic;
    AnimationValueEncoding value_encoding;
    std::uint16_t channel_mask;
    std::uint16_t reserved0;
    std::uint32_t binding_handle;
};

struct MaterialTrackSample final {
    Float4 value;
    MaterialTrackSemantic semantic;
    AnimationInterpolationMode interpolation_mode;
    std::uint16_t channel_mask;
    std::uint16_t reserved0;
};

struct PathMotionBinding final {
    AnimationTargetRef target;
    AnimationPathHandle path_handle;
    AnimationPathOrientationMode orientation_mode;
    std::uint8_t apply_flags;
    std::uint16_t reserved0;
    float roll_offset_radians;
    Float3 up_hint;
};

template<DimensionTag DimensionT>
struct PathMotionSample;

template<>
struct PathMotionSample<Dim2> final {
    Float2 position;
    Float2 tangent;
    Float2 scale;
    float rotation_radians;
    float normalized_t;
    std::uint32_t reserved0;
};

template<>
struct PathMotionSample<Dim3> final {
    Float3 position;
    float normalized_t;
    Float3 tangent;
    float reserved0;
    Quaternion rotation;
    Float3 scale;
    float reserved1;
};

struct CameraTrackBinding final {
    AnimationTargetRef target;
    std::uint16_t apply_flags;
    std::uint16_t reserved0;
    float shake_weight;
    float reserved1;
};

template<DimensionTag DimensionT>
struct CameraTrackSample;

template<>
struct CameraTrackSample<Dim2> final {
    Float2 position;
    float rotation_radians;
    float orthographic_height;
    float zoom;
    Float2 shake_offset;
};

template<>
struct CameraTrackSample<Dim3> final {
    Float3 position;
    float vertical_fov_radians;
    Quaternion rotation;
    Float3 shake_offset;
    float orthographic_height;
    float reserved0;
};

struct ResourceAnimationBinding final {
    AnimationTargetRef target;
    std::uint32_t resource_handle;
    std::uint32_t binding_handle;
    std::uint32_t apply_flags;
    std::uint32_t reserved0;
};

struct ResourceAnimationSample final {
    Float4 parameters0;
    Float4 parameters1;
};

template<DimensionTag DimensionT, typename KindT>
struct AnimationComponent;

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, PropertyTrack> final {
    AnimationPlaybackState playback;
    PropertyTrackBinding binding;
    PropertyTrackSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, MaterialTrack> final {
    AnimationPlaybackState playback;
    MaterialTrackBinding binding;
    MaterialTrackSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, PathMotion> final {
    AnimationPlaybackState playback;
    PathMotionBinding binding;
    PathMotionSample<DimensionT> sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, CameraTrack> final {
    AnimationPlaybackState playback;
    CameraTrackBinding binding;
    CameraTrackSample<DimensionT> sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, Skeletal> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, VertexDeform> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, Morph> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, FrameSequence> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, PhysicsDriven> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, ParametricGeometry> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT>
struct AnimationComponent<DimensionT, ParticleSimulation> final {
    AnimationPlaybackState playback;
    ResourceAnimationBinding binding;
    ResourceAnimationSample sample;
    AnimationRuntimeState runtime;
};

template<DimensionTag DimensionT, typename KindT>
using Animation = AnimationComponent<DimensionT, KindT>;

template<typename T>
concept PurePodAnimationComponent = std::is_standard_layout_v<T> &&
                                    std::is_trivial_v<T>;

static_assert(PurePodAnimationComponent<AnimationClipHandle>);
static_assert(PurePodAnimationComponent<AnimationPathHandle>);
static_assert(PurePodAnimationComponent<AnimationTargetRef>);
static_assert(PurePodAnimationComponent<AnimationPlaybackState>);
static_assert(PurePodAnimationComponent<AnimationRuntimeState>);
static_assert(PurePodAnimationComponent<PropertyTrackBinding>);
static_assert(PurePodAnimationComponent<PropertyTrackSample>);
static_assert(PurePodAnimationComponent<MaterialTrackBinding>);
static_assert(PurePodAnimationComponent<MaterialTrackSample>);
static_assert(PurePodAnimationComponent<PathMotionBinding>);
static_assert(PurePodAnimationComponent<PathMotionSample<Dim2>>);
static_assert(PurePodAnimationComponent<PathMotionSample<Dim3>>);
static_assert(PurePodAnimationComponent<CameraTrackBinding>);
static_assert(PurePodAnimationComponent<CameraTrackSample<Dim2>>);
static_assert(PurePodAnimationComponent<CameraTrackSample<Dim3>>);
static_assert(PurePodAnimationComponent<ResourceAnimationBinding>);
static_assert(PurePodAnimationComponent<ResourceAnimationSample>);
static_assert(PurePodAnimationComponent<Animation<Dim2, PropertyTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, PropertyTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, MaterialTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, MaterialTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, PathMotion>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, PathMotion>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, CameraTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, CameraTrack>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, Skeletal>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, Skeletal>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, VertexDeform>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, VertexDeform>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, Morph>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, Morph>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, FrameSequence>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, FrameSequence>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, PhysicsDriven>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, PhysicsDriven>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, ParametricGeometry>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, ParametricGeometry>>);
static_assert(PurePodAnimationComponent<Animation<Dim2, ParticleSimulation>>);
static_assert(PurePodAnimationComponent<Animation<Dim3, ParticleSimulation>>);

} // namespace vr::ecs
