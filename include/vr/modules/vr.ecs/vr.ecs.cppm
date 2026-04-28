module;
// Global module fragment
#include "vr/detail/vr_module_fwd.hpp"
#include "Center/Memory/Container/Vector/McVector.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

export module vr.ecs;
import vr.types;

export {
namespace vr::ecs {

// ============================================================================
// Common concepts & POD types
// ============================================================================

enum class SceneDimension : std::uint8_t { dim2 = 0U, dim3 = 1U };
struct Dim2 final { static constexpr SceneDimension value = SceneDimension::dim2; };
struct Dim3 final { static constexpr SceneDimension value = SceneDimension::dim3; };

template<typename DimensionT>
concept DimensionTag = std::same_as<DimensionT, Dim2> || std::same_as<DimensionT, Dim3>;

template<DimensionTag DimensionT>
inline constexpr SceneDimension scene_dimension_v = DimensionT::value;

struct Rgba8 final { std::uint8_t r; std::uint8_t g; std::uint8_t b; std::uint8_t a; };

template<typename T>
concept PurePodComponent = std::is_standard_layout_v<T> && std::is_trivial_v<T>;

static_assert(PurePodComponent<Rgba8>);

// ============================================================================
// Appearance component
// ============================================================================

enum AppearanceDirtyFlags : std::uint32_t {
    appearance_dirty_style_flag = 1U << 0U,
    appearance_dirty_binding_flag = 1U << 1U,
};

enum class AppearanceBlendMode : std::uint8_t { opaque = 0U, alpha = 1U, additive = 2U, multiply = 3U, premultiplied = 4U };
enum class AppearanceAlphaMode : std::uint8_t { opaque = 0U, mask = 1U, blend = 2U };
enum class AppearancePaintMode : std::uint8_t { solid = 0U, linear_gradient = 1U, radial_gradient = 2U, pattern = 3U };
enum class AppearanceShadingModel3D : std::uint8_t { unlit = 0U, lit_pbr = 1U, lit_blinn = 2U };

struct AppearanceHandle final { std::uint32_t index; std::uint32_t generation; };

inline constexpr std::uint32_t invalid_appearance_index = (std::numeric_limits<std::uint32_t>::max)();
inline constexpr AppearanceHandle invalid_appearance_handle{ .index = invalid_appearance_index, .generation = 0U };

struct AppearanceRuntimeCommon final {
    std::uint32_t revision_style;
    std::uint32_t revision_binding;
    std::uint32_t upload_revision;
    std::uint32_t dirty_flags;
    std::uint64_t pipeline_key;
    std::uint64_t resource_key;
    std::uint64_t sort_key;
    AppearanceHandle gpu_record_handle;
    std::uint32_t gpu_record_index;
    std::uint8_t visible;
    std::uint8_t reserved0;
    std::uint16_t reserved1;
};

struct AppearanceStyle2D final {
    Rgba8 fill_color; Rgba8 stroke_color; float stroke_width_px; float opacity;
    float gradient_p0_x; float gradient_p0_y; float gradient_p1_x; float gradient_p1_y; float gradient_radius;
    std::int16_t layer; AppearanceBlendMode blend_mode; AppearanceAlphaMode alpha_mode; AppearancePaintMode paint_mode;
    std::uint8_t antialiasing; std::uint8_t premultiplied_alpha; std::uint16_t reserved0;
};

struct AppearanceStyle3D final {
    Rgba8 base_color; Rgba8 emissive_color; float opacity; float metallic; float roughness;
    float normal_scale; float occlusion_strength; float emissive_intensity; float alpha_cutoff;
    std::int16_t layer; AppearanceBlendMode blend_mode; AppearanceAlphaMode alpha_mode;
    AppearanceShadingModel3D shading_model;
    std::uint8_t double_sided; std::uint8_t cast_shadow; std::uint8_t receive_shadow; std::uint8_t reserved0;
};

struct AppearanceBinding2D final {
    std::uint32_t texture_base_id; std::uint32_t texture_mask_id; std::uint32_t texture_lut_id;
    std::uint32_t sampler_state_id; std::uint32_t binding_layout_id; std::uint32_t reserved0;
};

struct AppearanceBinding3D final {
    std::uint32_t texture_base_color_id; std::uint32_t texture_normal_id; std::uint32_t texture_metal_rough_id;
    std::uint32_t texture_occlusion_id; std::uint32_t texture_emissive_id;
    std::uint32_t sampler_state_id; std::uint32_t binding_layout_id; std::uint32_t reserved0;
};

template<DimensionTag DimensionT> struct AppearanceComponent;
template<> struct AppearanceComponent<Dim2> final {
    using StyleType = AppearanceStyle2D; using BindingType = AppearanceBinding2D;
    StyleType style; BindingType binding; AppearanceRuntimeCommon runtime;
};
template<> struct AppearanceComponent<Dim3> final {
    using StyleType = AppearanceStyle3D; using BindingType = AppearanceBinding3D;
    StyleType style; BindingType binding; AppearanceRuntimeCommon runtime;
};
template<DimensionTag DimensionT> using Appearance = AppearanceComponent<DimensionT>;

template<typename T>
concept PurePodAppearanceComponent = std::is_standard_layout_v<T> && std::is_trivial_v<T>;

static_assert(PurePodAppearanceComponent<AppearanceHandle>);
static_assert(PurePodAppearanceComponent<AppearanceRuntimeCommon>);
static_assert(PurePodAppearanceComponent<AppearanceStyle2D>);
static_assert(PurePodAppearanceComponent<AppearanceStyle3D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding2D>);
static_assert(PurePodAppearanceComponent<AppearanceBinding3D>);
static_assert(PurePodAppearanceComponent<Appearance<Dim2>>);
static_assert(PurePodAppearanceComponent<Appearance<Dim3>>);
static_assert(sizeof(AppearanceRuntimeCommon) <= 64U);

// ============================================================================
// Geometry component
// ============================================================================

enum GeometryDirtyFlags : std::uint32_t {
    geometry_dirty_data_flag = 1U << 0U, geometry_dirty_style_flag = 1U << 1U,
    geometry_dirty_runtime_flag = 1U << 2U, geometry_dirty_bounds_flag = 1U << 3U,
};

enum class GeometryRenderPassHint : std::uint8_t { overlay = 0U, opaque = 1U, transparent = 2U };
enum class Geometry2DTopology : std::uint8_t { fill = 0U, stroke = 1U, fill_and_stroke = 2U };
enum class Geometry2DFillRule : std::uint8_t { non_zero = 0U, even_odd = 1U };
enum class Geometry2DLineJoin : std::uint8_t { miter = 0U, round = 1U, bevel = 2U };
enum class Geometry2DLineCap : std::uint8_t { butt = 0U, round = 1U, square = 2U };
enum class Geometry3DTopology : std::uint8_t { triangles = 0U, lines = 1U, points = 2U };
enum class Geometry3DShadingModel : std::uint8_t { unlit = 0U, lit = 1U };

struct GeometryPathInlineData final {
    static constexpr std::uint32_t inline_capacity_bytes = 1024U;
    std::uint32_t size_bytes; std::uint32_t capacity_bytes; std::uint32_t revision; std::uint32_t reserved;
    std::uint8_t bytes[inline_capacity_bytes];
};

struct GeometryMeshRoute final { std::uint32_t submesh_index; std::uint16_t lod_index; std::uint16_t flags; };

struct GeometryRuntimeRoute final {
    std::uint64_t sort_key; std::uint32_t geometry_id; std::uint32_t material_id;
    std::uint32_t batch_tag; std::uint32_t user_data;
    AppearanceHandle appearance_handle;
    std::uint32_t appearance_pipeline_bucket; std::uint32_t appearance_resource_bucket;
    std::uint16_t depth_bin; std::uint8_t visible; GeometryRenderPassHint pass_hint;
    std::uint32_t dirty_flags;
};

struct GeometryStyle2D final {
    float stroke_width_px; float miter_limit; Rgba8 fill_color; Rgba8 stroke_color;
    std::int16_t layer; Geometry2DTopology topology; Geometry2DFillRule fill_rule;
    Geometry2DLineJoin line_join; Geometry2DLineCap line_cap;
    std::uint8_t antialiasing; std::uint8_t reserved0; std::uint16_t reserved1;
};

struct GeometryStyle3D final {
    Rgba8 albedo_color; std::uint8_t depth_test; std::uint8_t depth_write; std::uint8_t double_sided;
    Geometry3DTopology topology; Geometry3DShadingModel shading_model;
    std::uint8_t cast_shadow; std::uint8_t receive_shadow; std::uint8_t reserved0;
    float metallic; float roughness; float normal_scale; float line_width;
};

struct GeometryRuntime2D final {
    GeometryRuntimeRoute route; std::uint32_t path_command_count; std::uint32_t tessellation_revision;
    std::uint32_t path_data_hash; std::uint32_t reserved0; Float2 bounds_min; Float2 bounds_max;
};

struct GeometryRuntime3D final {
    GeometryRuntimeRoute route; std::uint32_t mesh_revision; std::uint32_t meshlet_count_hint;
    std::uint32_t reserved0; std::uint32_t reserved1; Float3 bounds_min; Float3 bounds_max;
};

template<DimensionTag DimensionT> struct GeometryComponent;
template<> struct GeometryComponent<Dim2> final {
    using StyleType = GeometryStyle2D; using RuntimeType = GeometryRuntime2D;
    GeometryPathInlineData path; StyleType style; RuntimeType runtime;
};
template<> struct GeometryComponent<Dim3> final {
    using StyleType = GeometryStyle3D; using RuntimeType = GeometryRuntime3D;
    GeometryMeshRoute mesh; StyleType style; RuntimeType runtime;
};
template<DimensionTag DimensionT> using Geometry = GeometryComponent<DimensionT>;

template<typename T>
concept PurePodGeometryComponent = std::is_standard_layout_v<T> && std::is_trivial_v<T>;

static_assert(PurePodGeometryComponent<GeometryPathInlineData>);
static_assert(PurePodGeometryComponent<GeometryMeshRoute>);
static_assert(PurePodGeometryComponent<GeometryRuntimeRoute>);
static_assert(PurePodGeometryComponent<GeometryStyle2D>);
static_assert(PurePodGeometryComponent<GeometryStyle3D>);
static_assert(PurePodGeometryComponent<GeometryRuntime2D>);
static_assert(PurePodGeometryComponent<GeometryRuntime3D>);
static_assert(PurePodGeometryComponent<Geometry<Dim2>>);
static_assert(PurePodGeometryComponent<Geometry<Dim3>>);
static_assert(sizeof(GeometryRuntimeRoute) <= 48U);

// ============================================================================
// Surface component
// ============================================================================

enum SurfaceDirtyFlags : std::uint32_t {
    surface_dirty_source_flag = 1U << 0U, surface_dirty_style_flag = 1U << 1U, surface_dirty_runtime_flag = 1U << 2U,
};
enum class SurfaceRenderPassHint : std::uint8_t { overlay = 0U, opaque = 1U, transparent = 2U };
enum class Surface2DSourceKind : std::uint8_t { none = 0U, image = 1U, sprite = 2U };
enum class Surface2DBlendMode : std::uint8_t { alpha = 0U, additive = 1U, multiply = 2U, screen = 3U };
enum class Surface3DFilterMode : std::uint8_t { linear = 0U, nearest = 1U, anisotropic = 2U };
enum class Surface3DAddressMode : std::uint8_t { wrap = 0U, clamp = 1U, mirror = 2U };

struct SurfaceRuntimeRoute final {
    std::uint64_t sort_key; std::uint32_t surface_id; std::uint32_t material_id;
    std::uint32_t batch_tag; std::uint32_t user_data;
    AppearanceHandle appearance_handle;
    std::uint32_t appearance_pipeline_bucket; std::uint32_t appearance_resource_bucket;
    std::uint16_t depth_bin; std::uint8_t visible; SurfaceRenderPassHint pass_hint;
    std::uint32_t dirty_flags;
};

struct Surface2DSourceRoute final {
    std::uint32_t image_id; std::uint32_t sprite_id; std::uint32_t atlas_page_id;
    Surface2DSourceKind source_kind; std::uint8_t reserved0; std::uint16_t reserved1;
};
struct Surface3DTextureRoute final { std::uint32_t texture_id; std::uint32_t sampler_id; std::uint16_t uv_set; std::uint16_t flags; };

struct SurfaceStyle2D final {
    float uv_u0; float uv_v0; float uv_u1; float uv_v1; Rgba8 tint_color; float opacity;
    std::int16_t layer; Surface2DBlendMode blend_mode;
    std::uint8_t flip_x; std::uint8_t flip_y; std::uint8_t premultiplied_alpha; std::uint8_t reserved0;
};
struct SurfaceStyle3D final {
    Rgba8 tint_color; float uv_scale_u; float uv_scale_v; float uv_bias_u; float uv_bias_v; float opacity;
    std::uint8_t depth_test; std::uint8_t depth_write; std::uint8_t double_sided;
    Surface3DFilterMode filter_mode; Surface3DAddressMode address_u; Surface3DAddressMode address_v;
    Surface3DAddressMode address_w; std::uint8_t reserved0;
};

struct SurfaceRuntime2D final {
    SurfaceRuntimeRoute route; Surface2DSourceRoute source; std::uint32_t source_revision;
    std::uint32_t reserved0; Float2 size; Float2 pivot;
};
struct SurfaceRuntime3D final {
    SurfaceRuntimeRoute route; Surface3DTextureRoute texture; std::uint32_t texture_revision;
    std::uint32_t reserved0; std::uint32_t reserved1;
};

template<DimensionTag DimensionT> struct SurfaceComponent;
template<> struct SurfaceComponent<Dim2> final {
    using StyleType = SurfaceStyle2D; using RuntimeType = SurfaceRuntime2D;
    StyleType style; RuntimeType runtime;
};
template<> struct SurfaceComponent<Dim3> final {
    using StyleType = SurfaceStyle3D; using RuntimeType = SurfaceRuntime3D;
    StyleType style; RuntimeType runtime;
};
template<DimensionTag DimensionT> using Surface = SurfaceComponent<DimensionT>;

template<typename T>
concept PurePodSurfaceComponent = std::is_standard_layout_v<T> && std::is_trivial_v<T>;

static_assert(PurePodSurfaceComponent<SurfaceRuntimeRoute>);
static_assert(PurePodSurfaceComponent<Surface2DSourceRoute>);
static_assert(PurePodSurfaceComponent<Surface3DTextureRoute>);
static_assert(PurePodSurfaceComponent<SurfaceStyle2D>);
static_assert(PurePodSurfaceComponent<SurfaceStyle3D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime2D>);
static_assert(PurePodSurfaceComponent<SurfaceRuntime3D>);
static_assert(PurePodSurfaceComponent<Surface<Dim2>>);
static_assert(PurePodSurfaceComponent<Surface<Dim3>>);
static_assert(sizeof(SurfaceRuntimeRoute) <= 48U);

// ============================================================================
// Transform component (common infrastructure)
// ============================================================================

enum TransformDirtyFlags : std::uint32_t {
    transform_dirty_local_flag = 1U << 0U, transform_dirty_world_flag = 1U << 1U, transform_dirty_hierarchy_flag = 1U << 2U,
};
struct TransformHierarchyLink final { std::int32_t parent_index; std::int32_t first_child_index; std::int32_t next_sibling_index; std::int32_t reserved0; };
struct TransformStyle2D final { Float2 position; float rotation_radians; Float2 scale; };
struct TransformRuntime2D final { Affine2x3 local_matrix; Affine2x3 world_matrix; TransformHierarchyLink hierarchy; std::uint32_t local_revision; std::uint32_t world_revision; std::uint32_t cached_parent_world_revision; std::uint32_t dirty_flags; };
struct TransformStyle3D final { Float3 position; Quaternion rotation; Float3 scale; std::uint32_t reserved0; };
struct TransformRuntime3D final { Matrix4x4 local_matrix; Matrix4x4 world_matrix; TransformHierarchyLink hierarchy; std::uint32_t local_revision; std::uint32_t world_revision; std::uint32_t cached_parent_world_revision; std::uint32_t dirty_flags; };

template<DimensionTag DimensionT> struct TransformComponent;
template<> struct TransformComponent<Dim2> final { using StyleType = TransformStyle2D; using RuntimeType = TransformRuntime2D; StyleType style; RuntimeType runtime; };
template<> struct TransformComponent<Dim3> final { using StyleType = TransformStyle3D; using RuntimeType = TransformRuntime3D; StyleType style; RuntimeType runtime; };
template<DimensionTag DimensionT> using Transform = TransformComponent<DimensionT>;

// ============================================================================
// Camera component
// ============================================================================

enum class CameraProjectionMode : std::uint8_t { orthographic = 0U, perspective = 1U };
enum CameraDirtyFlags : std::uint32_t { camera_dirty_projection_flag = 1U << 0U, camera_dirty_view_flag = 1U << 1U, camera_dirty_runtime_flag = 1U << 2U };
struct CameraViewport final { float origin_x; float origin_y; float width; float height; };
struct CameraStyle2D final { float orthographic_height; float aspect_ratio; float near_plane; float far_plane; float zoom; std::uint8_t y_down; std::uint8_t reserved0; std::uint16_t reserved1; CameraViewport viewport; };
struct CameraStyle3D final { CameraProjectionMode projection_mode; std::uint8_t reverse_z; std::uint16_t reserved0; float vertical_fov_radians; float orthographic_height; float aspect_ratio; float near_plane; float far_plane; CameraViewport viewport; };
struct CameraRuntimeData final { Matrix4x4 view_matrix; Matrix4x4 projection_matrix; Matrix4x4 view_projection_matrix; std::uint32_t culling_mask; std::uint32_t revision; std::uint32_t dirty_flags; std::uint32_t reserved0; };

template<DimensionTag DimensionT> struct CameraComponent;
template<> struct CameraComponent<Dim2> final { using StyleType = CameraStyle2D; StyleType style; CameraRuntimeData runtime; };
template<> struct CameraComponent<Dim3> final { using StyleType = CameraStyle3D; StyleType style; CameraRuntimeData runtime; };
template<DimensionTag DimensionT> using Camera = CameraComponent<DimensionT>;

// ============================================================================
// Bounds component
// ============================================================================

enum BoundsDirtyFlags : std::uint32_t { bounds_dirty_local_flag = 1U << 0U, bounds_dirty_runtime_flag = 1U << 1U, bounds_dirty_visibility_flag = 1U << 2U };
struct BoundsStyle2D final { Float2 local_min; Float2 local_max; };
struct BoundsRuntime2D final { Float2 world_min; Float2 world_max; Float2 world_center; Float2 world_extents; float world_radius; std::uint32_t local_revision; std::uint32_t world_revision; std::uint32_t transform_world_revision; std::uint32_t visibility_mask; std::uint32_t dirty_flags; std::uint32_t reserved0; };
struct BoundsStyle3D final { Float3 local_min; float reserved0; Float3 local_max; float reserved1; };
struct BoundsRuntime3D final { Float3 world_min; float world_radius; Float3 world_max; float reserved0; Float3 world_center; std::uint32_t local_revision; Float3 world_extents; std::uint32_t world_revision; std::uint32_t transform_world_revision; std::uint32_t visibility_mask; std::uint32_t dirty_flags; std::uint32_t reserved1; };

template<DimensionTag DimensionT> struct BoundsComponent;
template<> struct BoundsComponent<Dim2> final { using StyleType = BoundsStyle2D; using RuntimeType = BoundsRuntime2D; StyleType style; RuntimeType runtime; };
template<> struct BoundsComponent<Dim3> final { using StyleType = BoundsStyle3D; using RuntimeType = BoundsRuntime3D; StyleType style; RuntimeType runtime; };
template<DimensionTag DimensionT> using Bounds = BoundsComponent<DimensionT>;

[[nodiscard]] constexpr std::uint32_t NextBoundsRevision(std::uint32_t current_revision_) noexcept {
    return (current_revision_ == std::numeric_limits<std::uint32_t>::max()) ? 1U : (current_revision_ + 1U);
}

// ============================================================================
// Culling system
// ============================================================================

struct CullingBuildOptions final { bool enable_culling_mask_filter = true; bool enable_frustum_culling = true; bool enable_aabb_refine = true; bool write_visibility_bits = false; };
struct CullingBuildStats final {
    std::uint32_t input_count = 0U; std::uint32_t scanned_count = 0U; std::uint32_t candidate_count = 0U; std::uint32_t out_of_range_candidate_count = 0U;
    std::uint32_t visible_count = 0U; std::uint32_t culled_by_mask_count = 0U; std::uint32_t culled_by_frustum_count = 0U;
    std::uint32_t culled_by_invalid_bounds_count = 0U; std::uint32_t sphere_reject_count = 0U; std::uint32_t aabb_reject_count = 0U;
    std::uint32_t plane_test_count = 0U; std::uint64_t visible_set_signature = 0U;
    bool used_mask_filter = false; bool used_frustum_filter = false; bool used_aabb_refine = false; bool wrote_visibility_bits = false;
};

template<DimensionTag DimensionT>
struct CullingScratch final {
    vr::McVector<std::uint32_t> visible_indices{}; vr::McVector<std::uint32_t> visibility_stamps{}; std::uint32_t visibility_epoch = 1U;
    void Reserve(std::uint32_t n) { visible_indices.reserve(n); visibility_stamps.reserve(n); }
};

template<DimensionTag DimensionT>
class CullingSystem final {
public:
    using BoundsType = Bounds<DimensionT>; using CameraType = Camera<DimensionT>; using ScratchType = CullingScratch<DimensionT>;
    static constexpr std::uint32_t k_plane_count = std::same_as<DimensionT, Dim2> ? 4U : 6U;
    struct FrustumPlane final { float nx; float ny; float nz; float d; };
    struct FrustumPlanes final { FrustumPlane planes[6U]{}; std::uint32_t count = 0U; };
    struct PreparedCamera final { FrustumPlanes frustum_planes{}; std::uint32_t culling_mask = 0xFFFFFFFFU; std::uint8_t has_camera = 0U; std::uint8_t use_mask_filter = 0U; std::uint8_t use_frustum_filter = 0U; std::uint8_t use_aabb_refine = 0U; };

    static void Reserve(ScratchType& s, std::uint32_t n) { s.Reserve(n); }

    [[nodiscard]] static PreparedCamera PrepareCamera(const CameraType* cam, const CullingBuildOptions& opts = {}) noexcept {
        PreparedCamera p{}; p.use_mask_filter = opts.enable_culling_mask_filter ? 1U : 0U;
        p.use_frustum_filter = opts.enable_frustum_culling ? 1U : 0U;
        p.use_aabb_refine = (opts.enable_frustum_culling && opts.enable_aabb_refine) ? 1U : 0U;
        if (cam == nullptr) { p.has_camera = 0U; p.use_mask_filter = 0U; p.use_frustum_filter = 0U; p.use_aabb_refine = 0U; return p; }
        p.has_camera = 1U; p.culling_mask = cam->runtime.culling_mask;
        if (p.use_frustum_filter != 0U) p.frustum_planes = BuildFrustumPlanes(cam->runtime.view_projection_matrix);
        return p;
    }

    [[nodiscard]] static CullingBuildStats BuildVisibleSet(const BoundsType* b, std::uint32_t n, const CameraType* cam, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSet(b, n, PrepareCamera(cam, o), s, o);
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSet(const BoundsType* b, std::uint32_t n, const CameraType& cam, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSet(b, n, &cam, s, o);
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSet(const BoundsType* b, std::uint32_t n, const PreparedCamera& pc, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSetInternal(b, n, nullptr, 0U, pc, s, o);
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, std::uint32_t cc, const CameraType* cam, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSetFromCandidates(b, n, ci, cc, PrepareCamera(cam, o), s, o);
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, std::uint32_t cc, const CameraType& cam, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSetFromCandidates(b, n, ci, cc, &cam, s, o);
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSetFromCandidates(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, std::uint32_t cc, const PreparedCamera& pc, ScratchType& s, const CullingBuildOptions& o = {}) {
        return BuildVisibleSetInternal(b, n, ci, cc, pc, s, o);
    }
    [[nodiscard]] static const vr::McVector<std::uint32_t>& VisibleIndices(const ScratchType& s) noexcept { return s.visible_indices; }
    [[nodiscard]] static bool IsVisible(const ScratchType& s, std::uint32_t i) noexcept {
        return i < s.visibility_stamps.size() && s.visibility_stamps[i] == s.visibility_epoch;
    }

private:
    static constexpr std::uint64_t k_seed = 14695981039346656037ULL;
    static constexpr std::uint64_t k_prime = 1099511628211ULL;
    static void SMix(std::uint64_t& h, std::uint64_t v) noexcept { h ^= v; h *= k_prime; }
    [[nodiscard]] static std::uint64_t FinalizeSig(std::uint64_t h, std::uint32_t c) noexcept { SMix(h, c); return h ? h : k_seed; }
    static void EnsureVisSize(ScratchType& s, std::uint32_t n) {
        auto old = s.visibility_stamps.size(); if (old >= n) return;
        s.visibility_stamps.resize(n); std::fill(s.visibility_stamps.begin() + (std::ptrdiff_t)old, s.visibility_stamps.end(), 0U);
    }
    static void AdvanceEpoch(ScratchType& s) {
        if (s.visibility_epoch == std::numeric_limits<std::uint32_t>::max()) { std::fill(s.visibility_stamps.begin(), s.visibility_stamps.end(), 0U); s.visibility_epoch = 1U; return; }
        ++s.visibility_epoch;
    }
    template<bool WV> static void AppendIdx(ScratchType& s, std::uint32_t i, std::uint64_t& sig, std::uint32_t& c) {
        s.visible_indices.push_back(i); if constexpr (WV) s.visibility_stamps[i] = s.visibility_epoch; SMix(sig, i); ++c;
    }
    template<bool AR> [[nodiscard]] static bool IntersectsFrustum(const BoundsType& b, const FrustumPlanes& fp, CullingBuildStats& st) noexcept {
        float cx = b.runtime.world_center.x, cy = b.runtime.world_center.y, cz = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) cz = b.runtime.world_center.z;
        float r = std::max(0.0F, b.runtime.world_radius); bool all_in = true;
        for (std::uint32_t pi = 0U; pi < fp.count; ++pi) { const auto& p = fp.planes[pi]; ++st.plane_test_count;
            if (p.nx*cx + p.ny*cy + p.nz*cz + p.d < -r) { ++st.sphere_reject_count; return false; }
            if (p.nx*cx + p.ny*cy + p.nz*cz + p.d < r) all_in = false; }
        if constexpr (!AR) return true;
        if (all_in) return true;
        float mx = b.runtime.world_min.x, my = b.runtime.world_min.y, Mx = b.runtime.world_max.x, My = b.runtime.world_max.y, mz = 0.0F, Mz = 0.0F;
        if constexpr (std::same_as<DimensionT, Dim3>) { mz = b.runtime.world_min.z; Mz = b.runtime.world_max.z; }
        for (std::uint32_t pi = 0U; pi < fp.count; ++pi) { const auto& p = fp.planes[pi]; ++st.plane_test_count;
            if (p.nx*(p.nx>=0?Mx:mx) + p.ny*(p.ny>=0?My:my) + p.nz*(p.nz>=0?Mz:mz) + p.d < 0.0F) { ++st.aabb_reject_count; return false; } }
        return true;
    }
    template<bool CS, bool MF, bool FF, bool AR, bool WV>
    static void Scan(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, const PreparedCamera& pc, CullingBuildStats& st, ScratchType& s, std::uint64_t& sig, std::uint32_t& vc) {
        for (std::uint32_t i = 0U; i < st.scanned_count; ++i) { std::uint32_t idx = CS ? ci[i] : i;
            if (idx >= n) { ++st.out_of_range_candidate_count; continue; }
            const auto& bb = b[idx];
            if constexpr (std::same_as<DimensionT, Dim2>) { if (!(bb.runtime.world_min.x <= bb.runtime.world_max.x && bb.runtime.world_min.y <= bb.runtime.world_max.y)) { ++st.culled_by_invalid_bounds_count; continue; } }
            else { if (!(bb.runtime.world_min.x <= bb.runtime.world_max.x && bb.runtime.world_min.y <= bb.runtime.world_max.y && bb.runtime.world_min.z <= bb.runtime.world_max.z)) { ++st.culled_by_invalid_bounds_count; continue; } }
            if constexpr (MF) { if ((bb.runtime.visibility_mask & pc.culling_mask) == 0U) { ++st.culled_by_mask_count; continue; } }
            if constexpr (FF) { if (!IntersectsFrustum<AR>(bb, pc.frustum_planes, st)) { ++st.culled_by_frustum_count; continue; } }
            AppendIdx<WV>(s, idx, sig, vc);
        }
    }
    template<bool CS> static void Dispatch(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, const PreparedCamera& pc, const CullingBuildOptions& o, CullingBuildStats& st, ScratchType& s, std::uint64_t& sig, std::uint32_t& vc) {
        bool mf=pc.use_mask_filter!=0, ff=pc.use_frustum_filter!=0, ar=pc.use_aabb_refine!=0, wv=o.write_visibility_bits;
        if(ff){if(mf){if(wv){if(ar)Scan<CS,1,1,1,1>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,1,1,0,1>(b,n,ci,pc,st,s,sig,vc);}else{if(ar)Scan<CS,1,1,1,0>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,1,1,0,0>(b,n,ci,pc,st,s,sig,vc);}}
        else{if(wv){if(ar)Scan<CS,0,1,1,1>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,0,1,0,1>(b,n,ci,pc,st,s,sig,vc);}else{if(ar)Scan<CS,0,1,1,0>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,0,1,0,0>(b,n,ci,pc,st,s,sig,vc);}}}
        else{if(mf){if(wv)Scan<CS,1,0,0,1>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,1,0,0,0>(b,n,ci,pc,st,s,sig,vc);}
        else{if(wv)Scan<CS,0,0,0,1>(b,n,ci,pc,st,s,sig,vc);else Scan<CS,0,0,0,0>(b,n,ci,pc,st,s,sig,vc);}}
    }
    [[nodiscard]] static CullingBuildStats BuildVisibleSetInternal(const BoundsType* b, std::uint32_t n, const std::uint32_t* ci, std::uint32_t cc, const PreparedCamera& pc, ScratchType& s, const CullingBuildOptions& o) {
        CullingBuildStats st{}; st.input_count=n; st.used_mask_filter=pc.use_mask_filter!=0; st.used_frustum_filter=pc.use_frustum_filter!=0; st.used_aabb_refine=pc.use_aabb_refine!=0; st.wrote_visibility_bits=o.write_visibility_bits;
        s.visible_indices.clear(); AdvanceEpoch(s); if(n==0U||b==nullptr){st.visible_set_signature=FinalizeSig(k_seed,0U);return st;}
        Reserve(s,n); if(o.write_visibility_bits) EnsureVisSize(s,n);
        bool cs=cc>0U; st.candidate_count=cs?cc:n; st.scanned_count=st.candidate_count;
        if(cs&&ci==nullptr){st.out_of_range_candidate_count=cc;st.visible_count=0U;st.visible_set_signature=FinalizeSig(k_seed,0U);return st;}
        std::uint64_t sig=k_seed; std::uint32_t vc=0U;
        if(cs)Dispatch<1>(b,n,ci,pc,o,st,s,sig,vc);else Dispatch<0>(b,n,ci,pc,o,st,s,sig,vc);
        st.visible_count=vc; st.visible_set_signature=FinalizeSig(sig,vc); return st;
    }
    [[nodiscard]] static FrustumPlane NormPlane(const FrustumPlane& p) noexcept { float l2=p.nx*p.nx+p.ny*p.ny+p.nz*p.nz; if(l2<=1e-20F) return {0,0,0,-1.0F}; float inv=1.0F/std::sqrt(l2); return {p.nx*inv,p.ny*inv,p.nz*inv,p.d*inv}; }
    [[nodiscard]] static FrustumPlanes BuildFrustumPlanes(const Matrix4x4& vp) noexcept {
        float r0x=vp.m[0],r0y=vp.m[4],r0z=vp.m[8],r0w=vp.m[12],r1x=vp.m[1],r1y=vp.m[5],r1z=vp.m[9],r1w=vp.m[13],r2x=vp.m[2],r2y=vp.m[6],r2z=vp.m[10],r2w=vp.m[14],r3x=vp.m[3],r3y=vp.m[7],r3z=vp.m[11],r3w=vp.m[15];
        FrustumPlanes o{}; o.count=k_plane_count;
        o.planes[0]=NormPlane({r3x+r0x,r3y+r0y,r3z+r0z,r3w+r0w}); o.planes[1]=NormPlane({r3x-r0x,r3y-r0y,r3z-r0z,r3w-r0w});
        o.planes[2]=NormPlane({r3x+r1x,r3y+r1y,r3z+r1z,r3w+r1w}); o.planes[3]=NormPlane({r3x-r1x,r3y-r1y,r3z-r1z,r3w-r1w});
        if constexpr (std::same_as<DimensionT, Dim3>) { o.planes[4]=NormPlane({r2x,r2y,r2z,r2w}); o.planes[5]=NormPlane({r3x-r2x,r3y-r2y,r3z-r2z,r3w-r2w}); }
        return o;
    }
};

// ============================================================================
// Appearance system
// ============================================================================

[[nodiscard]] constexpr std::uint32_t NextAppearanceRevision(std::uint32_t r) noexcept { return (r==(std::numeric_limits<std::uint32_t>::max)())?1U:(r+1U); }

template<DimensionTag DimensionT>
class AppearanceSystem final {
public:
    using AppearanceType = Appearance<DimensionT>; using StyleType = typename AppearanceType::StyleType; using BindingType = typename AppearanceType::BindingType;

    static void Initialize(AppearanceType& c) noexcept { SetDefaultStyle(c); SetDefaultBinding(c); SetDefaultRuntime(c); }

    static void SetDefaultStyle(AppearanceType& c) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) {
            c.style.fill_color=Rgba8{255U,255U,255U,255U}; c.style.stroke_color=Rgba8{255U,255U,255U,255U}; c.style.stroke_width_px=1.0F; c.style.opacity=1.0F;
            c.style.gradient_p0_x=0.0F; c.style.gradient_p0_y=0.0F; c.style.gradient_p1_x=1.0F; c.style.gradient_p1_y=0.0F; c.style.gradient_radius=1.0F;
            c.style.layer=0; c.style.blend_mode=AppearanceBlendMode::alpha; c.style.alpha_mode=AppearanceAlphaMode::blend; c.style.paint_mode=AppearancePaintMode::solid; c.style.antialiasing=1U; c.style.premultiplied_alpha=0U; c.style.reserved0=0U;
        } else {
            c.style.base_color=Rgba8{255U,255U,255U,255U}; c.style.emissive_color=Rgba8{0U,0U,0U,255U}; c.style.opacity=1.0F; c.style.metallic=0.0F; c.style.roughness=1.0F;
            c.style.normal_scale=1.0F; c.style.occlusion_strength=1.0F; c.style.emissive_intensity=0.0F; c.style.alpha_cutoff=0.5F;
            c.style.layer=0; c.style.blend_mode=AppearanceBlendMode::opaque; c.style.alpha_mode=AppearanceAlphaMode::opaque; c.style.shading_model=AppearanceShadingModel3D::lit_pbr;
            c.style.double_sided=0U; c.style.cast_shadow=1U; c.style.receive_shadow=1U; c.style.reserved0=0U;
        }
    }

    static void SetDefaultBinding(AppearanceType& c) noexcept {
        if constexpr (std::same_as<DimensionT, Dim2>) c.binding=AppearanceBinding2D{};
        else c.binding=AppearanceBinding3D{};
    }

    static void SetDefaultRuntime(AppearanceType& c) noexcept {
        c.runtime.revision_style=1U; c.runtime.revision_binding=1U; c.runtime.upload_revision=0U; c.runtime.dirty_flags=appearance_dirty_style_flag|appearance_dirty_binding_flag;
        c.runtime.pipeline_key=0U; c.runtime.resource_key=0U; c.runtime.sort_key=0U; c.runtime.gpu_record_handle=invalid_appearance_handle; c.runtime.gpu_record_index=invalid_appearance_index;
        c.runtime.visible=1U; c.runtime.reserved0=0U; c.runtime.reserved1=0U;
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const AppearanceType& c) noexcept { return c.runtime.dirty_flags; }
    [[nodiscard]] static bool HasDirtyFlags(const AppearanceType& c, std::uint32_t m) noexcept { return (c.runtime.dirty_flags&m)!=0U; }
    static void MarkDirty(AppearanceType& c, std::uint32_t m) noexcept { c.runtime.dirty_flags|=m; }
    static void ClearDirtyFlags(AppearanceType& c, std::uint32_t m) noexcept { c.runtime.dirty_flags&=~m; }
    [[nodiscard]] static std::uint32_t StyleRevision(const AppearanceType& c) noexcept { return c.runtime.revision_style; }
    [[nodiscard]] static std::uint32_t BindingRevision(const AppearanceType& c) noexcept { return c.runtime.revision_binding; }
    [[nodiscard]] static std::uint32_t UploadRevision(const AppearanceType& c) noexcept { return c.runtime.upload_revision; }
    [[nodiscard]] static bool IsVisible(const AppearanceType& c) noexcept { return c.runtime.visible!=0U; }
    [[nodiscard]] static bool IsVisibleForBatch(const AppearanceType& c) noexcept {
        if(c.runtime.visible==0U) return false;
        if constexpr (std::same_as<DimensionT, Dim2>) { if(c.style.paint_mode==AppearancePaintMode::pattern) return c.binding.texture_base_id!=0U; }
        return true;
    }
    static void SetVisible(AppearanceType& c, bool v) noexcept { std::uint8_t vv=v?1U:0U; if(c.runtime.visible==vv) return; c.runtime.visible=vv; MarkStyleRevisionDirty(c); }
    static void SetRuntimeKeys(AppearanceType& c, std::uint64_t pk, std::uint64_t rk, std::uint64_t sk) noexcept { c.runtime.pipeline_key=pk; c.runtime.resource_key=rk; c.runtime.sort_key=sk; }
    static void SetGpuRecordHandle(AppearanceType& c, AppearanceHandle h) noexcept { c.runtime.gpu_record_handle=h; c.runtime.gpu_record_index=h.index; }
    static void MarkUploaded(AppearanceType& c) noexcept { c.runtime.upload_revision=NextAppearanceRevision(c.runtime.upload_revision); ClearDirtyFlags(c,appearance_dirty_style_flag|appearance_dirty_binding_flag); }
    [[nodiscard]] static std::int16_t Layer(const AppearanceType& c) noexcept { return c.style.layer; }
    [[nodiscard]] static AppearanceBlendMode BlendMode(const AppearanceType& c) noexcept { return c.style.blend_mode; }
    [[nodiscard]] static AppearanceAlphaMode AlphaMode(const AppearanceType& c) noexcept { return c.style.alpha_mode; }
    static void SetLayer(AppearanceType& c, std::int16_t l) noexcept { if(c.style.layer==l) return; c.style.layer=l; MarkStyleRevisionDirty(c); }
    static void SetBlendMode(AppearanceType& c, AppearanceBlendMode m) noexcept { if(c.style.blend_mode==m) return; c.style.blend_mode=m; MarkStyleRevisionDirty(c); }
    static void SetAlphaMode(AppearanceType& c, AppearanceAlphaMode m) noexcept { if(c.style.alpha_mode==m) return; c.style.alpha_mode=m; MarkStyleRevisionDirty(c); }
    static void SetOpacity(AppearanceType& c, float o) noexcept { float cl=std::clamp(o,0.0F,1.0F); if(c.style.opacity==cl) return; c.style.opacity=cl; MarkStyleRevisionDirty(c); }
    static void SetBindingLayoutId(AppearanceType& c, std::uint32_t id) noexcept { if(c.binding.binding_layout_id==id) return; c.binding.binding_layout_id=id; MarkBindingRevisionDirty(c); }
    static void SetSamplerStateId(AppearanceType& c, std::uint32_t id) noexcept { if(c.binding.sampler_state_id==id) return; c.binding.sampler_state_id=id; MarkBindingRevisionDirty(c); }

    // 2D-specific setters
    static void SetFillColor(AppearanceType& c, Rgba8 v) noexcept requires std::same_as<DimensionT,Dim2> { if(IsSameColor(c.style.fill_color,v)) return; c.style.fill_color=v; MarkStyleRevisionDirty(c); }
    static void SetStrokeColor(AppearanceType& c, Rgba8 v) noexcept requires std::same_as<DimensionT,Dim2> { if(IsSameColor(c.style.stroke_color,v)) return; c.style.stroke_color=v; MarkStyleRevisionDirty(c); }
    static void SetStrokeWidthPx(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim2> { float cl=std::max(0.0F,v); if(c.style.stroke_width_px==cl) return; c.style.stroke_width_px=cl; MarkStyleRevisionDirty(c); }
    static void SetPaintMode(AppearanceType& c, AppearancePaintMode m) noexcept requires std::same_as<DimensionT,Dim2> { if(c.style.paint_mode==m) return; c.style.paint_mode=m; MarkStyleRevisionDirty(c); }
    static void SetGradientLinear(AppearanceType& c, float x0,float y0,float x1,float y1) noexcept requires std::same_as<DimensionT,Dim2> { if(c.style.gradient_p0_x==x0&&c.style.gradient_p0_y==y0&&c.style.gradient_p1_x==x1&&c.style.gradient_p1_y==y1) return; c.style.gradient_p0_x=x0;c.style.gradient_p0_y=y0;c.style.gradient_p1_x=x1;c.style.gradient_p1_y=y1; MarkStyleRevisionDirty(c); }
    static void SetGradientRadius(AppearanceType& c, float r) noexcept requires std::same_as<DimensionT,Dim2> { float cl=std::max(0.0F,r); if(c.style.gradient_radius==cl) return; c.style.gradient_radius=cl; MarkStyleRevisionDirty(c); }
    static void SetAntialiasing(AppearanceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim2> { std::uint8_t en=v?1U:0U; if(c.style.antialiasing==en) return; c.style.antialiasing=en; MarkStyleRevisionDirty(c); }
    static void SetPremultipliedAlpha(AppearanceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim2> { std::uint8_t en=v?1U:0U; if(c.style.premultiplied_alpha==en) return; c.style.premultiplied_alpha=en; MarkStyleRevisionDirty(c); }
    static void SetTextureBaseId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim2> { if(c.binding.texture_base_id==id) return; c.binding.texture_base_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureMaskId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim2> { if(c.binding.texture_mask_id==id) return; c.binding.texture_mask_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureLutId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim2> { if(c.binding.texture_lut_id==id) return; c.binding.texture_lut_id=id; MarkBindingRevisionDirty(c); }

    // 3D-specific setters
    static void SetBaseColor(AppearanceType& c, Rgba8 v) noexcept requires std::same_as<DimensionT,Dim3> { if(IsSameColor(c.style.base_color,v)) return; c.style.base_color=v; MarkStyleRevisionDirty(c); }
    static void SetEmissiveColor(AppearanceType& c, Rgba8 v) noexcept requires std::same_as<DimensionT,Dim3> { if(IsSameColor(c.style.emissive_color,v)) return; c.style.emissive_color=v; MarkStyleRevisionDirty(c); }
    static void SetMetallic(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::clamp(v,0.0F,1.0F); if(c.style.metallic==cl) return; c.style.metallic=cl; MarkStyleRevisionDirty(c); }
    static void SetRoughness(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::clamp(v,0.0F,1.0F); if(c.style.roughness==cl) return; c.style.roughness=cl; MarkStyleRevisionDirty(c); }
    static void SetNormalScale(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::max(0.0F,v); if(c.style.normal_scale==cl) return; c.style.normal_scale=cl; MarkStyleRevisionDirty(c); }
    static void SetOcclusionStrength(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::clamp(v,0.0F,1.0F); if(c.style.occlusion_strength==cl) return; c.style.occlusion_strength=cl; MarkStyleRevisionDirty(c); }
    static void SetEmissiveIntensity(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::max(0.0F,v); if(c.style.emissive_intensity==cl) return; c.style.emissive_intensity=cl; MarkStyleRevisionDirty(c); }
    static void SetAlphaCutoff(AppearanceType& c, float v) noexcept requires std::same_as<DimensionT,Dim3> { float cl=std::clamp(v,0.0F,1.0F); if(c.style.alpha_cutoff==cl) return; c.style.alpha_cutoff=cl; MarkStyleRevisionDirty(c); }
    static void SetShadingModel(AppearanceType& c, AppearanceShadingModel3D m) noexcept requires std::same_as<DimensionT,Dim3> { if(c.style.shading_model==m) return; c.style.shading_model=m; MarkStyleRevisionDirty(c); }
    static void SetDoubleSided(AppearanceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.double_sided==en) return; c.style.double_sided=en; MarkStyleRevisionDirty(c); }
    static void SetCastShadow(AppearanceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.cast_shadow==en) return; c.style.cast_shadow=en; MarkStyleRevisionDirty(c); }
    static void SetReceiveShadow(AppearanceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.receive_shadow==en) return; c.style.receive_shadow=en; MarkStyleRevisionDirty(c); }
    static void SetTextureBaseColorId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.binding.texture_base_color_id==id) return; c.binding.texture_base_color_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureNormalId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.binding.texture_normal_id==id) return; c.binding.texture_normal_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureMetalRoughId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.binding.texture_metal_rough_id==id) return; c.binding.texture_metal_rough_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureOcclusionId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.binding.texture_occlusion_id==id) return; c.binding.texture_occlusion_id=id; MarkBindingRevisionDirty(c); }
    static void SetTextureEmissiveId(AppearanceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.binding.texture_emissive_id==id) return; c.binding.texture_emissive_id=id; MarkBindingRevisionDirty(c); }

private:
    [[nodiscard]] static bool IsSameColor(Rgba8 a, Rgba8 b) noexcept { return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a; }
    static void MarkStyleRevisionDirty(AppearanceType& c) noexcept { c.runtime.revision_style=NextAppearanceRevision(c.runtime.revision_style); MarkDirty(c,appearance_dirty_style_flag); }
    static void MarkBindingRevisionDirty(AppearanceType& c) noexcept { c.runtime.revision_binding=NextAppearanceRevision(c.runtime.revision_binding); MarkDirty(c,appearance_dirty_binding_flag); }
};

// ============================================================================
// Geometry system
// ============================================================================

template<DimensionTag DimensionT>
class GeometrySystem final {
public:
    using GeometryType = Geometry<DimensionT>; using RuntimeType = typename GeometryType::RuntimeType;

    static constexpr std::uint32_t sk_batch_b=14U, sk_minor_b=16U, sk_geom_b=16U, sk_mat_b=16U, sk_pass_b=2U;
    static constexpr std::uint32_t sk_batch_s=0U, sk_minor_s=sk_batch_s+sk_batch_b, sk_geom_s=sk_minor_s+sk_minor_b, sk_mat_s=sk_geom_s+sk_geom_b, sk_pass_s=sk_mat_s+sk_mat_b;
    static constexpr std::uint32_t sk_bind_s=sk_minor_s;
    static constexpr std::uint64_t sk_batch_m=(1ULL<<sk_batch_b)-1U, sk_minor_m=(1ULL<<sk_minor_b)-1U, sk_geom_m=(1ULL<<sk_geom_b)-1U, sk_mat_m=(1ULL<<sk_mat_b)-1U, sk_pass_m=(1ULL<<sk_pass_b)-1U;
    static_assert(sk_pass_b+sk_mat_b+sk_geom_b+sk_minor_b+sk_batch_b==64U);

    static void Initialize(GeometryType& c) noexcept {
        if constexpr (std::same_as<DimensionT,Dim2>) { c.path.size_bytes=0U; c.path.capacity_bytes=GeometryPathInlineData::inline_capacity_bytes; c.path.revision=0U; c.path.reserved=0U; std::fill(std::begin(c.path.bytes),std::end(c.path.bytes),std::uint8_t{0U}); }
        else { c.mesh.submesh_index=0U; c.mesh.lod_index=0U; c.mesh.flags=0U; }
        SetDefaultStyle(c); SetDefaultRuntime(c); RebuildSortKey(c);
    }

    static void SetDefaultStyle(GeometryType& c) noexcept {
        if constexpr (std::same_as<DimensionT,Dim2>) { c.style.stroke_width_px=1.0F; c.style.miter_limit=4.0F; c.style.fill_color=Rgba8{255U,255U,255U,255U}; c.style.stroke_color=Rgba8{0U,0U,0U,255U}; c.style.layer=0; c.style.topology=Geometry2DTopology::fill; c.style.fill_rule=Geometry2DFillRule::non_zero; c.style.line_join=Geometry2DLineJoin::miter; c.style.line_cap=Geometry2DLineCap::butt; c.style.antialiasing=1U; c.style.reserved0=0U; c.style.reserved1=0U; }
        else { c.style.albedo_color=Rgba8{255U,255U,255U,255U}; c.style.depth_test=1U; c.style.depth_write=1U; c.style.double_sided=0U; c.style.topology=Geometry3DTopology::triangles; c.style.shading_model=Geometry3DShadingModel::lit; c.style.cast_shadow=1U; c.style.receive_shadow=1U; c.style.reserved0=0U; c.style.metallic=0.0F; c.style.roughness=1.0F; c.style.normal_scale=1.0F; c.style.line_width=1.0F; }
        MarkDirty(c,geometry_dirty_style_flag|geometry_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(GeometryType& c) noexcept {
        c.runtime.route.sort_key=0U; c.runtime.route.geometry_id=0U; c.runtime.route.material_id=0U; c.runtime.route.batch_tag=0U; c.runtime.route.user_data=0U;
        c.runtime.route.appearance_handle=invalid_appearance_handle; c.runtime.route.appearance_pipeline_bucket=0U; c.runtime.route.appearance_resource_bucket=0U;
        c.runtime.route.depth_bin=0U; c.runtime.route.visible=1U;
        c.runtime.route.pass_hint=std::same_as<DimensionT,Dim2>?GeometryRenderPassHint::overlay:GeometryRenderPassHint::opaque;
        c.runtime.route.dirty_flags=geometry_dirty_data_flag|geometry_dirty_style_flag|geometry_dirty_runtime_flag|geometry_dirty_bounds_flag;
        if constexpr (std::same_as<DimensionT,Dim2>) { c.runtime.path_command_count=0U; c.runtime.tessellation_revision=0U; c.runtime.path_data_hash=0U; c.runtime.reserved0=0U; c.runtime.bounds_min=Float2{0,0}; c.runtime.bounds_max=Float2{0,0}; }
        else { c.runtime.mesh_revision=0U; c.runtime.meshlet_count_hint=0U; c.runtime.reserved0=0U; c.runtime.reserved1=0U; c.runtime.bounds_min=Float3{0,0,0}; c.runtime.bounds_max=Float3{0,0,0}; }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const GeometryType& c) noexcept { return c.runtime.route.dirty_flags; }
    [[nodiscard]] static bool HasDirtyFlags(const GeometryType& c, std::uint32_t m) noexcept { return (c.runtime.route.dirty_flags&m)!=0U; }
    static void MarkDirty(GeometryType& c, std::uint32_t m) noexcept { c.runtime.route.dirty_flags|=m; }
    static void ClearDirtyFlags(GeometryType& c, std::uint32_t m) noexcept { c.runtime.route.dirty_flags&=~m; }
    static void SetVisible(GeometryType& c, bool v) noexcept { c.runtime.route.visible=v?1U:0U; MarkDirty(c,geometry_dirty_runtime_flag); }
    static void SetRenderPassHint(GeometryType& c, GeometryRenderPassHint h) noexcept { c.runtime.route.pass_hint=h; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetRuntimeRoute(GeometryType& c, std::uint32_t gid, std::uint32_t mid, std::uint32_t bt) noexcept { c.runtime.route.geometry_id=gid; c.runtime.route.material_id=mid; c.runtime.route.batch_tag=bt; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetGeometryId(GeometryType& c, std::uint32_t id) noexcept { c.runtime.route.geometry_id=id; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetMaterialId(GeometryType& c, std::uint32_t id) noexcept { c.runtime.route.material_id=id; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetBatchTag(GeometryType& c, std::uint32_t t) noexcept { c.runtime.route.batch_tag=t; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetUserData(GeometryType& c, std::uint32_t d) noexcept { c.runtime.route.user_data=d; MarkDirty(c,geometry_dirty_runtime_flag); }
    static void SetAppearanceHandle(GeometryType& c, AppearanceHandle h) noexcept { if(c.runtime.route.appearance_handle.index==h.index&&c.runtime.route.appearance_handle.generation==h.generation) return; c.runtime.route.appearance_handle=h; MarkDirty(c,geometry_dirty_runtime_flag); }
    static void ClearAppearanceHandle(GeometryType& c) noexcept { if(c.runtime.route.appearance_handle.index==invalid_appearance_handle.index&&c.runtime.route.appearance_handle.generation==invalid_appearance_handle.generation&&c.runtime.route.appearance_pipeline_bucket==0U&&c.runtime.route.appearance_resource_bucket==0U) return; c.runtime.route.appearance_handle=invalid_appearance_handle; c.runtime.route.appearance_pipeline_bucket=0U; c.runtime.route.appearance_resource_bucket=0U; MarkDirty(c,geometry_dirty_runtime_flag); }
    [[nodiscard]] static bool SetAppearanceRuntimeLink(GeometryType& c, AppearanceHandle ah, std::uint64_t ask, std::uint64_t apk, std::uint64_t ark) noexcept {
        std::uint32_t pb=static_cast<std::uint32_t>(apk), rb=static_cast<std::uint32_t>(ark);
        bool ch=c.runtime.route.appearance_handle.index!=ah.index||c.runtime.route.appearance_handle.generation!=ah.generation||c.runtime.route.appearance_pipeline_bucket!=pb||c.runtime.route.appearance_resource_bucket!=rb||c.runtime.route.material_id!=rb||c.runtime.route.sort_key!=ask;
        if(!ch) return false; c.runtime.route.appearance_handle=ah; c.runtime.route.appearance_pipeline_bucket=pb; c.runtime.route.appearance_resource_bucket=rb; c.runtime.route.material_id=rb; c.runtime.route.sort_key=ask; MarkDirty(c,geometry_dirty_runtime_flag); return true;
    }
    static void SetDepthBin(GeometryType& c, std::uint16_t db) noexcept requires std::same_as<DimensionT,Dim3> { c.runtime.route.depth_bin=db; MarkDirty(c,geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetLayer(GeometryType& c, std::int16_t l) noexcept requires std::same_as<DimensionT,Dim2> { c.style.layer=l; MarkDirty(c,geometry_dirty_style_flag|geometry_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetBounds(GeometryType& c, const Float2& mn, const Float2& mx) noexcept requires std::same_as<DimensionT,Dim2> { c.runtime.bounds_min=mn; c.runtime.bounds_max=mx; MarkDirty(c,geometry_dirty_bounds_flag|geometry_dirty_runtime_flag); }
    static void SetBounds(GeometryType& c, const Float3& mn, const Float3& mx) noexcept requires std::same_as<DimensionT,Dim3> { c.runtime.bounds_min=mn; c.runtime.bounds_max=mx; MarkDirty(c,geometry_dirty_bounds_flag|geometry_dirty_runtime_flag); }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const GeometryType& c) noexcept {
        std::uint64_t pb=static_cast<std::uint64_t>(c.runtime.route.pass_hint)&sk_pass_m, mb=static_cast<std::uint64_t>(c.runtime.route.material_id)&sk_mat_m, gb=static_cast<std::uint64_t>(c.runtime.route.geometry_id)&sk_geom_m, bb=static_cast<std::uint64_t>(c.runtime.route.batch_tag)&sk_batch_m;
        std::uint64_t minor=0U;
        if constexpr (std::same_as<DimensionT,Dim2>) minor=(static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(c.style.layer)-static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min())))&sk_minor_m);
        else minor=static_cast<std::uint64_t>(c.runtime.route.depth_bin)&sk_minor_m;
        return (pb<<sk_pass_s)|(mb<<sk_mat_s)|(gb<<sk_geom_s)|(minor<<sk_minor_s)|(bb<<sk_batch_s);
    }
    static void RebuildSortKey(GeometryType& c) noexcept { c.runtime.route.sort_key=ComposeSortKey(c); }
    [[nodiscard]] static std::uint64_t SortKey(const GeometryType& c) noexcept { return c.runtime.route.sort_key; }
    [[nodiscard]] static std::uint64_t BindingSortKey(const GeometryType& c) noexcept { return BindingSortKey(c.runtime.route.sort_key); }
    [[nodiscard]] static std::uint64_t BindingSortKey(std::uint64_t sk) noexcept { return sk>>sk_bind_s; }
    [[nodiscard]] static std::uint32_t ExtractPassBucket(std::uint64_t sk) noexcept { return (sk>>sk_pass_s)&sk_pass_m; }
    [[nodiscard]] static std::uint32_t ExtractMaterialBucket(std::uint64_t sk) noexcept { return (sk>>sk_mat_s)&sk_mat_m; }
    [[nodiscard]] static std::uint32_t ExtractGeometryBucket(std::uint64_t sk) noexcept { return (sk>>sk_geom_s)&sk_geom_m; }
    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sk) noexcept { return (sk>>sk_minor_s)&sk_minor_m; }
    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sk) noexcept { return (sk>>sk_batch_s)&sk_batch_m; }
    [[nodiscard]] static bool IsVisibleForBatch(const GeometryType& c) noexcept { if constexpr (std::same_as<DimensionT,Dim2>) return c.runtime.route.visible!=0U&&c.path.size_bytes>0U; else return c.runtime.route.visible!=0U&&c.runtime.route.geometry_id!=0U; }
};

// ============================================================================
// Surface system
// ============================================================================

template<DimensionTag DimensionT>
class SurfaceSystem final {
public:
    using SurfaceType = Surface<DimensionT>; using RuntimeType = typename SurfaceType::RuntimeType;

    static constexpr std::uint32_t sk_batch_b=14U, sk_minor_b=16U, sk_surf_b=16U, sk_mat_b=16U, sk_pass_b=2U;
    static constexpr std::uint32_t sk_batch_s=0U, sk_minor_s=sk_batch_s+sk_batch_b, sk_surf_s=sk_minor_s+sk_minor_b, sk_mat_s=sk_surf_s+sk_surf_b, sk_pass_s=sk_mat_s+sk_mat_b;
    static constexpr std::uint32_t sk_bind_s=sk_surf_s;
    static constexpr std::uint64_t sk_batch_m=(1ULL<<sk_batch_b)-1U, sk_minor_m=(1ULL<<sk_minor_b)-1U, sk_surf_m=(1ULL<<sk_surf_b)-1U, sk_mat_m=(1ULL<<sk_mat_b)-1U, sk_pass_m=(1ULL<<sk_pass_b)-1U;
    static_assert(sk_pass_b+sk_mat_b+sk_surf_b+sk_minor_b+sk_batch_b==64U);

    static void Initialize(SurfaceType& c) noexcept { SetDefaultStyle(c); SetDefaultRuntime(c); RebuildSortKey(c); }

    static void SetDefaultStyle(SurfaceType& c) noexcept {
        if constexpr (std::same_as<DimensionT,Dim2>) { c.style.uv_u0=0.0F; c.style.uv_v0=0.0F; c.style.uv_u1=1.0F; c.style.uv_v1=1.0F; c.style.tint_color=Rgba8{255U,255U,255U,255U}; c.style.opacity=1.0F; c.style.layer=0; c.style.blend_mode=Surface2DBlendMode::alpha; c.style.flip_x=0U; c.style.flip_y=0U; c.style.premultiplied_alpha=0U; c.style.reserved0=0U; }
        else { c.style.tint_color=Rgba8{255U,255U,255U,255U}; c.style.uv_scale_u=1.0F; c.style.uv_scale_v=1.0F; c.style.uv_bias_u=0.0F; c.style.uv_bias_v=0.0F; c.style.opacity=1.0F; c.style.depth_test=1U; c.style.depth_write=0U; c.style.double_sided=0U; c.style.filter_mode=Surface3DFilterMode::linear; c.style.address_u=Surface3DAddressMode::wrap; c.style.address_v=Surface3DAddressMode::wrap; c.style.address_w=Surface3DAddressMode::wrap; c.style.reserved0=0U; }
        MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag);
    }

    static void SetDefaultRuntime(SurfaceType& c) noexcept {
        c.runtime.route.sort_key=0U; c.runtime.route.surface_id=0U; c.runtime.route.material_id=0U; c.runtime.route.batch_tag=0U; c.runtime.route.user_data=0U;
        c.runtime.route.appearance_handle=invalid_appearance_handle; c.runtime.route.appearance_pipeline_bucket=0U; c.runtime.route.appearance_resource_bucket=0U;
        c.runtime.route.depth_bin=0U; c.runtime.route.visible=1U;
        c.runtime.route.pass_hint=std::same_as<DimensionT,Dim2>?SurfaceRenderPassHint::overlay:SurfaceRenderPassHint::opaque;
        c.runtime.route.dirty_flags=surface_dirty_source_flag|surface_dirty_style_flag|surface_dirty_runtime_flag;
        if constexpr (std::same_as<DimensionT,Dim2>) { c.runtime.source.image_id=0U; c.runtime.source.sprite_id=0U; c.runtime.source.atlas_page_id=0U; c.runtime.source.source_kind=Surface2DSourceKind::none; c.runtime.source.reserved0=0U; c.runtime.source.reserved1=0U; c.runtime.source_revision=0U; c.runtime.reserved0=0U; c.runtime.size=Float2{0,0}; c.runtime.pivot=Float2{0.5F,0.5F}; }
        else { c.runtime.texture.texture_id=0U; c.runtime.texture.sampler_id=0U; c.runtime.texture.uv_set=0U; c.runtime.texture.flags=0U; c.runtime.texture_revision=0U; c.runtime.reserved0=0U; c.runtime.reserved1=0U; }
    }

    [[nodiscard]] static std::uint32_t DirtyFlags(const SurfaceType& c) noexcept { return c.runtime.route.dirty_flags; }
    [[nodiscard]] static bool HasDirtyFlags(const SurfaceType& c, std::uint32_t m) noexcept { return (c.runtime.route.dirty_flags&m)!=0U; }
    static void MarkDirty(SurfaceType& c, std::uint32_t m) noexcept { c.runtime.route.dirty_flags|=m; }
    static void ClearDirtyFlags(SurfaceType& c, std::uint32_t m) noexcept { c.runtime.route.dirty_flags&=~m; }
    static void SetVisible(SurfaceType& c, bool v) noexcept { std::uint8_t vv=v?1U:0U; if(c.runtime.route.visible==vv) return; c.runtime.route.visible=vv; MarkDirty(c,surface_dirty_runtime_flag); }
    static void SetRenderPassHint(SurfaceType& c, SurfaceRenderPassHint h) noexcept { if(c.runtime.route.pass_hint==h) return; c.runtime.route.pass_hint=h; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetRuntimeRoute(SurfaceType& c, std::uint32_t sid, std::uint32_t mid, std::uint32_t bt) noexcept { if(c.runtime.route.surface_id==sid&&c.runtime.route.material_id==mid&&c.runtime.route.batch_tag==bt) return; c.runtime.route.surface_id=sid; c.runtime.route.material_id=mid; c.runtime.route.batch_tag=bt; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetSurfaceId(SurfaceType& c, std::uint32_t id) noexcept { if(c.runtime.route.surface_id==id) return; c.runtime.route.surface_id=id; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetMaterialId(SurfaceType& c, std::uint32_t id) noexcept { if(c.runtime.route.material_id==id) return; c.runtime.route.material_id=id; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetBatchTag(SurfaceType& c, std::uint32_t t) noexcept { if(c.runtime.route.batch_tag==t) return; c.runtime.route.batch_tag=t; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetUserData(SurfaceType& c, std::uint32_t d) noexcept { if(c.runtime.route.user_data==d) return; c.runtime.route.user_data=d; MarkDirty(c,surface_dirty_runtime_flag); }
    static void SetAppearanceHandle(SurfaceType& c, AppearanceHandle h) noexcept { if(c.runtime.route.appearance_handle.index==h.index&&c.runtime.route.appearance_handle.generation==h.generation) return; c.runtime.route.appearance_handle=h; MarkDirty(c,surface_dirty_runtime_flag); }
    static void ClearAppearanceHandle(SurfaceType& c) noexcept { if(c.runtime.route.appearance_handle.index==invalid_appearance_handle.index&&c.runtime.route.appearance_handle.generation==invalid_appearance_handle.generation&&c.runtime.route.appearance_pipeline_bucket==0U&&c.runtime.route.appearance_resource_bucket==0U) return; c.runtime.route.appearance_handle=invalid_appearance_handle; c.runtime.route.appearance_pipeline_bucket=0U; c.runtime.route.appearance_resource_bucket=0U; MarkDirty(c,surface_dirty_runtime_flag); }
    [[nodiscard]] static bool SetAppearanceRuntimeLink(SurfaceType& c, AppearanceHandle ah, std::uint64_t ask, std::uint64_t apk, std::uint64_t ark) noexcept {
        std::uint32_t pb=static_cast<std::uint32_t>(apk), rb=static_cast<std::uint32_t>(ark);
        bool ch=c.runtime.route.appearance_handle.index!=ah.index||c.runtime.route.appearance_handle.generation!=ah.generation||c.runtime.route.appearance_pipeline_bucket!=pb||c.runtime.route.appearance_resource_bucket!=rb||c.runtime.route.material_id!=rb||c.runtime.route.sort_key!=ask;
        if(!ch) return false; c.runtime.route.appearance_handle=ah; c.runtime.route.appearance_pipeline_bucket=pb; c.runtime.route.appearance_resource_bucket=rb; c.runtime.route.material_id=rb; c.runtime.route.sort_key=ask; MarkDirty(c,surface_dirty_runtime_flag); return true;
    }
    static void SetDepthBin(SurfaceType& c, std::uint16_t db) noexcept requires std::same_as<DimensionT,Dim3> { if(c.runtime.route.depth_bin==db) return; c.runtime.route.depth_bin=db; MarkDirty(c,surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetOpacity(SurfaceType& c, float o) noexcept { float cl=std::clamp(o,0.0F,1.0F); if(c.style.opacity==cl) return; c.style.opacity=cl; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetTintColor(SurfaceType& c, Rgba8 v) noexcept { if(IsSameColor(c.style.tint_color,v)) return; c.style.tint_color=v; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetUvRect(SurfaceType& c, float u0,float v0,float u1,float v1) noexcept requires std::same_as<DimensionT,Dim2> { if(c.style.uv_u0==u0&&c.style.uv_v0==v0&&c.style.uv_u1==u1&&c.style.uv_v1==v1) return; c.style.uv_u0=u0;c.style.uv_v0=v0;c.style.uv_u1=u1;c.style.uv_v1=v1; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetLayer(SurfaceType& c, std::int16_t l) noexcept requires std::same_as<DimensionT,Dim2> { if(c.style.layer==l) return; c.style.layer=l; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetBlendMode(SurfaceType& c, Surface2DBlendMode m) noexcept requires std::same_as<DimensionT,Dim2> { if(c.style.blend_mode==m) return; c.style.blend_mode=m; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetFlip(SurfaceType& c, bool fx,bool fy) noexcept requires std::same_as<DimensionT,Dim2> { std::uint8_t x=fx?1U:0U,y=fy?1U:0U; if(c.style.flip_x==x&&c.style.flip_y==y) return; c.style.flip_x=x;c.style.flip_y=y; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetPremultipliedAlpha(SurfaceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim2> { std::uint8_t en=v?1U:0U; if(c.style.premultiplied_alpha==en) return; c.style.premultiplied_alpha=en; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetImageId(SurfaceType& c, std::uint32_t id, std::uint32_t ap=0U) noexcept requires std::same_as<DimensionT,Dim2> { bool ch=c.runtime.source.image_id!=id||c.runtime.source.atlas_page_id!=ap||c.runtime.source.source_kind!=Surface2DSourceKind::image||c.runtime.route.surface_id!=id; if(!ch) return; c.runtime.source.image_id=id;c.runtime.source.atlas_page_id=ap;c.runtime.source.source_kind=Surface2DSourceKind::image;c.runtime.route.surface_id=id;++c.runtime.source_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetSpriteId(SurfaceType& c, std::uint32_t id, std::uint32_t ap=0U) noexcept requires std::same_as<DimensionT,Dim2> { bool ch=c.runtime.source.sprite_id!=id||c.runtime.source.atlas_page_id!=ap||c.runtime.source.source_kind!=Surface2DSourceKind::sprite||c.runtime.route.surface_id!=id; if(!ch) return; c.runtime.source.sprite_id=id;c.runtime.source.atlas_page_id=ap;c.runtime.source.source_kind=Surface2DSourceKind::sprite;c.runtime.route.surface_id=id;++c.runtime.source_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetAtlasPageId(SurfaceType& c, std::uint32_t ap) noexcept requires std::same_as<DimensionT,Dim2> { if(c.runtime.source.atlas_page_id==ap) return; c.runtime.source.atlas_page_id=ap;++c.runtime.source_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); }
    static void SetSize(SurfaceType& c, const Float2& s) noexcept requires std::same_as<DimensionT,Dim2> { if(c.runtime.size.x==s.x&&c.runtime.size.y==s.y) return; c.runtime.size=s; MarkDirty(c,surface_dirty_runtime_flag); }
    static void SetPivot(SurfaceType& c, const Float2& p) noexcept requires std::same_as<DimensionT,Dim2> { if(c.runtime.pivot.x==p.x&&c.runtime.pivot.y==p.y) return; c.runtime.pivot=p; MarkDirty(c,surface_dirty_runtime_flag); }
    static void SetTextureId(SurfaceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { bool ch=c.runtime.texture.texture_id!=id||c.runtime.route.surface_id!=id; if(!ch) return; c.runtime.texture.texture_id=id;c.runtime.route.surface_id=id;++c.runtime.texture_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetSamplerId(SurfaceType& c, std::uint32_t id) noexcept requires std::same_as<DimensionT,Dim3> { if(c.runtime.texture.sampler_id==id) return; c.runtime.texture.sampler_id=id;++c.runtime.texture_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); }
    static void SetTextureRoute(SurfaceType& c, std::uint32_t tid,std::uint32_t sid,std::uint16_t uv,std::uint16_t fl) noexcept requires std::same_as<DimensionT,Dim3> { bool ch=c.runtime.texture.texture_id!=tid||c.runtime.texture.sampler_id!=sid||c.runtime.texture.uv_set!=uv||c.runtime.texture.flags!=fl||c.runtime.route.surface_id!=tid; if(!ch) return; c.runtime.texture.texture_id=tid;c.runtime.texture.sampler_id=sid;c.runtime.texture.uv_set=uv;c.runtime.texture.flags=fl;c.runtime.route.surface_id=tid;++c.runtime.texture_revision; MarkDirty(c,surface_dirty_source_flag|surface_dirty_runtime_flag); RebuildSortKey(c); }
    static void SetUvTransform(SurfaceType& c, float su,float sv,float bu,float bv) noexcept requires std::same_as<DimensionT,Dim3> { if(c.style.uv_scale_u==su&&c.style.uv_scale_v==sv&&c.style.uv_bias_u==bu&&c.style.uv_bias_v==bv) return; c.style.uv_scale_u=su;c.style.uv_scale_v=sv;c.style.uv_bias_u=bu;c.style.uv_bias_v=bv; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetDepthTest(SurfaceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.depth_test==en) return; c.style.depth_test=en; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetDepthWrite(SurfaceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.depth_write==en) return; c.style.depth_write=en; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetDoubleSided(SurfaceType& c, bool v) noexcept requires std::same_as<DimensionT,Dim3> { std::uint8_t en=v?1U:0U; if(c.style.double_sided==en) return; c.style.double_sided=en; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetFilterMode(SurfaceType& c, Surface3DFilterMode m) noexcept requires std::same_as<DimensionT,Dim3> { if(c.style.filter_mode==m) return; c.style.filter_mode=m; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }
    static void SetAddressMode(SurfaceType& c, Surface3DAddressMode au,Surface3DAddressMode av,Surface3DAddressMode aw) noexcept requires std::same_as<DimensionT,Dim3> { if(c.style.address_u==au&&c.style.address_v==av&&c.style.address_w==aw) return; c.style.address_u=au;c.style.address_v=av;c.style.address_w=aw; MarkDirty(c,surface_dirty_style_flag|surface_dirty_runtime_flag); }

    [[nodiscard]] static std::uint64_t ComposeSortKey(const SurfaceType& c) noexcept {
        std::uint64_t pb=static_cast<std::uint64_t>(c.runtime.route.pass_hint)&sk_pass_m, mb=static_cast<std::uint64_t>(c.runtime.route.material_id)&sk_mat_m, sb=static_cast<std::uint64_t>(c.runtime.route.surface_id)&sk_surf_m, bb=static_cast<std::uint64_t>(c.runtime.route.batch_tag)&sk_batch_m;
        std::uint64_t minor=0U;
        if constexpr (std::same_as<DimensionT,Dim2>) minor=(static_cast<std::uint64_t>(static_cast<std::uint32_t>(static_cast<std::int32_t>(c.style.layer)-static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min())))&sk_minor_m);
        else minor=static_cast<std::uint64_t>(c.runtime.route.depth_bin)&sk_minor_m;
        return (pb<<sk_pass_s)|(mb<<sk_mat_s)|(sb<<sk_surf_s)|(minor<<sk_minor_s)|(bb<<sk_batch_s);
    }
    static void RebuildSortKey(SurfaceType& c) noexcept { c.runtime.route.sort_key=ComposeSortKey(c); }
    [[nodiscard]] static std::uint64_t SortKey(const SurfaceType& c) noexcept { return c.runtime.route.sort_key; }
    [[nodiscard]] static std::uint64_t BindingSortKey(const SurfaceType& c) noexcept { return BindingSortKey(c.runtime.route.sort_key); }
    [[nodiscard]] static std::uint64_t BindingSortKey(std::uint64_t sk) noexcept { return sk>>sk_bind_s; }
    [[nodiscard]] static std::uint32_t ExtractPassBucket(std::uint64_t sk) noexcept { return (sk>>sk_pass_s)&sk_pass_m; }
    [[nodiscard]] static std::uint32_t ExtractMaterialBucket(std::uint64_t sk) noexcept { return (sk>>sk_mat_s)&sk_mat_m; }
    [[nodiscard]] static std::uint32_t ExtractSurfaceBucket(std::uint64_t sk) noexcept { return (sk>>sk_surf_s)&sk_surf_m; }
    [[nodiscard]] static std::uint32_t ExtractMinorBucket(std::uint64_t sk) noexcept { return (sk>>sk_minor_s)&sk_minor_m; }
    [[nodiscard]] static std::uint32_t ExtractBatchBucket(std::uint64_t sk) noexcept { return (sk>>sk_batch_s)&sk_batch_m; }
    [[nodiscard]] static bool IsVisibleForBatch(const SurfaceType& c) noexcept {
        if(c.runtime.route.visible==0U) return false;
        if constexpr (std::same_as<DimensionT,Dim2>) { switch(c.runtime.source.source_kind){case Surface2DSourceKind::image:return c.runtime.source.image_id!=0U; case Surface2DSourceKind::sprite:return c.runtime.source.sprite_id!=0U; default:return c.runtime.route.surface_id!=0U;} }
        else return c.runtime.texture.texture_id!=0U;
    }
private:
    [[nodiscard]] static bool IsSameColor(const Rgba8& a, const Rgba8& b) noexcept { return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a; }
};

// ============================================================================
// Appearance runtime system
// ============================================================================

struct AppearancePipelinePolicy final { std::uint16_t pipeline_domain_id; std::uint16_t pass_id; std::uint8_t queue_id; std::uint8_t reserved0; std::uint16_t reserved1; };
struct AppearanceSortPolicy final { std::uint8_t queue_bucket; std::uint8_t reserved0; std::uint16_t default_depth_bucket; std::uint16_t tie_breaker_seed; std::uint16_t pipeline_bucket_override; };
struct AppearanceRuntimeBuildConfig final { bool force_full_rebuild; bool rebuild_keys_even_if_clean; std::uint32_t merge_gap; };
struct AppearanceRuntimeBuildHint final { const std::uint32_t* dirty_component_indices; std::uint32_t dirty_component_count; std::uint8_t use_dirty_component_indices; std::uint8_t reserved0; std::uint16_t reserved1; };
struct AppearanceUploadRange final { std::uint32_t begin_index; std::uint32_t count; };
struct AppearanceRuntimeBuildStats final { std::uint32_t component_count; std::uint32_t scanned_count; std::uint32_t visible_count; std::uint32_t updated_record_count; std::uint32_t updated_key_count; std::uint32_t upload_range_count; std::uint32_t out_of_range_dirty_count; std::uint32_t cache_epoch; std::uint8_t full_rebuild; std::uint8_t used_dirty_indices; std::uint16_t reserved0; };

template<DimensionTag DimensionT> struct AppearanceGpuRecord;
template<> struct alignas(16) AppearanceGpuRecord<Dim2> final {
    std::array<float,4U> fill_rgba; std::array<float,4U> stroke_rgba; std::array<float,4U> gradient_line; std::array<float,4U> params; std::array<std::uint32_t,4U> misc_u32; std::array<std::uint32_t,4U> textures_u32;
};
template<> struct alignas(16) AppearanceGpuRecord<Dim3> final {
    std::array<float,4U> base_rgba; std::array<float,4U> emissive_rgba; std::array<float,4U> material_params; std::array<float,4U> extras; std::array<std::uint32_t,4U> flags_u32; std::array<std::uint32_t,4U> textures0_u32; std::array<std::uint32_t,4U> textures1_u32;
};
static_assert(alignof(AppearanceGpuRecord<Dim2>)==16U&&alignof(AppearanceGpuRecord<Dim3>)==16U);
static_assert((sizeof(AppearanceGpuRecord<Dim2>)%16U)==0U&&(sizeof(AppearanceGpuRecord<Dim3>)%16U)==0U);
static_assert(PurePodAppearanceComponent<AppearanceGpuRecord<Dim2>>&&PurePodAppearanceComponent<AppearanceGpuRecord<Dim3>>);
static_assert(PurePodAppearanceComponent<AppearanceUploadRange>&&PurePodAppearanceComponent<AppearanceRuntimeBuildStats>);

template<DimensionTag DimensionT> struct AppearanceRuntimeCache { AppearancePipelinePolicy pipeline_policy; AppearanceSortPolicy sort_policy; std::uint32_t component_count; std::uint32_t epoch; bool valid; };
template<DimensionTag DimensionT>
struct AppearanceRuntimeScratch final {
    vr::McVector<AppearanceGpuRecord<DimensionT>> gpu_records{}; vr::McVector<AppearanceUploadRange> upload_ranges{};
    vr::McVector<std::uint32_t> dirty_component_indices{}; vr::McVector<std::uint32_t> handle_generations{};
    AppearanceRuntimeCache<DimensionT> cache{};
};

[[nodiscard]] constexpr std::uint32_t NextAppearanceRuntimeEpoch(std::uint32_t e) noexcept { return (e==(std::numeric_limits<std::uint32_t>::max)())?1U:(e+1U); }

template<DimensionTag DimensionT>
class AppearanceRuntimeSystem final {
public:
    using AppearanceType = Appearance<DimensionT>; using AppearanceSystemType = AppearanceSystem<DimensionT>;
    using ScratchType = AppearanceRuntimeScratch<DimensionT>; using GpuRecordType = AppearanceGpuRecord<DimensionT>;

    static constexpr std::uint32_t sk_tie_b=12U, sk_depth_b=16U, sk_pipe_b=16U, sk_layer_b=16U, sk_queue_b=4U;
    static constexpr std::uint32_t sk_tie_s=0U, sk_depth_s=sk_tie_s+sk_tie_b, sk_pipe_s=sk_depth_s+sk_depth_b, sk_layer_s=sk_pipe_s+sk_pipe_b, sk_queue_s=sk_layer_s+sk_layer_b;
    static constexpr std::uint64_t sk_tie_m=(1ULL<<sk_tie_b)-1U, sk_depth_m=(1ULL<<sk_depth_b)-1U, sk_pipe_m=(1ULL<<sk_pipe_b)-1U, sk_layer_m=(1ULL<<sk_layer_b)-1U, sk_queue_m=(1ULL<<sk_queue_b)-1U;
    static_assert(sk_queue_b+sk_layer_b+sk_pipe_b+sk_depth_b+sk_tie_b==64U);

    [[nodiscard]] static constexpr AppearancePipelinePolicy DefaultPipelinePolicy() noexcept { return {0U,0U,0U,0U,0U}; }
    [[nodiscard]] static constexpr AppearanceSortPolicy DefaultSortPolicy() noexcept { return {0U,0U,0U,0U,0xFFFFU}; }
    [[nodiscard]] static constexpr AppearanceRuntimeBuildConfig DefaultBuildConfig() noexcept { return {false,false,0U}; }
    [[nodiscard]] static constexpr AppearanceRuntimeBuildHint DefaultBuildHint() noexcept { return {nullptr,0U,0U,0U,0U}; }

    static void Reserve(ScratchType& s, std::uint32_t n) { auto r=(std::size_t)n; s.gpu_records.reserve(r); s.dirty_component_indices.reserve(r); s.handle_generations.reserve(r); s.upload_ranges.reserve(r); }

    [[nodiscard]] static AppearanceRuntimeBuildStats Build(AppearanceType* comps, std::uint32_t n, ScratchType& s,
        const AppearancePipelinePolicy& pp=DefaultPipelinePolicy(), const AppearanceSortPolicy& sp=DefaultSortPolicy(),
        const AppearanceRuntimeBuildConfig& bc=DefaultBuildConfig(), const AppearanceRuntimeBuildHint& bh=DefaultBuildHint());

    [[nodiscard]] static const GpuRecordType* GpuRecords(const ScratchType& s) noexcept { return s.gpu_records.data(); }
    [[nodiscard]] static std::uint32_t GpuRecordCount(const ScratchType& s) noexcept { return (std::uint32_t)s.gpu_records.size(); }
    [[nodiscard]] static const AppearanceUploadRange* UploadRanges(const ScratchType& s) noexcept { return s.upload_ranges.data(); }
    [[nodiscard]] static std::uint32_t UploadRangeCount(const ScratchType& s) noexcept { return (std::uint32_t)s.upload_ranges.size(); }
    [[nodiscard]] static std::uint32_t ExtractSortQueue(std::uint64_t sk) noexcept { return (sk>>sk_queue_s)&sk_queue_m; }
    [[nodiscard]] static std::uint32_t ExtractSortLayer(std::uint64_t sk) noexcept { return (sk>>sk_layer_s)&sk_layer_m; }
    [[nodiscard]] static std::uint32_t ExtractSortPipelineBucket(std::uint64_t sk) noexcept { return (sk>>sk_pipe_s)&sk_pipe_m; }
    [[nodiscard]] static std::uint32_t ExtractSortDepth(std::uint64_t sk) noexcept { return (sk>>sk_depth_s)&sk_depth_m; }
    [[nodiscard]] static std::uint32_t ExtractSortTieBreaker(std::uint64_t sk) noexcept { return (sk>>sk_tie_s)&sk_tie_m; }
};

// ============================================================================
// Appearance link system
// ============================================================================

struct AppearanceLinkStats final {
    std::uint32_t scanned_count; std::uint32_t resolved_count; std::uint32_t updated_count; std::uint32_t missing_handle_count;
    std::uint32_t out_of_range_handle_count; std::uint32_t generation_mismatch_count; std::uint32_t invisible_appearance_count; std::uint32_t out_of_range_component_index_count;
};
static_assert(PurePodAppearanceComponent<AppearanceLinkStats>);

template<DimensionTag DimensionT>
class AppearanceLinkSystem final {
public:
    using AppearanceType = Appearance<DimensionT>; using AppearanceSystemType = AppearanceSystem<DimensionT>;
    using GeometryType = Geometry<DimensionT>; using GeometrySystemType = GeometrySystem<DimensionT>;
    using SurfaceType = Surface<DimensionT>; using SurfaceSystemType = SurfaceSystem<DimensionT>;

    [[nodiscard]] static AppearanceLinkStats ApplyToGeometryAligned(GeometryType* gc, std::uint32_t gn, const AppearanceType* ac, std::uint32_t an) noexcept {
        AppearanceLinkStats st{}; if(gc==nullptr||gn==0U) return st;
        for(std::uint32_t i=0U;i<gn;++i) LinkOneGeometry(gc[i],ac,an,st); return st;
    }
    [[nodiscard]] static AppearanceLinkStats ApplyToGeometryAligned(GeometryType* gc, std::uint32_t gn, const AppearanceType* ac, std::uint32_t an, const std::uint32_t* gi, std::uint32_t gic) noexcept {
        AppearanceLinkStats st{}; if(gc==nullptr||gn==0U) return st; if(gi==nullptr||gic==0U) return ApplyToGeometryAligned(gc,gn,ac,an);
        for(std::uint32_t i=0U;i<gic;++i) { std::uint32_t idx=gi[i]; if(idx>=gn){++st.out_of_range_component_index_count;continue;} LinkOneGeometry(gc[idx],ac,an,st); } return st;
    }
    [[nodiscard]] static AppearanceLinkStats ApplyToSurfaceAligned(SurfaceType* sc, std::uint32_t sn, const AppearanceType* ac, std::uint32_t an) noexcept {
        AppearanceLinkStats st{}; if(sc==nullptr||sn==0U) return st;
        for(std::uint32_t i=0U;i<sn;++i) LinkOneSurface(sc[i],ac,an,st); return st;
    }
    [[nodiscard]] static AppearanceLinkStats ApplyToSurfaceAligned(SurfaceType* sc, std::uint32_t sn, const AppearanceType* ac, std::uint32_t an, const std::uint32_t* si, std::uint32_t sic) noexcept {
        AppearanceLinkStats st{}; if(sc==nullptr||sn==0U) return st; if(si==nullptr||sic==0U) return ApplyToSurfaceAligned(sc,sn,ac,an);
        for(std::uint32_t i=0U;i<sic;++i) { std::uint32_t idx=si[i]; if(idx>=sn){++st.out_of_range_component_index_count;continue;} LinkOneSurface(sc[idx],ac,an,st); } return st;
    }

private:
    static void LinkOneGeometry(GeometryType& gc, const AppearanceType* ac, std::uint32_t an, AppearanceLinkStats& st) noexcept {
        ++st.scanned_count; AppearanceHandle h=gc.runtime.route.appearance_handle;
        if(h.index==invalid_appearance_index||h.generation==0U){++st.missing_handle_count;return;}
        if(ac==nullptr||h.index>=an){++st.out_of_range_handle_count;return;}
        const AppearanceType& aco=ac[h.index];
        if(aco.runtime.gpu_record_handle.index!=h.index||aco.runtime.gpu_record_handle.generation!=h.generation){++st.generation_mismatch_count;return;}
        if(!AppearanceSystemType::IsVisibleForBatch(aco)){++st.invisible_appearance_count;return;}
        ++st.resolved_count;
        if(GeometrySystemType::SetAppearanceRuntimeLink(gc,h,aco.runtime.sort_key,aco.runtime.pipeline_key,aco.runtime.resource_key)) ++st.updated_count;
    }
    static void LinkOneSurface(SurfaceType& sc, const AppearanceType* ac, std::uint32_t an, AppearanceLinkStats& st) noexcept {
        ++st.scanned_count; AppearanceHandle h=sc.runtime.route.appearance_handle;
        if(h.index==invalid_appearance_index||h.generation==0U){++st.missing_handle_count;return;}
        if(ac==nullptr||h.index>=an){++st.out_of_range_handle_count;return;}
        const AppearanceType& aco=ac[h.index];
        if(aco.runtime.gpu_record_handle.index!=h.index||aco.runtime.gpu_record_handle.generation!=h.generation){++st.generation_mismatch_count;return;}
        if(!AppearanceSystemType::IsVisibleForBatch(aco)){++st.invisible_appearance_count;return;}
        ++st.resolved_count;
        if(SurfaceSystemType::SetAppearanceRuntimeLink(sc,h,aco.runtime.sort_key,aco.runtime.pipeline_key,aco.runtime.resource_key)) ++st.updated_count;
    }
};

// ============================================================================
// Geometry path command types (needed by geometry runtime system)
// ============================================================================

enum class GeometryPathCommandType : std::uint8_t { move_to = 0U, line_to = 1U, quad_to = 2U, cubic_to = 3U, close = 4U };
struct GeometryPathCommandHeader final { GeometryPathCommandType type; std::uint8_t command_size_bytes; std::uint16_t reserved0; };
struct GeometryPathMoveToCommand final { GeometryPathCommandHeader header; Float2 to; };
struct GeometryPathLineToCommand final { GeometryPathCommandHeader header; Float2 to; };
struct GeometryPathQuadToCommand final { GeometryPathCommandHeader header; Float2 control; Float2 to; };
struct GeometryPathCubicToCommand final { GeometryPathCommandHeader header; Float2 control0; Float2 control1; Float2 to; };
struct GeometryPathCloseCommand final { GeometryPathCommandHeader header; };
struct GeometryPathCommandView final { GeometryPathCommandType type = GeometryPathCommandType::move_to; const std::uint8_t* bytes = nullptr; std::uint32_t size_bytes = 0U; };

// ============================================================================
// Geometry batch system (POD types + scratch, used by geometry runtime)
// ============================================================================

struct GeometryBatchItem final { std::uint64_t sort_key; std::uint32_t component_index; std::uint32_t reserved0; };
struct GeometryBatchBuildStats final { std::uint32_t total_count; std::uint32_t scanned_count; std::uint32_t visible_count; std::uint32_t hidden_count; std::uint32_t empty_count; std::uint32_t out_of_range_candidate_count; std::uint8_t used_candidate_indices; std::uint8_t reserved0; std::uint16_t reserved1; };
static_assert(PurePodGeometryComponent<GeometryBatchItem>);
static_assert(PurePodGeometryComponent<GeometryBatchBuildStats>);

template<DimensionTag DimensionT>
struct GeometryBatchScratch final {
    vr::McVector<GeometryBatchItem> visible_items{}; vr::McVector<GeometryBatchItem> radix_scratch{}; vr::McVector<std::uint32_t> ordered_indices{};
};

template<DimensionTag DimensionT>
class GeometryBatchSystem final {
public:
    using GeometryType = Geometry<DimensionT>; using GeometrySystemType = GeometrySystem<DimensionT>; using ScratchType = GeometryBatchScratch<DimensionT>;
    static constexpr std::uint32_t radix_bits_per_pass = 8U;
    static void Reserve(ScratchType& s, std::uint32_t n) { auto r=(std::size_t)n; s.visible_items.reserve(r); s.radix_scratch.reserve(r); s.ordered_indices.reserve(r); }
    [[nodiscard]] static GeometryBatchBuildStats BuildVisibleItems(const GeometryType*, std::uint32_t, ScratchType&, bool = false);
    [[nodiscard]] static GeometryBatchBuildStats BuildVisibleItemsFromCandidates(const GeometryType*, std::uint32_t, const std::uint32_t*, std::uint32_t, ScratchType&, bool = false);
    [[nodiscard]] static GeometryBatchBuildStats BuildAndSort(const GeometryType*, std::uint32_t, ScratchType&, bool = true);
    [[nodiscard]] static GeometryBatchBuildStats BuildAndSortFromCandidates(const GeometryType*, std::uint32_t, const std::uint32_t*, std::uint32_t, ScratchType&, bool = true);
    [[nodiscard]] static std::uint32_t VisibleCount(const ScratchType&) noexcept;
    [[nodiscard]] static const GeometryBatchItem* SortedItems(const ScratchType&) noexcept;
    [[nodiscard]] static const std::uint32_t* OrderedIndices(const ScratchType&) noexcept;
    [[nodiscard]] static std::uint32_t OrderedIndexCount(const ScratchType&) noexcept;
};

// ============================================================================
// Surface batch system (POD types + scratch, used by surface runtime)
// ============================================================================

struct SurfaceBatchItem final { std::uint64_t sort_key; std::uint32_t component_index; std::uint32_t reserved0; };
struct SurfaceBatchBuildStats final { std::uint32_t total_count; std::uint32_t scanned_count; std::uint32_t visible_count; std::uint32_t hidden_count; std::uint32_t missing_source_count; std::uint32_t out_of_range_candidate_count; std::uint8_t used_candidate_indices; std::uint8_t reserved0; std::uint16_t reserved1; };
static_assert(PurePodSurfaceComponent<SurfaceBatchItem>);
static_assert(PurePodSurfaceComponent<SurfaceBatchBuildStats>);

template<DimensionTag DimensionT>
struct SurfaceBatchScratch final {
    vr::McVector<SurfaceBatchItem> visible_items{}; vr::McVector<SurfaceBatchItem> radix_scratch{}; vr::McVector<std::uint32_t> ordered_indices{};
};

template<DimensionTag DimensionT>
class SurfaceBatchSystem final {
public:
    using SurfaceType = Surface<DimensionT>; using SurfaceSystemType = SurfaceSystem<DimensionT>; using ScratchType = SurfaceBatchScratch<DimensionT>;
    static constexpr std::uint32_t radix_bits_per_pass = 8U;
    static void Reserve(ScratchType& s, std::uint32_t n) { auto r=(std::size_t)n; s.visible_items.reserve(r); s.radix_scratch.reserve(r); s.ordered_indices.reserve(r); }
    [[nodiscard]] static SurfaceBatchBuildStats BuildVisibleItems(const SurfaceType*, std::uint32_t, ScratchType&, bool = false);
    [[nodiscard]] static SurfaceBatchBuildStats BuildVisibleItemsFromCandidates(const SurfaceType*, std::uint32_t, const std::uint32_t*, std::uint32_t, ScratchType&, bool = false);
    [[nodiscard]] static SurfaceBatchBuildStats BuildAndSort(const SurfaceType*, std::uint32_t, ScratchType&, bool = true);
    [[nodiscard]] static SurfaceBatchBuildStats BuildAndSortFromCandidates(const SurfaceType*, std::uint32_t, const std::uint32_t*, std::uint32_t, ScratchType&, bool = true);
    [[nodiscard]] static std::uint32_t VisibleCount(const ScratchType&) noexcept;
    [[nodiscard]] static const SurfaceBatchItem* SortedItems(const ScratchType&) noexcept;
    [[nodiscard]] static const std::uint32_t* OrderedIndices(const ScratchType&) noexcept;
    [[nodiscard]] static std::uint32_t OrderedIndexCount(const ScratchType&) noexcept;
};

// ============================================================================
// Geometry runtime system (POD/Runtime types)
// ============================================================================

enum class GeometryRuntimeCacheStatus : std::uint8_t { miss = 0U, hit_reused = 1U, hit_partial_update = 2U };
enum class GeometryRuntimeCacheMissReason : std::uint8_t { none = 0U, invalid_input = 1U, cold_start = 2U, components_pointer_changed = 3U, transforms_pointer_changed = 4U, component_count_changed = 5U, geometry_signature_changed = 6U, visibility_signature_changed = 7U, transform_signature_changed = 8U, build_config_changed = 9U };

struct Geometry2DPathPrimitive final { float x0; float y0; float x1; float y1; std::uint32_t fill_color_rgba8; std::uint32_t stroke_color_rgba8; float stroke_width_px; std::uint32_t params; std::uint32_t component_index; std::uint32_t user_data; };
struct Geometry2DDrawBatch final { std::uint64_t sort_key; std::uint32_t primitive_begin; std::uint32_t primitive_count; std::uint32_t geometry_id; std::uint32_t material_id; std::uint32_t first_component_index; std::uint32_t params; };
struct Geometry2DRuntimeBuildConfig final { std::uint32_t quad_subdivision = 8U; std::uint32_t cubic_subdivision = 12U; std::uint32_t max_primitives_per_component = 0U; float zero_length_epsilon = 1e-6F; bool build_ordered_indices = true; };
struct Geometry2DRuntimeBuildHint final { std::uint64_t external_build_signature = 0U; std::uint8_t use_external_build_signature = 0U; std::uint8_t reserved0 = 0U; std::uint16_t reserved1 = 0U; std::uint32_t reserved2 = 0U; };
struct Geometry2DRuntimeBuildStats final { GeometryBatchBuildStats batch{}; std::uint32_t emitted_primitive_count = 0U; std::uint32_t emitted_batch_count = 0U; std::uint32_t approximated_quad_count = 0U; std::uint32_t approximated_cubic_count = 0U; std::uint32_t truncated_component_count = 0U; std::uint32_t cache_epoch = 0U; std::uint64_t build_signature = 0U; GeometryRuntimeCacheStatus cache_status = GeometryRuntimeCacheStatus::miss; GeometryRuntimeCacheMissReason cache_miss_reason = GeometryRuntimeCacheMissReason::none; bool cache_reused = false; bool cache_valid_before_build = false; bool cache_key_matched = false; bool signature_from_hint = false; };
struct Geometry2DRuntimeCache final { const Geometry<Dim2>* components = nullptr; std::uint32_t component_count = 0U; std::uint64_t signature = 0U; std::uint32_t epoch = 0U; Geometry2DRuntimeBuildConfig build_config{}; Geometry2DRuntimeBuildStats last_stats{}; bool valid = false; };
struct Geometry2DRuntimeScratch final { GeometryBatchScratch<Dim2> batch_scratch{}; vr::McVector<Geometry2DPathPrimitive> primitives{}; vr::McVector<Geometry2DDrawBatch> draw_batches{}; Geometry2DRuntimeCache cache{}; };

struct Geometry3DGpuInstance final { float world_m00; float world_m01; float world_m02; float world_m03; float world_m10; float world_m11; float world_m12; float world_m13; float world_m20; float world_m21; float world_m22; float world_m23; float world_m30; float world_m31; float world_m32; float world_m33; float bounds_min_x; float bounds_min_y; float bounds_min_z; float reserved0; float bounds_max_x; float bounds_max_y; float bounds_max_z; float reserved1; float metallic; float roughness; float normal_scale; float line_width; std::uint32_t albedo_rgba8; std::uint32_t params; std::uint32_t geometry_id; std::uint32_t material_id; std::uint32_t submesh_index; std::uint32_t component_index; std::uint32_t user_data; std::uint32_t reserved2; };
struct Geometry3DDrawBatch final { std::uint64_t sort_key; std::uint32_t instance_begin; std::uint32_t instance_count; std::uint32_t geometry_id; std::uint32_t material_id; std::uint32_t submesh_index; std::uint32_t first_component_index; std::uint32_t params; };
struct Geometry3DRuntimeBuildConfig final { bool build_ordered_indices = true; };
struct Geometry3DRuntimeBuildHint final { std::uint64_t external_geometry_signature = 0U; std::uint64_t external_transform_signature = 0U; std::uint64_t external_visible_set_signature = 0U; const std::uint32_t* transform_dirty_component_indices = nullptr; const std::uint32_t* visible_component_indices = nullptr; std::uint32_t transform_dirty_component_count = 0U; std::uint32_t visible_component_count = 0U; std::uint8_t use_external_geometry_signature = 0U; std::uint8_t use_external_transform_signature = 0U; std::uint8_t use_visible_component_indices = 0U; std::uint8_t use_external_visible_set_signature = 0U; };
struct Geometry3DRuntimeBuildStats final { GeometryBatchBuildStats batch{}; std::uint32_t candidate_component_count = 0U; std::uint32_t emitted_instance_count = 0U; std::uint32_t emitted_batch_count = 0U; std::uint32_t transform_rewritten_instance_count = 0U; std::uint32_t depth_test_batch_count = 0U; std::uint32_t depth_write_batch_count = 0U; std::uint32_t shadow_cast_batch_count = 0U; std::uint32_t cache_epoch = 0U; std::uint64_t geometry_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; GeometryRuntimeCacheStatus cache_status = GeometryRuntimeCacheStatus::miss; GeometryRuntimeCacheMissReason cache_miss_reason = GeometryRuntimeCacheMissReason::none; bool cache_reused = false; bool transform_only_update = false; bool used_visible_component_indices = false; bool cache_valid_before_build = false; bool cache_key_matched = false; bool geometry_signature_from_hint = false; bool transform_signature_from_hint = false; bool visible_set_signature_from_hint = false; bool transform_update_from_dirty_hint = false; };
struct Geometry3DRuntimeCache final { const Geometry<Dim3>* components = nullptr; const Transform<Dim3>* transforms = nullptr; std::uint32_t component_count = 0U; std::uint32_t candidate_component_count = 0U; std::uint64_t geometry_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; std::uint32_t epoch = 0U; vr::McVector<std::uint32_t> instance_world_revisions{}; vr::McVector<std::uint32_t> component_to_instance_index{}; Geometry3DRuntimeBuildConfig build_config{}; Geometry3DRuntimeBuildStats last_stats{}; bool valid = false; };
struct Geometry3DRuntimeScratch final { GeometryBatchScratch<Dim3> batch_scratch{}; vr::McVector<Geometry3DGpuInstance> instances{}; vr::McVector<Geometry3DDrawBatch> draw_batches{}; Geometry3DRuntimeCache cache{}; };

static_assert(PurePodGeometryComponent<Geometry2DPathPrimitive>);
static_assert(PurePodGeometryComponent<Geometry2DDrawBatch>);
static_assert(PurePodGeometryComponent<Geometry3DGpuInstance>);
static_assert(PurePodGeometryComponent<Geometry3DDrawBatch>);
static_assert(alignof(Geometry3DGpuInstance) == 4U);

// Forward declarations: full implementations deferred to impl units
template<DimensionTag DimensionT> class GeometryRuntimeSystem;

// ============================================================================
// Surface runtime system (POD/Runtime types)
// ============================================================================

enum class SurfaceRuntimeCacheStatus : std::uint8_t { miss = 0U, hit_reused = 1U, hit_partial_update = 2U };
enum class SurfaceRuntimeCacheMissReason : std::uint8_t { none = 0U, invalid_input = 1U, cold_start = 2U, components_pointer_changed = 3U, transforms_pointer_changed = 4U, component_count_changed = 5U, surface_signature_changed = 6U, visibility_signature_changed = 7U, transform_signature_changed = 8U, build_config_changed = 9U };

struct Surface2DGpuInstance final { float world_m00; float world_m01; float world_m02; float world_m10; float world_m11; float world_m12; float size_x; float size_y; float pivot_x; float pivot_y; float uv_u0; float uv_v0; float uv_u1; float uv_v1; float opacity; std::uint32_t tint_rgba8; std::uint32_t params; std::uint32_t surface_id; std::uint32_t material_id; std::uint32_t atlas_page_id; std::uint32_t component_index; std::uint32_t user_data; std::uint32_t source_kind; std::uint32_t reserved0; };
struct Surface2DDrawBatch final { std::uint64_t sort_key; std::uint32_t instance_begin; std::uint32_t instance_count; std::uint32_t surface_id; std::uint32_t material_id; std::uint32_t atlas_page_id; std::uint32_t first_component_index; std::uint32_t params; };
struct Surface2DRuntimeBuildConfig final { bool build_ordered_indices = true; };
struct Surface2DRuntimeBuildHint final { std::uint64_t external_surface_signature = 0U; std::uint64_t external_transform_signature = 0U; std::uint64_t external_visible_set_signature = 0U; const std::uint32_t* transform_dirty_component_indices = nullptr; const std::uint32_t* visible_component_indices = nullptr; std::uint32_t transform_dirty_component_count = 0U; std::uint32_t visible_component_count = 0U; std::uint8_t use_external_surface_signature = 0U; std::uint8_t use_external_transform_signature = 0U; std::uint8_t use_visible_component_indices = 0U; std::uint8_t use_external_visible_set_signature = 0U; };
struct Surface2DRuntimeBuildStats final { SurfaceBatchBuildStats batch{}; std::uint32_t candidate_component_count = 0U; std::uint32_t emitted_instance_count = 0U; std::uint32_t emitted_batch_count = 0U; std::uint32_t transform_rewritten_instance_count = 0U; std::uint32_t image_source_instance_count = 0U; std::uint32_t sprite_source_instance_count = 0U; std::uint32_t cache_epoch = 0U; std::uint64_t surface_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; SurfaceRuntimeCacheStatus cache_status = SurfaceRuntimeCacheStatus::miss; SurfaceRuntimeCacheMissReason cache_miss_reason = SurfaceRuntimeCacheMissReason::none; bool cache_reused = false; bool transform_only_update = false; bool used_visible_component_indices = false; bool cache_valid_before_build = false; bool cache_key_matched = false; bool surface_signature_from_hint = false; bool transform_signature_from_hint = false; bool visible_set_signature_from_hint = false; bool transform_update_from_dirty_hint = false; };
struct Surface2DRuntimeCache final { const Surface<Dim2>* components = nullptr; const Transform<Dim2>* transforms = nullptr; std::uint32_t component_count = 0U; std::uint32_t candidate_component_count = 0U; std::uint64_t surface_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; std::uint32_t epoch = 0U; vr::McVector<std::uint32_t> instance_world_revisions{}; vr::McVector<std::uint32_t> component_to_instance_index{}; Surface2DRuntimeBuildConfig build_config{}; Surface2DRuntimeBuildStats last_stats{}; bool valid = false; };
struct Surface2DRuntimeScratch final { SurfaceBatchScratch<Dim2> batch_scratch{}; vr::McVector<Surface2DGpuInstance> instances{}; vr::McVector<Surface2DDrawBatch> draw_batches{}; Surface2DRuntimeCache cache{}; };

struct Surface3DGpuInstance final { float world_m00; float world_m01; float world_m02; float world_m03; float world_m10; float world_m11; float world_m12; float world_m13; float world_m20; float world_m21; float world_m22; float world_m23; float world_m30; float world_m31; float world_m32; float world_m33; float uv_scale_u; float uv_scale_v; float uv_bias_u; float uv_bias_v; float opacity; std::uint32_t tint_rgba8; std::uint32_t params; std::uint32_t texture_id; std::uint32_t sampler_id; std::uint32_t material_id; std::uint32_t component_index; std::uint32_t user_data; std::uint32_t uv_set; std::uint32_t texture_flags; };
struct Surface3DDrawBatch final { std::uint64_t sort_key; std::uint32_t instance_begin; std::uint32_t instance_count; std::uint32_t texture_id; std::uint32_t sampler_id; std::uint32_t material_id; std::uint32_t first_component_index; std::uint32_t params; };
struct Surface3DRuntimeBuildConfig final { bool build_ordered_indices = true; };
struct Surface3DRuntimeBuildHint final { std::uint64_t external_surface_signature = 0U; std::uint64_t external_transform_signature = 0U; std::uint64_t external_visible_set_signature = 0U; const std::uint32_t* transform_dirty_component_indices = nullptr; const std::uint32_t* visible_component_indices = nullptr; std::uint32_t transform_dirty_component_count = 0U; std::uint32_t visible_component_count = 0U; std::uint8_t use_external_surface_signature = 0U; std::uint8_t use_external_transform_signature = 0U; std::uint8_t use_visible_component_indices = 0U; std::uint8_t use_external_visible_set_signature = 0U; };
struct Surface3DRuntimeBuildStats final { SurfaceBatchBuildStats batch{}; std::uint32_t candidate_component_count = 0U; std::uint32_t emitted_instance_count = 0U; std::uint32_t emitted_batch_count = 0U; std::uint32_t transform_rewritten_instance_count = 0U; std::uint32_t depth_test_batch_count = 0U; std::uint32_t depth_write_batch_count = 0U; std::uint32_t cache_epoch = 0U; std::uint64_t surface_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; SurfaceRuntimeCacheStatus cache_status = SurfaceRuntimeCacheStatus::miss; SurfaceRuntimeCacheMissReason cache_miss_reason = SurfaceRuntimeCacheMissReason::none; bool cache_reused = false; bool transform_only_update = false; bool used_visible_component_indices = false; bool cache_valid_before_build = false; bool cache_key_matched = false; bool surface_signature_from_hint = false; bool transform_signature_from_hint = false; bool visible_set_signature_from_hint = false; bool transform_update_from_dirty_hint = false; };
struct Surface3DRuntimeCache final { const Surface<Dim3>* components = nullptr; const Transform<Dim3>* transforms = nullptr; std::uint32_t component_count = 0U; std::uint32_t candidate_component_count = 0U; std::uint64_t surface_signature = 0U; std::uint64_t transform_signature = 0U; std::uint64_t visible_set_signature = 0U; std::uint32_t epoch = 0U; vr::McVector<std::uint32_t> instance_world_revisions{}; vr::McVector<std::uint32_t> component_to_instance_index{}; Surface3DRuntimeBuildConfig build_config{}; Surface3DRuntimeBuildStats last_stats{}; bool valid = false; };
struct Surface3DRuntimeScratch final { SurfaceBatchScratch<Dim3> batch_scratch{}; vr::McVector<Surface3DGpuInstance> instances{}; vr::McVector<Surface3DDrawBatch> draw_batches{}; Surface3DRuntimeCache cache{}; };

static_assert(PurePodSurfaceComponent<Surface2DGpuInstance>);
static_assert(PurePodSurfaceComponent<Surface2DDrawBatch>);
static_assert(PurePodSurfaceComponent<Surface3DGpuInstance>);
static_assert(PurePodSurfaceComponent<Surface3DDrawBatch>);
static_assert(alignof(Surface2DGpuInstance) == 4U);
static_assert(alignof(Surface3DGpuInstance) == 4U);

// Forward declarations: full implementations deferred to impl units
template<DimensionTag DimensionT> class SurfaceRuntimeSystem;

// ============================================================================
// Surface upload plan system (POD types for partial upload)
// ============================================================================

struct SurfaceUploadPatchRange final { std::uint32_t instance_begin; std::uint32_t instance_count; };
struct SurfaceUploadPlanStats final { std::uint32_t requested_component_count; std::uint32_t resolved_instance_count; std::uint32_t dropped_component_count; std::uint32_t range_count; std::uint32_t covered_instance_count; std::uint32_t merged_adjacent_count; std::uint32_t merged_gap_instance_count; bool used_dense_path; };
struct SurfaceUploadPlanBuildOptions final { std::uint32_t merge_gap_instances; std::uint32_t dense_path_min_dirty_count; std::uint32_t dense_path_min_coverage_percent; };
static_assert(PurePodSurfaceComponent<SurfaceUploadPatchRange>);
static_assert(PurePodSurfaceComponent<SurfaceUploadPlanStats>);

template<DimensionTag DimensionT>
struct SurfaceUploadPlanScratch final {
    vr::McVector<std::uint32_t> instance_indices{}; vr::McVector<SurfaceUploadPatchRange> ranges{}; vr::McVector<std::uint8_t> dense_marks{};
};

template<DimensionTag DimensionT>
class SurfaceUploadPlanSystem final {
public:
    using ScratchType = SurfaceUploadPlanScratch<DimensionT>;
    [[nodiscard]] static constexpr SurfaceUploadPlanBuildOptions DefaultBuildOptions() noexcept {
        return SurfaceUploadPlanBuildOptions{.merge_gap_instances = 0U, .dense_path_min_dirty_count = 64U, .dense_path_min_coverage_percent = 25U};
    }
    static void Reserve(ScratchType&, std::uint32_t);
    [[nodiscard]] static SurfaceUploadPlanStats Build(const SurfaceUploadPlanScratch<DimensionT>&, std::uint32_t, SurfaceUploadPlanScratch<DimensionT>&, const SurfaceUploadPlanBuildOptions& = {});
};

} // namespace vr::ecs
} // export
