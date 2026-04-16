if(KIO_BUILD_SIMPLE)
  add_subdirectory(simple)
endif()

if(KIO_BUILD_EPOLL)
  add_subdirectory(epoll)
endif()

if(KIO_BUILD_KQUEUE)
  add_subdirectory(kqueue)
endif()

if(KIO_BUILD_URING)
  add_subdirectory(uring)
endif()
