cmake_minimum_required(VERSION 3.10)
project(sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Export the simulator's symbols so dlopen'd setup plugins can resolve them.
# ld64 spells this -export_dynamic and rejects the GNU -rdynamic spelling.
if(APPLE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-export_dynamic")
else()
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
  message(STATUS "No build type specified; defaulting to Release")
endif()

option(ENABLE_LTO "Enable link-time optimization (Release only)" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer + UBSan (verification builds)" OFF)
option(RELOCATABLE "Bake a loader-relative rpath so binaries find a co-located SystemC (release bundles)" OFF)

if(RELOCATABLE)
  # sim loads the SystemC runtime from a co-located copy or the bundled
  # SystemC install; plugins then resolve SystemC from the already-loaded
  # library. A loader-relative rpath keeps this working wherever the bundle is
  # unpacked. glibc spells the token $ORIGIN, dyld spells it @loader_path.
  set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
  if(APPLE)
    set(CMAKE_INSTALL_RPATH "@loader_path;@loader_path/systemc/lib;@loader_path/systemc/lib64")
  else()
    set(CMAKE_INSTALL_RPATH "$ORIGIN;$ORIGIN/systemc/lib;$ORIGIN/systemc/lib64")
  endif()
endif()

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "AddressSanitizer + UndefinedBehaviorSanitizer enabled")
endif()

if(ENABLE_LTO AND NOT ENABLE_ASAN)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
  if(ipo_supported)
    message(STATUS "LTO/IPO supported and enabled")
  else()
    message(WARNING "LTO requested but not supported: ${ipo_output}")
    set(ENABLE_LTO OFF)
  endif()
endif()

set(SETUPS_DIR "${PROJECT_SOURCE_DIR}/setups" CACHE PATH "Directory containing setup subdirectories")
set(BUILD_SETUP "" CACHE STRING "Name of setup library to build (default: build all)")

include(FetchContent)

# SystemC: prefer SYSTEMC_PATH, then an installed CMake package. The runtime
# library name is platform-dependent (libsystemc.so on Linux, libsystemc.dylib
# on macOS) and the install lands in lib/ or lib64/, so probe both.
set(SYSTEMC_PATH $ENV{SYSTEMC_PATH})
set(SYSTEMC_INCLUDE_DIR "")
set(SYSTEMC_LIB "")

if(SYSTEMC_PATH)
  foreach(SYSTEMC_LIBDIR lib lib64)
    set(SYSTEMC_CANDIDATE
        "${SYSTEMC_PATH}/${SYSTEMC_LIBDIR}/libsystemc${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(NOT SYSTEMC_LIB AND EXISTS "${SYSTEMC_CANDIDATE}")
      set(SYSTEMC_LIB "${SYSTEMC_CANDIDATE}")
      set(SYSTEMC_INCLUDE_DIR "${SYSTEMC_PATH}/include")
    endif()
  endforeach()
endif()

if(NOT SYSTEMC_LIB)
  find_package(SystemCLanguage QUIET)
  if(SystemCLanguage_FOUND)
    set(SYSTEMC_LIB SystemC::systemc)
    message(STATUS "Using SystemC from CMake package: ${SystemCLanguage_DIR}")
  else()
    message(FATAL_ERROR
      "SystemC not found. Set the SYSTEMC_PATH environment variable to a SystemC "
      "install (with lib/libsystemc${CMAKE_SHARED_LIBRARY_SUFFIX} or "
      "lib64/libsystemc${CMAKE_SHARED_LIBRARY_SUFFIX}), or make a SystemC CMake "
      "package discoverable via CMAKE_PREFIX_PATH.")
  endif()
endif()

# yaml-cpp: a prebuilt copy when YAML_CPP_DIR is given (offline bundle builds),
# otherwise the pinned release fetched from upstream.
set(YAML_CPP_DIR "" CACHE PATH "Prebuilt yaml-cpp with include/ and lib/libyaml-cpp.a; empty = fetch")
if(YAML_CPP_DIR)
  add_library(yaml-cpp::yaml-cpp STATIC IMPORTED)
  set_target_properties(yaml-cpp::yaml-cpp PROPERTIES
    IMPORTED_LOCATION "${YAML_CPP_DIR}/lib/libyaml-cpp.a"
    INTERFACE_INCLUDE_DIRECTORIES "${YAML_CPP_DIR}/include")
  message(STATUS "Using prebuilt yaml-cpp: ${YAML_CPP_DIR}")
else()
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    # 0.9.0 rather than 0.8.0: the latter declares cmake_minimum_required
    # (VERSION 3.4), which CMake 4 rejects outright, so it cannot be configured
    # by any toolchain new enough to ship that release. Upstream's tag naming
    # gained the yaml-cpp- prefix in this release.
    GIT_TAG yaml-cpp-0.9.0
  )
  FetchContent_MakeAvailable(yaml-cpp)
endif()

# Simulator (skipped when building only a single setup plugin).
if(NOT BUILD_SETUP)
  file(GLOB_RECURSE PROJECT_SOURCES "${PROJECT_SOURCE_DIR}/src/*.cpp")

  add_executable(sim ${PROJECT_SOURCES})

  target_include_directories(sim PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${SYSTEMC_INCLUDE_DIR}
  )

  target_link_libraries(sim PRIVATE
    ${SYSTEMC_LIB}
    yaml-cpp::yaml-cpp
  )

  # Dev builds place sim at the repo root; relocatable bundle builds leave it in
  # the build tree for the release step to stage.
  if(NOT RELOCATABLE)
    set_target_properties(sim PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
    )
  endif()

  if(ENABLE_LTO)
    set_target_properties(sim PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# One shared setup plugin (libsetup.so) per setup directory, built from its
# src/ sources. BUILD_SETUP restricts the build to a single setup.
file(GLOB SETUP_DIRS "${SETUPS_DIR}/*")

foreach(SETUP_PATH ${SETUP_DIRS})
  if(IS_DIRECTORY ${SETUP_PATH})
    get_filename_component(SETUP_NAME ${SETUP_PATH} NAME)

    if(BUILD_SETUP AND NOT SETUP_NAME STREQUAL BUILD_SETUP)
      continue()
    endif()

    set(LIB_NAME "setup_${SETUP_NAME}")
    file(GLOB SETUP_CPP "${SETUP_PATH}/src/*.cpp" "${SETUP_PATH}/src/*.c")

    if(SETUP_CPP)
      add_library(${LIB_NAME} SHARED ${SETUP_CPP})

      target_include_directories(${LIB_NAME} PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${SETUP_PATH}/include
        ${SYSTEMC_INCLUDE_DIR}
      )

      target_link_libraries(${LIB_NAME} PRIVATE
        ${SYSTEMC_LIB}
        yaml-cpp::yaml-cpp
      )

      set_target_properties(${LIB_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${SETUP_PATH}"
        OUTPUT_NAME "setup"
        # Keep the plugin named libsetup.so on every platform. The loader and
        # the GUI address it by that exact name, and dlopen resolves it by path
        # rather than by the platform's library naming convention, so there is
        # no benefit to the native .dylib suffix here.
        SUFFIX ".so"
      )

      if(APPLE)
        # Plugins call into the simulator (Core::read, Core::dma, ...), whose
        # definitions live in the sim executable rather than in any library the
        # plugin links against. ld64's two-level namespace rejects those
        # undefined symbols at link time; dynamic_lookup defers them to a
        # flat-namespace lookup performed at dlopen time, which finds them in
        # the host executable because sim is linked with -export_dynamic.
        set_target_properties(${LIB_NAME} PROPERTIES
          LINK_FLAGS "-Wl,-undefined,dynamic_lookup")
      endif()

      if(ENABLE_LTO)
        set_target_properties(${LIB_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
      endif()
    endif()
  endif()
endforeach()
