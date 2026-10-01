# WebGPU is slow on one Windows machine — notes, not a TIE bug report

Status: **not currently a TIE defect.** An earlier draft of this file claimed
TIE's HWC4 f16 conv family was ~60x slower than ONNX Runtime on this machine.
That claim was wrong and has been withdrawn. Do not send it upstream.

## What the measurements actually show

Segmentation GPU wait on the Windows machine, measured with the camera
stopped on a still frame (the app's engine selector at the time ran the
identical pipeline on each engine, so nothing but the engine differed):

| engine | segmentation GPU wait |
| --- | --- |
| TIE, WebGPU | ~50 ms |
| onnxruntime-web, WebGPU | ~50 ms |
| onnxruntime-web, WASM | ~30 ms |

Two conclusions follow:

- **TIE is not the outlier.** It performs the same as a mature, heavily
  optimised runtime on the same GPU, running the same model. Whatever is
  slow here is slow for both.
- **The GPU loses to the CPU.** A WASM execution provider beating two
  independent WebGPU implementations is characteristic of a weak integrated
  GPU, a poor D3D12/ANGLE path, or GPU power limits — not of any one
  engine's kernels.

For contrast, on Apple/Metal the same frame is 11 to 12.6 ms on TIE, and TIE
beats ort-web WebGPU there (4.4 ms vs 8.7 ms in the isolated benchmark).

## How the earlier wrong conclusion happened

Two mistakes, both worth avoiding next time:

1. An earlier figure of ~200 ms for the segmentation frame was taken **while
   the live camera loop was running**, so it included the loop competing for
   the same queue. The uncontended figure is ~50 ms. Always profile from a
   still image with the camera stopped.
2. The engine A/B was reported qualitatively ("ONNX has significantly less
   GPU wait") and written up before the numbers were in hand. The numbers
   showed the two engines level.

## What is still open

- The live camera loop on Windows was reported at 200 to 500 ms per
  segmentation, against ~50 ms measured in isolation. That degradation under
  sustained load is real and unexplained. The open question is whether the
  ONNX engine degrades the same way in the loop; if it does, the cause is the
  machine under sustained load rather than anything engine-specific.
- The adapter string for the machine has not been recorded. The app prints it
  in the Profile GPU panel; it would settle the weak-iGPU hypothesis quickly.

## One genuine limitation worth raising with TIE eventually

TIE is WebGPU-only. `feat: hwc4+f16 conv storage, replacing the chw conv
family` also removed the CHW f32 conv kernels, so there is a single conv path
and it is f16-only. On a machine like this one — where a CPU execution
provider is the fastest option available — a TIE consumer has nothing to fall
back to, and `supportsF16()` returns true, so the capability check does not
predict poor performance. That is a portability observation, not a
performance bug, and it should be raised as such.

## How to reproduce

```sh
yarn workspace web-example server   # backend (search only; not on the hot path)
yarn workspace web-example dev      # client
```

- Per-kernel GPU times: scan once, press **Profile GPU**. Profile from a still
  image with the camera stopped. `/bench.html` reports each model's total and
  slowest-kernel GPU time at the end of a run.
- Timing the whole pipeline: `/bench.html` over the benchmark photos, with
  `?runs=` and `?warmup=` to set the iteration counts. That measures one engine
  build against another, not one engine against a different one.
