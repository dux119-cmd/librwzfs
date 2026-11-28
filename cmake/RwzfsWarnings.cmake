# Warnings for the library: reported, not errors (so a new compiler cannot break the build). Each flag is checked first,
# so a compiler without it (MSVC, or Clang without a GCC-only warning) skips it.
include(CheckCXXCompilerFlag)

set(RWZFS_WARNING_FLAGS
  -Wall -Wextra -Wdeprecated -Wno-unknown-pragmas
  -Wno-conversion -Wno-narrowing
  -Wdisabled-optimization -Wduplicated-branches -Wtrampolines -Wvla
  -Wlogical-op -Wduplicated-cond -Wpointer-arith
  -Wredundant-decls -Wfloat-conversion -Weffc++
  -Wextra-semi
  -Wimplicit-fallthrough -Winvalid-utf8
  -Wtrivial-auto-var-init -Wconversion -Wsign-conversion -Wnarrowing)

function(rwzfs_warnings target)
  set(active "")
  foreach(flag IN LISTS RWZFS_WARNING_FLAGS)
    string(MAKE_C_IDENTIFIER "RWZ_HAS${flag}" var)
    check_cxx_compiler_flag("${flag}" ${var})
    if(${var})
      list(APPEND active "${flag}")
    endif()
  endforeach()
  target_compile_options(${target} PRIVATE ${active})
endfunction()
