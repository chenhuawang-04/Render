# ECS Font Quality Performance Snapshot (2026-04-26)

- baseline: `bench/baselines/ecs_font_quality_2026-04-26_baseline.json`
- after: `bench/baselines/ecs_font_quality_2026-04-26_after_round2.json`
- command: `./build/bench/vr_bench_runner.exe --report-json <file>`

`delta% = (after - baseline) / baseline * 100`; negative is faster.

| Benchmark | Baseline mean(ms) | After mean(ms) | Delta% |
|---|---:|---:|---:|
| EcsTextBatchSystem_dim2_build_and_sort_4k | 36.931 | 24.158 | -34.58% |
| EcsTextBatchSystem_dim2_build_visible_only_4k | 35.830 | 36.676 | +2.36% |
| EcsTextRuntimeSystem_dim2_build_1k | 29.442 | 28.707 | -2.49% |
| EcsTextSystem_dim2_append_text_hot_path | 40.005 | 26.234 | -34.42% |
| EcsTextSystem_dim2_set_text_hot_path | 32.512 | 34.968 | +7.55% |
| EcsTextSystem_dim3_set_append_route | 39.800 | 26.352 | -33.79% |
| FrameRetireHost_collect_null_device_fast_reject | 39.341 | 39.149 | -0.49% |
| FrameRetireHost_enqueue_image_view_only | 30.571 | 29.642 | -3.04% |
| FrameRetireHost_enqueue_mixed_handles | 35.169 | 51.742 | +47.12% |
| FrameSyncHost_advance_frame_ring | 27.575 | 35.369 | +28.26% |
| FreeTypeHost_rasterize_ascii_cold_cache_refill | 24.887 | 38.222 | +53.58% |
| FreeTypeHost_rasterize_ascii_hot_cache | 32.632 | 34.568 | +5.93% |
| GlyphAtlasHost_resolve_ascii_hot_cache | 29.391 | 27.218 | -7.40% |
| Runtime_text_renderer_2d_tick_loop | 2109.112 | 2118.004 | +0.42% |
| VulkanTypes_align_up_checked_hot_path | 36.103 | 34.280 | -5.05% |
| VulkanTypes_checked_add_overflow_guard | 30.222 | 37.711 | +24.78% |
| VulkanTypes_should_dedicate_allocation_policy_eval | 24.555 | 38.250 | +55.77% |

## Focus Metrics
- **EcsTextRuntimeSystem_dim2_build_1k**: 29.442 -> 28.707 ms (-2.49%)
- **Runtime_text_renderer_2d_tick_loop**: 2109.112 -> 2118.004 ms (+0.42%)
- **GlyphAtlasHost_resolve_ascii_hot_cache**: 29.391 -> 27.218 ms (-7.40%)
- **FreeTypeHost_rasterize_ascii_hot_cache**: 32.632 -> 34.568 ms (+5.93%)
