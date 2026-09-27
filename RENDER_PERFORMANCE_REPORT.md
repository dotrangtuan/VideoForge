# VideoForge Editor 0.10.3 — Render performance

This patch builds on the working 0.10.2 QueueSeekFix source. It retains range intersection/input seeking and does not incorporate the separate 0.10.1 package. No project or queue schema changes.

## What changed

- Decode and apply the static image's effects once, then loop the processed frame with an exact rational frame timebase. This avoids repeating image decompression, scaling, blur and other fixed effects every frame. The repeated layer is still bounded to its own clip interval.
- Use `max(1, min(4, logical_cpu_count/2))` filter threads when output pixel area is at least 1280x720. Smaller exports keep one thread.
- For a known opaque input whose unrotated dimensions have the same aspect ratio as the canvas, and with one effective non-neutral Zoom only, scale/crop a compact planar-RGB layer and place it directly at the original integer center coordinates. This avoids alpha premultiply/unpremultiply and full-frame transparent padding. Unknown or alpha formats, other aspect ratios, rotated inputs, multiple active Zoom effects and Crop/Blur/Opacity stacks keep the general compositor. Sources without an active Zoom also keep the general path: the initial alternative was measured to regress that case and was removed.
- Omit neutral Crop and 100% Opacity filter operations.
- Reuse stream metadata across clips/jobs (up to 128 source records), invalidating by file size and modification time. Failed probes are not cached.
- Log `Render pipeline` and `Encode settings` so filter processing and actual encoding mode are visible.

## What did not change

Resolution, frame rate, source trimming, range-seek fix, CPU libx264 medium / CRF 18, hardware H.264 target 10 Mbps, and AAC 192 kbps settings remain the same. Decoding and compositing still run on CPU; only H.264 encoding uses the existing hardware selection. No new CUDA/QSV/AMF decode/composite path was enabled without hardware verification.

## End-to-end benchmark

Environment: Ubuntu 24.04 x86-64, GCC 13.3, Qt 6.4.2, FFmpeg 6.1.1, 9 logical CPUs reported in the environment. Both versions are Release builds. Each case exports 6 seconds at 1920x1080 / 30 fps to H.264/AAC MP4, CPU libx264 medium / CRF 18. Sources are a testsrc2 video and a testsrc2 still; Blur = 1% when enabled. Full export time includes source probing, encoder initialization and MP4 finalization.

Two serial runs per version/case, with the version ordering reversed for the second run. No test/build workload ran concurrently with the timed exports. The table shows arithmetic means. The standalone harness and reproduction instructions are in `tests/performance/`.

| Case | 0.10.2 | 0.10.3 | Relative speed | Decoded video SSIM versus 0.10.2 |
|---|---:|---:|---:|---:|
| Single video, no effects | 2.705 s | 2.538 s | 1.07x | 1.000000 |
| 80% Zoom over a still | 7.498 s | 3.853 s | 1.95x | 0.996918 |
| 80% Zoom over a blurred still | 16.675 s | 3.928 s | 4.25x | 0.996832 |
| 80% Zoom over a video | 7.710 s | 4.378 s | 1.76x | 0.996841 |

All compared outputs contain exactly 180 video frames at 30/1 fps and 1920x1080. Decoded audio hashes match in all four cases (these benchmark cases have silent audio). The separate regression suite covers real audio/gain/fades.

The compact RGB path changes some rounding/conversion details; output is not promised bit-identical. The measured SSIM exceeds 0.9968 for the composite cases and is 1.0 for the simple video case. These are similarity measurements against the prior renderer, not proof of equal perceptual quality on every source. Encoder quality settings have not been lowered.

Raw measurements: `tests/performance/measurements.json`; decoded-output checks: `tests/performance/output-comparison.json`. The small number of runs and synthetic fixtures limit generalization. These results compare VideoForge versions, not Premiere, and do not promise a 2x speedup for every timeline.

## Build and regression checks

Release application and tests compile successfully. Seventeen selected test functions pass, with Qt reporting 19 passes including setup/cleanup, zero failures. Full output is `tests/RENDER_PERFORMANCE_TEST_RESULTS.txt`.

Existing tests cover composition/mixing, source trims, gain/fades, sequence frame rates, ordered effects and transparent edges, V2 batching, queue sequencing/retry/cancel, range frames/gaps/fades, late source seeks, skipped inputs, destination preservation and source overwrite protection.

New tests cover:

- A source-trimmed transparent still with Blur crossing an In/Out cut at 30000/1001 fps: correct visibility/end time and exactly 39 output frames.
- Compact opaque Zoom at 10%, 55%, 100%, 175% and 400%, compared with the general FFmpeg layer geometry; mean decoded RGB error under 6/255 after encoding. Opacity selects the general fallback.
- Replacing a probed source at the same path with a different aspect ratio and an added audio stream: metadata refreshes, letterboxing is preserved and audio is present.

## Practical limits and comparing Premiere

This patch has not been built/tested natively on the user's Windows/MSVC/Qt 6.11.2 setup or on NVIDIA/Intel/AMD hardware. The focused suite does not validate UI playback. GPU acceleration, HDR/color-managed delivery and every VFR/rotation/container variant are not covered by this synthetic test matrix.

Premiere can use hardware-accelerated decode and encode on supported media/hardware. VideoForge's hardware checkbox only chooses an encoder; it does not make the RGB composition GPU-based. Matching resolution/fps alone does not establish equal codec, bitrate/quality mode, effects and device usage. For a user-machine comparison, record CPU/GPU, actual `Platform encoder`/`Encode settings` lines, Premiere export settings and elapsed time on the same source interval.

Official references: [FFmpeg filter threading](https://ffmpeg.org/ffmpeg.html), [FFmpeg loop and overlay filters](https://ffmpeg.org/ffmpeg-filters.html), [Adobe hardware encoding/decoding](https://helpx.adobe.com/premiere/desktop/get-started/technical-requirements/hardware-accelerated-decoding-and-encoding.html).
