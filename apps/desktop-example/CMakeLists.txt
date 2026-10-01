cmake_minimum_required(VERSION 3.14)
project(desktop_example CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The package supplies the ONNX backend, config loading and the scanner service;
# this app supplies the models, the databases and a main(). Forwarded so one
# configure of this app produces the whole plugin.
option(BUILD_OBS_MODULE "Build the OBS plugin" OFF)

if(NOT DEFINED DESKTOP_PACKAGE_DIR)
  set(DESKTOP_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/desktop-card-scanner")
endif()
add_subdirectory("${DESKTOP_PACKAGE_DIR}" "${CMAKE_BINARY_DIR}/cardscanner_desktop")

# Headless harness: scans a still image with no OBS in the loop.
add_executable(desktop_card_scanner src/main.cpp)
target_link_libraries(desktop_card_scanner PRIVATE cardscanner_desktop)

# The long-running process the OBS module talks to. Owns the models, the
# database and the scan session; the OBS module owns none of that.
add_executable(card-scanner-server src/server_main.cpp)
target_link_libraries(card-scanner-server PRIVATE cardscanner_desktop)

# --- One-artifact OBS plugin ---
# Everything the scanner needs goes inside the bundle: the module, the server it
# spawns, the models, the databases and the overlay.
if(BUILD_OBS_MODULE AND APPLE)
  set(PLUGIN_DEST "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-card-scanner.plugin")
  set(PKG "${DESKTOP_PACKAGE_DIR}")

  add_custom_target(install-obs-plugin
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/Contents/MacOS"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/Contents/Resources/locale"

      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:obs-card-scanner>"
              "${PLUGIN_DEST}/Contents/MacOS/obs-card-scanner"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:card-scanner-server>"
              "${PLUGIN_DEST}/Contents/Resources/card-scanner-server"
      COMMAND ${CMAKE_COMMAND} -E copy "${PKG}/data/Info.plist"
              "${PLUGIN_DEST}/Contents/Info.plist"
      COMMAND ${CMAKE_COMMAND} -E copy "${PKG}/data/locale/en-US.ini"
              "${PLUGIN_DEST}/Contents/Resources/locale/en-US.ini"

      # config.json is copied unchanged and the assets keep their relative
      # layout, so its "assets/models" paths resolve inside the bundle.
      COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
              "${PLUGIN_DEST}/Contents/Resources/config.json"
      COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/web/dist/index.html"
              "${PLUGIN_DEST}/Contents/Resources/overlay.html"
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
              "${PLUGIN_DEST}/Contents/Resources/assets/models"
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/databases"
              "${PLUGIN_DEST}/Contents/Resources/assets/databases"

      DEPENDS obs-card-scanner card-scanner-server
      COMMENT "Assembling obs-card-scanner.plugin (module + server + models + overlay)"
  )
endif()
