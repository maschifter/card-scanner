# API Documentation

Complete API reference for `@cardnexus/card-scanner`.

---

## Table of Contents

- [API Documentation](#api-documentation)
  - [Table of Contents](#table-of-contents)
  - [Core Functions](#core-functions)
    - [initializeScanner](#initializescanner)
    - [releaseScanner](#releasescanner)
    - [scanFrame](#scanframe)
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
  segmentationModelPath: '/path/to/segmentation_model.pte',
  embeddingModelPath: '/path/to/embedding_model.pte',
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

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:240-340`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

---

### releaseScanner

Releases all scanner resources (models, databases). Should be called in cleanup (e.g., `useEffect` return).

```typescript
function releaseScanner(): void;
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

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:97-113`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

---

### scanFrame

Scans a Vision Camera frame for cards. Runs inside a worklet (synchronous, but executes ML asynchronously internally).

```typescript
function scanFrame(frame: Frame): Detection;
```

**Parameters:**

- `frame` - Vision Camera frame object

**Returns:**

- `Detection` - Scan result with detected cards (see [Detection](#detection))

**Example:**

```typescript
const frameProcessor = useFrameProcessor(
  (frame) => {
    'worklet';

    if (!isScanning) return;

    runAsync(frame, () => {
      'worklet';

      const detection = scanFrame(frame);

      if (detection.success && detection.cards.length > 0) {
        // Move to JS thread to update state
        processDetectionCallback(detection);
      }
    });
  },
  [isScanning, processDetectionCallback],
);
```

**Notes:**

- Automatically throttles to `maxFrameRate` (default: 5 FPS)
- Skips blurry frames based on `blurThreshold`
- Low-light enhancement applied if `lowLightThreshold` exceeded
- Multi-game search: no need to specify game name

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:345-375`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

---

## Image Scanning

### scanImage

Scans a static image file for multiple cards. Useful for batch processing or scanning saved photos.

```typescript
function scanImage(imagePath: string): Promise<Detection>;
```

**Parameters:**

- `imagePath` - Path to image file

**Returns:**

- `Promise<Detection>` - Scan result with detected cards

**Example:**

```typescript
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
- Applies same quality filters as `scanFrame` (blur detection, low-light enhancement)
- Captured card images (if enabled in config) are automatically saved to the cache directory
- Game-specific processing (MTG set symbols, FAB colors) also runs if configured
- **Images saved to cache directory** (temporary storage, auto-cleaned by OS):
  - iOS: `Library/Caches/card-images/`
  - Android: `cache/card-images/`

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:437-503`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

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

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:500-576`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

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

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:578-656`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

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

**Implementation:** [`packages/react-native-card-scanner/src/index.ts:256-270`](../packages/react-native-card-scanner/src/index.ts)

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

**Implementation:** [`packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp:658-712`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

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
  scanMode: 'single' | 'multiple';

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

  // Optional: Frame quality
  blurThreshold?: number; // Min blur score (default: 100, 0 = disabled)
  lowLightThreshold?: number; // Min brightness (default: 65, 0 = disabled)
  lowLightGamma?: number; // Gamma correction (default: 2.0)
  maxFrameRate?: number; // Max FPS for ML (default: 5)

  // Required: Game class mapping
  gameClassMapping: Record<number, string>; // YOLO class ID → game name
  // Example: { 0: "fab", 1: "lorcana", 2: "mtg", 3: "onepiece", ... }

  // Optional: Game-specific configuration (per-game features and models)
  gameSpecificConfig?: Record<
    string, // Game name (e.g., "mtg", "fab", "pokemon")
    {
      // Optional: Game-specific embedding model for improved accuracy
      embeddingModelPath?: string;

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

**Implementation:** [`packages/react-native-card-scanner/src/index.ts:8-42`](../packages/react-native-card-scanner/src/index.ts)

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
  conf?: number; // Detection confidence
}
```

---

### AlternativeMatch

Alternative card match (lower confidence).

```typescript
interface AlternativeMatch {
  cardId: string;
  name: string;
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

The scanner supports adding new games without modifying native code by providing a custom `gameClassMapping`:

```typescript
const result = await initializeScanner({
  segmentationModelPath: '/path/to/yolo_model.pte',
  embeddingModelPath: '/path/to/embedding_model.pte',
  scanMode: 'single',
  segmentationThreshold: 0.7,
  iouThreshold: 0.7,
  confidenceThreshold: 0.6,
  maxMatches: 5,
  searchCandidates: 100,
  captureImage: true,

  // Custom game mapping for your new YOLO model
  gameClassMapping: {
    0: "fab",
    1: "lorcana",
    2: "mtg",
    3: "onepiece",
    4: "pokemon",
    5: "riftbound",
    6: "rise",
    7: "sorcery",
    8: "your-new-game" // Add your new game here
  }
});
```

**Requirements:**
1. Train a YOLO segmentation model that includes your new game as a class
2. Create a database with card embeddings for the new game
3. Provide the `gameClassMapping` matching your model's class IDs to game names

**Standard Mapping (for existing games):**
```typescript
gameClassMapping: {
  0: "fab",
  1: "lorcana",
  2: "mtg",
  3: "onepiece",
  4: "pokemon",
  5: "riftbound",
  6: "rise",
  7: "sorcery"
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
  embeddingModelPath: '/path/to/default_embedding_model.pte', // Fallback for all games

  scanMode: 'single',
  segmentationThreshold: 0.7,
  iouThreshold: 0.7,
  confidenceThreshold: 0.6,
  maxMatches: 5,
  searchCandidates: 100,
  captureImage: true,

  gameClassMapping: {
    0: "fab",
    1: "lorcana",
    2: "mtg",
    3: "onepiece",
    4: "pokemon",
    5: "riftbound",
    6: "rise",
    7: "sorcery"
  },

  gameSpecificConfig: {
    // MTG: Use specialized embedder + set symbol detection
    mtg: {
      embeddingModelPath: '/path/to/mtg_embedding_model.pte', // MTG-specific embedder
      setSymbolDetection: {
        detectionModelPath: '/path/to/mtg/set_symbol_detection.pte',
        embeddingModelPath: '/path/to/mtg/set_symbol_embedder.pte',
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
        modelPath: '/path/to/fab/fab_color_classifier.pte',
      },
    },

    // Other games will use the default embeddingModelPath
  },
});
```

### Benefits

- **Optimal Accuracy:** Each game database is searched with its own specialized embedding model
- **Maintains Adaptive Search:** Preserves the multi-database search strategy for uncertain predictions
- **Flexible Architecture:** Mix game-specific and default models as needed
- **Scalability:** Add specialized models incrementally without changing code
- **Backward Compatible:** If no game-specific model is provided, falls back to the default

### Implementation Details

The per-game embedding computation happens in [`SearchStrategy.cpp:76-89`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp):

```cpp
// For each probable game:
for (const auto &gameToSearch : topGames) {
  // Select game-specific embedder or fall back to default
  rncardscanner::CardEmbeddingModel *embedder = defaultEmbedder;
  auto gameSpecificModel = CardScannerInstaller::getEmbeddingModelForGame(gameToSearch);
  if (gameSpecificModel) {
    embedder = gameSpecificModel.get();
  }

  // Compute embedding with the selected model for this game
  auto embeddingResult = embedder->computeEmbedding(cardImage);

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

For `scanFrame` (worklet context), check the `Detection.success` field:

```typescript
const detection = scanFrame(frame);
if (!detection.success) {
  console.error('Scan failed:', detection.error);
}
```
