# Builds an import library for libobs on Windows.
#
# A DLL cannot be linked against directly; MSVC needs the matching .lib, and the
# OBS installer ships none - only obs.dll. Rather than making everyone fetch the
# separate OBS development package and match its version by hand, derive the
# import library from the obs.dll that is actually installed: dumpbin lists the
# exports, and lib /def turns that list into a .lib.
#
# Deriving it from the installed DLL also means it cannot drift out of step with
# the runtime it will be loaded into, which fetching a dev package by version
# number can.
#
#   generate_obs_import_lib(<obs.dll> <output.lib>)
function(generate_obs_import_lib DLL_PATH OUT_LIB)
  if(NOT EXISTS "${DLL_PATH}")
    message(FATAL_ERROR
      "obs.dll not found at ${DLL_PATH}\n"
      "Set OBS_INSTALL_DIR to your OBS Studio installation, e.g.\n"
      "  -DOBS_INSTALL_DIR=\"C:/Program Files/obs-studio\"")
  endif()

  get_filename_component(_out_dir "${OUT_LIB}" DIRECTORY)
  file(MAKE_DIRECTORY "${_out_dir}")
  set(_def "${_out_dir}/obs.def")
  get_filename_component(_msvc_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
  if(EXISTS "${OUT_LIB}" AND NOT "${DLL_PATH}" IS_NEWER_THAN "${OUT_LIB}")
    message(STATUS "obs import lib: up to date (${OUT_LIB})")
    return()
  endif()

  message(STATUS "obs import lib: generating from ${DLL_PATH}")
  execute_process(
      COMMAND "${_msvc_bin}/dumpbin.exe" /exports "${DLL_PATH}"
      OUTPUT_VARIABLE _exports
      RESULT_VARIABLE _dump_result
      ERROR_VARIABLE _dump_error)
  if(NOT _dump_result EQUAL 0)
    message(FATAL_ERROR "dumpbin failed on ${DLL_PATH}: ${_dump_error}")
  endif()

  set(_names "")
  string(REPLACE "\n" ";" _lines "${_exports}")
  foreach(_line IN LISTS _lines)
    if(_line MATCHES "^[ \t]+[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([A-Za-z_][A-Za-z0-9_]*)")
      string(APPEND _names "${CMAKE_MATCH_1}\n")
    endif()
  endforeach()
  if(_names STREQUAL "")
    message(FATAL_ERROR "no exports parsed out of ${DLL_PATH}")
  endif()
  get_filename_component(_dll_name "${DLL_PATH}" NAME)
  file(WRITE "${_def}" "LIBRARY ${_dll_name}\nEXPORTS\n${_names}")

  execute_process(
      COMMAND "${_msvc_bin}/lib.exe" /nologo "/def:${_def}" /machine:x64 "/out:${OUT_LIB}"
      RESULT_VARIABLE _lib_result
      ERROR_VARIABLE _lib_error
      OUTPUT_QUIET)
  if(NOT _lib_result EQUAL 0)
    message(FATAL_ERROR "lib /def failed: ${_lib_error}")
  endif()
  message(STATUS "obs import lib: wrote ${OUT_LIB}")
endfunction()
