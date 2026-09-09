cmake_minimum_required(VERSION 3.14)
project(cardscanner)

set (CMAKE_VERBOSE_MAKEFILE ON)
set (CMAKE_CXX_STANDARD 20)

set(CMAKE_ANDROID_PAGE_SIZE 16384)

set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

include("${REACT_NATIVE_DIR}/ReactAndroid/cmake-utils/folly-flags.cmake")
add_compile_options(${folly_FLAGS})

string(APPEND CMAKE_CXX_FLAGS " -DRCT_NEW_ARCH_ENABLED")

# Optimization for release builds. AGP configures a release variant as
# RelWithDebInfo (CMake default -O2 -g), so both configurations get the flags;
# the trailing -O3 overrides -O2. Set here, before the subdirectories, so the
# core pipeline picks them up too. No -ffast-math: iOS builds without it, so
# FP semantics stay identical across platforms.
foreach(cfg RELEASE RELWITHDEBINFO)
  string(APPEND CMAKE_CXX_FLAGS_${cfg} " -O3 -funroll-loops")
  string(APPEND CMAKE_C_FLAGS_${cfg} " -O3 -funroll-loops")
endforeach()

set(ANDROID_CPP_DIR "${CMAKE_SOURCE_DIR}/src/main/cpp")
set(JNI_LIBS_DIR "${CMAKE_SOURCE_DIR}/src/main/jniLibs")
set(COMMON_CPP_DIR "${CMAKE_SOURCE_DIR}/../common")
# Core lives in the sibling package. CORE_DIR is passed by build.gradle from a
# node resolve of @cardnexus/card-scanner-core; the fallback keeps a bare
# `cmake` invocation working inside the monorepo.
if(NOT DEFINED CORE_DIR)
  set(CORE_DIR "${CMAKE_SOURCE_DIR}/../../card-scanner-core")
endif()
set(LIBS_DIR "${CMAKE_SOURCE_DIR}/../third-party/android/libs")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../third-party/include")

# Passed by build.gradle from the app's app.json; applies to every target below.
if(NOT DEFINED CARDSCANNER_BENCHMARK)
  set(CARDSCANNER_BENCHMARK 0)
endif()
if(NOT DEFINED CARDSCANNER_BENCHMARK_VERBOSE)
  set(CARDSCANNER_BENCHMARK_VERBOSE 0)
endif()
add_compile_definitions(
  CARDSCANNER_BENCHMARK=${CARDSCANNER_BENCHMARK}
  CARDSCANNER_BENCHMARK_VERBOSE=${CARDSCANNER_BENCHMARK_VERBOSE})

add_subdirectory("${CORE_DIR}" "${CMAKE_BINARY_DIR}/cardscanner_core")

add_subdirectory("${ANDROID_CPP_DIR}")

# ------- Nitro (VisionCamera v5 frame processor) -------
include("${CMAKE_CURRENT_SOURCE_DIR}/../nitrogen/generated/android/CardScanner+autolinking.cmake")

# Attach Nitrogen include paths (autolinking cmake uses directory-scoped commands).
target_include_directories(
  CardScanner
  PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/../nitrogen/generated/shared/c++"
  "${CMAKE_CURRENT_SOURCE_DIR}/../nitrogen/generated/android/c++"
  "${CMAKE_CURRENT_SOURCE_DIR}/../nitrogen/generated/android"
)

# Required by CardScannerOnLoad.cpp for Nitro HybridObject build.
target_compile_definitions(
  CardScanner
  PRIVATE
  BUILDING_CARDSCANNER_WITH_GENERATED_CMAKE_PROJECT
)