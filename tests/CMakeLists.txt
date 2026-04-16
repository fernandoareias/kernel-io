include(GoogleTest)

# Helper to register a backend smoke test. Drop a tests/test_<backend>.cpp file
# (e.g. tests/test_simple.cpp) and uncomment the matching line below.
function(kio_add_backend_test backend)
  add_executable(test_${backend} test_${backend}.cpp)
  target_link_libraries(test_${backend}
    PRIVATE
      GTest::gtest_main
      spdlog::spdlog
  )
  kio_set_warnings(test_${backend})
  gtest_discover_tests(test_${backend})
endfunction()

# Uncomment the line for each backend once you create its test file:
# if(KIO_BUILD_SIMPLE) kio_add_backend_test(simple) endif()
# if(KIO_BUILD_EPOLL)  kio_add_backend_test(epoll)  endif()
# if(KIO_BUILD_KQUEUE) kio_add_backend_test(kqueue) endif()
