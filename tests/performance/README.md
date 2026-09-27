# Reproduce the CPU export benchmark

Build this standalone CMake target against the source directory of the version being measured. Use Release and the same Qt/compiler and FFmpeg executables for both versions. Hardware encoding is deliberately disabled, and all cases use the exporter defaults: 1920x1080 at 30 fps, libx264 medium / CRF 18, AAC 192 kbps.

Create a fixture directory and generate media with:

```sh
ffmpeg -y -f lavfi -i testsrc2=s=1920x1080:r=30:d=6 -c:v libx264 -preset ultrafast -crf 18 source.mp4
ffmpeg -y -f lavfi -i testsrc2=s=1920x1080:r=30:d=0.1 -frames:v 1 -threads 1 background.png
```

Run the benchmark executable with three arguments:

```sh
VideoForgeRenderBench /absolute/fixture/directory version_label single
VideoForgeRenderBench /absolute/fixture/directory version_label still
VideoForgeRenderBench /absolute/fixture/directory version_label blur
VideoForgeRenderBench /absolute/fixture/directory version_label video
```

The folder must contain `source.mp4` and `background.png`. Outputs are `<label>-<scenario>.mp4`; repeated runs with the same label overwrite those benchmark outputs. Each run prints elapsed milliseconds and writes the export log beside its output. A 120-second timeout cancels the run; extend it in RenderBench.cpp for slower machines.

Cases: one video; 80% Zoom over a still; 80% Zoom over a blurred still (Blur = 1%); 80% Zoom over a video. Run each version twice with the ordering reversed on the second pass, on an otherwise idle machine. Compare mean elapsed time and verify duration/frame count plus decoded output. These are synthetic tests, not Premiere comparisons or a promise about every timeline.
