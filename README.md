# CARD-NEXUS

## Quick Start - Example App

### Prerequisites

- Node.js (v18+)
- Yarn (v4.1.1)
- Git LFS
- Xcode (for iOS development)
- Android Studio (for Android development)

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
yarn ios
```

#### Android

```bash
cd apps/example
yarn android
```

#### Development Server

```bash
cd apps/example
yarn start
```
