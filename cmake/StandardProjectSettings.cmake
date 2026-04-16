set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Debug for single-config generators if nothing was set explicitly.
get_property(_kio_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _kio_is_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# compile_commands.json for clangd / IDEs.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable colored diagnostics when running through Ninja or other non-tty wrappers.
if(NOT DEFINED CMAKE_COLOR_DIAGNOSTICS)
  set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

# Optional ccache for faster rebuilds.
find_program(KIO_CCACHE_PROGRAM ccache)
if(KIO_CCACHE_PROGRAM)
  message(STATUS "ccache found: using ${KIO_CCACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${KIO_CCACHE_PROGRAM}")
endif()
