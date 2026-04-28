# C++20 Module Conversion — Phase Progress Report

**Date**: 2026-04-28
**Branch**: `ecs_font_quality_optimization`
**Scope**: Convert 55 .hpp headers into true .cppm C++20 module interface units

---

## 1. Completed Modules (10/10)

| # | Module | File | Status | Lines | Contents |
|---|--------|------|--------|-------|----------|
| 1 | `vr.types` | `include/vr/modules/vr.types/vr.types.cppm` | Done | ~280 | dimension, spatial_types, spatial_math, text_types, geometry_types, surface_types + centralized `vr::McVector<T>` |
| 2 | `vr.context` | `include/vr/modules/vr.context/vr.context.cppm` | Done | ~140 | VulkanContext, VulkanInstanceCreateInfo, VulkanDeviceCreateInfo, QueueFamilyIndices |
| 3 | `vr.platform` | `include/vr/modules/vr.platform/vr.platform.cppm` | Done | ~260 | WindowSurface<Sdl3BackendTag>, RenderHost<Sdl3BackendTag>, BackendTag, WindowCreateInfo |
| 4 | `vr.resource` | `include/vr/modules/vr.resource/vr.resource.cppm` | Done | ~300 | GpuMemoryHost, BufferHost, ImageHost, SamplerHost + all resource PODs |
| 5 | `vr.render` | `include/vr/modules/vr.render/vr.render.cppm` | Done | ~1000 | SwapchainHost, FrameSyncHost, FrameCommandHost, FrameRetireHost, RenderLoopHost, DescriptorHost, UploadHost, PipelineHost |
| 6 | `vr.ecs` | `include/vr/modules/vr.ecs/vr.ecs.cppm` | Done | ~600 | Transform, Camera, Bounds, CullingSystem (common infrastructure) |
| 7 | `vr.text` | `include/vr/modules/vr.text/vr.text.cppm` | Done | ~1800 | FreeTypeHost, GlyphAtlasHost, GlyphUploadHost, TextRenderer2D/3D + text ECS systems (Text, TextSystem, TextBatchSystem, TextRuntimeSystem, TextRender3DSystem) |
| 8 | `vr.geometry` | `include/vr/modules/vr.geometry/vr.geometry.cppm` | Done | ~600 | GeometryResourceHost, GeometryMaterialHost, GeometryImageHost, GeometryUploadHost, GeometryRenderer2D/3D |
| 9 | `vr.surface` | `include/vr/modules/vr.surface/vr.surface.cppm` | Done | ~500 | SurfaceImageHost, SurfaceUploadHost, SurfaceRenderer2D/3D |
| 10 | `vr.runtime` | `include/vr/modules/vr.runtime/vr.runtime.cppm` | Done | ~160 | RenderRuntimeHost (extracted from vr.render to break circular dependency) |

### Helper Files

| File | Status | Purpose |
|------|--------|---------|
| `include/vr/detail/vr_module_fwd.hpp` | Done | Global module fragment header — `NOMINMAX` + `<vulkan/vulkan.h>` + `McVector.hpp` |

---

## 2. Key Design Decisions

### 2.1 Centralized McVector alias
All local `McVector<T>` aliases (GeometryMcVector, SurfaceMcVector, FrameMcVector, etc.) are replaced by a single `vr::McVector<T>` exported from `vr.types`. This eliminates template duplication across modules.

### 2.2 External dependencies via global module fragment
Fast_math, MemoryCenter, SDL3, FreeType, and Vulkan headers remain as `#include` in the global module fragment. Only VR-project code uses `export module` / `import`. This keeps the modules buildable without modifying external dependency build systems.

### 2.3 Circular dependency resolution
**Render ↔ Text**: `render_runtime_host.hpp` includes text headers, while text renderers depend on render types.
**Solution**: Extracted `RenderRuntimeHost` into `vr.runtime` module that imports both `vr.render` and `vr.text`.

**Text ↔ ECS**: Text renderers depend on ECS types (Transform, Camera, Bounds, CullingSystem), while text-specific ECS systems depend on text::FreeTypeHost and text::GlyphAtlasHost.
**Solution**: Common ECS components/systems go in `vr.ecs`. Text-specific ECS systems (TextSystem, TextBatchSystem, TextRuntimeSystem, TextRender3DSystem) are co-located with the text module in `vr.text`.

### 2.4 Module dependency chain
```
vr.types
  ↓
vr.context
  ↓
vr.platform  +  vr.resource
  ↓
vr.render
  ↓
vr.ecs  (common ECS components: Transform, Camera, Bounds, CullingSystem)
  ↓
vr.text  +  vr.geometry  +  vr.surface
  ↓
vr.runtime
```

### 2.5 ECS system distribution
- **vr.ecs**: Common infrastructure — Transform, Camera, Bounds components + CullingSystem + TransformSystem + CameraSystem + BoundsSystem
- **vr.text**: Text component (Text<Dim2/3>, TextStyle, TextRuntimeBatchData) + TextSystem + TextBatchSystem + TextRuntimeSystem + TextRender3DSystem
- **vr.geometry / vr.surface**: Geometry/Surface ECS systems co-located in vr.ecs (they don't depend on domain host types)

### 2.6 Implementation units deferred
The 24 `.cpp` files currently remain as traditional source files. Module implementation units (`module vr.<name>;`) will be created after the `.cppm` interface files are all complete and the build system is updated.

### 2.7 CMake unchanged
Per user instruction, `CMakeLists.txt` has not been modified. Build system migration will be a separate step after all `.cppm` files are written.

---

## 3. Directory Structure

```
include/vr/
  detail/
    vr_module_fwd.hpp              ← helper for global module fragment
  modules/
    vr.types/vr.types.cppm         ← foundation (types, math, McVector)
    vr.context/vr.context.cppm     ← VulkanContext declarations
    vr.platform/vr.platform.cppm   ← WindowSurface + RenderHost
    vr.resource/vr.resource.cppm   ← GPU memory, buffer, image, sampler
    vr.render/vr.render.cppm       ← swapchain, sync, command, loop, descriptor, pipeline, upload
    vr.ecs/vr.ecs.cppm             ← common ECS: Transform, Camera, Bounds, CullingSystem
    vr.text/vr.text.cppm           ← FreeType, glyph, text ECS systems, text renderers
    vr.geometry/vr.geometry.cppm   ← geometry resource/material/image/upload, renderers
    vr.surface/vr.surface.cppm     ← surface image/upload, renderers
    vr.runtime/vr.runtime.cppm     ← RenderRuntimeHost
  ecs/component/*.hpp              ← (unchanged original headers)
  ecs/system/*.hpp                 ← (unchanged)
  render/*.hpp                     ← (unchanged)
  ...                              ← (all original headers preserved)
```

---

## 4. Statistics

| Metric | Count |
|--------|-------|
| Total modules planned | 10 |
| Modules completed | 10 |
| .cppm files written | 10 |
| Total .cppm lines | ~5,640 |
| Headers inlined | ~55 |
| Local McVector aliases eliminated | ~28 |
| Original .hpp files modified | 0 |
| CMake changes | 0 |

---

## 5. Next Steps

1. **Add remaining ECS types to vr.ecs**: Appearance component/system, Geometry/Surface components and their batch/runtime/path/mesh systems (currently referenced by domain modules but not fully defined in vr.ecs)
2. **Update CMakeLists.txt**: Bump to 3.28+, enable `CXX_SCAN_FOR_MODULES`, add per-module targets with `FILE_SET CXX_MODULES`
3. **Create module implementation units**: Convert 24 .cpp files to `module vr.<name>;` format
4. **Thin backward-compat wrappers**: Convert old .hpp headers to `#pragma once` + `import vr.<name>;`
5. **Build and verify**: cmake --build with 0 errors
