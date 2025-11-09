cmake_minimum_required(VERSION 3.14)
project(rncardscanner)

set(CMAKE_CXX_STANDARD 17)

# --- Fetch ObjectBox C-API and C++ headers ---
include(FetchContent)
FetchContent_Declare(
    objectbox-c
    GIT_REPOSITORY https://github.com/objectbox/objectbox-c.git
    GIT_TAG        v4.1.0
)
FetchContent_MakeAvailable(objectbox-c)

# --- Find ObjectBox Generator ---
find_package(ObjectBoxGenerator 4.0.0 REQUIRED)

# --- Source Directories ---
set(CPP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cpp")

# --- Core C++ library with ObjectBox ---
add_library(
    cardscanner_cpp
    STATIC
    "${CPP_DIR}/CardScanner.cpp"
    "${CPP_DIR}/ObjectBoxDB.cpp"
    "${CPP_DIR}/ObjectBoxTest.cpp"
    "${CPP_DIR}/PathProvider.cpp"
)

add_obx_schema(
    TARGET cardscanner_cpp
    SCHEMA_FILES "${CPP_DIR}/schema.fbs"
    INSOURCE
    CXX_STANDARD 14
)

target_include_directories(
    cardscanner_cpp
    PUBLIC
    "${CPP_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_SOURCE_DIR}/third-party/include"
)

target_link_libraries(
    cardscanner_cpp
    PUBLIC
    objectbox
)
