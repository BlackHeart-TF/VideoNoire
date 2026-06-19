# VideoNoire (clipthing)

VideoNoire is a small Qt/C++ desktop video editor focused on fast clip-and-join workflows with a simple multi-track timeline.

Current scope:

- Add source media, arrange it on a simple multi-track timeline, then clip and join it.
- Preview sources and timeline positions with GStreamer playback.
- Mix timeline audio clips into the exported MP4.
- Show lightweight hover thumbnails for video timeline clips.

Interactive preview uses GStreamer. Export and hover thumbnail extraction are delegated to `ffmpeg`, which keeps rendering predictable while the editor is still small.

## Requirements

- CMake 3.21 or newer
- A C++17 compiler
- Qt 6.5 or newer with the Widgets module
- GStreamer development headers for `gstreamer-1.0` and `gstreamer-video-1.0`
- GStreamer runtime plugins for the media formats you want to preview
- FFmpeg available on `PATH`

## Build On Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/clipthing
```

On Arch Linux, the usual packages are:

```sh
sudo pacman -S cmake ninja qt6-base gstreamer gst-plugins-base gst-plugins-good gst-plugins-bad gst-plugins-ugly gst-libav ffmpeg
```

If CMake is run from a polluted IDE shell and reports `Could not find CMAKE_ROOT`, configure from a clean environment:

```sh
env -i PATH=/usr/bin:/bin HOME="$HOME" cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```


## Build On Windows

Install Qt 6, GStreamer for Windows, and FFmpeg. Configure with CMake from a Qt-enabled developer shell where GStreamer `pkg-config` files are available:

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\clipthing.exe
```

Make sure `ffmpeg.exe` is available on `PATH` at runtime.

Cross-compiling a Windows build from Linux requires Windows-target Qt and GStreamer development packages. A MinGW compiler alone is not enough; CMake must find Windows Qt and Windows GStreamer libraries, not the host Linux ones.

## Notes

The exporter normalizes video clips to H.264/AAC MP4 segments before joining them. Timeline audio clips are rendered into a temporary audio bed and muxed into the final MP4.

The timeline currently exposes multiple video tracks, but export treats video clips as a sorted clip-and-join sequence rather than a true layered video compositor. Audio clips are layered and mixed.
