# =============================================================================
# StageAssets.cmake - copy SRC/* to DST/ so the demos find assets/ next to the
# executables whatever the working directory at launch.
#
# Invoked by the chronoui_assets target:
#   cmake -DSRC=... -DDST=... -P StageAssets.cmake
#
# One pass: file(GLOB_RECURSE) + copy-if-different, so the build log gets one
# line instead of one per file.
# =============================================================================

if(NOT SRC OR NOT DST)
    message(FATAL_ERROR "StageAssets.cmake needs -DSRC=... -DDST=...")
endif()

file(GLOB_RECURSE _files RELATIVE "${SRC}" "${SRC}/*")

file(MAKE_DIRECTORY "${DST}")
foreach(_f ${_files})
    get_filename_component(_dir "${_f}" DIRECTORY)
    if(_dir)
        file(MAKE_DIRECTORY "${DST}/${_dir}")
    endif()
    # configure_file COPYONLY = copy if different, timestamps refreshed.
    configure_file("${SRC}/${_f}" "${DST}/${_f}" COPYONLY)
endforeach()
