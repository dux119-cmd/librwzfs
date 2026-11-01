# Deletes and recreates the test archive. Run as a CTest fixture setup step.
# Inputs: -DMKDWARFS=<mkdwarfs> -DOUT=<image> -DSRC=<scratch dir>
# The archive content is fixed: DIR/FILE.DAT containing "payload". Nothing else.
if(NOT DEFINED MKDWARFS OR NOT DEFINED OUT OR NOT DEFINED SRC)
  message(FATAL_ERROR "make_fixture.cmake needs MKDWARFS, OUT and SRC")
endif()

get_filename_component(OUT_DIR "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")
file(REMOVE "${OUT}")
file(REMOVE_RECURSE "${SRC}")
file(MAKE_DIRECTORY "${SRC}/DIR")
file(WRITE "${SRC}/DIR/FILE.DAT" "payload")

execute_process(
  COMMAND "${MKDWARFS}" -i "${SRC}" -o "${OUT}"
  RESULT_VARIABLE result
  OUTPUT_QUIET)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "mkdwarfs failed (${result}) building ${OUT}")
endif()
