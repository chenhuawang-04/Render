# ECS Phase3 Refactor Baseline Snapshot

- timestamp: 2026-04-28_141458
- commit: b43588b
- command: `vr_bench_runner --iterations 512 --warmup 2 --runs 9`

## ecs_appearance_phase3_baseline_2026-04-28_141458.json

| case | mean_ns/iter | p95_ns/iter | mean_ms |
|---|---:|---:|---:|
| EcsAppearanceFrameCoordinator_dim3_dual_renderer_shared_build_1k | 17,980.556 | 19,949.805 | 9.206 |
| EcsAppearanceLinkSystem_dim3_geometry_1k_dirty_hint | 2,580.165 | 2,624.414 | 1.321 |
| EcsAppearancePrepareStage_dim3_dual_renderer_duplicate_build_1k | 20,124.848 | 20,639.453 | 10.304 |
| EcsAppearanceRuntimeSystem_dim2_build_1k_full_rebuild | 86,115.213 | 87,952.344 | 44.091 |
| EcsAppearanceRuntimeSystem_dim3_build_1k_dirty_hint | 2,502.843 | 2,574.805 | 1.281 |

## ecs_geometry_runtime_phase3_baseline_2026-04-28_141458.json

| case | mean_ns/iter | p95_ns/iter | mean_ms |
|---|---:|---:|---:|
| EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half | 35,309.570 | 36,083.008 | 18.079 |
| EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild | 438,882.031 | 446,380.469 | 224.708 |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only | 68,862.196 | 73,107.422 | 35.257 |
| EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 5,291.819 | 5,716.602 | 2.709 |

## ecs_surface_runtime_phase3_baseline_2026-04-28_141458.json

| case | mean_ns/iter | p95_ns/iter | mean_ms |
|---|---:|---:|---:|
| EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half | 76,963.802 | 79,673.438 | 39.405 |
| EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild | 135,826.042 | 139,406.445 | 69.543 |
| EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint | 5,010.851 | 6,638.477 | 2.566 |

