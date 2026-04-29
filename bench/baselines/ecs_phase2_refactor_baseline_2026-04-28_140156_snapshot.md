# ECS Phase2 Refactor Baseline Snapshot

- CapturedAt: 2026-04-28 14:02:28
- Commit: `b43588b`
- IterationsPerCase: 512
- WarmupRuns: 2
- MeasuredRuns: 9

## Files
- `bench/baselines/ecs_appearance_phase2_baseline_2026-04-28_140156.json`
- `bench/baselines/ecs_geometry_runtime_phase2_baseline_2026-04-28_140156.json`
- `bench/baselines/ecs_surface_runtime_phase2_baseline_2026-04-28_140156.json`

## Metrics
| Case | mean_ns/iter | p95_ns/iter | mean_ms |
|---|---:|---:|---:|
| `EcsAppearanceLinkSystem_dim3_geometry_1k_dirty_hint` | 4,425.651 | 4,575.586 | 2.266 |
| `EcsAppearanceRuntimeSystem_dim2_build_1k_full_rebuild` | 146,944.748 | 151,728.516 | 75.236 |
| `EcsAppearanceRuntimeSystem_dim3_build_1k_dirty_hint` | 4,305.773 | 4,566.797 | 2.205 |
| `EcsGeometryRuntimeSystem_dim3_build_1k_candidate_visibility_half` | 61,403.624 | 64,270.117 | 31.439 |
| `EcsGeometryRuntimeSystem_dim3_build_1k_full_rebuild` | 795,026.345 | 866,638.086 | 407.053 |
| `EcsGeometryRuntimeSystem_dim3_build_1k_transform_only` | 132,494.835 | 157,275.586 | 67.837 |
| `EcsGeometryRuntimeSystem_dim3_build_1k_transform_only_dirty_hint` | 9,305.187 | 11,143.164 | 4.764 |
| `EcsSurfaceRuntimeSystem_dim3_build_1k_candidate_visibility_half` | 156,964.106 | 176,519.141 | 80.366 |
| `EcsSurfaceRuntimeSystem_dim3_build_1k_full_rebuild` | 280,691.732 | 311,897.461 | 143.714 |
| `EcsSurfaceRuntimeSystem_dim3_build_1k_transform_only_dirty_hint` | 8,562.109 | 10,261.133 | 4.384 |

