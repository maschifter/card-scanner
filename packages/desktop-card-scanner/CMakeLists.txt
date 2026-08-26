cmake_minimum_required(VERSION 3.14)
project(desktop_card_scanner CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core is consumed as sources, exactly as the mobile package consumes it.
if(NOT DEFINED CORE_DIR)
  set(CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../card-scanner-core")
endif()
add_subdirectory("${CORE_DIR}" "${CMAKE_BINARY_DIR}/cardscanner_core")

find_package(OpenCV REQUIRED)

add_executable(desktop_card_scanner src/main.cpp)
target_link_libraries(desktop_card_scanner PRIVATE cardscanner_core ${OpenCV_LIBS})
target_include_directories(desktop_card_scanner PRIVATE ${OpenCV_INCLUDE_DIRS})

# No backend linked yet, so inference::loadSession is undefined. Fine for a
# placeholder that never constructs a model; the follow-up adds OnnxSession.cpp.
