# Pipeline Architecture

Complete step-by-step explanation of the card scanning pipeline with source code references.

---

## Table of Contents

- [Overview](#overview)
- [Architecture Diagram](#architecture-diagram)
- [Entry Point](#entry-point)
- [Pre-Processing Filters](#pre-processing-filters)
- [Pipeline Stages](#pipeline-stages)
- [Game-Specific Processing](#game-specific-processing)
- [Optimization Strategies](#optimization-strategies)
- [Thread Safety](#thread-safety)

---

## Overview

The scanner processes camera frames through a **5-stage pipeline**:

1. **Card Segmentation** - YOLO detects cards and predicts games
2. **Image Extraction** - Crops cards from frame
3. **Low-Light Enhancement** - Brightens dark images (optional)
4. **Card Recognition** - Searches databases for matches
5. **Game-Specific Metadata** - Detects set symbols (MTG) or colors (FAB)

**Key Features:**

- Multi-game support (MTG, Lorcana, FAB, Pokémon, etc.)
- Automatic game detection via YOLO
- Adaptive database search (early exit optimization)
- Disambiguation-gated metadata detection
- Frame throttling and blur filtering

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     Camera Frame Input                       │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│               PRE-PROCESSING FILTERS                         │
├─────────────────────────────────────────────────────────────┤
│  1. Blur Detection      → Skip if too blurry                │
│  2. Frame Rate Limiting → Max 5 FPS to ML pipeline          │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────┐
│  STAGE 1: Card Segmentation (YOLO)                          │
├─────────────────────────────────────────────────────────────┤
│  • Detect cards → bounding boxes                            │
│  • Predict games → top 3 game predictions                   │
│  • Optional: Dewarped images                                │
│  • Scan mode filter: single/multiple                        │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
                  For Each Detection
                         │
         ┌───────────────┴───────────────┐
         ▼                               ▼
┌──────────────────────┐        ┌──────────────────────┐
│  STAGE 2: Extract    │        │  (Parallel           │
│  Card Image          │        │   Processing)        │
│  • Crop from bbox    │        │                      │
│  • Or use dewarped   │        │                      │
└──────────┬───────────┘        └──────────────────────┘
           │
           ▼
┌──────────────────────────────────────────────────────────────┐
│  STAGE 3: Low-Light Enhancement (Optional)                   │
├──────────────────────────────────────────────────────────────┤
│  • Check luminance < lowLightThreshold                       │
│  • Apply gamma correction (γ = lowLightGamma)                │
│  • Normalize to [0, 255]                                     │
└──────────┬───────────────────────────────────────────────────┘
           │
           ▼
┌──────────────────────────────────────────────────────────────┐
│  STAGE 4: Card Recognition                                   │
├──────────────────────────────────────────────────────────────┤
│  4.1 Compute Embedding                                       │
│      └─> CardEmbeddingModel → feature vector                │
│  4.2 Extract Top Games                                       │
│      └─> Filter YOLO predictions by confidence              │
│  4.3 Multi-Database Search (Adaptive)                        │
│      ├─> Search game 1 database                             │
│      ├─> If confident + only 1 game → STOP (optimization)   │
│      └─> Else search remaining games                        │
│  4.4 Filter to Best Game                                     │
│      └─> Keep only matches from best game above threshold   │
└──────────┬───────────────────────────────────────────────────┘
           │
           ▼
      Has matches?
           │
      ┌────┴────┐
      │  Yes    │  No → Skip game-specific
      ▼         │
┌──────────────────────────────────────────────────────────────┐
│  STAGE 5: Game-Specific Metadata (Conditional)              │
├──────────────────────────────────────────────────────────────┤
│  5A. MTG Set Symbol Detection (if MTG + ambiguous)          │
│      ├─> Check if top 2 matches are close                   │
│      ├─> Detect symbol box with YOLO                        │
│      ├─> Crop symbol region                                 │
│      └─> Match against set symbol database                  │
│                                                              │
│  5B. FAB Color Detection (if FAB + ambiguous)               │
│      ├─> Check if top 2 matches are close                   │
│      ├─> Extract top-left region (3-dot indicator)          │
│      └─> Classify color (red/yellow/blue)                   │
└──────────┬───────────────────────────────────────────────────┘
           │
           ▼
    ┌────────────┐
    │  Detection │
    │   Result   │
    └────────────┘
```

---

## Entry Point

### `ScannerPipeline::processFrame`

**Source:** [`packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp:16`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

```cpp
dto::ScanResult ScannerPipeline::processFrame(
    const cv::Mat &frameImage,
    const dto::ScannerConfig &config,
    DatabaseManager *dbManager,
    YoloSegmentationModel *yoloModel,
    CardEmbeddingModel *embeddingModel,
    SetSymbolYoloModel *setSymbolYolo,
    SetSymbolEmbedder *setSymbolEmbedder,
    FABColorClassifier *fabColorClassifier
)
```

**Parameters:**

- `frameImage` - RGB camera frame (cv::Mat)
- `config` - Scanner configuration
- `dbManager` - Database manager for card lookups
- Models - ML models (YOLO, embedder, etc.)

**Returns:**

- `ScanResult` - Contains all detected cards with metadata

---

## Pre-Processing Filters

### 1. Blur Detection

**Source:** [`ScannerPipeline.cpp:29-41`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

**Purpose:** Skip blurry frames to save ML compute

```cpp
if (config.blurThreshold > 0.0) {
  double blurScore = utils::ImageUtils::calculateBlurScore(frameImage);
  if (blurScore < config.blurThreshold) {
    return result; // Skip this frame
  }
}
```

**Method:** [`ImageUtils::calculateBlurScore`](../packages/react-native-card-scanner/common/rncardscanner/utils/ImageUtils.h)

- Computes Laplacian variance
- Higher score = sharper image
- Typical threshold: 100

---

### 2. Frame Rate Throttling

**Source:** [`ScannerPipeline.cpp:43-62`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

**Purpose:** Limit ML pipeline to max N FPS (configurable)

```cpp
if (config.maxFrameRate > 0) {
  int minIntervalMs = 1000 / config.maxFrameRate;

  std::lock_guard<std::mutex> lock(mlThrottleMutex);
  auto now = std::chrono::steady_clock::now();
  auto timeSinceLastML = /* ... */;

  if (timeSinceLastML < minIntervalMs) {
    return result; // Too soon, skip
  }

  lastMLProcessTime = now;
}
```

**Default:** 5 FPS (200ms between frames)
**Thread-safe:** Uses mutex for timestamp checking

---

## Pipeline Stages

### STAGE 1: Card Segmentation

**Source:** [`ScannerPipeline.cpp:64-65`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

```cpp
auto segmentation = performSegmentation(frameImage, config, yoloModel);
```

**Implementation:** [`ScannerPipeline.cpp:92-109`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

#### Step 1.1: YOLO Detection

```cpp
std::vector<Detection> rawDetections = yoloModel->detect(frameImage);
```

**Model:** YoloSegmentationModel
**Output:**

- Bounding boxes
- Confidence scores
- Game predictions (top 3)
- Optional dewarped images

**Reference:** [`models/YoloSegmentationModel.cpp`](../packages/react-native-card-scanner/common/rncardscanner/models/YoloSegmentationModel.cpp)

#### Step 1.2: Scan Mode Filter

```cpp
if (config.scanMode == "single") {
  // Keep only highest confidence detection
  if (!rawDetections.empty()) {
    auto best = std::max_element(/* ... */);
    rawDetections = {*best};
  }
}
```

**Modes:**

- `"single"` - Returns only best detection
- `"multiple"` - Returns all detections

---

### STAGE 2: Image Extraction

**Source:** [`ScannerPipeline.cpp:202-206`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

```cpp
card.croppedImage = utils::ImageUtils::extractCardImage(frameImage, detection);
if (card.croppedImage.empty()) {
  return card; // Failed, skip this detection
}
```

**Method:** [`ImageUtils::extractCardImage`](../packages/react-native-card-scanner/common/rncardscanner/utils/ImageUtils.h)

**Logic:**

1. Prefer dewarped image from YOLO if available
2. Otherwise, crop using bounding box with bounds checking

**Error Handling:** Returns empty card if extraction fails

---

### STAGE 3: Low-Light Enhancement

**Source:** [`ScannerPipeline.cpp:208-217`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

**Condition:** `lowLightThreshold > 0` AND image is dark

```cpp
if (config.lowLightThreshold > 0.0 &&
    utils::ImageUtils::isLowLight(processingImage, config.lowLightThreshold)) {

  processingImage = utils::ImageUtils::adjustGamma(
      processingImage,
      config.lowLightGamma  // Default: 2.0
  );
  cv::normalize(processingImage, processingImage, 0, 255, cv::NORM_MINMAX);
}
```

**Detection:** [`ImageUtils::isLowLight`](../packages/react-native-card-scanner/common/rncardscanner/utils/ImageUtils.h)

- Computes average luminance
- Returns true if average < threshold

**Enhancement:** Gamma correction (default γ=2.0)

---

### STAGE 4: Card Recognition

**Source:** [`ScannerPipeline.cpp:220-222`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

```cpp
card.matches = recognizeCard(processingImage, detection, config,
                             dbManager, embeddingModel);
```

**Implementation:** [`ScannerPipeline.cpp:125`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp) → [`SearchStrategy.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp)

#### Step 4.1: Compute Embedding

```cpp
std::vector<float> embedding = embeddingModel->computeEmbedding(cardImage);
```

**Model:** CardEmbeddingModel
**Output:** Feature vector (256 dimensions for cards and 128 dimensions for set symbols)

#### Step 4.2: Extract Top Games

**Source:** [`SearchStrategy.cpp:27`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp)

```cpp
std::vector<std::string> extractTopGames(
    const std::vector<GamePrediction> &gamePredictions
) {
  // Filter by MIN_YOLO_GAME_CONFIDENCE (0.1)
  // Take top MAX_GAME_PREDICTIONS (3)
}
```

**Constants:**

- `MIN_YOLO_GAME_CONFIDENCE` = 0.1
- `MAX_GAME_PREDICTIONS` = 3

#### Step 4.3: Multi-Database Search (Adaptive)

**Source:** [`SearchStrategy.cpp:57-112`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp)

```cpp
std::vector<CardSearchResult> searchMultipleDatabases(
    const std::vector<float> &embedding,
    const std::vector<std::string> &topGames,
    const dto::ScannerConfig &config,
    DatabaseManager &dbManager
) {
  std::vector<CardSearchResult> allResults;
  bool shouldSearchMore = false;

  for (size_t i = 0; i < topGames.size(); i++) {
    const auto &gameToSearch = topGames[i];

    try {
      ObjectBoxDB *gameDb = dbManager.getOrCreateStore(gameToSearch);
      if (!gameDb) continue;

      // Search in this game's database
      auto gameResults = gameDb->search_similar_cards(embedding, config.searchCandidates);

      // Tag results with game name
      for (auto &result : gameResults) {
        result.gameName = gameToSearch;
        allResults.push_back(result);
      }

      // Optimization: only search more games if first search is uncertain
      if (i == 0 && !gameResults.empty()) {
        float topScore = gameResults[0].score;

        // Search additional games if:
        // 1. Top score below confidence threshold + delta OR
        // 2. Multiple high-confidence games predicted by YOLO
        if (topScore < config.confidenceThreshold + SEARCH_MORE_THRESHOLD_DELTA ||
            topGames.size() > 1) {
          shouldSearchMore = true;
        } else {
          // High confidence match in first game, skip remaining searches
          break;
        }
      }

      // After first search, only continue if needed
      if (i > 0 && !shouldSearchMore) break;

    } catch (const std::exception &e) {
      continue; // Skip failed game database
    }
  }

  return allResults;
}
```

**Optimization:** Early exit after first database if:

- Top score ≥ threshold + delta (0.1) AND
- Only 1 game predicted by YOLO

**Constants:**

- `SEARCH_MORE_THRESHOLD_DELTA` = 0.1

#### Step 4.4: Filter to Best Game

**Source:** [`SearchStrategy.cpp:114-156`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp)

```cpp
std::vector<dto::CardMatch> filterToBestGame(
    const std::vector<CardSearchResult> &allResults,
    const dto::ScannerConfig &config
) {
  if (allResults.empty()) return {};

  // Sort all results by score (descending)
  auto sortedResults = allResults;
  std::sort(sortedResults.begin(), sortedResults.end(),
            [](const CardSearchResult &a, const CardSearchResult &b) {
              return a.score > b.score;
            });

  // Find the best match game (highest similarity score)
  std::string bestMatchGame = "";
  if (sortedResults[0].score >= config.confidenceThreshold) {
    bestMatchGame = sortedResults[0].gameName;
  }

  if (bestMatchGame.empty()) {
    return {}; // No confident match
  }

  // Filter: keep only cards from best match game
  std::vector<dto::CardMatch> filteredMatches;
  for (const auto &result : sortedResults) {
    if (result.gameName == bestMatchGame &&
        result.score >= config.confidenceThreshold) {
      filteredMatches.push_back(convertToCardMatch(result));

      if (filteredMatches.size() >= static_cast<size_t>(config.maxMatches)) {
        break;
      }
    }
  }

  return filteredMatches;
}
```

**Result:** Up to `config.maxMatches` cards from best game above `config.confidenceThreshold`

---

### STAGE 5: Game-Specific Metadata

**Source:** [`ScannerPipeline.cpp:224-233`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

Only runs if `card.hasMatches()` returns true.

---

## Game-Specific Processing

### 5A. MTG Set Symbol Detection

**Source:** [`ScannerPipeline.cpp:142-157`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp) → [`SetSymbolProcessor.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/SetSymbolProcessor.cpp)

#### Conditions

**Source:** [`SetSymbolProcessor.cpp:15-38`](../packages/react-native-card-scanner/common/rncardscanner/core/SetSymbolProcessor.cpp)

```cpp
// 1. Card is MTG
if (cardMatches[0].gameName != "mtg") return empty;

// 2. Models available
if (!yoloModel || !embedder || !database) return empty;

// 3. Disambiguation needed
if (cardMatches.size() >= 2) {
  float scoreDiff = cardMatches[0].score - cardMatches[1].score;
  if (scoreDiff > disambiguationThreshold) {
    return empty; // Top match is clearly best, skip
  }
}
```

**Disambiguation Threshold:** Default 0.02 (2% score difference)

#### Processing Steps

**Source:** [`SetSymbolProcessor.cpp:40-55`](../packages/react-native-card-scanner/common/rncardscanner/core/SetSymbolProcessor.cpp)

```cpp
// Step 1: Detect symbol bounding box
auto detections = yoloModel->detect(cardImage);
if (detections.empty()) return empty;
cv::Rect symbolBox = detections[0].bbox;

// Step 2: Crop symbol region
cv::Mat symbolImage = cardImage(symbolBox).clone();

// Step 3: Match against database
auto embedding = embedder->computeEmbedding(symbolImage);
auto results = database->search("set_symbols", embedding, 1);

if (!results.empty() && results[0].score >= confidenceThreshold) {
  return SetSymbolInfo(results[0].cardId, results[0].score);
}
```

**Models:**

- `SetSymbolYoloModel` - Detects symbol location
- `SetSymbolEmbedder` - Generates symbol embedding
- `ObjectBoxDB` - Set symbol database

**Reference:** [`models/mtg/SetSymbolYoloModel.cpp`](../packages/react-native-card-scanner/common/rncardscanner/models/mtg/SetSymbolYoloModel.cpp)

---

### 5B. FAB Color Variant Detection

**Source:** [`ScannerPipeline.cpp:159-184`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp) → [`FABColorProcessor.cpp`](../packages/react-native-card-scannerr/common/rncardscanner/core/FABColorProcessor.cpp)

#### Conditions

**Source:** [`FABColorProcessor.cpp:15-38`](../packages/react-native-card-scanner/common/rncardscanner/core/FABColorProcessor.cpp)

```cpp
// 1. Card is FAB
if (cardMatches[0].gameName != "fab") return empty;

// 2. Model available
if (!fabClassifier) return empty;

// 3. Disambiguation needed (same logic as MTG)
if (cardMatches.size() >= 2) {
  float scoreDiff = cardMatches[0].score - cardMatches[1].score;
  if (scoreDiff > disambiguationThreshold) {
    return empty;
  }
}
```

#### Processing Steps

**Source:** [`FABColorProcessor.cpp:40-54`](../packages/react-native-card-scanner/common/rncardscanner/core/FABColorProcessor.cpp)

```cpp
// Step 1: Extract dots region (top-left corner)
cv::Mat dotsRegion = extractDotsRegion(cardImage, dotsRegionRatio, minDotsRegionSize);

// Step 2: Classify color
return fabClassifier->classifyColor(dotsRegion);
```

**Extract Dots Region:** [`FABColorProcessor.cpp:57-76`](../packages/react-native-card-scanner/common/rncardscanner/core/FABColorProcessor.cpp)

```cpp
cv::Mat extractDotsRegion(const cv::Mat &cardImage,
                          double dotsRegionRatio,    // Default: 0.20
                          int minDotsRegionSize) {   // Default: 50
  int regionSize = std::min(cardImage.cols, cardImage.rows) * dotsRegionRatio;
  if (regionSize < minDotsRegionSize) {
    regionSize = minDotsRegionSize;
  }

  cv::Rect dotsRect(0, 0, regionSize, regionSize);
  return cardImage(dotsRect).clone();
}
```

**Model:** [`FABColorClassifier.cpp`](../packages/react-native-card-scanner/common/rncardscanner/models/fab/FABColorClassifier.cpp)

- Input: Top-left square region
- Output: Color ("red", "yellow", "blue") + similarity

---

## Optimization Strategies

### 1. Blur Filtering

- **Where:** [`ScannerPipeline.cpp:29-41`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)
- **Impact:** Skips ML on bad frames (~1-2ms blur check vs ~200ms ML)

### 2. Frame Throttling

- **Where:** [`ScannerPipeline.cpp:43-62`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)
- **Impact:** Limits to 5 FPS max (configurable)

### 3. Scan Mode Filtering

- **Where:** [`ScannerPipeline.cpp:104-109`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)
- **Impact:** Single mode processes 1 detection instead of N

### 4. Early Database Exit

- **Where:** [`SearchStrategy.cpp:76-80`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp)
- **Impact:** Stops after first database if high confidence

### 5. Disambiguation Gating

- **Where:** Set symbol & color detection conditions
- **Impact:** Only runs expensive operations when needed for disambiguation

---

## Thread Safety

### Model Access

**Source:** [`RnCardScannerInstaller.cpp:45`](../packages/react-native-card-scanner/common/rncardscanner/RnCardScannerInstaller.cpp)

```cpp
static std::mutex modelMutex_;
```

All model initialization/access protected by mutex.

### Frame Throttling

**Source:** [`ScannerPipeline.cpp:12-13`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp)

```cpp
static std::chrono::steady_clock::time_point lastMLProcessTime;
static std::mutex mlThrottleMutex;
```

Thread-safe timestamp checking for frame rate limiting.

### Database Manager

**Source:** [`database/DatabaseManager.cpp`](../packages/react-native-card-scanner/common/rncardscanner/database/DatabaseManager.cpp)

All database operations are thread-safe via internal locking.

---

## Error Handling

### Graceful Degradation

```cpp
try {
  card = processDetection(frameImage, detection, index, config, /*...*/);
} catch (const std::exception &e) {
  log(LOG_LEVEL::Error, "Detection processing failed: %s", e.what());
  // Return partial card with detection info only
  card.boundingBox = detection.bbox;
  card.detectionConfidence = detection.confidence;
}
```

**Strategy:** Pipeline continues even if individual detections fail

### Empty Results

All stages return empty/default values on failure:

- Empty `cv::Mat` for image operations
- Empty vectors for matches
- Empty structs for metadata

**Example:**

```cpp
if (card.croppedImage.empty()) {
  return card; // Skip remaining stages
}
```

---

## Related Documentation

- [API Reference](API.md) - Complete API documentation
- Source Code:
  - [`ScannerPipeline.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/ScannerPipeline.cpp) - Main pipeline
  - [`SearchStrategy.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/SearchStrategy.cpp) - Database search
  - [`SetSymbolProcessor.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/SetSymbolProcessor.cpp) - MTG set symbols
  - [`FABColorProcessor.cpp`](../packages/react-native-card-scanner/common/rncardscanner/core/FABColorProcessor.cpp) - FAB colors
