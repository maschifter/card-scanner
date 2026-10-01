# The server's directory holds exactly the DLLs it loads, so it is copied whole;
# stale DLLs in DEST_DIR are removed rather than left to accumulate.
file(GLOB _dlls "${SRC_DIR}/*.dll")
if(NOT _dlls)
  message(FATAL_ERROR "No DLLs found beside the server in '${SRC_DIR}'.")
endif()

foreach(_src IN LISTS _dlls)
  get_filename_component(_name "${_src}" NAME)
  list(APPEND _keep "${_name}")
endforeach()

file(GLOB _installed "${DEST_DIR}/*.dll")
foreach(_old IN LISTS _installed)
  get_filename_component(_name "${_old}" NAME)
  if(NOT _name IN_LIST _keep)
    message(STATUS "Removing stale ${_name} from the bundle")
    file(REMOVE "${_old}")
  endif()
endforeach()

file(COPY ${_dlls} DESTINATION "${DEST_DIR}")
