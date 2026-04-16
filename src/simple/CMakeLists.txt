add_executable(kernel-io-simple
  main.cpp
)

target_link_libraries(kernel-io-simple
  PRIVATE
    spdlog::spdlog
)

target_include_directories(kernel-io-simple
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

kio_set_warnings(kernel-io-simple)
