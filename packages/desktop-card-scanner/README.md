# @cardnexus/desktop-card-scanner

The desktop wrapper around `card-scanner-core`, mirroring how
`mobile-card-scanner` wraps it for React Native. Core stays platform-agnostic;
this package supplies the inference backend, the OBS bindings, and the wire
protocol between them.

For running the OBS plugin, see the
[desktop example README](../../apps/desktop-example/README.md). This document
covers the package itself.

## What is in here

The package splits along the process boundary. Everything in `service/`,
`ipc/`, `protocol/`, `backends/`, `config/`, and `http/` links into the scanner
server. Everything in `module/` and `obsbridge/` links into the OBS plugin,
which takes `ipc/FrameProtocol.h` and nothing else from the server side.

### Server side

| Path                            | What it does                                                                                                                        |
| ------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `backends/onnx/OnnxSession.cpp` | Defines `cardscanner::inference::loadSession` against ONNX Runtime, and the only translation unit that includes it.                 |
| `service/ScannerService`        | Owns the worker thread and a depth-1 keep-latest mailbox. A newer frame evicts the older one; the producer never waits.             |
| `core/ScanSession`              | The state machine: idle, detecting, candidate ready, emitted. Holds the history ring and the thresholds. Lives in `card-scanner-core`. |
| `service/ScannerServer`         | The façade that owns the whole server. Construct it, call `start()`, and the sockets, the worker, and the lookups come up together. |
| `service/ProductClient`         | Resolves card names and art over HTTP on its own thread, so the scan worker never waits on the network.                             |
| `protocol/StateMessage`         | Serializes the whole scanner state as one JSON message.                                                                             |
| `ipc/FrameServer`               | Receives frames on port 27846, validates the header, and converts the scan region to RGB. Serves one filter at a time.              |
| `ipc/ControlServer`             | The WebSocket endpoint on port 27845; the overlay and the dock both attach to it.                                                   |
| `config/ScannerConfigLoader`    | Maps a JSON config onto `ScannerConfig`; core validates the result.                                                                 |

### OBS side

| Path                    | What it does                                                                                               |
| ----------------------- | ---------------------------------------------------------------------------------------------------------- |
| `module/obs-module.cpp` | The filter: registers it, reads its settings, draws the scan region, and adds the overlay to a scene.      |
| `module/ServerProcess`  | Starts the scanner server, supervises it, and stops it.                                                    |
| `obsbridge/FrameClient` | The frame socket: a depth-1 mailbox and a writer thread, so `filter_video` never blocks.                   |
| `ipc/FrameProtocol.h`   | The wire format. Depends on `<cstdint>` alone, which is what lets the module link libobs and nothing else. |

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

Every dependency arrives at configure time, pinned to a hash (archives) or a
commit (git checkouts): OpenCV, nlohmann/json and IXWebSocket as sources, ONNX
Runtime and the ObjectBox host runtime as prebuilt shared libraries, and
libcurl as sources on Windows, where the system has none. Nothing but the
toolchain is looked up on the host.

To build the OBS module as well, configure with `-DBUILD_OBS_MODULE=ON`. That
also fetches the obs-studio source, because OBS ships no headers in its app
bundle. Pin `OBS_VERSION` to the OBS you have installed: older headers load
fine, newer ones fail when OBS loads the module. It has run on 30.2 (Windows)
and 32 (macOS).

### OpenCV

The libraries are OpenCV 4.14.0, built static: only `core`, `imgproc` and
`imgcodecs`, with the bundled JPEG, PNG and zlib, no GUI, no video, no OpenCL,
no host libraries. On macOS OpenCV also builds oneTBB in, because GCD ignores
`setNumThreads()`, and pulls KleidiCV, whose kernels make `resize` and the YUV
conversions several times faster; both are downloads OpenCV pins itself. About
a minute and a half of build time on an M2 Pro.

Core's vendored headers are opencv-mobile's 4.11 set, the same the mobile
packages link against. The 4.14 libraries link to them because every
declaration and enum value the pipeline uses is identical in both tags; OpenCV
does not promise that across minor versions, so a bump means re-checking it.

They are linked `LINK_ONLY`, so their include directories never reach the
compiler and core's vendored headers stay the only OpenCV headers in the
binary. OpenCV also caches `EXECUTABLE_OUTPUT_PATH`, which would move every
later executable into `<build>/bin`; the package unsets it.

### Shared libraries beside the executable

ONNX Runtime and ObjectBox ship as shared libraries only, so
`cardscanner_copy_runtime_dlls()` copies them beside every executable that
links `cardscanner_desktop`, on macOS as well as Windows. Those executables
carry `@executable_path` as their only rpath, so a copied directory keeps
working wherever it lands. Call the function on any new executable.

## Hardware acceleration

`OnnxSession` tries execution providers in order and falls through on failure:
CoreML then CPU on macOS, DirectML then CPU on Windows, CPU on Linux. It logs
which provider took each model, because a silent fallback to CPU looks exactly
like acceleration that did nothing.

To pin a provider, set `CARD_SCANNER_EP`:

```sh
CARD_SCANNER_EP=cpu ./build/server/card-scanner-server config.json
```

The shipped models are fp16, which is what lets CoreML run them on the Neural
Engine; that needs ONNX Runtime 1.24.4 or newer, pinned in `CMakeLists.txt`.
On an idle M2 Pro, Release, over a 19-game 92-image sweep, a scan takes 3.5 ms
end to end against 8.3 ms with the fp32 set, and the segmentation model alone
runs in 0.9 ms on the ANE against 16 ms on the CPU EP. All five models go to
CoreML; `SetSymbolRecognitionModel` is the one it makes slower (about 3 ms
against 1 ms on CPU), but it only runs for MTG.

Benchmark on a quiet machine with `CARD_SCANNER_EP` held fixed and
compare the p50: the ANE queues behind other ML clients, OBS included, and the
mean then measures the queue. Embeddings are compared by cosine against a
database built in fp32, so after swapping a model check the score distribution
and not only the top-1 count.

### Windows

DirectML is compiled into ONNX Runtime, so the build takes the runtime from
NuGet rather than the GitHub release the other platforms use: those assets are
CPU-only or CUDA, and CUDA would demand a CUDA and cuDNN install on the user's
machine. Any Direct3D 12 GPU works, NVIDIA, AMD and Intel alike.

Every DLL the server loads must sit beside the executable.
`cardscanner_copy_runtime_dlls()` puts the linked ones there (`objectbox.dll`,
`onnxruntime.dll`) plus two that nothing links:
`onnxruntime_providers_shared.dll`, which ONNX Runtime loads by hand, and
`DirectML.dll` — deliberately the redistributable, not the older copy in
`System32`. OpenCV and libcurl are static, so no DLL of theirs exists to
forget; libcurl is built HTTP-only on Schannel, Windows' own TLS.

Anything assembling a bundle must copy the whole directory rather than
re-derive the list, as `install-obs-plugin` does. A missing DLL costs nothing
at build time and fails at startup with `STATUS_DLL_NOT_FOUND` (0xC0000135),
which `cmd` reports as "is not recognized as an internal or external
command".

## `gameClassMapping` is a real contract

It maps segmentation-model class ids to the databases each one should search,
and its **entry count decodes the model's prediction tensor**. It must be
contiguous from `0`, and it must match the class order the model was exported
with.

The loader enforces contiguity and rejects empty entries, and the first scan
checks the entry count against the model: the prediction tensor carries
`4 + numClasses + 32` channels per anchor, and a count that disagrees fails
with an error naming both numbers. The order is not checked. A map with the
right count in the wrong order raises nothing; it misroutes every database
search and looks like poor recognition.

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
