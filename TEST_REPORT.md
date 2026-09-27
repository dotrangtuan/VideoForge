# Current patch: 0.10.3

See `RENDER_PERFORMANCE_REPORT.md` for this patch's Linux Release build, 17 regression cases, output comparisons and measured CPU export performance. `tests/RENDER_PERFORMANCE_TEST_RESULTS.txt` contains the final test output. Historical Windows reports below do not validate 0.10.3 on Windows.

---

# Current patch: 0.10.2

For this patch's build, 14 regression tests, benchmark and untested Windows/UI limitations, read `QUEUE_SEEK_FIX_REPORT.md`. The historical reports below describe earlier releases and are not new Windows validation of 0.10.2.

---

# VideoForge Editor 0.10.0 — verification report

Completed: 2026-09-26, from the delivered 0.9.4 source, in a separate workspace build.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests compile and link successfully.
- Full regression with FFmpeg/ffprobe 7.1.1: **50 passed, 0 failed, 0 skipped**, 25.772 seconds; CTest 1/1 passed in 25.86 seconds.
- Three new integration tests cover selecting the first N or all V2 video clips, timeline order independent of storage order, skipping images, invalid/zero/excess counts, missing folders/sources, reserved filename characters/device names, duplicate names within one batch and against queued outputs, existing file protection, persistence/reopen, and cancellation of the batch dialog.
- Real sequential renders use differently trimmed V2 clips over changing V1 backgrounds, zoom effects and audio. Decoded corner/centre colors verify both foreground and the correct background for each interval. ffprobe verifies 18 and 21 video frames (600 and 700 ms at 30 fps). Decoded audio contains the expected sound. The editor's unrelated In/Out range does not affect jobs.
- Source snapshot isolation and queue restart are tested. Capacity rejection and simulated journal write failure leave no partial batch; missing-source failure preserves the journal bytes. Existing source/output overwrite and export cancellation tests pass.
- Offscreen screenshots of the batch dialog and full Export panel inspected: count/all controls, output folder, table with timecodes, queue capacity and action buttons are readable. Progress updates do not rebuild unchanged preview rows.
- Existing fractional-fps ranged export, NVENC, clip editing, preview, Resources deletion, independent V1/V2 import, progress/cancel, autosave and queue regression tests pass. Native file-folder pickers and an active long-running production queue were not manually exercised for this feature.
- Limits: 100 jobs including completed history, whole-sequence source references per snapshot, and existing In/Out pre-roll processing. Late timeline jobs can prepare at 0% for a while. Clip endpoints align to output frames; no fast seeking, parallel export, bulk-project selection or standalone Encoder process is added. macOS and large production batches were not tested.
- No installed project, FFmpeg binary or NVIDIA driver was changed. Source ZIP only; project and queue schemas are unchanged.

## Historical 0.9.4 report

Completed: 2026-09-26, from the delivered 0.9.3 source in a separate workspace build.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests build successfully.
- Full regression with external FFmpeg/ffprobe 7.1.1: **47 passed, 0 failed, 0 skipped**; CTest 1/1 passed in 25.05 seconds.
- Updated builder test verifies one clip per selected file, full video durations, ten-second image source durations, mixed backgrounds in order, default mute/enabled audio, and separate 24-hour bounds for each import track. Failed builds preserve the caller's existing result.
- New editor integration test exercises unequal existing V1/V2 lengths, gaps, a later unrelated A2 clip, repeated imports, empty V1 or V2, single-operation Undo/Redo, and save/reopen of independent start times.
- Real FFmpeg export verifies a one-second background ends naturally beneath a longer original: the uncovered corner becomes black rather than repeating, while the original remains visible. Existing muted/enabled audio checks pass.
- Existing preview, timecode, Resources removal, background mode, In/Out, export queue, NVENC and cancellation regression tests pass. Offscreen screenshot inspected: originals end at four seconds while the single image stays at its ten-second duration, with timeline Fit showing both ends.
- The change affects new background-composition imports only. Existing project timelines, including old auto-repeated backgrounds, are loaded without rearrangement. Images still use the editor's existing ten-second default. Both source selections are still required for one import operation.
- macOS and very large playlists were not tested. No project schema change, renderer change, installed-file modification or FFmpeg/driver update.

## Historical 0.9.3 report

Completed: 2026-09-26, from the delivered 0.9.2 source in a separate workspace build.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests build successfully.
- Full regression with FFmpeg/ffprobe 7.1.1: **46 passed, 0 failed, 0 skipped**, 22.415 seconds; CTest 1/1 passed in 22.58 seconds.
- New integration test uses the actual Delete shortcut and Remove All Resources button. It covers unused resources, multiple selection, one source repeated on different tracks, No/default No preserving clean project state, Yes removing every matching clip, and the empty-list disabled button.
- The test verifies remaining clips keep their start positions, source bytes are unchanged, other source files still exist, deletion survives save/reopen and autosave, clearing the sequence leaves a valid export range, and removed clips form one timeline Undo/Redo step.
- Existing editing, preview, timecode, background composition, In/Out, project, export/NVENC, progress/cancel and queue tests pass. Resource deletion does not mutate queued snapshots.
- Offscreen Resources panel and confirmation dialog screenshots inspected: button text, clip/resource counts, Yes/No and details control are readable. The test explicitly reactivates the main window after modal dismissal because the offscreen platform does not restore native window activation automatically.
- Source files are never deleted. Resource-list deletion itself has no Undo; timeline Undo restores clips only, as documented. Native OS file pickers were not automated. macOS was not built or run here.
- No user-installed files, FFmpeg binaries or NVIDIA drivers were modified. Project schema remains unchanged.

## Historical 0.9.2 report

Completed: 2026-09-26, from the delivered 0.9.1 source in a separate workspace build.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests build successfully.
- Existing regression suite: **45 passed, 0 failed, 0 skipped**. CTest 1/1 passed in 20.71 seconds with external FFmpeg/ffprobe 7.1.1.
- Inspected full-window offscreen screenshots in standard and background modes at 1450 px width. Media controls and Resources are adjacent independent columns; all imported original/background files appear in the full-height list. Add to Timeline, preview timecode/playback controls, range controls and timeline are visible without overlap.
- The actual MediaListWidget and its existing import, selection, double-click and drag handlers are reused; only widget parenting/layout changes. Existing import/project/background/preview/queue/export tests pass. No new tests were added for this layout-only change.
- Screenshot platform uses light widget styling; the installed app continues to use the native platform theme. Native OS file-dialog interaction and a manual drag gesture were not automated for this release.
- macOS was not built or run here. No new platform-specific APIs or project schema changes. User-installed files, FFmpeg binaries and NVIDIA drivers were not modified.

## Historical 0.9.1 report

Completed: 2026-09-25, from the delivered 0.9.0 source, in a separate workspace build.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests compile and link successfully.
- Full regression with FFmpeg/ffprobe 7.1.1: **45 passed, 0 failed, 0 skipped**, 22.876 seconds; CTest 1/1 passed in 22.96 seconds.
- Timecode tests cover all eight supported timebases, frame/second/minute/hour rollover, negative time clamping, rounded millisecond frame positions and explicit 29.97 NDF numbering after one hour.
- Ruler tests cover minor/major spacing at four zoom scales, fractional rates, bounded tick count for visible intervals, invalid inputs and stable tick alignment after horizontal scrolling.
- UI tests cover current/total timecode, timebase changes, next/previous frame, save/reopen propagation and clearing the timeline. Existing long/short/24-hour Fit and resize tests pass.
- Fit and detailed timeline screenshots inspected: labels are legible, small ticks and baseline are visible, preview timecode fits its transport row, and all five tracks remain visible. Offscreen screenshots use the test platform's light widget theme; actual app theme follows the native platform.
- Existing background composition, In/Out, FX, audio, export, NVENC, queue, cancellation and project regression tests pass.
- NDF display only; no drop-frame mode. Export progress and In/Out entry remain in seconds. No project format or render changes.
- macOS was not built or run in this Windows environment. The change uses cross-platform Qt APIs. No user-installed files, FFmpeg binaries or NVIDIA driver were modified.

## Historical 0.9.0 report

Completed: 2026-09-25, from the delivered 0.8.2 source. Separate workspace build; the installed Downloads project, working FFmpeg 7.1.1 and NVIDIA driver were not changed.

- Windows x64 MSVC 19.44 / Qt 6.11.2 / Ninja Debug: render library, editor and tests build successfully.
- Full regression: **42 passed, 0 failed, 0 skipped**, 22.14 seconds (CTest 1/1, 22.26 seconds).
- Final focused checks after media-panel width adjustment and hardware coverage: 4 passed including setup/cleanup, 2.28 seconds.
- Multiple originals and mixed backgrounds, exact coverage, repeat/truncate including a 50 ms tail, single-image duration, append placement preserving existing clips, atomic Undo/Redo, clip-role mute and invalid sources tested.
- UI mode switching, background sound toggle, Set In/Set Out from playhead, scope switching, bounds rejection, empty timeline, project/autosave round trips, schema 3 compatibility and malformed range rejection tested.
- Preview/export corner and centre pixels agree after manually shrinking the original. Export audio is silent by default and audible after enabling background sound.
- Fixed a reproduced preview race: effects applied while a seek was pending could activate a decoder twice and leave paused preview black. Decoder activation now waits for the pending seek. The regression reproducer and prior preview tests pass.
- In/Out composition spans an overlay transition and checks resumed base video, black gaps, audio fade phase, progress duration, and exactly 30 output frames at 30 and 29.97 fps.
- Hardware ranged export passed with NVIDIA NVENC available through FFmpeg 7.1.1 on this RTX 3060 / driver 596.21. No driver update.
- Queue stores, reloads and renders range snapshots; per-job dialog range edits do not modify the editor's range. Existing overwrite protection, cancellation and batch tests pass.
- Offscreen UI image inspected after widening the mode panel: both multi-file buttons, audio checkbox and range controls are readable; preview frame and green range markers are visible.
- macOS build/runtime not available here. Very large repeat counts were not stress-tested. Ranged export still processes frames before In (correctness-first, not fast seek). Native OS file-picker interaction was not automated; the actual multi-selection handlers call the same tested import/validation path.

## Historical 0.8.2 report

Completed: 2026-09-25. Built from the actual 0.8.1 source; its src files were hash-compared with the user's installed project before delivery (no differences).

- Windows x64, MSVC 19.44, Qt 6.11.2, Ninja, Debug build succeeded.
- Full regression suite with external FFmpeg/ffprobe 7.1.1: **37 passed, 0 failed, 0 skipped**, about 19.7 seconds; CTest 1/1 passed.
- New tests cover whole-sequence Fit including late muted audio, fractional pixels/second, resizing, move/trim/delete, undo/redo, drag coordinate stability, returning from a scrolled detailed view, ruler seeking, empty/120ms/24-hour sequences, Fit button, and save/load persistence.
- The offscreen 32-minute multitrack timeline render was visually inspected: start and last audio clip are visible, labels are spaced, and clip ends have a right margin.
- Existing edit/preview/FX/compositing/audio/export/progress/cancel/queue tests passed with the new timeline.
- macOS was not built or tested in this Windows environment. No platform-specific timeline API was introduced.
- No installation in Downloads or changes to the user's driver/FFmpeg were performed. The package is source only.

## Historical 0.8.0 report

Completed: 2026-09-24. Based on the actual user-verified 0.7.0 source. Development and validation used a separate workspace copy; the user's Downloads project and build-dev were not modified.

## Build and results

- Windows x64, Visual Studio 2022 Build Tools / MSVC 19.44, Qt 6.11.2, Ninja, CMake 3.30.5, Debug.
- External FFmpeg/ffprobe 4.3.1; Qt Multimedia reports its bundled FFmpeg 7.1.5 backend.
- VideoForgeRender static library, VideoForgeEditor and VideoForgeTests compile and link successfully. The render target links Qt Core/Gui and has no Qt Widgets/Multimedia dependency.
- Final CTest: 1/1 suite passed in approximately 41.4 seconds. QtTest: **35 passed, 0 failed, 0 skipped** (33 test cases plus setup/cleanup).
- The six new queue tests also passed independently before the complete regression run.

## New queue verification

- Snapshot isolation and real sequential output: two jobs retain different FX, frame sizes and rational fps after the caller changes its source data. Actual decoded pixels verify 50% opacity in the first output; ffprobe verifies the second output at 180x320 and 30000/1001. Start events establish job order; completion/progress signals and restored completed states are checked.
- Failure continuation and retry: a source removed after enqueue makes that job fail; the next job completes. Restoring the source and retrying on CPU succeeds. Completed-job retry and duplicate/existing destinations are rejected.
- Stop and cancel: stopping a long export marks it Cancelled, leaves later jobs Pending and publishes no incomplete destination. Reordering/removing pending jobs and restarting work. Cancelling only the current job lets the next job complete.
- Persistence and interrupted recovery: a live export shut down through the queue is recovered as Interrupted, while pending work stays pending. A simulated saved Running record is also recovered as Interrupted without auto-starting. Retry/output rename persist. A malformed journal remains unchanged and disables writes.
- Process ownership: a second queue object cannot acquire the first instance's live process lock or start its jobs.
- Destination protection: source paths and duplicate destinations are rejected. A destination created after enqueue is preserved. A second test creates a file after exporter validation but before final publication; its exact bytes survive and the temporary encoded output is removed.
- Storage failure: replacing the test journal with a directory makes writes fail. Failed removal leaves the in-memory job intact and Start cannot dispatch unrecorded work. This is an I/O-failure simulation, not a physical disk-full test.
- Editor/dialog integration: real Add to Queue controls set per-job dimensions/fps without changing the editor sequence. Later FX edits do not change the queued snapshot. Starting through the queue UI produces a completed output. Closing the queue window leaves export active; declining the editor's interrupt-and-close prompt also leaves it active. Stop and normal project save still work.
- A screenshot of the queue dialog was visually inspected: job rows, per-job settings/status, progress, controls and detail/log area are readable.

## Regression coverage retained

All previous timeline, FX, preview, project and export checks pass: five-track move/trim/split/drop/snapping and undo/redo; project schema compatibility, save/open, relinking and autosave/recovery; gain/fades and mix behavior; decoder preloading, live layer colors, seek/scrub/frame stepping and transport; sequence settings and eight fractional/integer fps outputs; short portrait/square/HD/UHD exports; ordered FX comparisons, transparent-edge blur/zoom checks; export progress, cancellation and source protection.

The earlier preview lifecycle assertion that the window owns one exporter continues to pass after ownership moved under ExportQueue. Offscreen font/RHI/raise warnings and generated-source D3D11 fallback messages are expected on this headless test path.

## Limits and scope

- This release adds current-sequence snapshots to one persistent queue. Bulk project-file selection, per-clip output, schedules, parallel rendering, IPC and a standalone/background VideoForge Encoder are not implemented.
- Closing the queue window hides it. Closing the editor interrupts its current export. Reopening never starts jobs automatically; Retry restarts an interrupted video from the beginning.
- Source media is referenced rather than copied. Replacing source contents can change a queued output. Immutable snapshots cover edit data/settings, not the underlying file bytes.
- No forced process crash, power loss or orphan-process adoption test. A saved Running state and normal shutdown/reopen were exercised. A force kill may leave UUID-named .part.mp4 files or an orphan FFmpeg process; partial files are not adopted/resumed.
- The 100-job / 128 MB limits are enforced but not stress-benchmarked. Snapshot serialization, source probing and graph preparation remain synchronous; FFmpeg encoding runs in a separate process.
- macOS compilation, playback, VideoToolbox and deployment were not available on this Windows host. Hardware encoding across GPU vendors, sustained 4K/60 fps, physical audio synchronization and portable deployment on another PC were not measured. Pixel/output tests use CPU/libx264.
- Existing preview limitations remain: software compositing at monitor resolution, independent Qt audio outputs and small resampling/color differences relative to final export. No new promise of pixel/sample-identical preview or real-time performance.

README.md covers the shared core, storage/locking, output protection and queue semantics. WINDOWS_QUICK_START.txt gives Vietnamese incremental build and usage instructions.
