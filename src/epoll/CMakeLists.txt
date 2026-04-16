add_executable(kernel-io-epoll
  main.cpp
)

target_link_libraries(kernel-io-epoll
  PRIVATE
    spdlog::spdlog
)

target_include_directories(kernel-io-epoll
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

kio_set_warnings(kernel-io-epoll)
