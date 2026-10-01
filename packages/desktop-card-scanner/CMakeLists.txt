cmake_minimum_required(VERSION 3.24)
# Before project(). 14.0 is what the pinned ONNX Runtime needs; unset, the
# build host's OS version becomes the floor.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
project(cardscanner_desktop CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# A Debug OpenCV is slow to build and to run, so unspecified means Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)
# Archives pin a SHA-256, git checkouts a commit; only obs-studio follows the
# installed OBS. A version bump means a new hash: `curl -L <url> | shasum -a 256`.
# A git pin must be the commit a tag points at: GIT_SHALLOW clones only tips.

# --- OpenCV ---
# Static, three modules, before core so a --benchmark toggle does not rebuild it.
# Core's opencv-mobile 4.11 headers match 4.14 for the API used; re-check on a bump.
set(OPENCV_VERSION "4.14.0")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_LIST "core,imgproc,imgcodecs" CACHE STRING "" FORCE)
foreach(_opt
    BUILD_TESTS BUILD_PERF_TESTS BUILD_EXAMPLES BUILD_opencv_apps BUILD_DOCS
    BUILD_JAVA BUILD_opencv_python2 BUILD_opencv_python3 BUILD_WITH_DEBUG_INFO
    WITH_FFMPEG WITH_GSTREAMER WITH_AVFOUNDATION WITH_COCOA WITH_OPENGL WITH_VTK
    WITH_OPENCL WITH_DIRECTX WITH_IPP WITH_LAPACK WITH_EIGEN
    WITH_PROTOBUF WITH_ADE WITH_ITT WITH_QUIRC WITH_OPENEXR WITH_TIFF WITH_WEBP
    WITH_OPENJPEG WITH_JASPER WITH_AVIF WITH_IMGCODEC_HDR WITH_IMGCODEC_SUNRASTER
    WITH_IMGCODEC_PXM WITH_IMGCODEC_PFM WITH_IMGCODEC_GIF
    OPENCV_GENERATE_PKGCONFIG CV_TRACE
    # MSVC: keep the /MD CRT everything else uses.
    BUILD_WITH_STATIC_CRT)
  set(${_opt} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(_opt BUILD_ZLIB BUILD_JPEG BUILD_PNG)
  set(${_opt} ON CACHE BOOL "" FORCE)
endforeach()
# GCD ignores setNumThreads() and slows small parallel_for_ loops, so macOS gets
# OpenCV's bundled oneTBB, static. KleidiCV is default on ARM; OpenCV pins both.
if(APPLE)
  set(WITH_TBB ON CACHE BOOL "" FORCE)
  set(BUILD_TBB ON CACHE BOOL "" FORCE)
else()
  set(WITH_TBB OFF CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    opencv
    URL "https://github.com/opencv/opencv/archive/refs/tags/${OPENCV_VERSION}.tar.gz"
    URL_HASH SHA256=ee8fb9b30eb60850431b4656447080e3737b56e45719c92b67f245950609f86e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(opencv)
# oneTBB's ucontext use needs this on macOS; its own CMake adds it, OpenCV's
# 3rdparty wrapper does not.
if(APPLE AND TARGET tbb)
  target_compile_definitions(tbb PRIVATE _XOPEN_SOURCE)
endif()
# OpenCV caches this and would move every later executable into <build>/bin.
unset(EXECUTABLE_OUTPUT_PATH CACHE)

# Core is consumed as sources, exactly as the mobile package consumes it.
if(NOT DEFINED CORE_DIR)
  set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../card-scanner-core")
endif()
# Opt-in dev switch for core's timers - dock timings, [perf] rows, --benchmark;
# 0 compiles them out. Must precede core's add_subdirectory or the link fails.
set(CARDSCANNER_BENCHMARK 0 CACHE STRING
    "1 compiles core's benchmark timers in, 0 compiles them out")
set(CARDSCANNER_BENCHMARK_VERBOSE 0 CACHE STRING
    "1 adds core's per-iteration benchmark log rows")
add_compile_definitions(
    CARDSCANNER_BENCHMARK=${CARDSCANNER_BENCHMARK}
    CARDSCANNER_BENCHMARK_VERBOSE=${CARDSCANNER_BENCHMARK_VERBOSE})
if(CARDSCANNER_BENCHMARK AND NOT CMAKE_CONFIGURATION_TYPES
   AND (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "Debug"))
  message(WARNING
      "CARDSCANNER_BENCHMARK=1 with CMAKE_BUILD_TYPE='${CMAKE_BUILD_TYPE}': the "
      "benchmark would time an unoptimised build. Use -DCMAKE_BUILD_TYPE=Release.")
endif()
add_subdirectory("${CORE_DIR}" "${CMAKE_BINARY_DIR}/cardscanner_core")
# Core logs one timestamped stderr line here, not os_log or stdout.
target_compile_definitions(cardscanner_core PUBLIC CARDSCANNER_LOG_STDERR)

# --- ONNX Runtime ---
# Fetched rather than vendored or shell-scripted, so the version is pinned in
# one place per platform. Windows needs its own pin: see the branch below.
set(ONNXRUNTIME_VERSION "1.29.0")
set(ORT_RELEASES "https://github.com/microsoft/onnxruntime/releases/download")
# The GitHub release archives use include/ and lib/; the Windows NuGet package
# has its own layout.
set(ORT_INCLUDE_SUBDIR "include")
set(ORT_RUNTIME_SUBDIR "lib")
if(APPLE)
  if(CMAKE_OSX_ARCHITECTURES)
    if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      message(FATAL_ERROR
        "The pinned ONNX Runtime archive is arm64-only and this build targets "
        "'${CMAKE_OSX_ARCHITECTURES}'. Switch ORT_ARCHIVE to the universal2 "
        "package if another architecture is ever needed.")
    endif()
  elseif(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    message(FATAL_ERROR
      "The pinned ONNX Runtime archive is arm64-only and this Mac is "
      "${CMAKE_SYSTEM_PROCESSOR}. Switch ORT_ARCHIVE to the universal2 "
      "package if an Intel build is ever needed.")
  endif()
  set(ORT_ARCHIVE "onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz")
  set(ORT_URL "${ORT_RELEASES}/v${ONNXRUNTIME_VERSION}/${ORT_ARCHIVE}")
  set(ORT_SHA256 "d0706fc34f315d8c88639d0a8c81f2e09e815f282cabed3493c06a054352cf92")
  # The install name (otool -D): the copy beside the executable must match it.
  set(ORT_LIBNAME "libonnxruntime.1.dylib")
elseif(WIN32)
  # DirectML ORT is NuGet-only; Windows ML 2.3.42 = ORT 1.27.1 + DirectML 1.15.4.
  set(WINML_VERSION "2.3.42")
  set(ORT_URL
      "https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/${WINML_VERSION}/microsoft.windows.ai.machinelearning.${WINML_VERSION}.nupkg")
  set(ORT_SHA256 "6487e2602a303e27cf699cf40ea158823b0b98ba2c18d96698a930f47260734d")
  # A .nupkg is a zip, but CMake's extractor dispatches on the extension and
  # does not know that one, so the download is renamed on its way in.
  set(ORT_ARCHIVE "winml-${WINML_VERSION}.zip")
  set(ORT_INCLUDE_SUBDIR "include/winml")
  set(ORT_RUNTIME_SUBDIR "runtimes/win-x64/native")
  set(ORT_IMPLIB_SUBDIR "lib/native/x64")
  set(ORT_LIBNAME "onnxruntime.dll")
else()
  set(ORT_ARCHIVE "onnxruntime-linux-x64-${ONNXRUNTIME_VERSION}.tgz")
  set(ORT_URL "${ORT_RELEASES}/v${ONNXRUNTIME_VERSION}/${ORT_ARCHIVE}")
  set(ORT_SHA256 "c3fddc4f139a045b0c4902c57410f0694f1c2fdf9b6939fbe38b1aeae7cd14ba")
  set(ORT_LIBNAME "libonnxruntime.so")
endif()

FetchContent_Declare(
    onnxruntime
    URL "${ORT_URL}"
    URL_HASH SHA256=${ORT_SHA256}
    DOWNLOAD_NAME "${ORT_ARCHIVE}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(onnxruntime)

# GLOBAL: consumers in other directories name these in the copy step below.
add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/${ORT_RUNTIME_SUBDIR}/${ORT_LIBNAME}"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/${ORT_INCLUDE_SUBDIR}")
if(WIN32)
  set_target_properties(onnxruntime PROPERTIES
      IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/${ORT_IMPLIB_SUBDIR}/onnxruntime.lib")

  # Not a target (no import lib, onnxruntime.dll delay-loads it), so TARGET_RUNTIME_DLLS
  # cannot list it, and the DirectML built into Windows is older than ORT needs.
  set(CARDSCANNER_EXTRA_RUNTIME_DLLS
      "${onnxruntime_SOURCE_DIR}/${ORT_RUNTIME_SUBDIR}/DirectML.dll"
      CACHE INTERNAL "DLLs that must sit beside anything linking cardscanner_desktop")
endif()

# --- ObjectBox runtime ---
# Core fetches objectbox-c HEADERS only and deliberately links no runtime - iOS
# takes the pod, Android the .so from the AAR. Desktop is the easy case: an
# official host build exists for every platform we target.
set(OBJECTBOX_VERSION "4.1.0")
if(APPLE)
  set(OBX_ARCHIVE "objectbox-macos-universal.zip")
  set(OBX_SHA256 "857b3bd62c8b26d4fe65fa1abb48d984c575cd38694b37118075223827b4a592")
  set(OBX_LIBNAME "libobjectbox.dylib")
elseif(WIN32)
  set(OBX_ARCHIVE "objectbox-windows-x64.zip")
  set(OBX_SHA256 "151d73a987d4e36475b3c8215b49cb63ffadcc66163f663b3f4114e82f9178ae")
  set(OBX_LIBNAME "objectbox.dll")
else()
  set(OBX_ARCHIVE "objectbox-linux-x64.tar.gz")
  set(OBX_SHA256 "dd4b725f2215485557e649bd44206004c6ee30519bd75d167670d7cc33104825")
  set(OBX_LIBNAME "libobjectbox.so")
endif()

FetchContent_Declare(
    objectbox-runtime
    URL "https://github.com/objectbox/objectbox-c/releases/download/v${OBJECTBOX_VERSION}/${OBX_ARCHIVE}"
    URL_HASH SHA256=${OBX_SHA256}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(objectbox-runtime)

add_library(objectbox SHARED IMPORTED GLOBAL)
set_target_properties(objectbox PROPERTIES
    IMPORTED_LOCATION "${objectbox-runtime_SOURCE_DIR}/lib/${OBX_LIBNAME}")
if(WIN32)
  set_target_properties(objectbox PROPERTIES
      IMPORTED_IMPLIB "${objectbox-runtime_SOURCE_DIR}/lib/objectbox.lib")
endif()

# --- HTTP ---
# libcurl, not IXWebSocket's HTTP client (the product endpoint answers it with
# a 500). System library on macOS and Linux; built in on Windows, Schannel TLS.
if(WIN32)
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
  set(HTTP_ONLY ON CACHE BOOL "" FORCE)
  foreach(_opt CURL_ZLIB CURL_BROTLI CURL_ZSTD CURL_USE_LIBPSL CURL_USE_LIBSSH2
               USE_LIBIDN2 USE_NGHTTP2)
    set(${_opt} OFF CACHE BOOL "" FORCE)
  endforeach()
  FetchContent_Declare(
      curl
      URL "https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.gz"
      URL_HASH SHA256=d54dd598bf05927a726deb38df31c6a255ba83ff1de57c5d1464dac3ed8f44a1
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  # curl's BUILD_TESTING clashes with CTest's; a normal variable shadows it here.
  set(BUILD_TESTING OFF)
  FetchContent_MakeAvailable(curl)
  unset(BUILD_TESTING)
else()
  find_package(CURL REQUIRED)
endif()

# --- JSON ---
# The config file and the control protocol.
FetchContent_Declare(
    nlohmann_json
    URL "https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz"
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# --- WebSocket ---
# Server-side only. The OBS module gets none of this: it speaks raw TCP with a
# packed header, which is why it can link libobs and nothing else.
# TLS off: the one HTTPS call (product lookup) goes through libcurl. Keeping
# TLS on would build mbedTLS into the Windows link for nothing.
set(USE_TLS OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    # v12.0.1 is the first release whose SocketServer::stop() closes the
    # listening fd exactly once (#585); v11.4.5 closed the number again on
    # every teardown, after the OS had reused it.
    GIT_TAG 64fae7676bd8fe31f7cb4bcde7a6841892dad65e  # v12.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ixwebsocket)

# --- Desktop runtime library ---
# Core, an inference backend, the scan session and the product client. Knows
# nothing about OBS: the module links a frame tap and a socket and none of this,
# which keeps a crash in ONNX or ObjectBox out of OBS's process.
add_library(cardscanner_desktop STATIC
    src/backends/onnx/OnnxSession.cpp
    src/benchmark/OfflineBenchmark.cpp
    src/config/ScannerConfigLoader.cpp
    src/service/ScannerService.cpp
    src/service/ScannerServer.cpp
    src/service/ProductClient.cpp
    src/protocol/StateMessage.cpp
    src/http/HttpClient.cpp
    src/util/OpenCvThreads.cpp
    src/util/ParentWatchdog.cpp
    src/ipc/FrameServer.cpp
    src/ipc/ControlServer.cpp
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
  # PUBLIC, not PRIVATE: <windows.h> arrives through Socket.h, which is a
  # header consumers include, so the definitions have to reach them too.
  #   NOMINMAX             - windows.h defines min/max as macros, which mangle
  #                          every std::min/std::max in OpenCV's headers
  #                          (imgproc.hpp's LineIterator is the first to blow
  #                          up) and anywhere else they are called qualified.
  #   WIN32_LEAN_AND_MEAN  - keeps winsock.h (v1) from being pulled in ahead of
  #                          winsock2.h, whose declarations then collide.
  target_compile_definitions(cardscanner_desktop PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

target_link_libraries(cardscanner_desktop
    PUBLIC
    cardscanner_core
    onnxruntime
    objectbox
    # LINK_ONLY: take the libraries, refuse their include directories, so
    # everything above core compiles against core's vendored headers alone.
    $<LINK_ONLY:opencv_core>
    $<LINK_ONLY:opencv_imgproc>
    $<LINK_ONLY:opencv_imgcodecs>
    PRIVATE
    # JSON and HTTP are implementation details; the public surface is
    # ScannerConfig, ScannerServer and the protocol structs.
    nlohmann_json::nlohmann_json
    CURL::libcurl
    ixwebsocket
)

# --- Runtime library deployment ---
# ONNX Runtime and ObjectBox are shared-only, so they sit beside each executable:
# no rpath on Windows, @executable_path alone on macOS. One copier per directory.
function(cardscanner_copy_runtime_dlls target)
  if(APPLE)
    set_target_properties(${target} PROPERTIES
        INSTALL_RPATH "@executable_path"
        BUILD_WITH_INSTALL_RPATH ON)
    set(_libs "$<TARGET_FILE:onnxruntime>" "$<TARGET_FILE:objectbox>")
  elseif(WIN32)
    set(_libs "$<TARGET_RUNTIME_DLLS:${target}>" ${CARDSCANNER_EXTRA_RUNTIME_DLLS})
  else()
    return()
  endif()
  get_target_property(_dir ${target} RUNTIME_OUTPUT_DIRECTORY)
  if(NOT _dir)
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}")
  elseif(NOT IS_ABSOLUTE "${_dir}")
    set(_dir "${CMAKE_CURRENT_BINARY_DIR}/${_dir}")
  endif()
  string(MD5 _key "${_dir}")
  set(_copier "cardscanner_runtime_dlls_${_key}")
  if(NOT TARGET ${_copier})
    add_custom_target(${_copier}
        COMMENT "Copying the ONNX Runtime and ObjectBox runtime libraries into ${_dir}")
  endif()
  add_custom_command(TARGET ${_copier} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              ${_libs}
              "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS)
  add_dependencies(${target} ${_copier})
endfunction()

# --- Checks ---
# Assert the InferenceSession contract against a committed fixture, and core's
# config parser against hand-built documents, so neither needs the card models
# or databases. -DBUILD_TESTING=OFF skips them.
include(CTest)
if(BUILD_TESTING)
  add_executable(test_onnx_session tests/test_onnx_session.cpp)
  target_link_libraries(test_onnx_session PRIVATE cardscanner_desktop)
  target_compile_definitions(test_onnx_session PRIVATE
      TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
  cardscanner_copy_runtime_dlls(test_onnx_session)
  add_test(NAME onnx_session COMMAND test_onnx_session)

  add_executable(test_config_parse tests/test_config_parse.cpp)
  target_link_libraries(test_config_parse PRIVATE cardscanner_desktop)
  cardscanner_copy_runtime_dlls(test_config_parse)
  add_test(NAME config_parse COMMAND test_config_parse)
endif()

# Compares two card databases by content. A tool rather than a test, since it
# needs two real databases.
add_executable(db_parity tools/db_parity.cpp)
target_link_libraries(db_parity PRIVATE cardscanner_desktop)
cardscanner_copy_runtime_dlls(db_parity)

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
      GIT_TAG 71fd833d9666141edcd1d3c109a80e228303d8d7  # v0.8.2
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
      "${obs-studio_SOURCE_DIR}/UI/obs-frontend-api"
      "${simde_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/obs-generated"
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_features(obs-card-scanner PRIVATE cxx_std_20)

  # .so on Apple because OBS looks for that inside a .plugin bundle; Windows
  # loads plain DLLs out of obs-plugins/64bit.
  if(WIN32)
    set_target_properties(obs-card-scanner PROPERTIES PREFIX "" SUFFIX ".dll")
  else()
    set_target_properties(obs-card-scanner PROPERTIES PREFIX "" SUFFIX ".so")
  endif()

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
  elseif(WIN32)
    # Windows has no equivalent of dynamic_lookup: every symbol must resolve at
    # link time, so the module needs libobs' import library. The OBS installer
    # ships obs.dll without it, so derive one from that DLL.
    if(NOT DEFINED OBS_INSTALL_DIR)
      set(OBS_INSTALL_DIR "C:/Program Files/obs-studio"
          CACHE PATH "OBS Studio installation directory")
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateObsImportLib.cmake")
    set(_obs_lib "${CMAKE_CURRENT_BINARY_DIR}/obs-import/obs.lib")
    generate_obs_import_lib("${OBS_INSTALL_DIR}/bin/64bit/obs.dll" "${_obs_lib}")
    target_link_libraries(obs-card-scanner PRIVATE "${_obs_lib}")
    set(_obs_fe_dll "${OBS_INSTALL_DIR}/bin/64bit/obs-frontend-api.dll")
    if(EXISTS "${_obs_fe_dll}")
      set(_obs_fe_lib "${CMAKE_CURRENT_BINARY_DIR}/obs-import/obs-frontend-api.lib")
      generate_obs_import_lib("${_obs_fe_dll}" "${_obs_fe_lib}")
      target_link_libraries(obs-card-scanner PRIVATE "${_obs_fe_lib}")
    endif()
    target_compile_definitions(obs-card-scanner PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(obs-card-scanner PRIVATE ws2_32)
  endif()

endif()
