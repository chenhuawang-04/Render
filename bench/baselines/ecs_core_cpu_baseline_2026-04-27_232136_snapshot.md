# ECS Core CPU Baseline (2026-04-27 23:21:36)

- Source JSON: `bench/baselines/ecs_core_cpu_baseline_2026-04-27_232136.json`
- Filter: `Ecs*` + tags `core, ecs, cpu`
- Params: warmup=2, runs=7, min-duration-ms=20

| Benchmark | mean_ns_per_iteration | items_per_second | bytes_per_second |
|---|---:|---:|---:|
| EcsBoundsSystem_dim3_update_aligned_4k_dirty_indices |  |  |  |
| EcsBoundsSystem_dim3_update_aligned_4k_full_scan |  |  |  |
| EcsCullingSystem_dim3_candidate_scan_4k_of_16k |  |  |  |
| EcsCullingSystem_dim3_full_scan_16k |  |  |  |
| EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half |  |  |  |
| EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild |  |  |  |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only |  |  |  |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint |  |  |  |
| EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half |  |  |  |
| EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild |  |  |  |
| EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint |  |  |  |
| EcsTextBatchSystem_dim2_build_and_sort_4k |  |  |  |
| EcsTextBatchSystem_dim2_build_visible_only_4k |  |  |  |
| EcsTextRuntimeSystem_dim2_build_1k |  |  |  |
| EcsTextSystem_dim2_append_text_hot_path |  |  |  |
| EcsTextSystem_dim2_set_text_hot_path |  |  |  |
| EcsTextSystem_dim3_set_append_route |  |  |  |
