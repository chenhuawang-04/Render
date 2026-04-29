# ECS Font Quality Baseline Snapshot (Pre-Optimization)

- Date: 2026-04-27
- Source JSON: `ecs_font_quality_2026-04-27_133153_pre_opt.json`
- Command: `vr_bench_runner --runs 9 --warmup 2 --min-duration-ms 40 --report-json ...`

## Run Summary

- selected_count: 17
- executed_count: 17
- completed_count: 17
- failed_count: 0
- skipped_count: 0
- total_duration_ms: 33,591.555

## Key Metrics

| Benchmark | mean_ms | median_ms | p95_ms | items/s | bytes/s |
|---|---:|---:|---:|---:|---:|
| EcsTextBatchSystem_dim2_build_and_sort_4k | 63.126 | 63.370 | 68.191 | 12,782,483.11 | 204,519,729.82 |
| EcsTextBatchSystem_dim2_build_visible_only_4k | 61.288 | 61.125 | 65.878 | 121,367,803.51 | 1,941,884,856.09 |
| EcsTextRuntimeSystem_dim2_build_1k | 41.118 | 40.945 | 42.631 | 398,464.04 | 113,562,250.55 |
| EcsTextSystem_dim2_append_text_hot_path | 59.803 | 59.967 | 60.960 | 35,520,719.79 | 189,443,838.86 |
| EcsTextSystem_dim2_set_text_hot_path | 45.491 | 44.882 | 54.071 | 48,306,162.38 | 386,449,299.01 |
| EcsTextSystem_dim3_set_append_route | 67.667 | 62.671 | 97.962 | 12,628,220.91 | 113,653,988.16 |
| FrameRetireHost_collect_null_device_fast_reject | 66.058 | 66.359 | 68.452 | 1,705,557.19 | 27,288,915.02 |
| FrameRetireHost_enqueue_image_view_only | 54.952 | 50.266 | 76.827 | 34,705,617.12 | 277,644,936.95 |
| FrameRetireHost_enqueue_mixed_handles | 45.326 | 42.010 | 59.696 | 34,003,398.07 | 272,027,184.54 |
| FrameSyncHost_advance_frame_ring | 40.104 | 39.628 | 42.832 | 153,946,346.08 | 615,785,384.31 |
| FreeTypeHost_rasterize_ascii_cold_cache_refill | 64.520 | 64.026 | 85.100 | 50,061.85 | 3,203,958.38 |
| FreeTypeHost_rasterize_ascii_hot_cache | 61.116 | 59.499 | 81.052 | 10,382,091.83 | 664,453,877.15 |
| GlyphAtlasHost_resolve_ascii_hot_cache | 48.512 | 48.713 | 61.379 | 10,390,767.65 | 166,252,282.35 |
| Runtime_text_renderer_2d_tick_loop | 2,201.436 | 2,204.141 | 2,261.046 | 0.45 | 356.13 |
| VulkanTypes_align_up_checked_hot_path | 53.931 | 53.431 | 56.351 | 351,093,404.07 | 2,808,747,232.59 |
| VulkanTypes_checked_add_overflow_guard | 57.240 | 56.533 | 60.540 | 400,751,806.04 | 6,412,028,896.61 |
| VulkanTypes_should_dedicate_allocation_policy_eval | 39.283 | 38.248 | 42.995 | 150,852,431.25 | 1,206,819,450.01 |

## Notes

- This snapshot is saved before the next geometry optimization round.
- Use this file as baseline reference when comparing post-optimization results.
