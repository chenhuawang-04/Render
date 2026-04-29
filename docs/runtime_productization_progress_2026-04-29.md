# Runtime Productization Progress (2026-04-29)

## Scope

This iteration focused on **P0 runtime hardening** for Vulkan resource lifetime safety under ECS-driven, high-frequency updates:

- Deferred retirement for transient GPU buffers/images used by upload/runtime systems.
- Frame-timeline aware collection (`last_submitted_value` / `completed_submit_value`).
- Integration with existing `RenderRuntimeHost` and renderer prepare flow.
- Optional descriptor lifecycle validation layer (compile-time + runtime toggles).

## Implemented

### 1. Deferred retire for upload stream buffers

Applied to:

- `GeometryUploadHost`
- `SurfaceUploadHost`
- `LightShadowUploadHost`

Each host now:

- Accepts frame timeline values in `BeginFrame(...)`.
- Tracks safe retire value (`max(last_submitted, completed) + 1`).
- Retires replaced buffers instead of immediate destroy during capacity growth.
- Collects only when `retire_value <= completed_submit_value`.
- Flushes all retired resources in shutdown path.

### 2. Deferred retire for text glyph page images

Applied to:

- `GlyphUploadHost`

When atlas pages shrink/recreate due generation/dimension changes:

- Old page images are retired, not destroyed immediately.
- Retired pages are collected by completed submit value.
- Runtime host now passes frame timeline values to `UploadDirtyPages(...)`.

### 3. Renderer/runtime integration updates

Prepare-stage integration now forwards timeline data into upload hosts:

- `GeometryRenderer2D`
- `GeometryRenderer3D`
- `SurfaceRenderer2D`
- `SurfaceRenderer3D`
- `RenderRuntimeHost` -> `GlyphUploadHost`

### 4. Optional descriptor validation layer

Added descriptor lifecycle validation in `DescriptorHost` with two-level control:

1. **Compile-time switch** (zero-overhead OFF path):
   - CMake option: `VR_ENABLE_DESCRIPTOR_VALIDATION`
   - Default: `OFF`
   - When `OFF`, validation code is fully compiled out (`#if VR_ENABLE_DESCRIPTOR_VALIDATION`), so there is no runtime branch cost in hot paths.

2. **Runtime switch** (only when compile-time `ON`):
   - `DescriptorHostCreateInfo::enable_validation` (default `true`)
   - Can be disabled per runtime instance without changing call sites.

Validation behavior when enabled:

- Tracks descriptor sets by frame arena generation.
- On `BeginFrame(frame_index)`, after `vkResetDescriptorPool`, invalidates all tracked sets from that frame index.
- `UpdateSet(...)` verifies descriptor set liveness and throws a clear error for stale/unknown sets.

### 5. Unified Retire Bus in runtime hosts

Introduced a reusable `render::RetireBus<T>` and integrated it into multiple hosts:

- `GeometryUploadHost`
- `SurfaceUploadHost`
- `LightShadowUploadHost`
- `SurfaceImageHost`
- `GeometryImageHost`
- `GlyphUploadHost`
- `ShadowAtlasHost`

Benefits:

- Shared retirement/collection semantics across resource hosts.
- Less duplicate lifetime code and lower maintenance cost.
- Preserves high-performance behavior (append + linear sweep, no per-node heap churn in hot paths when reserved).

## Validation

Build + tests + runtime smoke executed:

- `cmake --build build -j 8`
- `ctest --test-dir build --output-on-failure`
- `sdl_surface_light_shadow_2d_demo --frames 4 --fps 30 --freeze 0 --log-every-frame 1`

All completed successfully in this branch.

## Why this matters

The new behavior removes a major class of validation/lifetime failures:

- `vkDestroyBuffer` / `vkDestroyImage` while still referenced by descriptors or in-flight command buffers.
- Mid-frame reallocation hazards caused by immediate destruction on stream growth.
- Stale descriptor set reuse after per-frame pool reset.

This keeps ECS systems pure while hardening backend safety and preserving hot-path performance characteristics.
