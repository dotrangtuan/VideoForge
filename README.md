# VideoForge Editor 0.10.3

## New in 0.10.3 — render pipeline optimization

Retains the 0.10.2 range-seek fix. Still images are decoded/transformed once and their processed frame is reused. Exports with at least 1280x720 pixels use up to four filter threads (bounded by the available logical CPUs), with one thread below 720p. Known opaque, canvas-filling video with only a single effective Zoom (other than 100%) uses a compact RGB layer, avoiding redundant alpha processing and transparent canvas padding. Other aspect ratios, unknown/alpha pixel formats, rotated sources and complex FX use the general compositor. Neutral Crop/Opacity operations are omitted. Source stream metadata is reused and invalidated when file size or modification time changes.

Encoder choices, CPU `medium` / CRF 18, hardware target 10 Mbps, AAC 192 kbps, output dimensions and frame rate are unchanged. This is still CPU decode/compositing with optional hardware H.264 encoding, not a complete GPU pipeline. The export log now reports filter threads, cached stills, compact layers and actual encoder settings.

See `RENDER_PERFORMANCE_REPORT.md` for measurements and validation limits. See `CAP_NHAT_WINDOWS.txt` for update commands, including a forced rebuild if Ninja reports no work.

## New in 0.10.2 — bounded range rendering

Fixes increasing preparation time for later V2 queue jobs. The renderer now builds only the requested frame-aligned interval, excludes non-overlapping/inactive inputs, and performs accurate input seeking before decoding video/audio. Still images loop only over the needed interval. Gain/fades are evaluated on the original clip clock; layer order, mute/visibility and the H.264/AAC settings remain supported.

This patch is based on the 0.10.0 ZIP supplied in this conversation. It does not incorporate a separate 0.10.1 package. Project/queue schemas are unchanged. Pending queue snapshots use the updated renderer after restart.

See `CAP_NHAT_WINDOWS.txt` for CMD instructions and `QUEUE_SEEK_FIX_REPORT.md` for measured results and test limits. There is still normal file probing, keyframe lead-in decoding and encoder initialization; zero startup time is not promised. Non-seekable/unindexed inputs and unusual timestamp layouts may still seek slowly.

## New in 0.10.0 — batch queue from V2 clips

Open **Export > Add V2 Videos to Queue...**. Choose the first **N** video clips from left to right or **All videos on V2**, select an output folder, then **Add to Queue**. The count is limited to video clips currently on V2; still images and audio-only clips are excluded. Split clips count separately. The preview table shows names, starts and durations using sequence timecode.

- One MP4 job is created per chosen V2 clip, sorted by timeline start time, using its current trimmed duration. Each job exports the complete timeline composition over that interval: V1 background, V3 overlays, ordered FX and mixed audio, respecting visibility and mute. Editor In/Out is ignored. Gaps between V2 clips are not added to either output.
- Names follow V2 clip names, falling back to source filenames. Video extensions become `.mp4`; unsupported filename characters and Windows device names are made safe. Existing files, source paths, queued outputs and duplicates within the new batch are avoided with ` (2)`, ` (3)`, etc. Existing files are never overwritten.
- Output dimensions and fps use current Sequence Settings; hardware preference can be changed in the batch dialog. Boundaries use the output frame grid, so sub-frame endpoints can differ by less than one frame from their millisecond duration.
- Jobs store snapshots of the current edits. Start a stopped queue with **Start / Resume**; adding alone does not start it. If the queue is already running, new jobs join its pending tail. Later edits do not modify queued jobs.
- The existing 100-job limit includes completed jobs. A batch commits all at once; invalid sources, insufficient capacity or a persistence error leave no partial batch. Remove old jobs to make room. File bytes remain referenced, not copied.
- As of 0.10.2, range jobs seek to the selected source interval and compose on a local clock. Jobs remain sequential. Native macOS and large production-scale batches were not tested.

Project and queue formats are unchanged. See TEST_REPORT.md for Windows checks.

## New in 0.9.4 — append originals and backgrounds independently

Background composition now adds each selected original video once, in order, after the last clip on **V2**, and each selected background once, in order, after the last clip on **V1**. The two tracks have independent end times. An empty track starts at zero; clips on V3/A1/A2 do not delay either list.

Videos use their full source duration; each image starts at ten seconds. No background repetition, end trimming, image stretching or matching track lengths is performed. Both source selectors still support multiple files; selecting both lists commits one addition with timeline Undo/Redo. Original framing stays unchanged and background audio defaults muted.

This applies to new imports only. Existing timeline arrangements and saved projects remain intact. To replace an old auto-repeated arrangement, remove its clips and import again. Whole-timeline export still runs to the latest clip end across all tracks; choose In/Out when a shorter output is wanted.

## New in 0.9.3 — remove imported resources

- **Remove All Resources** clears the Resources list. Select one or more resources and press **Delete** to remove only that selection; Ctrl/Shift multi-selection is supported. Delete is scoped to the Resources list and retains its existing clip-delete behavior when the timeline has focus.
- If any target resource is used on the timeline, one Yes/No dialog shows the resource and matching clip counts. **No** (also the default/Escape choice) leaves everything unchanged. **Yes** removes the resources and every clip using those files across all tracks, preserving the positions of remaining clips. Details lists the selected resource names.
- Resources unused on the current timeline are removed directly. Removing all resources affects clips using those listed resources; it does not clear unrelated clips whose source is absent from Resources.
- Source files on disk and previously queued export snapshots are preserved. Removal is included in project save/autosave. The all-resources button is disabled when the list is empty.
- Matching timeline clips are removed in one timeline Undo step. Undo restores those clips only; it does not restore Resources entries. Use Import Media to add the source files to Resources again.

Windows MSVC / Qt 6.11.2 checks are recorded in TEST_REPORT.md; macOS was not tested. No project schema changes or FFmpeg/driver changes.

## New in 0.9.2 — separate Resources column

- The workspace now has four adjacent, resizable columns: **Media**, **Resources**, **Program Monitor**, and **Video FX / Export**.
- Media contains editing mode, background setup and Import Media. Resources contains the imported media list and Add to Timeline; the list uses the available column height in both editing modes.
- Existing multiple selection, drag to timeline and double-click to add behavior is preserved. Original/background imports and reopened projects populate the same Resources list.
- The preview scrub slider and HH:MM:SS:FF display have their own row above the playback/edit buttons to make room for the additional column.

No project-format, import, composition or export changes. Keep the working build-dev and FFmpeg binaries when updating source. Windows validation and macOS limitations are recorded in TEST_REPORT.md.

## New in 0.9.1 — preview timecode and timeline ruler

- The Program Monitor displays current position / sequence duration as **HH:MM:SS:FF** (hours, minutes, seconds, frames), using the sequence timebase. Frame stepping, seeking, changing timebase and reopening a project update this display.
- The timeline uses the same timecode, with major labels, medium/minor ticks and a yellow-green baseline. Labels adapt to zoom and stay spaced apart. At close zoom the divisions fall on individual frames where space permits; Fit still shows the entire sequence.
- Timecode is **non-drop-frame (NDF)**. For 23.976 / 29.97 / 59.94 fps, numbering uses nominal 24 / 30 / 60 fps while positions use the exact rational timebase. Therefore the displayed timecode can differ from elapsed wall-clock time. A drop-frame option is not included.
- This is a display change: source timing, sequence fps and export behavior are preserved. Export progress and In/Out numeric entry remain in seconds. Project schema remains 4; 0.9.0 projects open directly.

Windows MSVC / Qt 6.11.2 validation is recorded in TEST_REPORT.md. macOS was not available for testing. Keep the working FFmpeg binaries in build-dev when updating source.

## New in 0.9.0 — background composition and export ranges

### Editing modes

The Media panel defaults to **Standard edit**. Choose **Background composition** to reveal two multiple-file selectors:

1. **Original videos... (V2)** selects one or more videos.
2. **Background files... (V1)** selects one or more videos/images. Either selector can be used first. Selecting both appends each list after the last clip on its own track, as one Undo operation.
3. Originals follow the file dialog's returned order on V2. Their size/effects are unchanged. Adjust Zoom/Crop in Video FX when the original covers the background.
4. As of 0.9.4, backgrounds are appended once each to V1 at their own durations (ten seconds per image). Earlier 0.9.0–0.9.3 releases repeated/trimmed backgrounds to match originals; new imports no longer do that.
5. Background audio defaults off. **Background audio** enables/disables audio on all clips tagged as backgrounds; regular clips and original audio are unaffected. Track mute still takes precedence. Muted backgrounds display `[BG muted]` on their clips.

Switching modes changes the import controls, never deletes or rearranges existing clips. After insertion all clips can be moved, trimmed, split, given FX and deleted normally. Imported groups and background audio changes support Undo/Redo. Existing media-bin import/drop remains available in both modes. New selections append to each track independently; they do not replace previous media or fill gaps between existing clips.

### Whole sequence or In/Out export

- In **Export**, choose **Entire timeline** or **In / Out range**. Enter seconds, or position the playhead and click **Set In** / **Set Out**. When the timeline has keyboard focus, **I** and **O** do the same.
- The ruler highlights the selected range in green. Out is exclusive. Points are aligned to sequence frames; invalid, reversed or sub-frame ranges are rejected. Range endpoints are clamped when edits shorten the sequence.
- The queue dialog also offers a scope and editable In/Out values for that job. It keeps a snapshot of those values independently from future edits. Its details show the selected range and duration.
- Ranged export cuts the composed picture and mixed sound together, preserving source trims, timeline gaps, layering, FX and audio fade phase. Output timestamps restart at zero; progress/cancel refer to the selected interval. Fractional timebases use frame indices for video and the corresponding rational timestamps for audio.
- As of 0.10.2, the renderer seeks each overlapping input and renders only the selected interval. This is a composited re-encode, with cancellation supported.

Mode, clip role/mute and export range are saved in project/autosave/queue snapshots (project schema 4). Projects from schema 1–3 still load with standard edit and whole-timeline export. Use 0.9.0 to reopen projects saved by this version; older versions do not understand the new schema.

Windows MSVC/Qt 6.11.2 and FFmpeg 7.1.1 were tested, including NVIDIA NVENC ranged export. No driver update is needed on the tested RTX 3060 / driver 596.21. macOS is not tested. Very large playlists have not been stress-tested; the existing 24-hour and 10,000-clip bounds apply. Source ZIP only: keep the working FFmpeg binaries in build-dev.

## New in 0.8.2 — fit the timeline to the sequence

- Drag Zoom all the way left, click **Fit**, or press **Ctrl+Shift+F** to show the sequence from zero to the latest clip end across V1/V2/V3/A1/A2, including hidden and muted clips.
- New projects start fitted. Fit adjusts after adding, moving, trimming or deleting clips, undo/redo, loading a project, and resizing the window. A small right margin keeps the last clip edge visible.
- Zoom transitions logarithmically from Fit to detail; a visible playhead stays anchored when zooming. Time ruler spacing adapts from fractions of a second to minutes/hours.
- Fit is saved in projects/autosaves. Existing projects remain readable; their saved zoom levels now scale relative to sequence length and window width.
- Clip drag scale stays fixed during the gesture; Fit is recalculated on release to avoid a moving target.

Windows MSVC / Qt 6.11.2 build and regression tests passed. macOS uses the same Qt implementation, but has not been built or tested for this release. FFmpeg encoder diagnostics from 0.8.1 are included. No change to rendering, export queues or installed FFmpeg files.

C++17 / Qt Widgets + Multimedia editor, continued from the user-verified 0.7 Video FX project. Builds with MSVC and Qt 6.11.2 on Windows. External FFmpeg/ffprobe are used for probing and final export; preview plays source media directly.

## New in 0.8 — persistent batch export

1. Open the **Export** tab and click **Add to Export Queue...**.
2. Choose a job name, a new MP4 filename, frame size, timebase and preferred encoder. These output settings do not change the editor's sequence settings.
3. Click **Add to Queue**. Edit the sequence or change the output settings and add another job as needed.
4. Click **Start / Resume** in the queue window. **Export / Start Queue...** adds a job and starts all pending jobs in their current order.

Each job stores a private snapshot of clip positions/trims, ordered FX, gain/fades, track visibility/mute and output settings. Editing or opening another project afterward does not change that snapshot. Media files are referenced, not copied: moving, deleting or replacing a source file can affect a queued export. Version 0.10.0 adds V2 per-clip batch export. Bulk selection of `.vfproj` files is not included; open projects individually to enqueue them into the same queue.

### Queue controls

- Jobs render one at a time through the same FFmpeg pipeline as preview/export settings. A failed job records its error and the queue continues to the next pending job. There are no per-job completion message boxes to block unattended operation.
- Each row shows name/output, dimensions/fps, encoder preference, state and percentage. Select it for the full output path, current elapsed time, remaining-time estimate, speed and the last 16 KB of log text.
- **Cancel current** cancels one job and continues to the next. **Stop queue** cancels the active job and leaves the rest pending. This is not a resumable pause within a partially encoded video.
- **Retry using CPU** changes a Failed/Cancelled/Interrupted job back to Pending and selects CPU/libx264. Click Start if the queue is stopped. Retry starts that video from the beginning.
- **Change output...** changes the filename of a non-running, non-completed job. This does not change its stored timeline. If media is missing, restore it at the recorded path, or relink in the editor and add a fresh job.
- **Up / Down** reorder adjacent Pending jobs. **Remove job** removes a non-running row and its stored snapshot; it never deletes an exported video.
- Queued exports never overwrite existing files. Duplicate destinations and source-media destinations are rejected. The path is checked again when starting and when publishing the encoded file. Choose a new filename if another program creates the destination while the job is waiting/running.

### Persistence and closing

Queue mutations and job state transitions are atomically saved in `queue.json` under Qt's application-local data directory, normally `%LOCALAPPDATA%/VideoForge/VideoForge Editor/export-queue/queue.json` on Windows. Set `VIDEOFORGE_QUEUE_DIR` to use a different folder. The queue is separate from `.vfproj` and project autosave. It is shared across projects for the current user, with a process lock preventing two editor instances from controlling it simultaneously. Close the owning instance and restart a second instance to acquire the queue.

Closing the queue window only hides it; export continues. Closing the editor asks whether to interrupt an active export. Pending jobs remain saved. The interrupted job is marked Interrupted on reopen; choose Retry and Start explicitly. Restored queues never auto-start. There is no standalone background encoder application or service in this release.

If the queue file is invalid, it is preserved and queue writes are disabled; keep a backup, move the invalid file aside and restart to create an empty queue. A failed journal write stops further dispatch rather than starting unrecorded work. Force-killing the application or a power loss can leave UUID-named `.part.mp4` files beside the destination; these are not completed outputs and are not adopted or resumed automatically.

### Shared render core

`VideoForgeRender` is a separate static CMake target linked by the editor and tests. It includes the clip/project data model, project JSON codec, ordered video effects, FFmpeg exporter and `ExportQueue` scheduler. It links Qt Core/Gui without Qt Widgets or Multimedia. The dialog and editor actions live in `ExportQueueDialog` and `QueueActions`. This prepares reuse by a future VideoForge Encoder; there is no IPC or separate encoder executable yet.

Limits: 100 retained jobs (including completed history) and a 128 MB queue file. Remove old rows to free queue capacity. Job settings are limited to frame size, the existing eight timebases and hardware preference; H.264/AAC MP4 settings otherwise retain the existing exporter defaults. No scheduling, parallel renders or automatic reframe. Large snapshot serialization, probing and graph setup still run on the UI thread; encoding itself runs in FFmpeg's separate process.

## Ordered video effects retained from 0.7

Select a video or image clip on V1/V2/V3 and open the **Video FX** tab to the right of the monitor. Choose an effect and click **Add FX**. Each added effect goes to the bottom of the list. Effects run from top to bottom in the order chosen by the user; duplicate effects are allowed.

| Effect | Controls | Behavior |
| --- | --- | --- |
| Crop | Left / Top / Right / Bottom, percent | Removes edges to transparency. Opposite edges must total 99% or less. Does not enlarge the remaining picture. |
| Blur | Radius, 0–5% of the shorter frame edge | One horizontal and one vertical box-blur pass, including alpha. Transparent edges are softened. |
| Opacity | 0–100% | Multiplies the clip's existing alpha; lower video tracks show through. Does not change audio volume. |
| Zoom | Scale, 10–400% | Scales around the center. Smaller pictures expose lower tracks; enlarged content outside the frame is clipped. |

- Source media is fitted into a transparent sequence-sized canvas first. Crop percentages and blur radius refer to that canvas, including any padding. The stack runs on that canvas before V1/V2/V3 compositing. Each effect outputs the same canvas size, so clipped content is not recovered by a later effect. Zoom is static and centered, with no pan controls or animation yet.
- For example, **Crop → Zoom** enlarges the cropped result; **Zoom → Crop** crops the enlarged result. Use **Up / Down** to change order, the checkbox to bypass an effect, and **Remove** to delete it.
- Spinner arrows apply immediately. When typing a number, press Enter or leave the field to apply. Save and Export commit pending values. Invalid crop totals restore the previous valid values.
- Add, edit, bypass, reorder and remove are undoable using the existing Undo/Redo controls. Splitting copies the stack to both halves; trimming keeps the stack. Up to 16 effects per clip.
- Preview updates without Build Preview. FX-only changes retain the decoded frame instead of reopening its source. Paused/static rendered layers are cached.
- Save/open, autosave/recovery and media relinking preserve the stack and its order. Old schema 1/2 projects load with empty stacks. Effects on a video clip moved to A1/A2 remain stored but inactive; move it back to a video track to use them.
- Export uses the same order and normalized geometry through FFmpeg filters. Preview works at monitor resolution using Qt image processing; scaling, blur rounding, color conversion and H.264 compression can produce small pixel differences. It is not a pixel-identical reference render.

Export controls are now under the **Export** tab beside Video FX. Progress, cancel and hardware-encoder selection are retained.

## Sequence Settings retained from 0.6

Open **Sequence > Sequence Settings...**, or click the settings button in the toolbar.

- Frame size presets: HD 1280x720, Full HD 1920x1080, Portrait 1080x1920, Square 1080x1080 and UHD 3840x2160. Custom width and height accept even numbers from 64 to 4096 pixels. Output uses square pixels and progressive video.
- Timebase: 23.976, 24, 25, 29.97, 30, 50, 59.94 or 60 fps. Fractional rates are stored and exported as exact ratios 24000/1001, 30000/1001 and 60000/1001.
- OK applies the settings to the entire sequence; Cancel keeps the previous values. Clip positions, trims, audio fades and playback speed stay unchanged when changing timebase.
- Preview adopts the canvas aspect ratio immediately. Sources fit inside the frame, preserving their proportions with black padding where needed. The software monitor fits within 640x640 pixels; export uses the full selected frame size.
- Use the transport's previous/next Frame buttons, or Left/Right while the timeline has focus, to step on the sequence frame grid. The position display includes a zero-based frame index (`F`). It is not SMPTE or drop-frame timecode.
- Save/open and autosave/recovery retain both settings. Version 1 projects from 0.5 load at the previous default, 1280x720 / 30 fps. Version 2 projects from 0.6 retain their sequence settings. Project saves still use schema version 3 from 0.7; queue metadata uses its own version 1 schema and is stored separately. Versions before 0.7 cannot open schema 3 projects.
- FFmpeg exports constant frame rate using the selected ratio, dropping or duplicating source frames as needed. There is no optical-flow interpolation. Output duration can round to a complete frame.

Default sequence settings remain 1280x720 / 30 fps.

## Project and editing features retained from 0.5

### Projects and recovery

- File > New/Open/Save/Save As; Ctrl+N/O/S and the platform Save As shortcut (Cmd equivalents on macOS).
- `.vfproj` stores the media bin, clip positions/source trims/tracks, gain/fades, ordered video effects, track visibility/mute, playhead, zoom and snapping. Media itself is referenced, not embedded. Paths are relative to the project where possible, so a project moved together with its media folder can still resolve its sources.
- A `*` in the title marks unsaved changes. New, Open and Close offer Save/Discard/Cancel. Failed saves preserve the unsaved state. Save commits an in-progress audio-field edit before writing.
- Project writes use a temporary file and atomic replacement. Invalid JSON, unsupported schema, invalid timing/track/audio settings, duplicate clip IDs or same-track overlaps are rejected without replacing the current project.
- Autosave every 30 seconds while there are committed unsaved changes. Each editor session has its own recovery file and a process lock. Autosave does not overwrite the main project.
- On restart, the newest available autosaved session is offered for recovery. No keeps it available through File > Recover Autosave. A recovered project remains unsaved until you save it. Sessions still open in another editor are skipped.
- Recovery files live under Qt's application-local data folder in `recovery` (normally `%LOCALAPPDATA%/VideoForge/VideoForge Editor/recovery` on Windows). The optional `VIDEOFORGE_RECOVERY_DIR` environment variable can override that folder for testing or a custom location.
- A successful normal save removes that session's autosave. A clean New/Open/Close after Save or Discard removes it as well. An abrupt exit leaves the last successful autosave available; up to 30 seconds of committed work can be lost. Entries still being typed are committed with Enter, leaving the field, Save or Close.

### Offline media

Missing files stay on the timeline. The media bin marks them `[Offline]`; File > Relink Missing Media asks for replacements. Relinking changes every clip referencing that path while keeping sequence positions and source trims. Replacement media must supply the required stream type and reach all retained out points. Relinking resets clip undo history. Missing sources remain silent/black in preview and must be resolved before export.

### Editing and audio

- Snapping aligns a dragged clip's start/end or a trim edge to the playhead, time zero or another clip edge within approximately eight pixels. Toggle Snapping in the toolbar or hold Alt while moving/trimming to bypass it. Existing same-track collision rejection remains.
- Select a video/audio clip and set Clip volume (0–200%), Fade in and Fade out (seconds) in the toolbar. Enter or leaving the field applies the edit. Changes are undoable and saved with the project.
- Fades are linear audio envelopes at the clip boundaries; overlapping fade-in/out envelopes multiply. Trimming caps fades to the remaining duration. Splitting keeps the outer fades, removes fades at the new internal cut and caps them to each half; splitting inside an existing fade can change its slope.
- Preview applies the same gain and fade envelope, updated on the transport timer. Its fixed 50% monitor master level leaves headroom for 200% clip gain. Export applies gain/fades before mixing, then hard peak protection. Preview uses the system mixer and is not sample-identical to export.
- Backspace/Delete removes a clip when the timeline has focus; Backspace in audio fields edits the number.

### Direct preview

No Build Preview step. Drag the ruler/transport slider to scrub and press Space to play/pause. A silent standby decoder preloads the next clip on each track up to two seconds ahead, then the player is reused at the cut. At most ten source players exist (five active, five standby). This reduces source-opening work at cuts; it does not guarantee zero latency or sample-accurate transitions.

## Existing features retained

V1/V2/V3 + A1/A2; media-bin/file-manager drag/drop; independent move/trim/split (Ctrl+B); non-ripple delete; clip undo/redo; timeline zoom; PNG alpha; track visibility/mute; direct layered preview; FFmpeg compositing and audio mixing; export progress, elapsed time, ETA, speed, cancel; hardware encoder detection with CPU fallback when no usable hardware encoder is found.

Video on A1/A2 plays audio only. Video track visibility affects picture independently of its audio mute. Same-track overlaps are rejected. Uncovered time is black/silent. Export uses the sequence frame size and timebase, with H.264 video and stereo 48kHz AAC audio.

## Windows — update your current project without rebuilding everything

1. Close VideoForge.
2. Extract this ZIP, then copy **the entire `src`, `scripts`, `tests`, `CMakeLists.txt` and documentation** into the existing project folder, replacing old source files. New source files must be included. Keep the original `build-dev` folder where it is; do not move its CMake cache to a different source directory.
3. Open Developer PowerShell for VS 2022 configured for x64. For the original project location:

```powershell
cd "C:\Users\Admin\Downloads\VideoForgeEditor_0.3.0_CrossPlatform"
$env:Path = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.11.2\msvc2022_64\bin;C:\ffmpeg\bin;$env:Path"
cmake --build build-dev --parallel
.\build-dev\VideoForgeEditor.exe
```

The old folder name can stay; the application identifies itself as 0.10.3. CMake automatically picks up the updated source list. Later edits need only the last two commands. Adjust the source/FFmpeg paths if yours differ. Keep the working ffmpeg.exe and ffprobe.exe beside VideoForgeEditor.exe in build-dev when updating the source.

## Fresh Windows extraction

In the new source directory, from the same x64 developer shell:

```powershell
$env:Path = "C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Qt\6.11.2\msvc2022_64\bin;C:\ffmpeg\bin;$env:Path"
cmake -S . -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="C:\Qt\6.11.2\msvc2022_64"
cmake --build build-dev --parallel
.\build-dev\VideoForgeEditor.exe
```

Both FFmpeg and ffprobe must be in PATH or beside the executable. The source ZIP does not include Qt, FFmpeg or a portable binary. The existing release packaging script remains in `scripts/build_windows.ps1`.

## macOS

Cross-platform Qt/QProcess code, bundle settings and VideoToolbox export selection remain present. macOS was not available for build or playback verification.

```sh
brew install qt ffmpeg cmake
cmake -S . -B build-dev -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-dev --parallel
open build-dev/VideoForgeEditor.app
```

## Tests

Qt Test and FFmpeg/ffprobe are needed in addition to the normal build dependencies.

```powershell
cmake -S . -B build-dev -DBUILD_TESTING=ON
cmake --build build-dev --parallel
ctest --test-dir build-dev --output-on-failure
Get-Content .\build-dev\test-results.txt
```

Tests use temporary generated media and a separate temporary recovery directory. See TEST_REPORT.md for results and limits.

## Remaining limits

- Software picture compositing within 640x640 (640x360 for a 16:9 sequence); source decode stays at original resolution. No proxy generation or GPU compositor. Long blur/effect stacks can reduce preview frame rate. Heavy 4K/HEVC/VFR sources may lag, and preloading adds decoder/memory usage.
- Timeline positions remain millisecond-based. Frame stepping computes each boundary from the rational timebase and rounds to the nearest millisecond without accumulating rounding drift. Free dragging and trimming are not constrained to whole frames. Source seeking depends on Qt's decoder/backend; direct preview is not a frame- or sample-identical rendering of export.
- Live audio is played through independent Qt outputs, not a sample-accurate custom mixer. Drift over 120ms triggers periodic resynchronization; backend/device latency can remain. Final export is the timing reference.
- Projects store edits and references, not copies of source media. Undo history and current clip selection are not saved. Undo does not track visibility/mute or sequence settings changes. There is no automatic merging or protection against two editor instances saving the same normal project file.
- Limits: 24 hours per source/sequence, 10000 project clips/media entries, 16 MB project file. No stress benchmark was performed at those limits. Still images have a ten-second editable source range. Clips must be at least 120ms.
- No video transitions, titles/subtitles, keyframes, HDR/color-management pipeline or waveform display yet. Long 4K/60 fps exports and hardware encoding across vendors have not been benchmarked.
- Import/relink probes run synchronously. Codec support follows the installed Qt/FFmpeg builds. Recovery's process-lock detection follows Qt/platform behavior.

Implementation references: [Qt QSaveFile](https://doc.qt.io/qt-6/qsavefile.html), [Qt QLockFile](https://doc.qt.io/qt-6/qlockfile.html), [Qt QVideoSink](https://doc.qt.io/qt-6/qvideosink.html).
