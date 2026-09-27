# VideoForge Editor 0.10.2 — Queue seek fix

Built from the attached 0.10.0 V2BatchExport ZIP. This is a focused correction for range/V2 queue startup cost, not a merge of the separate 0.10.1 package.

## Cause and change

The old exporter opened every timeline input, produced a composite from zero through Out, and discarded all frames before In at the final output trim. Later jobs therefore repeated progressively more decoding, scaling and compositing.

The patched exporter intersects clips with the frame-aligned output interval, omits inactive/outside inputs, applies accurate input `-ss` before `-i`, and composes the output on a local zero-based clock. Input reads are bounded by the intersecting duration plus a one-second flush allowance. Images loop without seeking. Audio fades run with the original clip-time offset before audio timestamps are reset for mixing. Delay is expressed in 48 kHz samples. Existing export codecs, bitrate/CRF and CPU presets are unchanged.

No project/queue schema changes. Existing pending snapshots use the new exporter after restart. Source validation still covers the stored timeline; files outside a range are not opened as FFmpeg inputs but a missing source is still reported by validation.

## Build and regression validation

Environment: Ubuntu 24.04 x86-64, GCC 13.3, Qt 6.4.2, FFmpeg 6.1.1. Release build of the application and integration tests succeeded.

14 selected test functions passed; Qt reports 16 passes including init/cleanup, zero failures. Test output is in `tests/QUEUE_SEEK_TEST_RESULTS.txt`.

- Existing compositing/audio mixing, trimmed export, gain/fades, eight sequence rates, effects output, V2 batch compositing/names/persistence, sequential queue, failure/retry, queue stop/cancel, range frames/gaps/fades, cancellation preservation and source overwrite protection.
- New `rangeSeeksSourceAndPreservesFades`: a moving H.264/AAC source with 4-second GOPs, original source trim, overlapping fade-in/out and gain. A two-second range on a clip at one hour seeks to source second 5. Five decoded image samples agree with the equivalent whole-clip reference within mean RGB error 4/255; audio RMS windows agree within 8% through the overlapping fade envelope.
- New `rangeSkipsUnrelatedInputsAtLatePositions`: 62 timeline clips, a trimmed transparent still and a hidden/muted layer; only two relevant inputs are opened for the range at minute 59. Layer colors and a late empty gap are checked. The fractional-rate gap contains exactly 30 frames at 30000/1001 fps.

## Startup measurement

Same two-second 640x360, 30 fps test segment (testsrc2, zoom over a continuous still-image background), CPU/libx264 medium CRF 18. Each variant runs jobs sequentially at 0, 60 and 300 seconds. Old and patched exporters use the same Qt/compiler/FFmpeg and source files. The final measurement was run without the regression suite running concurrently.

| Segment starts at | Old: first progress > 0 | Patched: first progress > 0 | Old: complete | Patched: complete |
|---|---:|---:|---:|---:|
| 0 seconds | 457 ms | 441 ms | 470 ms | 451 ms |
| 60 seconds | 1412 ms | 460 ms | 1422 ms | 468 ms |
| 300 seconds | 5758 ms | 440 ms | 5768 ms | 449 ms |

These are single-run synthetic measurements, not a promised speedup for a user's hardware or media. Progress is FFmpeg's first reported percentage above zero, including process/setup overhead; it is not a raw first-decoded-frame measurement. Long GOPs, unindexed/non-seekable files, heavy effects and storage still affect preparation/encoding time.

## Limits

- Native Windows/MSVC, the user's Qt 6.11.2/FFmpeg 7.1.1 setup, GPU encoders and macOS have not been tested for this patch. The source uses the same Qt/C++17 interfaces and FFmpeg options already available to this project.
- The first wider test run hit a GStreamer initialization crash in `exportRangeQueueSnapshotAndDialog` on this Linux environment. The UI/dialog test is not counted as passing. The final focused suite exercises the exporter and queue without that unavailable media-preview setup; no preview/UI implementation was changed.
- The new seek comparison covers regular timestamped H.264/AAC, WAV and PNG fixtures. Irregular timestamps, VFR, sparse/long-GOP sources and non-frame-aligned cuts are not an exhaustive frame-equivalence guarantee. Normal output frame quantization still applies.
- The queue remains sequential, and the composition filters remain CPU based. This fix removes work before In; it is not a general GPU rendering rewrite.

References: [FFmpeg input seek](https://ffmpeg.org/ffmpeg.html#Main-options) and [audio fades](https://ffmpeg.org/ffmpeg-filters.html#afade).
