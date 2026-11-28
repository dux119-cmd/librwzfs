# The examples and tools (docs/USAGE.md, docs/BENCHMARK.md). They need dwarfs. Built by default in a standalone build.

if(NOT RWZFS_WITH_DWARFS)
  return()
endif()

# The showcase: host tree, mounted archive, writes, revision. Checked by CTest.
add_executable(rwzfs_example usage.cpp)
target_link_libraries(rwzfs_example PRIVATE rwzfs)
add_test(NAME rwzfs_example COMMAND rwzfs_example ${CMAKE_BINARY_DIR}/example-work)

# rwz-tool: open, revise and create archives from the command line. Its usage check expects a failure.
add_executable(rwz-tool rwz_tool.cpp)
target_link_libraries(rwz-tool PRIVATE rwzfs)
add_test(NAME rwz_tool_usage COMMAND rwz-tool)
set_tests_properties(rwz_tool_usage PROPERTIES WILL_FAIL TRUE)

# The read benchmark: host tree vs mounted archive. POSIX only.
if(NOT WIN32)
  add_executable(rwzfs_bench bench_read.cpp)
  target_link_libraries(rwzfs_bench PRIVATE rwzfs)
endif()
