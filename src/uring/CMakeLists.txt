find_library(LIBURING_LIBRARY NAMES uring REQUIRED)

add_executable(kernel-io-uring
  main.cpp
)

target_link_libraries(kernel-io-uring
  PRIVATE
    spdlog::spdlog
    ${LIBURING_LIBRARY}
)

target_include_directories(kernel-io-uring
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

kio_set_warnings(kernel-io-uring)
