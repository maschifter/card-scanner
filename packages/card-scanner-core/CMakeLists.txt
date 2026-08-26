cmake_minimum_required(VERSION 3.14)
project(cardscanner_core)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(OBX_SCHEMA_CPP "${SRC_DIR}/database/schema.obx.cpp")

# --- ObjectBox entity code ---
# schema.obx.{cpp,hpp} and objectbox-model.h are generated from schema.fbs and
# COMMITTED, so a normal build never needs the generator. That matters: the
# generator is a HOST binary, and finding one from inside a cross-compile
# (Android, via add_subdirectory from the NDK toolchain) is exactly the fragile
# case this package should not force on its consumers.
#
# Regenerate deliberately after editing schema.fbs:
#   cmake -S . -B build -DCARDSCANNER_GENERATE_OBX_SCHEMA=ON && cmake --build build
option(CARDSCANNER_GENERATE_OBX_SCHEMA
       "Regenerate ObjectBox entity code from schema.fbs (needs a host toolchain)" OFF)
if(NOT EXISTS "${OBX_SCHEMA_CPP}")
  message(STATUS "cardscanner_core: generated ObjectBox schema absent, enabling codegen")
  set(CARDSCANNER_GENERATE_OBX_SCHEMA ON)
endif()

# --- objectbox-c headers ---
# Only the headers are consumed here (ObjectBoxDB.h includes <objectbox.hpp>);
# the runtime library for each target architecture is supplied by the wrapper
# package - the ObjectBox pod on iOS, libobjectbox-jni.so on Android.
include(FetchContent)

if(CARDSCANNER_GENERATE_OBX_SCHEMA)
  # Codegen needs objectbox-c's own CMakeLists, because that is what pulls in
  # FindObjectBoxGenerator. Force the HOST system name so it resolves a host
  # generator binary rather than one for the target. Fragile, and confined to
  # this opt-in path precisely because of that.
  set(ORIGINAL_CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
  set(CMAKE_SYSTEM_NAME ${CMAKE_HOST_SYSTEM_NAME})
  FetchContent_Declare(
      objectbox-c
      GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
      GIT_TAG        v4.1.0
  )
  FetchContent_MakeAvailable(objectbox-c)
  set(CMAKE_SYSTEM_NAME ${ORIGINAL_CMAKE_SYSTEM_NAME})
else()
  # Headers only. SOURCE_SUBDIR names a directory that does not exist, so
  # FetchContent downloads the tree without add_subdirectory'ing objectbox-c's
  # CMakeLists - which would otherwise try to download a prebuilt runtime for
  # the *target*. There is no such artifact for android-aarch64 (it 404s), and
  # we would not use it anyway: iOS links the ObjectBox pod, Android links
  # libobjectbox-jni.so extracted from the AAR, desktop links the host build.
  FetchContent_Declare(
      objectbox-c
      GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
      GIT_TAG        v4.1.0
      SOURCE_SUBDIR  headers-only-no-cmake
  )
  FetchContent_MakeAvailable(objectbox-c)
endif()

# --- Core C++ library ---
# Portable pipeline: OpenCV + ObjectBox only. Links no inference runtime and no
# platform bridge - the wrapper package supplies a backend defining
# inference::loadSession. Keep it that way; it is the whole point of the split.
file(GLOB_RECURSE PACKAGE_SOURCES CONFIGURE_DEPENDS "${SRC_DIR}/*.cpp" "${SRC_DIR}/*.c")
add_library(
    cardscanner_core
    STATIC
    ${PACKAGE_SOURCES}
)
set_target_properties(cardscanner_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(cardscanner_core PUBLIC cxx_std_20)

# Named target so a consumer can ask for the entity code alone (iOS does this
# from a pod script phase). A no-op when the committed files are used as-is.
add_custom_target(cardscanner_schema)

if(CARDSCANNER_GENERATE_OBX_SCHEMA)
  find_package(ObjectBoxGenerator 4.0.0 REQUIRED)
  add_obx_schema(
      TARGET cardscanner_core
      SCHEMA_FILES "${SRC_DIR}/database/schema.fbs"
      INSOURCE
      CXX_STANDARD 14
  )
  add_dependencies(cardscanner_schema cardscanner_core)
endif()

target_include_directories(
    cardscanner_core
    PUBLIC
    "${SRC_DIR}"
    # Flat angle includes (<ObjectBoxDB.h>, <Log.h>, <DatabaseManager.h> ...) are
    # house style across the tree and resolve only via these.
    "${SRC_DIR}/database"
    "${SRC_DIR}/models"
    "${SRC_DIR}/utils"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third-party/include"
    "${objectbox-c_SOURCE_DIR}/include"
    "${objectbox-c_SOURCE_DIR}/external"
)
