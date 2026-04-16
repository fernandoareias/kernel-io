add_executable(kernel-io-kqueue
  main.cpp
)

target_link_libraries(kernel-io-kqueue
  PRIVATE
    spdlog::spdlog
)

target_include_directories(kernel-io-kqueue
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

kio_set_warnings(kernel-io-kqueue)
