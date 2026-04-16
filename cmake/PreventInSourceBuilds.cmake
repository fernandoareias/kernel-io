function(kio_assert_out_of_source_build)
  get_filename_component(_src_dir "${CMAKE_SOURCE_DIR}" REALPATH)
  get_filename_component(_bin_dir "${CMAKE_BINARY_DIR}" REALPATH)

  if("${_src_dir}" STREQUAL "${_bin_dir}")
    message(FATAL_ERROR
      "In-source builds are not allowed. "
      "Please create a separate build directory and run cmake from there, e.g.:\n"
      "  cmake --preset debug\n"
      "Remove CMakeCache.txt and CMakeFiles/ from the source tree before retrying."
    )
  endif()
endfunction()

kio_assert_out_of_source_build()
