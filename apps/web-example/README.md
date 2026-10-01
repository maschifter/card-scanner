# web-example

Use this React client as the reference for `@cardnexus/web-card-scanner`.
Camera frames run through every model on WebGPU, and only the 256-d
embeddings travel to a backend that owns the card databases. Your browser
never downloads a database; the one-time model download is about 20 MB,
cached in OPFS.

## One-time setup

The model weights, their parity fixtures and the search databases are
committed through Git LFS (install it as the
[root README](../../README.md#prerequisites) describes). From the repo root:

```sh
git lfs install
git lfs pull --include "apps/web-example/**"   # or plain `git lfs pull` for every app
yarn
```

A backend that exits with `bad magic` is reading LFS pointer files: run the
pull above.

## Run

```sh
yarn workspace web-example server   # backend on :8787
yarn workspace web-example dev      # vite client on :5173 (proxies /api)
```

Open the app, then start the webcam or pick an image. Add `?img=<url>` to
scan a fixed image on load.

Open `/parity.html` (no backend needed) to check the TIE model ports against
the ONNX fixtures captured by the export script. Click Profile GPU in the
stats panel to list per-kernel GPU time for the segmentation model on devices
with `timestamp-query`.

Open `/bench.html` to time the pipeline over the mobile example's benchmark
photos (currently 92): two warm-up and ten measured scans per photo, stage
averages over the scans that ran the full pipeline, per-model GPU time and
slowest kernel. It saves a summary JSON similar to the desktop
summaries, with a per-photo list to compare two runs. The search backend must
be running. `?runs=`, `?warmup=`, `?limit=` and `?games=mtg,fab` narrow a run,
and `?input=288` picks the segmentation input size (default 384; the bench
never uses the slow-GPU fallback). The photos and their card ids are served
straight out of `apps/mobile-example` by `tools/benchmarkImagesPlugin.ts`.

Measure a slow machine on the production build, since the development build
costs it real main-thread time:

```sh
BENCH=1 yarn workspace web-example build
yarn workspace web-example preview   # https://localhost:4173, proxies /api
```

On Windows, set `BENCH=1` with your shell's own syntax: PowerShell and cmd
reject the inline form.

Run `yarn workspace web-example test` for the backend's accept-gate tests and
`yarn workspace web-example lint` to check formatting.

The dev server listens on all interfaces over self-signed HTTPS, so you can
open the client from another machine at `https://<this-machine-ip>:5173`.
Accept the certificate warning once. `/api` proxies to the backend on this
machine.

## Regenerating the assets

Only needed when the models or the mobile example's databases change. The
database export reads the mobile example's `.mdb` files, which are in Git LFS
too, so pull them first (a plain `git lfs pull` does).

```sh
# From the repo root. Needs: pip install lmdb numpy onnx onnxruntime safetensors
python3 apps/web-example/tools/export-web-models.py \
    --bundle <path-to-deployed-models-bundle> \
    --out apps/web-example/public/models
python3 apps/web-example/tools/export-web-db.py \
    --assets apps/mobile-example/assets \
    --out apps/web-example/server/data
```

## Layout

- `src/App.tsx`: controls, viewport, stats, results.
- `src/scanner.ts`: the page's single scanner instance and backend calls.
- `src/hooks/useScanner.ts`: boot sequence (WebGPU → backend → models) as
  state. `src/hooks/useCamera.ts`: webcam / still-image source.
  `src/hooks/useScanLoop.ts`: scan loop and telemetry.
  `src/hooks/useCardConfirmation.ts`: consecutive-frame confirmation.
- `src/components/`: `Viewport` (canvas + overlay), `Results` (card rows),
  `Stats` (timings, GPU profile).
- `src/tools/parity.ts`: the parity harness (plain TypeScript; HTML shell at
  the app root).
- `server/index.mjs`: the HTTP API (routes documented in its header).
  `server/search.mjs`: flat embedding files → brute-force dot-product top-k
  across a worker pool, plus the accept gate, filter-to-best-game logic, and
  card-name lookup.
