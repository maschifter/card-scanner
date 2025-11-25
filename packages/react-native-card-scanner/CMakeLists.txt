cmake_minimum_required(VERSION 3.14)
project(rncardscanner)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)

# --- Fetch ObjectBox C-API and C++ headers ---
include(FetchContent)

# The following is a workaround to force FetchContent to configure objectbox-c for the HOST system,
# even when cross-compiling. This is needed to get the host-side objectbox-generator.
# WARNING: This is fragile and relies on temporarily modifying internal CMake variables.
set(ORIGINAL_CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME})
    set(CMAKE_SYSTEM_NAME ${CMAKE_HOST_SYSTEM_NAME})


FetchContent_Declare(
    objectbox-c
    GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
    GIT_TAG        v4.1.0
)
FetchContent_MakeAvailable(objectbox-c)

# Restore the original system name after FetchContent is done.
set(CMAKE_SYSTEM_NAME ${ORIGINAL_CMAKE_SYSTEM_NAME})
#WARNING:  End of problematic block


# --- Find ObjectBox Generator ---
find_package(ObjectBoxGenerator 4.0.0 REQUIRED)

# --- Source Directories ---
set(COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/common/rncardscanner")

# --- Core C++ library with ObjectBox ---
file(GLOB PACKAGE_SOURCES "${CPP_DIR}/*.cpp")
add_library(
    cardscanner_cpp
    STATIC
    ${PACKAGE_SOURCES}
)

add_obx_schema(
    TARGET cardscanner_cpp
    SCHEMA_FILES "${COMMON_DIR}/database/schema.fbs"
    INSOURCE
    CXX_STANDARD 14
)

# Manually add the include directory from the fetched objectbox-c source.
# We cannot link against the 'objectbox' target because it's a binary compiled for the HOST architecture.
# Binaries for target architectures are provided idependently by iOS and Android build systems.
target_include_directories(
    cardscanner_cpp
    PUBLIC
    "${COMMON_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third-party/include"
    "${objectbox-c_SOURCE_DIR}/include"
    "${objectbox-c_SOURCE_DIR}/external"
)


