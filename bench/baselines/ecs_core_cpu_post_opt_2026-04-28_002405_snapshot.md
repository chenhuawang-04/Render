# ECS Core CPU Performance Snapshot (Round-3 Non-SIMD Optimization)

- Date: 2026-04-28
- Baseline JSON: `bench/baselines/ecs_core_cpu_baseline_2026-04-27_233020.json`
- Current JSON: `bench/baselines/ecs_core_cpu_post_opt_2026-04-28_002405.json`
- Metric: `mean_ns_per_iteration` (lower is better)

| Benchmark | Baseline ns/iter | Current ns/iter | Delta |
|---|---:|---:|---:|
| EcsBoundsSystem_dim3_update_aligned_4k_dirty_indices | 95.588 | 94.285 | -1.36% |
| EcsBoundsSystem_dim3_update_aligned_4k_full_scan | 21,429.694 | 17,289.727 | -19.32% |
| EcsCullingSystem_dim3_candidate_scan_4k_of_16k | 146,715.132 | 82,951.439 | -43.46% |
| EcsCullingSystem_dim3_full_scan_16k | 338,068.831 | 179,438.469 | -46.92% |
| EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half | 56,455.451 | 37,190.381 | -34.12% |
| EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild | 270,769.137 | 178,276.565 | -34.16% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only | 100,531.363 | 73,310.733 | -27.08% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 18,260.789 | 4,161.984 | -77.21% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half | 103,940.824 | 79,382.104 | -23.63% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild | 167,841.327 | 145,475.295 | -13.33% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 17,309.884 | 4,185.193 | -75.82% |
| EcsTextBatchSystem_dim2_build_and_sort_4k | 194,262.325 | 189,387.755 | -2.51% |
| EcsTextBatchSystem_dim2_build_visible_only_4k | 21,454.146 | 20,995.967 | -2.14% |
| EcsTextRuntimeSystem_dim2_build_1k | 1,904,141.071 | 1,414,122.857 | -25.73% |
| EcsTextSystem_dim2_append_text_hot_path | 50.482 | 47.828 | -5.26% |
| EcsTextSystem_dim2_set_text_hot_path | 11.105 | 9.464 | -14.77% |
| EcsTextSystem_dim3_set_append_route | 44.815 | 40.749 | -9.07% |

## Top Improvements
- EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint: -77.21%
- EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint: -75.82%
- EcsCullingSystem_dim3_full_scan_16k: -46.92%
- EcsCullingSystem_dim3_candidate_scan_4k_of_16k: -43.46%
- EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild: -34.16%
- EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half: -34.12%
- EcsGeometryRuntimeSystem_dim3_build_1k_transform_only: -27.08%
- EcsTextRuntimeSystem_dim2_build_1k: -25.73%
