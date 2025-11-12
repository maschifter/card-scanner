# `react-native-card-scanner`

This README provides detailed information about the `react-native-card-scanner` package, focusing on its core functionality and, specifically, the intricate process of integrating and linking the ObjectBox database across Android and iOS platforms.

## Project Overview

`react-native-card-scanner` is a React Native module designed to provide card scanning capabilities. At its heart, it utilizes a C++ module for model execution and relies on the ObjectBox database for embeddings.

## ObjectBox Integration: The Challenge

ObjectBox, presents a unique integration challenge for cross-platform React Native development. While ObjectBox provides C++ APIs, its official binary distributions are primarily for desktop operating systems (Linux, macOS, Windows). For mobile platforms like Android and iOS, ObjectBox offers language-specific SDKs (e.g., `objectbox-android` for Java/Kotlin and `objectbox-swift` for Swift).

The core problem is two-fold:
1.  **Binary Compatibility:** Directly using desktop C++ binaries on mobile is not feasible.
2.  **Schema Generation:** The ObjectBox C++ API requires a code generator (`objectbox-generator`) to create C++ wrapper files from `.fbs` schema definitions. This generator needs to be integrated into the build process.

## ObjectBox Integration: The Solution

This project overcomes these challenges by:
*   **Leveraging Existing Mobile SDKs:** Extracting the underlying C-based shared libraries (`.so` for Android, `.xcframework` for iOS) from the official `objectbox-android` and `objectbox-swift` packages. Binary inspection confirmed symbol compatibility with the C++ headers.
*   **CMake for Schema Generation:** Integrating `objectbox-generator` into the CMake build system to automate the generation of C++ schema wrappers, ensuring a consistent and manageable process for all developers.

## Detailed Integration Steps

### Common CMake Configuration

The `CMakeLists.txt` located in `packages/react-native-card-scanner/` is central to the ObjectBox integration:

*   **`FetchContent` Directive:** It uses `FetchContent` to download `objectbox-c`. This approach is derived from the [ObjectBox C++ documentation](https://cpp.objectbox.io/installation). This provides:
    *   ObjectBox headers (`.h` and `.hpp`).
    *   Headers for internal dependencies, such as FlatBuffers.
    *   The `objectbox-generator` tool, crucial for creating C++ wrapper files for schemas defined in `.fbs` files.
    *   The `cmake add_obx_schema` function, which automates the schema wrapper generation. This approach is preferred over a manual CLI tool to simplify developer workflow.
*   **C++ File Linking:** It is also responsible for linking C++ source files located in the `cpp/` directory.
    **Important C++ Integration Note:** As per ObjectBox documentation, it's required to have exactly one `.cpp` file in your project that defines `OBX_CPP_FILE` right before the inclusion of the `"objectbox.hpp"` header. This `#define` instructs the header to emit implementation definitions. Defining it in multiple files will lead to linker errors due to multiple symbol definitions. For more details, refer to the [ObjectBox C++ Getting Started guide](https://cpp.objectbox.io/getting-started).

### Android Specifics

Android integration involves extracting native binaries and integrating with the Gradle build system:

*   **`objectbox-android` Dependency:** The `android/build.gradle` file depends on the `objectbox-android` package, which provides the `.aar` (Android Archive) file.
*   **JNI Extraction:** A custom Gradle task named `extractSo` is defined. This task unzips the `.aar` file and copies the resulting `jni/` directory (containing compiled shared objects for four Android processor architectures) into `src/main`. These binaries are now also included in the Git repository for convenience.
*   **CMake Invocation:** After the JNI binaries are extracted, Android's build system calls its own `CMakeLists.txt`, which in turn triggers the common CMake configuration and a specific `android/src/main/cpp/CMakeLists.txt` for Android-specific native builds.

### iOS Specifics

iOS integration leverages CocoaPods and the interoperability of Swift with C/C++:

*   **`objectbox-swift` Pod:** The `react-native-card-scanner.podspec` fetches the `objectbox-swift` CocoaPods pod.
*   **`xcframework` Inclusion:** Fortunately, Swift (part of the LLVM language family) is designed for interoperability with C/C++. The build system can directly locate and include the `.xcframework` found within the downloaded pod.
*   **Podspec Run Script:** The common CMake configuration is executed for iOS via a run script defined within the `.podspec` file.

## Schema Management

ObjectBox schemas are defined in `.fbs` files. The `objectbox-generator` tool, integrated via CMake, automatically generates the necessary C++ wrapper files.

**Generated Files:**
The `objectbox-generator` will produce the following files:
*   `objectbox-model.h`
*   `objectbox-model.json`
*   `[your-schema-name].obx.hpp` (e.g., `schema.obx.hpp`)
*   `[your-schema-name].obx.cpp` (e.g., `schema.obx.cpp`)

**Source Control Recommendation:**
It is crucial to add all these generated files to your source control (e.g., Git). Most importantly, `objectbox-model.json` ensures compatibility with previous versions of your database after you make changes to the schema.

**Important Note:** When adding a *new* `.fbs` file that has not been previously used, you *might* need to manually run the common CMake configuration once to force the initial code generation. For editing existing `.fbs` files, the automatic generation typically works as expected.

## Usage and API

*(This section will be expanded as the public API for the card scanner module is finalized.)*

## Contributing

*(This section might be expanded with guidelines for contributing to the project.)*
