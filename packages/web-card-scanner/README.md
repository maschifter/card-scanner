# @cardnexus/web-card-scanner

Run the native card scanner pipeline in the browser on WebGPU. The package
builds on TIE (`@tie/core` and `@tie/zoo`, vendored as tarballs in `vendor/`
until the npm release). Every model runs in f16 with HWC4 conv storage. Your
page downloads only the model weights (about 20 MB, cached in OPFS) and sends
embeddings to a backend that owns the card databases.

## Quick start

```ts
import { WebCardScanner, httpSearchClient } from '@cardnexus/web-card-scanner';

const scanner = await WebCardScanner.create({
  modelsBaseUrl: '/models', // output of apps/web-example/tools/export-web-models.py
  search: httpSearchClient('/api'), // or your own SearchClient
  onProgress: (stage, done, total) => console.log(stage, done / total),
});

// A <video>, <img> (downscaled to 1280 px) or ImageData; needs pixels.
const { cards, timings } = await scanner.scan(videoElement);
```

Each `ScannedCard` gives you the detection box and quad in frame pixels (of
the downscaled frame for a video or image, see `captureSize`), the dewarped
card image, the L2-normalized embedding, the candidate games, the backend
matches, and the set-symbol or pitch-colour result when the disambiguation
stage ran. Use `ScanTimings` to see where the frame's time went. For a
complete client, see `apps/web-example`: `hooks/useScanner.ts` boots the
scanner and `hooks/useScanLoop.ts` drives the scan loop.

You need:

- A WebGPU device with `shader-f16`. Every current desktop and mobile browser
  that ships WebGPU has it, and `WebCardScanner.create` requests it.
- The `unplugin-typegpu` Vite plugin, because TIE kernel bodies compile at
  build time.
- The `tsover` TypeScript fork to type-check TIE sources (see this package's
  `devDependencies`). Keep it scoped to web workspaces.

## Search backend

Implement `SearchClient` to connect your backend: `searchCards(embedding,
games)` and `searchSetSymbol(embedding)`. `httpSearchClient(apiBaseUrl)`
speaks the reference HTTP API in `apps/web-example/server`, which also owns
the accept gate (top-1 at or above 0.60, or 0.45 with a 0.05 margin over the
runner-up) and keeps only the winning game's matches.

The scanner wraps your client in `cachedSearchClient` by default
(`reuseSearchResults`). A card that stays in view is searched once, and the
answer is reused while its embedding stays within cosine 0.97 of the searched
one (different printings sit near 0.7), for up to 2 s after it was last seen.
Concurrent frames on a new card share one in-flight request.

## Models

- CardSegmentationModel and SetSymbolDetectionModel: the deployed yolo26n
  checkpoints through `@tie/zoo/yolo26` (end-to-end one2one head, no NMS).
- CardRecognitionModel (timm `mobilenetv4_conv_small`),
  SetSymbolRecognitionModel (timm `mobilenetv3_small_075`), and
  ColorBarModel: TIE nn ports of the training repo's PyTorch definitions
  (`~/SWM_AI/card-scanner-models`, package `card_nexus`). Export BN-folded
  weights with `apps/web-example/tools/export-web-models.py`, and check parity against the
  deployed ONNX with the web example's `/parity.html`.

Conv stacks run HWC4 f16 end to end. A model enters the layout with `toHwc4`
at the stem, and only the Linear heads see an f32 row. TIE's conv epilogue
fuses SiLU and nothing else, so `models/ops.ts` composes ReLU, HardSwish, and
HardSigmoid from `clamp` and the scalar ops. Those pass HWC4 storage through
untouched, and the zero-padded channel lanes stay zero through both ReLU and
HardSwish. Global average pooling goes row-major for the mean and returns to
the layout. A squeeze-excite gate broadcasts back over the map with a
nearest-neighbour `upsample2d`, which for a one-pixel map is a copy.

The package consumes TIE unmodified: the vendored tarballs are packed straight
from upstream `main`, with no local patches. Keep `typegpu` on `^0.11`: the
tarballs depend on `^0.11.9`.

## Pipeline

`WebCardScanner.scan` runs: letterbox 384² (288 when the load test is slower
than 40 ms, see `slowGpuInputSize` and `scanner.loadTestMs`) → segmentation →
per detection: mask → quad (hull + approxPolyDP ladder, minAreaRect fallback)
→ perspective dewarp 403×640 → squash 224² → embedding → `SearchClient` → MTG
set-symbol or FaB colour-bar stage under the disambiguation gate, with the
sideways 180° retry.

Model inputs are preprocessed on the GPU (`inference/gpuPreprocess.ts`): one
compute dispatch samples the source through a placement (letterbox, crop, or
squash) into normalized CHW in the model's input buffer.

The frame stays on the GPU (`inference/gpuFrame.ts`). A video frame is copied
straight into a texture and downscaled there (Firefox cannot copy a video, so
there it takes the canvas path); a still is downscaled through a canvas first,
because copying a full-size photo straight to the GPU is slow. The
perspective dewarp is a compute pass on that texture, and the dewarped card's
pixels come back with its embedding in one round trip, for the UI thumbnail
and the stage crops.

If you use React, don't pass `ScannedCard` objects, or anything holding
`ImageData` or typed arrays, as component props. React's development build
deep-walks changed props to log re-renders, and walking a dewarp's pixel
array costs about 800 ms per scan. Map results to a slim view first. The
example's `hooks/useScanLoop.ts` shows the pattern with an `ImageBitmap`
thumbnail.

The pipeline is cross-checked line by line against `card-scanner-core`:
thresholds, decode, mask-to-quad including the topmost-edge orientation rule,
dewarp, flip cache, candidate-game routing, accept gate, disambiguation, and
sticky selection. Where the native C++ app and the training repo disagree,
this port follows the training repo (`card_nexus/config.py`, `inference/`):

- FaB colour logits are `[blue, red, yellow]`. The C++ label array is
  reversed against the deployed checkpoint's `class_to_idx`.
- The FaB indicator crop is the top-left 13.4% × 9.6% of the dewarp, and a
  colour read under 0.70 softmax confidence is discarded.
- The set-symbol crop grows the detected box by 10% before embedding.
- Disambiguation fires on a top-2 margin of 0.05 or less (native default:
  0.02).
- A card is accepted at top-1 of 0.60 or more, or 0.45 with a 0.05 margin
  (native: 0.60 only).
- `minGameConfidence` defaults to 0.1 (`MIN_GAME_CONF`). The native reference
  app passes 1e-5, which leaves only the 3-class and 4-database caps.

## Layout

The layout mirrors the native card-scanner-core:

- `constants.ts` and `types.ts`: every tuned number, and the public result
  and config types.
- `core/`: `setup.ts` (device, weights, runners), `pipeline.ts` (the
  per-frame loop: segmentation, decode, selection), `cardProcessor.ts` (per
  card: dewarp, embed, search, flip retry, stages), `cardSelection.ts`,
  `searchStrategy.ts` (candidate-game routing), `setSymbolProcessor.ts`,
  `fabColorProcessor.ts`, `httpSearchClient.ts`, `cachedSearchClient.ts`,
  `profile.ts`.
- `models/`: the TIE nn ports plus their shared layers and helpers.
- `inference/`: `runner.ts` (captured-frame model runner),
  `gpuPreprocess.ts` (pixels-to-normalized-CHW compute kernel) and
  `gpuFrame.ts` (frame texture, GPU dewarp).
- `utils/`: quad geometry, YOLO decode, box helpers, frame capture.
- `__tests__/`: vitest coverage of the pure parts. Run it with `yarn test`.

## Cross-frame stability

These mirror the native core and the mobile example, so a card doesn't
flicker in and out between frames:

- Group-box drop (`utils/decode.ts`, `dropGroupBoxes`): a detection that
  wraps two or more cards, or one much smaller card of a different aspect, is
  the table or a binder page and is discarded.
- Sticky single-card selection (`core/cardSelection.ts`, `scanMode:
  'single'`): the scanner follows the card it picked (IoU of 0.3 or more) and
  forgets it only after 500 ms unseen. A rival takes over when clearly closer
  to the frame centre, or dead-centre while the tracked card is missed.
- Sideways-flip cache: a sideways card's resolved 180° orientation is
  remembered for 3 s and tried first.
- In the React example, `hooks/useCardConfirmation.ts` confirms a card after
  the same id on 2 consecutive frames (4 when the top two matches are within
  1% or an MTG card has no set symbol), and `components/Viewport.tsx` eases
  boxes toward new detections and holds them 400 ms through a missed
  frame.

Known gaps versus native, all deliberate: no blur, low-light, or frame-rate
gates, and no NMS (YOLO26's one-to-one head doesn't need it). Card corners
come from this package's own convex-hull and Douglas-Peucker code rather than
OpenCV, so a corner can land one or two mask cells from the native one.
