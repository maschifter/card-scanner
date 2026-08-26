# @cardnexus/desktop-card-scanner

Placeholder. The desktop wrapper is implemented in a follow-up PR.

This package exists so the three-way split is real and so `card-scanner-core`
has a non-mobile consumer proving it builds standalone.

## What lands here later

- `OnnxSession.cpp` defining `cardscanner::inference::loadSession` against
  ONNX Runtime — the mobile side defines the same symbol against ExecuTorch in
  `mobile-card-scanner/common/backends/executorch/`.
- A frame source (`cv::VideoCapture`); there is no VisionCamera here, and
  `FrameExtractor`/`FrameTransform` are mobile-only by design.
- Platform paths — call `pathprovider::set_db_path` / `set_cache_path` before
  anything touches the database, exactly as the iOS and Android shims do.
- Optionally a websocket/TS surface. Not designed yet.

## Building the placeholder

```sh
cmake -S . -B build && cmake --build build
```

Needs OpenCV on the host (`brew install opencv`). ObjectBox is fetched by
core's CMakeLists at configure time.

Currently links no backend, so `inference::loadSession` is undefined — the
placeholder builds core and a `main()` that prints and exits, which is enough
to prove core has no mobile or runtime dependencies.
