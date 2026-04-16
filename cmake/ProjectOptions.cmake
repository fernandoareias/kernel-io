include(CMakeDependentOption)

# Platform-driven defaults for which backends are buildable.
if(APPLE)
  set(_kio_default_kqueue ON)
  set(_kio_default_epoll  OFF)
  set(_kio_default_uring  OFF)
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux")
  set(_kio_default_kqueue OFF)
  set(_kio_default_epoll  ON)
  set(_kio_default_uring  ON)
elseif(CMAKE_SYSTEM_NAME MATCHES "BSD")
  set(_kio_default_kqueue ON)
  set(_kio_default_epoll  OFF)
else()
  set(_kio_default_kqueue OFF)
  set(_kio_default_epoll  OFF)
endif()

option(KIO_BUILD_SIMPLE "Build the simple backend executable"  ON)
option(KIO_BUILD_EPOLL  "Build the epoll backend executable"   ${_kio_default_epoll})
option(KIO_BUILD_KQUEUE "Build the kqueue backend executable"  ${_kio_default_kqueue})
option(KIO_BUILD_URING  "Build the uring backend executable"   ${_kio_default_uring})
option(KIO_ENABLE_TESTS "Build tests"                          ON)

# Hard guards: forcing an incompatible backend on a platform that cannot run it
# should fail at configure time, not produce a cryptic compile error later.
if(KIO_BUILD_EPOLL AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
  message(FATAL_ERROR
    "KIO_BUILD_EPOLL=ON requires Linux (current: ${CMAKE_SYSTEM_NAME}). "
    "epoll is a Linux-specific syscall and cannot be built on this platform."
  )
endif()

if(KIO_BUILD_URING AND NOT CMAKE_SYSTEM_NAME MATCHES "Linux")
  message(FATAL_ERROR
    "KIO_BUILD_URING=ON requires Linux (current: ${CMAKE_SYSTEM_NAME}). "
    "uring is a Linux-specific syscall and cannot be built on this platform."
  )
endif()

if(KIO_BUILD_KQUEUE AND NOT (APPLE OR CMAKE_SYSTEM_NAME MATCHES "BSD"))
  message(FATAL_ERROR
    "KIO_BUILD_KQUEUE=ON requires macOS or BSD (current: ${CMAKE_SYSTEM_NAME}). "
    "kqueue is a BSD-family syscall and cannot be built on this platform."
  )
endif()

message(STATUS "kernel-io backends: simple=${KIO_BUILD_SIMPLE} epoll=${KIO_BUILD_EPOLL} kqueue=${KIO_BUILD_KQUEUE} uring=${KIO_BUILD_URING}")
message(STATUS "kernel-io tests:    ${KIO_ENABLE_TESTS}")
