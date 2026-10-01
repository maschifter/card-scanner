# @cardnexus/desktop-card-scanner

The desktop wrapper around `card-scanner-core`, mirroring how
`mobile-card-scanner` wraps it for React Native. Core stays platform-agnostic;
this package supplies the inference backend, the scan session, the OBS
bindings, and the wire protocol between them.

For running the OBS plugin, see the
[desktop example README](../../apps/desktop-example/README.md). This document
covers the package itself.

## What is in here

The package splits along the process boundary. Everything in `service/`,
`ipc/`, `protocol/`, `backends/`, `config/`, and `net/` links into the scanner
server. Everything in `module/` and `obsbridge/` links into the OBS plugin,
which takes `ipc/FrameProtocol.h` and nothing else from the server side.

### Server side

| Path | What it does |
|---|---|
| `backends/onnx/OnnxSession.cpp` | Defines `cardscanner::inference::loadSession` against ONNX Runtime, and the only translation unit that includes it. |
| `service/ScannerService` | Owns the worker thread and a depth-1 keep-latest mailbox. A newer frame evicts the older one; the producer never waits. |
| `service/ScanSession` | The state machine: idle, detecting, candidate ready, emitted. Holds the history ring and the thresholds. |
| `service/ScannerServer` | The façade that owns the whole server. Construct it, call `start()`, and the sockets, the worker, and the lookups come up together. |
| `service/ProductClient` | Resolves card names and art over HTTP on its own thread, so the scan worker never waits on the network. |
| `protocol/StateMessage` | Serializes the whole scanner state as one JSON message. |
| `ipc/FrameServer` | Receives frames on port 27846, validates the header, and converts the scan region to RGB. |
| `ipc/ControlServer` | The WebSocket endpoint on port 27845. One server for the process, so a second filter can attach. |
| `ipc/OverlayServer` | Serves the overlay page over HTTP on port 27847. |
| `config/ScannerConfigLoader` | Reads a JSON config into a fully populated `ScannerConfig`. |

### OBS side

| Path | What it does |
|---|---|
| `module/obs-module.cpp` | The filter: registers it, reads its settings, draws the scan region, and adds the overlay to a scene. |
| `module/ServerProcess` | Starts the scanner server, supervises it, and stops it. |
| `obsbridge/FrameClient` | The frame socket: a depth-1 mailbox and a writer thread, so `filter_video` never blocks. |
| `ipc/FrameProtocol.h` | The wire format. Depends on `<cstdint>` alone, which is what lets the module link libobs and nothing else. |

## The inference seam

`loadSession` is declared in core and deliberately left undefined there. Each
platform links exactly one definition: this package links the ONNX Runtime one,
and `mobile-card-scanner` links the ExecuTorch one. One backend per binary, and
the undefined symbol is the whole runtime-selection mechanism.

`tests/test_onnx_session.cpp` asserts that contract against a committed
fixture. It needs no card models and no databases.

## Building

```sh
cmake -S . -B build && cmake --build build
ctest --test-dir build
```

ONNX Runtime, the ObjectBox host runtime, nlohmann/json, and IXWebSocket are
fetched at configure time. OpenCV is the one host dependency.

To build the OBS module as well, configure with `-DBUILD_OBS_MODULE=ON`. That
also fetches the obs-studio source, because OBS ships no headers in its app
bundle. Pin `OBS_VERSION` to the OBS you have installed: the interface changes
between major versions, and a mismatch fails when OBS loads the module rather
than when you build it.

### OpenCV must be 4.x

Core compiles against the OpenCV **4** headers vendored in its `third-party/`
directory, so the libraries linked here must also be 4.x. Versions 4 and 5 are
not ABI compatible, and a mismatch links without complaint and misbehaves at
runtime, so CMake fails at configure time instead.

Homebrew's `opencv@4` is keg-only, so a bare `find_package` picks up `opencv` 5
on any machine that has both. CMake here locates `opencv@4` explicitly:

```sh
brew install opencv@4
```

`OpenCV_INCLUDE_DIRS` is deliberately not on the include path. Core exports its
own vendored headers, and those are the only OpenCV headers in the binary. The
build links the system libraries without compiling against their headers.

## Hardware acceleration

`OnnxSession` tries execution providers in order and falls through on failure:
CoreML then CPU on macOS, DirectML then CPU on Windows, CPU on Linux. It logs
which provider took each model, because a silent fallback to CPU looks exactly
like acceleration that did nothing.

To pin a provider, set `CARD_SCANNER_EP`:

```sh
CARD_SCANNER_EP=cpu ./build/card-scanner-server config.json
```

Models with external weights run on CPU regardless. The accelerators do not
read `.onnx.data` sidecars.

Measure before you assume this helps. On an Apple M-series machine, CoreML
moved the median scan from 34.2 ms to 33.0 ms, which is inside the noise, so
the ladder exists for the platforms where it pays rather than as a default win.
CoreML may also run fp16, and embeddings are compared by cosine against a
database built on CPU fp32, so check the score distribution and not only the
top-1 count.

## `gameClassMapping` is a real contract

It maps segmentation-model class ids to the databases each one should search,
and its **entry count decodes the model's prediction tensor**. It must be
contiguous from `0`, and it must match the class order the model was exported
with.

A mismatch does not raise an error. It misroutes every database search and
looks like poor recognition. The loader enforces contiguity and rejects empty
entries, which catches a malformed map but not a map that disagrees with the
model. Checking that needs the segmentation model's output channel dimension,
which is `4 + numClasses + 32`, and the backend does not expose shapes yet.

## Comparing two card databases

Databases and the embedder are a matched pair: a stored vector is comparable
only to a query vector from the same embedder. Mixing a database from one
source with an embedder from another is a valid experiment, but not a neutral
one. Check it first:

```sh
./build/db_parity <dirA> <dirB>     # each a directory holding data.mdb
```

It reports card-id coverage, how many embeddings are bit-identical, and the
cosine between the two stores' vectors for the same card. That last number
decides whether two sets are interchangeable: equivalent exports of one
checkpoint cosine at about 0.999999, and a genuinely different model lands
anywhere from 0.99 down to negative.

File hashes cannot answer this. Two stores built from identical data differ
byte for byte, because LMDB carries page state. Model file hashes cannot answer
it either: four exports of one model exist with four different digests and two
different file sizes, and two of them are numerically equivalent.

Desktop takes ONNX and mobile takes ExecuTorch, so a bundle built for mobile
carries no ONNX to install. Use the ONNX export of the same checkpoint and
confirm the pair with `db_parity`.

## Data layout

Call `pathprovider::set_db_path` and `set_cache_path` before anything touches
`DatabaseManager`, which reads the database path once when its singleton is
first constructed. `ScannerServer`'s callers do this at startup, the same way
`CardScanner.mm` and the Android shim do on mobile.

Databases live at `<databasesDir>/<game>/data.mdb` and are found by scanning
that directory.
