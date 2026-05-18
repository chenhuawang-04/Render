# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build and test commands

### Configure
- Dev debug with shader reflection + ABI checks:
  - `cmake --preset dev-debug`
- QA debug with tests + benchmarks:
  - `cmake --preset qa-debug`
- QA RelWithDebInfo with tests + benchmarks:
  - `cmake --preset qa-relwithdebinfo`

### Build
- Build shader artifacts only:
  - `cmake --build --preset build-dev-debug-shaders`
- Build all QA debug targets:
  - `cmake --build --preset build-qa-debug-all`
- Build tests only:
  - `cmake --build --preset build-qa-debug-tests`
- Build benchmarks only:
  - `cmake --build --preset build-qa-debug-bench`

### Run tests
- Run unit tests through CTest preset:
  - `ctest --preset qa-unit`
- Run integration tests through CTest preset:
  - `ctest --preset qa-integration`
- Run a specific CTest test from the QA debug build:
  - `ctest --test-dir build_preset/qa_debug -R vr_tests.unit --output-on-failure`
  - `ctest --test-dir build_preset/qa_debug -R vr_demo.pbr_material_grid_smoke --output-on-failure`

### Run native test/bench binaries directly
- List tests:
  - `./build_preset/qa_debug/vr_tests.exe --list`
- Run a single test case or pattern:
  - `./build_preset/qa_debug/vr_tests.exe --filter RuntimeIntegration --report-json ./build_preset/qa_debug/reports/tests_single.json`
- Run only integration-tagged tests:
  - `./build_preset/qa_debug/vr_tests.exe --include-tag integration --return-on-all-skipped 125`
- List benchmarks:
  - `./build_preset/qa_debug/vr_bench_runner.exe --list`
- Run a single benchmark or pattern:
  - `./build_preset/qa_debug/vr_bench_runner.exe --filter EcsGeometryRuntimeSystem --runs 9 --warmup 2 --min-duration-ms 40`

### Quality runner
- List available profiles:
  - `python scripts/testing/vr_quality_runner.py --list-profiles`
- Run unit-only profile:
  - `python scripts/testing/vr_quality_runner.py --profile test_unit`
- Run integration profile:
  - `python scripts/testing/vr_quality_runner.py --profile test_integration`
- Run benchmark smoke profile:
  - `python scripts/testing/vr_quality_runner.py --profile bench_smoke`
- Run the recommended full local validation profile:
  - `python scripts/testing/vr_quality_runner.py --profile quality_full`

## Build prerequisites and gotchas

- This project targets **C++23**, **Vulkan 1.3**, and uses **Ninja** in its presets.
- Top-level `CMakeLists.txt` expects local dependency roots for `MemoryCenterNew`, `Vector_New`, `fast_math`, `SDL3`, `FreeType`, and optionally `CrashTracer`. If configure fails, check the cache paths near `CMakeLists.txt`.
- Shader builds require `glslangValidator`. Shader reflection requires `spirv-cross`. Shader optimization requires `spirv-opt`.
- Shader compilation generates embedded headers and optional reflection/contract JSON into `build*/generated/`. Build failures can come from ABI/contract validation, not just GLSL syntax.
- Crash tracing is integrated at runtime entrypoints and writes reports to `crash_reports/`.

## High-level architecture

### Repository shape
- Public API lives under `include/vr/**`.
- Implementations live under `src/**`.
- Demos are under `examples/**`.
- Tests and benchmarks use custom in-repo frameworks under `tests/**` and `bench/**`.
- Architecture docs worth checking first:
  - `docs/architecture_manual.md`
  - `docs/render_runtime_contract_v0_1.md`
  - `docs/testing_framework_architecture.md`
  - `docs/file_index.md`

### Core runtime layers
1. **Platform/context layer**
   - `vr::VulkanContext` owns Vulkan instance/device/queues.
   - `vr::platform::WindowSurface` and `RenderHost` provide the SDL3 window/surface integration.

2. **Resource/runtime infrastructure**
   - `vr/resource/*` manages GPU memory, buffers, images, and samplers.
   - `vr/render/*` owns frame loop infrastructure: swapchain, sync, command buffers, upload staging, descriptors, pipelines, render targets, frame retire, and composition.

3. **ECS + submission layer**
   - ECS data lives in `vr/ecs/component/*`.
   - Stateless systems live in `vr/ecs/system/*`.
   - Scene submission is a separate layer built around `RenderView`, `RenderScenePacket`, and `SceneRecorder2D/3D`, which bridge ECS data into render passes.

4. **Domain hosts and renderers**
   - Text, geometry, surface, light, shadow, particle, environment, animation, and asset texture subsystems each split responsibilities between:
     - **hosts**: GPU resources, caching, uploads, preparation
     - **renderers/passes**: command recording

### Two runtime entry styles exist
- `include/vr/render/render_runtime_host.hpp` defines the established `RenderRuntimeHost` facade.
- `include/vr/runtime/runtime.hpp` defines the newer typed-services runtime (`Runtime<Profile>`, `RuntimeKernel`, service DAG).
- When reading runtime code, keep these distinct: the newer runtime is layered around service orchestration, while the older host remains a major integration surface and is still used by demos like `examples/sdl_runtime_demo.cpp`.

### Cross-cutting concepts that matter

#### 1. Bindless is a core engine contract
- `BindlessResourceSystem` centralizes texture/sampler access across renderers.
- Shader-side bindless declarations live in `shaders/include/vr/render/bindless.glsl`.
- Many render paths assume descriptor indexing support is available and validated early.
- When changing texture/resource plumbing, check both host-side descriptor setup and GLSL contracts.

#### 2. Appearance is the shared visual/material semantic layer
- `Appearance` is the common semantic hub across geometry/surface rendering instead of isolated per-renderer material systems.
- CPU-side preparation is centered on `appearance_gpu_prepare.hpp` and related appearance runtime systems.
- 3D PBR shading consumes appearance-provided texture bindings and parameters.

#### 3. Dim2/Dim3 templating is fundamental
- Many ECS systems, scene recorders, particle systems, animation systems, and renderers are dimension-templated.
- Prefer following the existing `Dim2`/`Dim3` split rather than adding runtime branching.

#### 4. Shader pipeline is part of the architecture
- GLSL sources in `shaders/` are compiled into generated C++ headers used by the engine.
- Reflection and contract-check tools live in `tools/`.
- If a render change touches descriptors, push constants, or shader I/O, expect corresponding generated-artifact and contract-test impact.

#### 5. Offscreen rendering and post-processing are first-class
- Render target allocation, pooling, bloom, and final composition are explicit subsystems, not ad hoc steps inside individual renderers.
- Relevant code lives around `render_target_host`, `render_target_pool`, `scene_render_target_set`, `scene_bloom_post_stack`, and `render_target_composite_renderer`.

#### 6. Environment/IBL is coordinated across multiple hosts/passes
- Sky/environment rendering, IBL bake, and environment GPU state are separate but coordinated subsystems.
- Changes here often span `SkyEnvironmentGpuHost`, `SkyEnvironmentPass`, `IBLHost`, and `IBLBakeHost`.

## Testing structure

- `vr_tests` is a custom native test runner, not GoogleTest/Catch2.
- CTest splits tests into:
  - `vr_tests.unit`
  - `vr_tests.integration`
  - `vr_demo.pbr_material_grid_smoke`
- The test binary supports `--filter`, `--include-tag`, `--exclude-tag`, and `--report-json`.
- `vr_bench_runner` is also custom and supports filtering, baselines, and JSON reports.
- `scripts/testing/quality_profiles.json` is the source of truth for the quality-runner profiles.

## Good entry points when orienting yourself

- `examples/sdl_runtime_demo.cpp` shows the canonical runtime initialization/tick/shutdown loop.
- `include/vr/render/render_runtime_host.hpp` shows what the classic runtime owns.
- `include/vr/runtime/runtime.hpp` and `include/vr/runtime/runtime_kernel.hpp` show the newer service-oriented runtime direction.
- `docs/render_runtime_contract_v0_1.md` summarizes stable runtime/recorder/renderer ownership and lifecycle expectations.
