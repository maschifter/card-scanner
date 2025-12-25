# CARD-NEXUS

A React Native card scanner package with ML-powered card recognition for trading card games (MTG, Lorcana, FAB, Pokémon, etc.).

## Platform Support

- **iOS**: 15.1+
- **Android**: API 29+ (Android 10+)

## Documentation

- [API Reference](docs/API.md) - Complete API documentation
- [Pipeline Architecture](docs/PIPELINE.md) - Detailed scanner pipeline explanation
- [Package README](packages/react-native-card-scanner/README.md) - ObjectBox integration & technical details

## Quick Start - Example App

### Prerequisites

- Node.js (v18+)
- Yarn (v4.1.1)
- Git LFS
- CMake (v3.18+) - Required for native code generation
- Xcode (for iOS development)
- Android Studio (for Android development)

**Installing CMake:**

```bash
# macOS
brew install cmake

# Ubuntu/Debian
sudo apt-get install cmake

# Windows
# Download from https://cmake.org/download/
```

### Installation

```bash
# Install Git LFS (if not already installed)
# macOS
brew install git-lfs

# Ubuntu/Debian
sudo apt-get install git-lfs

# Windows
# Download from https://git-lfs.github.com/

# Initialize Git LFS
git lfs install

# Clone the repository (or pull LFS files if already cloned)
git lfs pull

# Install dependencies
yarn
```

### Running the Example App

#### iOS

```bash
cd apps/example
yarn expo run:ios
```

#### Android

```bash
cd apps/example
yarn expo run:android
```

#### Development Server

```bash
cd apps/example
yarn expo start
```
