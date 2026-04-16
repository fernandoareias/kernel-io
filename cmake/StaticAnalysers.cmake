option(KIO_ENABLE_CLANG_TIDY "Run clang-tidy alongside the compiler" OFF)
option(KIO_ENABLE_CPPCHECK   "Run cppcheck alongside the compiler"   OFF)

if(KIO_ENABLE_CLANG_TIDY)
  find_program(KIO_CLANG_TIDY_EXE NAMES clang-tidy)
  if(KIO_CLANG_TIDY_EXE)
    message(STATUS "clang-tidy enabled: ${KIO_CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY "${KIO_CLANG_TIDY_EXE};--extra-arg=-Wno-unknown-warning-option")
  else()
    message(WARNING "KIO_ENABLE_CLANG_TIDY=ON but clang-tidy was not found in PATH")
  endif()
endif()

if(KIO_ENABLE_CPPCHECK)
  find_program(KIO_CPPCHECK_EXE NAMES cppcheck)
  if(KIO_CPPCHECK_EXE)
    message(STATUS "cppcheck enabled: ${KIO_CPPCHECK_EXE}")
    set(CMAKE_CXX_CPPCHECK
      "${KIO_CPPCHECK_EXE};--enable=warning,style,performance,portability;--inline-suppr;--suppress=missingIncludeSystem"
    )
  else()
    message(WARNING "KIO_ENABLE_CPPCHECK=ON but cppcheck was not found in PATH")
  endif()
endif()
