# ECS Core CPU Performance Snapshot (Round-2 Non-SIMD Optimization)

- Date: 2026-04-28
- Baseline JSON: `bench/baselines/ecs_core_cpu_baseline_2026-04-27_233020.json`
- Current JSON: `bench/baselines/ecs_core_cpu_post_opt_2026-04-28_001208.json`
- Metric: `mean_ns_per_iteration` (lower is better)

| Benchmark | Baseline ns/iter | Current ns/iter | Delta |
|---|---:|---:|---:|
| EcsBoundsSystem_dim3_update_aligned_4k_dirty_indices | 95.588 | 95.951 | +0.38% |
| EcsBoundsSystem_dim3_update_aligned_4k_full_scan | 21,429.694 | 23,579.401 | +10.03% |
| EcsCullingSystem_dim3_candidate_scan_4k_of_16k | 146,715.132 | 125,247.208 | -14.63% |
| EcsCullingSystem_dim3_full_scan_16k | 338,068.831 | 190,913.479 | -43.53% |
| EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half | 56,455.451 | 55,974.978 | -0.85% |
| EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild | 270,769.137 | 210,182.143 | -22.38% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only | 100,531.363 | 93,994.684 | -6.50% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 18,260.789 | 20,451.685 | +12.00% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half | 103,940.824 | 105,740.043 | +1.73% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild | 167,841.327 | 183,675.595 | +9.43% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 17,309.884 | 19,787.886 | +14.32% |
| EcsTextBatchSystem_dim2_build_and_sort_4k | 194,262.325 | 188,745.368 | -2.84% |
| EcsTextBatchSystem_dim2_build_visible_only_4k | 21,454.146 | 22,596.096 | +5.32% |
| EcsTextRuntimeSystem_dim2_build_1k | 1,904,141.071 | 1,837,798.413 | -3.48% |
| EcsTextSystem_dim2_append_text_hot_path | 50.482 | 52.224 | +3.45% |
| EcsTextSystem_dim2_set_text_hot_path | 11.105 | 10.486 | -5.57% |
| EcsTextSystem_dim3_set_append_route | 44.815 | 44.713 | -0.23% |

## Top Improvements
- EcsCullingSystem_dim3_full_scan_16k: -43.53%
- EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild: -22.38%
- EcsCullingSystem_dim3_candidate_scan_4k_of_16k: -14.63%
- EcsGeometryRuntimeSystem_dim3_build_1k_transform_only: -6.50%
- EcsTextSystem_dim2_set_text_hot_path: -5.57%
- EcsTextRuntimeSystem_dim2_build_1k: -3.48%

## Largest Regressions
- EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint: +14.32%
- EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint: +12.00%
- EcsBoundsSystem_dim3_update_aligned_4k_full_scan: +10.03%
- EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild: +9.43%
- EcsTextBatchSystem_dim2_build_visible_only_4k: +5.32%
- EcsTextSystem_dim2_append_text_hot_path: +3.45%
