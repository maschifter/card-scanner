cmake_minimum_required(VERSION 3.14)
project(cardscanner_desktop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# Core is consumed as sources, exactly as the mobile package consumes it.
if(NOT DEFINED CORE_DIR)
  set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../card-scanner-core")
endif()
add_subdirectory("${CORE_DIR}" "${CMAKE_BINARY_DIR}/cardscanner_core")

# --- OpenCV ---
# Core compiles against the OpenCV 4 headers vendored in its third-party/ dir,
# so the libraries linked here MUST be 4.x. They are not ABI-compatible with 5,
# and a mismatch links green and misbehaves at runtime, so fail at configure
# time instead.
#
# Homebrew's opencv@4 is keg-only, so a bare find_package picks up opencv 5 on
# any machine that has both. Point at it explicitly before searching.
if(APPLE AND NOT DEFINED OpenCV_DIR)
  execute_process(COMMAND brew --prefix opencv@4
                  OUTPUT_VARIABLE BREW_OPENCV4_PREFIX
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(BREW_OPENCV4_PREFIX AND EXISTS "${BREW_OPENCV4_PREFIX}/lib/cmake/opencv4")
    set(OpenCV_DIR "${BREW_OPENCV4_PREFIX}/lib/cmake/opencv4")
    message(STATUS "desktop: using Homebrew opencv@4 at ${OpenCV_DIR}")
  endif()
endif()

find_package(OpenCV 4 REQUIRED)
if(NOT OpenCV_VERSION MATCHES "^4\\.")
  message(FATAL_ERROR
    "OpenCV ${OpenCV_VERSION} found, but core's vendored headers are 4.x and the "
    "ABI differs. Install OpenCV 4 and point CMake at it, e.g.\n"
    "  brew install opencv@4\n"
    "  cmake -S . -B build -DOpenCV_DIR=$(brew --prefix opencv@4)/lib/cmake/opencv4")
endif()

# --- ONNX Runtime ---
# Fetched rather than vendored or shell-scripted: one version-pinned source of
# truth that also works on Windows.
set(ONNXRUNTIME_VERSION "1.23.2")
if(APPLE)
  set(ORT_ARCHIVE "onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
  set(ORT_LIBNAME "libonnxruntime.dylib")
elseif(WIN32)
  # The DirectML build, not the stock one: the plain win-x64 package carries no
  # DML provider, so the acceleration ladder would silently be CPU-only.
  set(ORT_ARCHIVE "onnxruntime-win-x64-directml-${ONNXRUNTIME_VERSION}.zip")
  set(ORT_LIBNAME "onnxruntime.lib")
else()
  set(ORT_ARCHIVE "onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
  set(ORT_LIBNAME "libonnxruntime.so")
endif()

FetchContent_Declare(
    onnxruntime
    URL "https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/${ORT_ARCHIVE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/${ORT_LIBNAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include")
if(WIN32)
  set_target_properties(onnxruntime PROPERTIES
      IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib")
endif()

# --- ObjectBox runtime ---
# Core fetches objectbox-c HEADERS only and deliberately links no runtime - iOS
# takes the pod, Android the .so from the AAR. Desktop is the easy case: an
# official host build exists for every platform we target.
set(OBJECTBOX_VERSION "4.1.0")
if(APPLE)
  set(OBX_ARCHIVE "objectbox-macos-universal.zip")
  set(OBX_LIBNAME "libobjectbox.dylib")
elseif(WIN32)
  set(OBX_ARCHIVE "objectbox-windows-x64.zip")
  set(OBX_LIBNAME "objectbox.dll")
else()
  set(OBX_ARCHIVE "objectbox-linux-x64.tar.gz")
  set(OBX_LIBNAME "libobjectbox.so")
endif()

FetchContent_Declare(
    objectbox-runtime
    URL "https://github.com/objectbox/objectbox-c/releases/download/v${OBJECTBOX_VERSION}/${OBX_ARCHIVE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(objectbox-runtime)

add_library(objectbox SHARED IMPORTED)
set_target_properties(objectbox PROPERTIES
    IMPORTED_LOCATION "${objectbox-runtime_SOURCE_DIR}/lib/${OBX_LIBNAME}")
if(WIN32)
  set_target_properties(objectbox PROPERTIES
      IMPORTED_IMPLIB "${objectbox-runtime_SOURCE_DIR}/lib/objectbox.lib")
endif()

# --- HTTP ---
# System libcurl, rather than IXWebSocket's HTTP client, which the product
# endpoint answers with an HTML 500.
find_package(CURL REQUIRED)

# --- JSON ---
# The config file and the control protocol.
FetchContent_Declare(
    nlohmann_json
    URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- WebSocket ---
# Server-side only. The OBS module gets none of this: it speaks raw TCP with a
# packed header, which is why it can link libobs and nothing else.
# TLS on: the product lookup that gives the overlay a card name and its art is
# an HTTPS call. On Apple this uses Secure Transport, so it pulls in no OpenSSL.
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v12.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# --- Desktop runtime library ---
# Core, an inference backend, the scan session and the product client. Knows
# nothing about OBS: the module links a frame tap and a socket and none of this,
# which keeps a crash in ONNX or ObjectBox out of OBS's process.
add_library(cardscanner_desktop STATIC
    src/backends/onnx/OnnxSession.cpp
    src/config/ScannerConfigLoader.cpp
    src/service/ScanSession.cpp
    src/service/ScannerService.cpp
    src/service/ScannerServer.cpp
    src/service/ProductClient.cpp
    src/protocol/StateMessage.cpp
    src/net/HttpClient.cpp
    src/util/ParentWatchdog.cpp
    src/ipc/FrameServer.cpp
    src/ipc/ControlServer.cpp
    src/ipc/OverlayServer.cpp
)
set_target_properties(cardscanner_desktop PROPERTIES POSITION_INDEPENDENT_CODE ON)
# OpenCV_INCLUDE_DIRS is deliberately NOT added: core exports its own vendored
# opencv2 headers PUBLIC and those must be the only ones in the binary.
target_include_directories(cardscanner_desktop PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
if(WIN32)
  # Winsock is not linked by default.
  target_link_libraries(cardscanner_desktop PUBLIC ws2_32)
endif()

target_link_libraries(cardscanner_desktop
    PUBLIC
    cardscanner_core
    onnxruntime
    objectbox
    # LINK_ONLY: take the libraries, refuse their include directories, so
    # everything above core compiles against core's vendored headers alone.
    $<LINK_ONLY:${OpenCV_LIBS}>
    PRIVATE
    # JSON and HTTP are implementation details; the public surface is
    # ScannerConfig, ScannerServer and the protocol structs.
    nlohmann_json::nlohmann_json
    CURL::libcurl
    PUBLIC
    ixwebsocket
)

# --- Checks ---
# Asserts the InferenceSession contract against a committed fixture, so it runs
# without the card models or databases. -DBUILD_TESTING=OFF skips it.
include(CTest)
if(BUILD_TESTING)
  add_executable(test_onnx_session tests/test_onnx_session.cpp)
  target_link_libraries(test_onnx_session PRIVATE cardscanner_desktop)
  target_compile_definitions(test_onnx_session PRIVATE
      TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_test(NAME onnx_session COMMAND test_onnx_session)
endif()

# Compares two card databases by content. A tool rather than a test, since it
# needs two real databases.
add_executable(db_parity tools/db_parity.cpp)
target_link_libraries(db_parity PRIVATE cardscanner_desktop)

# --- OBS module ---
# Off by default: it downloads the obs-studio source for headers, which a plain
# library build has no need of.
#   cmake -S . -B build -DBUILD_OBS_MODULE=ON
option(BUILD_OBS_MODULE "Build the OBS plugin" OFF)
if(BUILD_OBS_MODULE)
  # OBS ships no headers in its app bundle, so they come from the source tree.
  # Pin this to the OBS actually installed: the ABI is not stable across major
  # versions, and a mismatched header set fails at load, not at build.
  set(OBS_VERSION "32.0.2" CACHE STRING "obs-studio version to take headers from")
  # SOURCE_SUBDIR names a directory that does not exist, so FetchContent
  # downloads the tree without configuring obs-studio's own build.
  FetchContent_Declare(
      obs-studio
      GIT_REPOSITORY https://github.com/obsproject/obs-studio.git
      GIT_TAG ${OBS_VERSION}
      GIT_SHALLOW TRUE
      GIT_SUBMODULES ""
      SOURCE_SUBDIR headers-only-no-cmake
  )
  FetchContent_MakeAvailable(obs-studio)

  # libobs' sse-intrin.h reaches for <simde/x86/sse2.h> on anything that is not
  # x86, and the shallow clone above skips upstream's submodule.
  FetchContent_Declare(
      simde
      GIT_REPOSITORY https://github.com/simd-everywhere/simde.git
      GIT_TAG v0.8.2
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR headers-only-no-cmake
  )
  FetchContent_MakeAvailable(simde)

  # obs-config.h includes obsconfig.h, which OBS's own CMake generates. The
  # module needs only the release-channel flags, both 0 for a stable release.
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/obs-generated/obsconfig.h"
       "#pragma once\n#define OBS_RELEASE_CANDIDATE 0\n#define OBS_BETA 0\n")

  add_library(obs-card-scanner MODULE
      src/module/obs-module.cpp
      src/module/ServerProcess.cpp
      src/obsbridge/FrameClient.cpp
  )

  # Deliberately NOT linked against cardscanner_desktop: OBS's process holds no
  # OpenCV, no ONNX Runtime and no ObjectBox, only libobs and a socket.
  target_include_directories(obs-card-scanner PRIVATE
      "${obs-studio_SOURCE_DIR}/libobs"
      "${obs-studio_SOURCE_DIR}/frontend/api"
      "${simde_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/obs-generated"
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_features(obs-card-scanner PRIVATE cxx_std_20)

  set_target_properties(obs-card-scanner PROPERTIES PREFIX "" SUFFIX ".so")
  if(APPLE)
    # OBS resolves libobs symbols when it loads the module.
    target_link_options(obs-card-scanner PRIVATE -undefined dynamic_lookup)

    # dynamic_lookup defers every unresolved symbol to load time, so a module
    # source calling into cardscanner_desktop links green and fails inside OBS
    # with "symbol not found in flat namespace". Only libobs is allowed to
    # resolve late; a cardscanner symbol left undefined is a link error here.
    add_custom_command(TARGET obs-card-scanner POST_BUILD
        COMMAND sh -c
            "! nm -u \"$1\" | grep -i cardscanner" check
            "$<TARGET_FILE:obs-card-scanner>"
        VERBATIM
        COMMENT "Checking the module resolves every cardscanner symbol")
  endif()

endif()
