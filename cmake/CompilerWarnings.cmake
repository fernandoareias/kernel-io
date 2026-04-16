set(KIO_CLANG_GCC_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wconversion
  -Wsign-conversion
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
)

set(KIO_GCC_EXTRA_WARNINGS
  -Wmisleading-indentation
  -Wduplicated-cond
  -Wduplicated-branches
  -Wlogical-op
  -Wuseless-cast
)

# Apply the project's warning set to a target.
function(kio_set_warnings target)
  if(NOT TARGET ${target})
    message(FATAL_ERROR "kio_set_warnings: '${target}' is not a target")
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    target_compile_options(${target} PRIVATE ${KIO_CLANG_GCC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE
      ${KIO_CLANG_GCC_WARNINGS}
      ${KIO_GCC_EXTRA_WARNINGS}
    )
  endif()
endfunction()
