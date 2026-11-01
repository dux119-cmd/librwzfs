# Deletes and recreates the test archive. Run as a CTest fixture setup step.
# Inputs: -DMKDWARFS=<mkdwarfs> -DOUT=<image> -DSRC=<scratch dir>
#
# Fixed content (any other archive fails the exact-content tests):
#   DIR/FILE.DAT            "payload"
#   DIR/SUB/inner.txt       "x"
#   DIR/UP        -> ../DIR/FILE.DAT    relative link inside a directory
#   DIR/UPROOT    -> ../../hostfile     relative link that climbs out of the archive
#   LINK_REL      -> DIR/FILE.DAT       relative link at the root
#   LINK_ABS      -> /rwz/DIR/FILE.DAT  absolute archive link
#   HOSTLINK      -> /etc/rwz-test-host absolute host link (target need not exist)
#   DIRLINK       -> DIR                directory link
#   NESTLINK      -> DIR/SUB            directory link to a nested directory
#   DANGLING      -> NOPE               dangling link
#   LOOP_A -> LOOP_B, LOOP_B -> LOOP_A  loop
if(NOT DEFINED MKDWARFS OR NOT DEFINED OUT OR NOT DEFINED SRC)
  message(FATAL_ERROR "make_fixture.cmake needs MKDWARFS, OUT and SRC")
endif()

get_filename_component(OUT_DIR "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${OUT_DIR}")
file(REMOVE "${OUT}")
file(REMOVE_RECURSE "${SRC}")
file(MAKE_DIRECTORY "${SRC}/DIR/SUB")

file(WRITE "${SRC}/DIR/FILE.DAT" "payload")
file(WRITE "${SRC}/DIR/SUB/inner.txt" "x")

file(CREATE_LINK "../DIR/FILE.DAT" "${SRC}/DIR/UP" SYMBOLIC)
file(CREATE_LINK "../../hostfile" "${SRC}/DIR/UPROOT" SYMBOLIC)
file(CREATE_LINK "DIR/FILE.DAT" "${SRC}/LINK_REL" SYMBOLIC)
file(CREATE_LINK "/rwz/DIR/FILE.DAT" "${SRC}/LINK_ABS" SYMBOLIC)
file(CREATE_LINK "/etc/rwz-test-host" "${SRC}/HOSTLINK" SYMBOLIC)
file(CREATE_LINK "DIR" "${SRC}/DIRLINK" SYMBOLIC)
file(CREATE_LINK "DIR/SUB" "${SRC}/NESTLINK" SYMBOLIC)
file(CREATE_LINK "NOPE" "${SRC}/DANGLING" SYMBOLIC)
file(CREATE_LINK "LOOP_B" "${SRC}/LOOP_A" SYMBOLIC)
file(CREATE_LINK "LOOP_A" "${SRC}/LOOP_B" SYMBOLIC)

execute_process(
  COMMAND "${MKDWARFS}" -i "${SRC}" -o "${OUT}"
  RESULT_VARIABLE result
  OUTPUT_QUIET)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "mkdwarfs failed (${result}) building ${OUT}")
endif()
