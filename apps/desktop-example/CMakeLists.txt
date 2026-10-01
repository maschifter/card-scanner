cmake_minimum_required(VERSION 3.26)
# Before project(); see the package's CMakeLists.
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
project(desktop_example CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

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
cardscanner_copy_runtime_dlls(desktop_card_scanner)

# The long-running process the OBS module talks to. Owns the models, the
# database and the scan session; the OBS module owns none of that.
add_executable(card-scanner-server src/server_main.cpp)
target_link_libraries(card-scanner-server PRIVATE cardscanner_desktop)
set_target_properties(card-scanner-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/server")
# The DLLs go beside the executable, so the copy the OBS module spawns is the
# copy that already has ONNX Runtime and DirectML next to it.
cardscanner_copy_runtime_dlls(card-scanner-server)

# --- One-artifact OBS plugin ---
# Everything the scanner needs goes inside the bundle: the module, the server it
# spawns, the models, the databases and the overlay.
if(BUILD_OBS_MODULE AND APPLE)
  # Overridable for tools/export.mjs; the scripts always pass it, a -D value stays cached.
  if(NOT PLUGIN_DEST)
    set(PLUGIN_DEST "$ENV{HOME}/Library/Application Support/obs-studio/plugins/obs-card-scanner.plugin")
  endif()
  set(PKG "${DESKTOP_PACKAGE_DIR}")

  add_custom_target(install-obs-plugin
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/Contents/MacOS"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/Contents/Resources/locale"

      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:obs-card-scanner>"
              "${PLUGIN_DEST}/Contents/MacOS/obs-card-scanner"
      # The whole directory: the runtime libraries beside the server go with it.
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              "$<TARGET_FILE_DIR:card-scanner-server>"
              "${PLUGIN_DEST}/Contents/Resources"
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
elseif(BUILD_OBS_MODULE AND WIN32)
  # Where OBS 32 loads third-party plugins from, no admin rights needed. Close OBS
  # first: Windows will not overwrite a loaded DLL.
  if(NOT PLUGIN_DEST)
    set(PLUGIN_DEST "$ENV{ProgramData}/obs-studio/plugins/obs-card-scanner")
  endif()
  set(PKG "${DESKTOP_PACKAGE_DIR}")

  add_custom_target(install-obs-plugin
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/bin/64bit"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUGIN_DEST}/data/locale"

      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:obs-card-scanner>"
              "${PLUGIN_DEST}/bin/64bit/obs-card-scanner.dll"
      COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:card-scanner-server>"
              "${PLUGIN_DEST}/data/card-scanner-server.exe"
      # The loader finds DLLs beside the executable, so they travel with it.
      COMMAND ${CMAKE_COMMAND}
              -D "SRC_DIR=$<TARGET_FILE_DIR:card-scanner-server>"
              -D "DEST_DIR=${PLUGIN_DEST}/data"
              -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyRuntimeDlls.cmake"
      COMMAND ${CMAKE_COMMAND} -E copy "${PKG}/data/locale/en-US.ini"
              "${PLUGIN_DEST}/data/locale/en-US.ini"

      # Same payload as the Apple block above; a change there is a change here.
      COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/config.json"
              "${PLUGIN_DEST}/data/config.json"
      COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/web/dist/index.html"
              "${PLUGIN_DEST}/data/overlay.html"
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"
              "${PLUGIN_DEST}/data/assets/models"
      COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
              "${CMAKE_CURRENT_SOURCE_DIR}/assets/databases"
              "${PLUGIN_DEST}/data/assets/databases"

      DEPENDS obs-card-scanner card-scanner-server
      COMMENT "Assembling the obs-card-scanner plugin (module + server + models + overlay)"
  )
endif()
