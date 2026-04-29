# ECS Core CPU Performance Snapshot (Post Optimization)

- Date: 2026-04-27
- Baseline JSON: `bench/baselines/ecs_core_cpu_baseline_2026-04-27_233020.json`
- Current JSON: `bench/baselines/ecs_core_cpu_post_opt_2026-04-27_234932.json`
- Metric: `mean_ns_per_iteration` (lower is better)

| Benchmark | Baseline ns/iter | Current ns/iter | Delta |
|---|---:|---:|---:|
| EcsBoundsSystem_dim3_update_aligned_4k_dirty_indices | 95.588 | 100.351 | +4.98% |
| EcsBoundsSystem_dim3_update_aligned_4k_full_scan | 21,429.694 | 18,787.542 | -12.33% |
| EcsCullingSystem_dim3_candidate_scan_4k_of_16k | 146,715.132 | 97,731.359 | -33.39% |
| EcsCullingSystem_dim3_full_scan_16k | 338,068.831 | 198,405.820 | -41.31% |
| EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half | 56,455.451 | 55,474.994 | -1.74% |
| EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild | 270,769.137 | 215,390.774 | -20.45% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only | 100,531.363 | 92,479.527 | -8.01% |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 18,260.789 | 18,897.507 | +3.49% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half | 103,940.824 | 101,718.785 | -2.14% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild | 167,841.327 | 177,574.880 | +5.80% |
| EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 17,309.884 | 18,662.035 | +7.81% |
| EcsTextBatchSystem_dim2_build_and_sort_4k | 194,262.325 | 216,077.778 | +11.23% |
| EcsTextBatchSystem_dim2_build_visible_only_4k | 21,454.146 | 21,893.296 | +2.05% |
| EcsTextRuntimeSystem_dim2_build_1k | 1,904,141.071 | 1,508,272.381 | -20.79% |
| EcsTextSystem_dim2_append_text_hot_path | 50.482 | 54.115 | +7.20% |
| EcsTextSystem_dim2_set_text_hot_path | 11.105 | 12.794 | +15.21% |
| EcsTextSystem_dim3_set_append_route | 44.815 | 48.425 | +8.05% |

> Note: Single-run benchmark results can vary with CPU frequency, thermal state, and background load.
