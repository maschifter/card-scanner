# API Documentation

Complete API reference for `@cardnexus/card-scanner`.

---

## Table of Contents

- [API Documentation](#api-documentation)
  - [Table of Contents](#table-of-contents)
  - [Core Functions](#core-functions)
    - [initializeScanner](#initializescanner)
    - [releaseScanner](#releasescanner)
    - [Frame Scanning (scanFrame)](#frame-scanning-scanframe)
  - [Image Scanning](#image-scanning)
    - [scanImage](#scanimage)
  - [Database Management](#database-management)
    - [listDatabases](#listdatabases)
    - [getDatabaseInfo](#getdatabaseinfo)
    - [swapDatabase](#swapdatabase)
    - [deleteDatabase](#deletedatabase)
  - [Type Definitions](#type-definitions)
    - [ScannerConfig](#scannerconfig)
    - [Detection](#detection)
    - [AsyncScanResult](#asyncscanresult)
    - [DetectedCard](#detectedcard)
    - [CapturedImage](#capturedimage)
    - [BoundingBox](#boundingbox)
    - [AlternativeMatch](#alternativematch)
    - [SetSymbolInfo](#setsymbolinfo)
    - [FABColorInfo](#fabcolorinfo)
    - [DatabaseInfo](#databaseinfo)
    - [InitializationResult](#initializationresult)
    - [SwapResult](#swapresult)
    - [DeleteResult](#deleteresult)
  - [Error Handling](#error-handling)

---

## Core Functions

### initializeScanner

Initializes the scanner with ML models and configuration. Must be called before using `scanFrame` and `scanImage`.

```typescript
function initializeScanner(
  config: ScannerConfig,
): Promise<InitializationResult>;
```

**Parameters:**

- `config` - Scanner configuration object (see [ScannerConfig](#scannerconfig))

**Returns:**

- `Promise<InitializationResult>` - Success/error result

**Example:**

```typescript
const result = await initializeScanner({
  segmentationModelPath: '/path/to/CardSegmentationModel.pte',
  embeddingModelPath: '/path/to/CardRecognitionModel.pte',
  scanMode: 'single',
  segmentationThreshold: 0.7,
  iouThreshold: 0.7,
  confidenceThreshold: 0.6,
  maxMatches: 5,
  searchCandidates: 100,
  captureImage: true,
  blurThreshold: 100,
  lowLightThreshold: 65,
  lowLightGamma: 2.0,
  maxFrameRate: 5,
});

if (!result.success) {
  console.error('Initialization failed:', result.error);
}
```

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:240-340`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

### releaseScanner

Releases all scanner resources (models, databases) on a background thread. Waits up to one second for in-flight work; if the scanner stays busy (for example, a running benchmark), the release is skipped and logged. Call it in cleanup (e.g., `useEffect` return); awaiting the result is optional.

```typescript
function releaseScanner(): Promise<void>;
```

**Example:**

```typescript
useEffect(() => {
  initializeScanner(config);

  return () => {
    releaseScanner(); // Cleanup
  };
}, []);
```

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:97-113`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

### Frame Scanning (scanFrame)

Camera frames are scanned through the `cardScannerPlugin` Nitro HybridObject. `scanFrame` runs the pipeline synchronously on the calling thread - the frame worklet offloads it to a Vision Camera v5 `AsyncRunner` task, so the camera pipeline never blocks; results arrive on a detection listener registered from the JS thread.

```typescript
cardScannerPlugin.scanFrame(frame: Frame, coordinateSnapshot: number[]): void;
cardScannerPlugin.setDetectionListener(
  listener: (result: AsyncScanResult) => void,
): void;
cardScannerPlugin.clearDetectionListener(): void;
```

**`scanFrame(frame, coordinateSnapshot)`** — synchronous scan, called from an `AsyncRunner` task:

- Runs the pipeline and sends the result to the listener, if one is registered. Frames are dropped silently before the pipeline runs when another scan is in progress, the throttle window opens later than the ~35ms accept margin, or the frame is unreadable
- **Takes ownership of the Frame**: `scanFrame` disposes it on every path (dropped, failed, scanned). When scanned, the Frame is disposed right after the pixel copy - before the throttle-window wait and ML - so the camera gets its buffer back as early as possible. Never call `frame.dispose()` after `scanFrame` - the only Frames the worklet disposes are the ones it never handed over (e.g. `runAsync` returned `false`)
- `coordinateSnapshot` - numbers passed through unchanged to the listener result; use it to snapshot frame→camera coordinate mapping while the frame is still alive

**`setDetectionListener(listener)`** — registers the single listener slot. Call from the JS thread (not a worklet); replaces any previous listener. **`clearDetectionListener()`** unregisters it; pending results are dropped.

**Example:**

```typescript
useEffect(() => {
  cardScannerPlugin.setDetectionListener((res) => {
    if (res.detection.success && res.detection.cards.length > 0) {
      // boundingBox is in raw frame-buffer coordinates; map it to view
      // space using res.frameWidth/frameHeight and res.coordinateSnapshot
      processDetection(res.detection);
    }
  });
  return () => cardScannerPlugin.clearDetectionListener();
}, [processDetection]);

const asyncRunner = useAsyncRunner();

const onFrame = useCallback(
  (frame: Frame) => {
    'worklet';

    let scheduled = false;
    try {
      // Taken while the frame is alive (the listener runs after disposal), and
      // inside the try so a throw here still hits the dispose in `finally`.
      const snapshot = frameToCameraSnapshot(frame);
      scheduled = asyncRunner.runAsync(() => {
        'worklet';
        // scanFrame owns the frame from here on and disposes it itself.
        try {
          cardScannerPlugin.scanFrame(frame, snapshot);
        } catch {}
      });
    } finally {
      // Runner busy (a scan is still running) - the frame never reached
      // scanFrame, so it is still ours to drop.
      if (!scheduled) frame.dispose();
    }
  },
  [asyncRunner],
);

const frameOutput = useFrameOutput({
  pixelFormat: 'rgb',
  targetResolution: CommonResolutions.FHD_16_9,
  onFrame,
});
```

**Notes:**

- One scan at a time: while a scan is running, newer frames are dropped (`runAsync` returns `false`; a concurrent `scanFrame` from another runner is rejected natively). The camera buffer is released as soon as the pixels are copied, so on Android (CameraX keep-only-latest) frames keep flowing during ML and the first frame after the scan ends is a fresh one
- Automatically throttles to `maxFrameRate` (default: 5 FPS); frames are rejected without copying unless the window is open or opens within ~35ms - an early frame is copied, released, and the scan sleeps the gap, so it starts right at the window instead of waiting for another camera frame
- Skips blurry frames based on `blurThreshold`
- Low-light enhancement applied if `lowLightThreshold` exceeded
- Multi-game search: no need to specify game name
- Returned bounding boxes are in raw frame-buffer coordinates - map them to view coordinates with Vision Camera v5's `frame.convertFramePointToCameraPoint` + `cameraRef.convertCameraPointToViewPoint` (see [`apps/example/utils/cameraCoords.ts`](../apps/example/utils/cameraCoords.ts))

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/HybridCardScannerPlugin.cpp`](../packages/mobile-card-scanner/common/rnbridge/HybridCardScannerPlugin.cpp)

---

## Image Scanning

### scanImage

Scans a static image file for multiple cards. Useful for batch processing or scanning saved photos.

```typescript
function scanImage(
  imagePath: string,
  mode?: 'single' | 'multiple',
): Promise<Detection>;
```

**Parameters:**

- `imagePath` - Path to image file
- `mode` - Optional per-call override of the configured `scanMode`; omit to keep it

**Returns:**

- `Promise<Detection>` - Scan result with detected cards

**Example:**

```typescript
// Scanned in the configured mode; pass 'multiple' to override it for this call
const result = await scanImage('/path/to/photo.jpg');

if (result.success && result.cards.length > 0) {
  console.log(
    `Found ${result.cards.length} cards in ${result.processingTime}ms`,
  );

  result.cards.forEach((card) => {
    console.log(
      `- ${card.cardId} (${card.gameName}): ${(card.confidenceScore * 100).toFixed(1)}%`,
    );
  });
}
```

**Notes:**

- Runs full ML pipeline asynchronously (non-blocking)
- Can detect multiple cards in a single image
- Serialized against `scanFrame`: camera frames are dropped while the scan runs, and the `maxFrameRate` throttle does not apply to it
- Applies same quality filters as frame scanning (blur detection, low-light enhancement)
- Captured card images (if enabled in config) are automatically saved to the cache directory
- Game-specific processing (MTG set symbols, FAB colors) also runs if configured
- **Images saved to cache directory** (temporary storage, auto-cleaned by OS):
  - iOS: `Library/Caches/card-images/`
  - Android: `cache/card-images/`

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:186-250`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

## Database Management

The package provides a CRUD-like JSI interface for managing game databases. All database operations run on background threads to avoid blocking the UI.

### listDatabases

Lists all installed game databases with full metadata.

```typescript
function listDatabases(): Promise<DatabaseInfo[]>;
```

**Returns:**

- `Promise<DatabaseInfo[]>` - Array of database information

**Example:**

```typescript
const databases = await listDatabases();
databases.forEach((db) => {
  console.log(`${db.gameName}: ${db.cardCount} cards, ${db.fileSize} bytes`);
  console.log(`Created: ${db.creationTimestamp}`);
});
```

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:500-576`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

### getDatabaseInfo

Gets detailed information about a specific game database.

```typescript
function getDatabaseInfo(gameName: string): Promise<DatabaseInfo | null>;
```

**Parameters:**

- `gameName` - Game identifier (e.g., `"mtg"`, `"lorcana"`)

**Returns:**

- `Promise<DatabaseInfo | null>` - Database info, or null if not found

**Example:**

```typescript
const info = await getDatabaseInfo('mtg');
if (info) {
  console.log(`MTG database: ${info.cardCount} cards`);
}
```

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:578-656`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

### swapDatabase

Swaps a game database with a new version.

```typescript
function swapDatabase(
  gameName: string,
  sourcePath: string,
): Promise<SwapResult>;
```

**Parameters:**

- `gameName` - Game identifier (e.g., `"mtg"`, `"lorcana"`)
- `sourcePath` - Path to new database file (`.mdb`)

**Returns:**

- `Promise<SwapResult>` - Success/error result

**Example:**

```typescript
const result = await swapDatabase('mtg', '/path/to/new_mtg_database.mdb');

if (result.success) {
  console.log('Database updated successfully');
} else {
  console.error('Update failed:', result.error);
}
```

**Implementation:** [`packages/mobile-card-scanner/src/index.ts:256-270`](../packages/mobile-card-scanner/src/index.ts)

---

### deleteDatabase

Deletes a game database and all its contents.

```typescript
function deleteDatabase(gameName: string): Promise<DeleteResult>;
```

**Parameters:**

- `gameName` - Game identifier to delete

**Returns:**

- `Promise<DeleteResult>` - Success/error result

**Example:**

```typescript
const result = await deleteDatabase('mtg');

if (result.success) {
  console.log('Database deleted successfully');
} else {
  console.error('Deletion failed:', result.error);
}
```

**Notes:**

- Automatically closes the database before deletion
- Removes entire database directory
- Cannot be undone

**Implementation:** [`packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp:658-712`](../packages/mobile-card-scanner/common/rnbridge/CardScannerInstaller.cpp)

---

## Type Definitions

### ScannerConfig

Scanner initialization configuration.

```typescript
interface ScannerConfig {
  // Required: Model paths
  segmentationModelPath: string;
  embeddingModelPath: string;

  // Required: Behavior
  scanMode: 'single' | 'multiple'; // scanImage can override this per call

  // Required: Thresholds
  segmentationThreshold: number; // YOLO confidence (default: 0.7)
  iouThreshold: number; // NMS IOU (default: 0.7)
  confidenceThreshold: number; // Min similarity (default: 0.6)

  // Required: Search parameters
  maxMatches: number; // Max matches per detection
  searchCandidates: number; // DB fetch size (recommended 100)

  // Optional: Features
  captureImage?: boolean; // Save cropped images
  disambiguationThreshold?: number; // Min score diff for game-specific detection (default: 0.02)
  minGameConfidence?: number; // Min YOLO class confidence for a game's DB to be searched (default: 0.1)

  // Optional: Frame quality
  blurThreshold?: number; // Min blur score (default: 100, 0 = disabled)
  lowLightThreshold?: number; // Min brightness (default: 65, 0 = disabled)
  lowLightGamma?: number; // Gamma correction (default: 2.0)
  maxFrameRate?: number; // Max FPS for ML (default: 5)

  // Required: Game class mapping
  gameClassMapping: Record<number, string | string[]>; // YOLO class ID → game name(s)
  // Example: { 0: "fab", 1: "lorcana", 2: ["pokemon", "pokemon-japan"], ... }
  // Keys must be contiguous from 0 — the entry count is the model's class count.
  // Use an array when one class covers several games (see "Adding New Games").

  // Optional: Game-specific configuration (per-game features and models)
  gameSpecificConfig?: Record<
    string, // Game name (e.g., "mtg", "fab", "pokemon")
    {
      // Optional: Game-specific embedding model for improved accuracy
      embeddingModelPath?: string;

      // Optional: Override the default confidence threshold for this game
      confidenceThreshold?: number;

      // MTG-specific: Set symbol detection
      setSymbolDetection?: {
        detectionModelPath: string;
        embeddingModelPath: string;
        detectionThreshold: number;
        confidenceThreshold: number;
      };

      // FAB-specific: Color variant detection
      colorDetection?: {
        modelPath: string;
        dotsRegionRatio?: number; // Region ratio (default: 0.20)
        minDotsRegionSize?: number; // Min region pixels (default: 50)
      };
    }
  >;
}
```

**Implementation:** [`packages/mobile-card-scanner/src/index.ts:8-42`](../packages/mobile-card-scanner/src/index.ts)

---

### Detection

Scan result containing detected cards.

```typescript
interface Detection {
  success: boolean; // Whether scan succeeded
  cards: DetectedCard[]; // Detected cards
  processingTime: number; // Processing time in ms
  error?: string; // Error message if failed
}
```

---

### AsyncScanResult

Result delivered to the detection listener. The Frame is disposed by then, so buffer dimensions and the caller's `coordinateSnapshot` ride along for box mapping.

```typescript
interface AsyncScanResult {
  detection: Detection;
  frameWidth: number; // Frame buffer width in pixels
  frameHeight: number; // Frame buffer height in pixels
  coordinateSnapshot: number[]; // Values passed to scanFrame, unchanged
}
```

---

### DetectedCard

Information about a single detected card.

```typescript
interface DetectedCard {
  // Primary match (optional, if a card is recognized in the database)
  cardId?: string;
  gameName?: string; // From best match (multi-game search)
  confidenceScore?: number; // Match confidence [0.0, 1.0]

  // Game prediction
  predictedGameName?: string; // Game predicted by YOLO, even if no DB match
  predictedGameConfidence?: number; // YOLO confidence for predicted game [0.0, 1.0]

  // Location
  boundingBox: BoundingBox;

  // Image
  capturedImage?: CapturedImage;

  // Alternative matches
  alternativeCards: AlternativeMatch[];

  // Game-specific metadata
  setSymbol?: SetSymbolInfo; // MTG: set symbol detection
  fabColor?: FABColorInfo; // FAB: color variant detection
}
```

---

### CapturedImage

Captured/saved card image metadata.

```typescript
interface CapturedImage {
  uri: string; // File path
  width: number; // Width in pixels
  height: number; // Height in pixels
  size: number; // File size in bytes
}
```

---

### BoundingBox

Card detection bounding box.

```typescript
interface BoundingBox {
  x1: number;
  y1: number;
  x2: number;
  y2: number;
  conf: number; // Detection confidence
}
```

---

### AlternativeMatch

Alternative card match (lower confidence).

```typescript
interface AlternativeMatch {
  cardId: string;
  confidence: number;
}
```

---

### SetSymbolInfo

MTG set symbol detection result.

```typescript
interface SetSymbolInfo {
  setCode: string; // Set code (e.g., "neo", "one")
  similarity: number; // Match confidence [0.0, 1.0]
}
```

---

### FABColorInfo

Flesh and Blood color variant detection result.

```typescript
interface FABColorInfo {
  color: string; // "red", "yellow", or "blue"
  similarity: number; // Match confidence [0.0, 1.0]
}
```

---

### DatabaseInfo

Complete database information with metadata.

```typescript
interface DatabaseInfo {
  gameName: string; // Game identifier (e.g., "mtg", "lorcana")
  path: string; // Full filesystem path to database
  cardCount: number; // Total number of cards in database
  creationTimestamp: string; // ISO 8601 creation timestamp
  fileSize?: number; // Database file size in bytes
}
```

**Example:**

```typescript
const databases = await listDatabases();
// [
//   {
//     gameName: "mtg",
//     path: "/path/to/db/mtg/data.mdb",
//     cardCount: 95234,
//     creationTimestamp: "2024-01-15T10:30:00Z",
//     fileSize: 524288000
//   }
// ]
```

---

### InitializationResult

Scanner initialization result.

```typescript
interface InitializationResult {
  success: boolean;
  error?: string;
}
```

---

### SwapResult

Database swap result.

```typescript
interface SwapResult {
  success: boolean;
  error?: string;
}
```

---

### DeleteResult

Database deletion result.

```typescript
interface DeleteResult {
  success: boolean;
  error?: string;
}
```

---

## Adding New Games

Games are added by providing a `gameClassMapping` that matches the segmentation model's classes. No native code changes are needed — but the mapping is not a label list, it is part of the decode:

> **The entry count is the model's class count**, which determines the stride into the prediction tensor (`4 + numClasses + 32` channels). Keys must be contiguous starting at 0. A mapping that disagrees with the model does not merely mislabel a class — it corrupts every box and mask coefficient. Both conditions are validated at init and throw.

```typescript
const result = await initializeScanner({
  segmentationModelPath: '/path/to/CardSegmentationModel.pte',
  embeddingModelPath: '/path/to/CardRecognitionModel.pte',
  scanMode: 'single',
  segmentationThreshold: 0.7,
  iouThreshold: 0.7,
  confidenceThreshold: 0.6,
  maxMatches: 5,
  searchCandidates: 100,
  captureImage: true,

  // Custom game mapping for your new YOLO model
  gameClassMapping: {
    0: 'fab',
    1: 'lorcana',
    2: 'mtg',
    3: 'onepiece',
    4: 'pokemon',
    5: 'riftbound',
    6: 'rise',
    7: 'sorcery',
    8: 'your-new-game', // Add your new game here
  },
});
```

**Requirements:**

1. Train a YOLO segmentation model that includes your new game as a class
2. Create a database with card embeddings for the new game
3. Provide the `gameClassMapping` matching your model's class IDs to game names

The export pipeline emits a `manifest.json` listing `class_names` in class order —
copy that list rather than transcribing it. The example app keeps its copy at
`apps/example/assets/model-manifest.json`.

### Merged Classes (one class, several games)

Some games are visually indistinguishable to the segmentation model — different regional printings of the same product, for instance. Those share a single model class, and the class maps to an **array** of database names:

```typescript
gameClassMapping: {
  0: "fab",
  1: "lorcana",
  2: "mtg",
  4: ["pokemon", "pokemon-japan"],       // model class "pokemon"
  10: ["dbs-fusion", "dbs-masters"],     // model class "dbs"
}
```

When such a class fires, **every listed database is searched** and the highest-scoring card decides the game — the sibling databases disambiguate what the model cannot. A plain string is shorthand for a one-element array, so existing mappings keep working unchanged.

This is why the database count can exceed the model's class count: the current model has 17 classes but 19 databases.

Two consequences worth knowing:

- **The first name is canonical.** It is what `predictedGameName` reports, since the model only ever predicted the class. Which sibling actually wins is decided by score and surfaces as `gameName`, so the ordering is cosmetic.
- **Databases searched per detection are capped at 4** (`MAX_GAME_DATABASES`), while the _class_ cap stays 3 (`MAX_TOP_PREDICTIONS`). Each database costs a full embedding pass plus a vector search, so when two merged classes both land in the top 3, the lowest-ranked names are dropped rather than searched.

**Current Mapping (17 classes → 19 databases):**

```typescript
gameClassMapping: {
  0: "fab",
  1: "lorcana",
  2: "mtg",
  3: "onepiece",
  4: ["pokemon", "pokemon-japan"], // merged
  5: "riftbound",
  6: "rise",
  7: "sorcery",
  8: "chrono-core",
  9: "cyberpunk",
  10: ["dbs-fusion", "dbs-masters"], // merged
  11: "eoa",
  12: "grand-archive",
  13: "gundam",
  14: "naruto-mythos",
  15: "palworld",
  16: "swu"
}
```

---

## Game-Specific Embedding Models

For improved accuracy, you can provide game-specific embedding models that are fine-tuned for individual games. The scanner uses the YOLO model's game prediction to automatically select the appropriate embedder for each detected card.

### How It Works

1. **YOLO Prediction:** The segmentation model predicts top probable games for each detected card (e.g., MTG 90%, Pokemon 85%)
2. **Multi-Game Search:** For each probable game:
   - **Model Selection:** Select game-specific embedder from `gameSpecificConfig` or use default
   - **Embedding Computation:** Compute embedding with that game's specialized model
   - **Database Search:** Search that game's database with its specialized embedding
3. **Result Aggregation:** Combine all search results from all probable games
4. **Best Match Selection:** Filter to the game with the highest confidence match

### Example Configuration

```typescript
const result = await initializeScanner({
  segmentationModelPath: '/path/to/yolo_model.pte',
  embeddingModelPath: '/path/to/CardRecognitionModel.pte', // Fallback for all games

  scanMode: 'single',
  segmentationThreshold: 0.7,
  iouThreshold: 0.7,
  confidenceThreshold: 0.6,
  maxMatches: 5,
  searchCandidates: 100,
  captureImage: true,

  gameClassMapping: {
    0: 'fab',
    1: 'lorcana',
    2: 'mtg',
    3: 'onepiece',
    4: 'pokemon',
    5: 'riftbound',
    6: 'rise',
    7: 'sorcery',
  },

  gameSpecificConfig: {
    // MTG: Use specialized embedder + set symbol detection
    mtg: {
      embeddingModelPath: '/path/to/mtg_embedding_model.pte', // MTG-specific embedder
      setSymbolDetection: {
        detectionModelPath: '/path/to/mtg/SetSymbolDetectionModel.pte',
        embeddingModelPath: '/path/to/mtg/SetSymbolRecognitionModel.pte',
        detectionThreshold: 0.3,
        confidenceThreshold: 0.6,
      },
    },

    // Pokemon: Use specialized embedder only
    pokemon: {
      embeddingModelPath: '/path/to/pokemon_embedding_model.pte',
    },

    // FAB: Use specialized embedder + color detection
    fab: {
      embeddingModelPath: '/path/to/fab_embedding_model.pte', // FAB-specific embedder
      colorDetection: {
        modelPath: '/path/to/fab/ColorBarModel.pte',
      },
    },

    // Other games will use the default embeddingModelPath
  },
});
```

### Benefits

- **Optimal Accuracy:** Each game database is searched with its own specialized embedding model
- **Works with multi-database search:** Every candidate game is searched with the right embedder for it
- **Flexible Architecture:** Mix game-specific and default models as needed
- **Scalability:** Add specialized models incrementally without changing code
- **Backward Compatible:** If no game-specific model is provided, falls back to the default

### Implementation Details

The per-game embedding computation happens in [`SearchStrategy.cpp:100-125`](../packages/card-scanner-core/src/core/SearchStrategy.cpp).
The embedders are passed down as a `GameEmbedders` map rather than fetched from a
registry, so the search has no global state to reach for:

```cpp
// For each probable game:
for (const auto &gameToSearch : topGames) {
  // Select game-specific embedder or fall back to default
  cardscanner::CardEmbeddingModel *embedder = defaultEmbedder;
  if (gameEmbedders != nullptr) {
    const auto it = gameEmbedders->find(gameToSearch);
    if (it != gameEmbedders->end() && it->second) {
      embedder = it->second.get();
    }
  }

  // Compute embedding with the selected model for this game
  CardEmbeddingResult embeddingResult = embedder->computeEmbedding(cardImage);
  const auto &embedding = embeddingResult.embedding;

  // Search this game's database with its specialized embedding
  auto gameResults = gameDb->search_similar_cards(embedding, config.searchCandidates);
}
```

---

## Error Handling

All async functions return result objects with `success` boolean and optional `error` string:

```typescript
const result = await initializeScanner(config);
if (!result.success) {
  console.error('Error:', result.error);
}
```

For frame scanning, check the `Detection.success` field on the listener result:

```typescript
cardScannerPlugin.setDetectionListener((res) => {
  if (!res.detection.success) {
    console.error('Scan failed:', res.detection.error);
  }
});
```
