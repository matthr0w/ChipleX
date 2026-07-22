cmake_minimum_required(VERSION 3.10)
project(sim LANGUAGES C CXX)

# -----------------------
# C++ standard
# -----------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -rdynamic")

# -----------------------
# Build type (default to Release for an optimized simulator build)
# -----------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
  message(STATUS "No build type specified; defaulting to Release")
endif()

# -----------------------
# Optional build configurations
# -----------------------
option(ENABLE_LTO "Enable link-time optimization (Release only)" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer + UBSan (verification builds)" OFF)

if(ENABLE_ASAN)
  # Sanitizer builds override optimization to keep stack traces usable.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -O1 -g)
  add_link_options(-fsanitize=address,undefined)
  message(STATUS "AddressSanitizer + UndefinedBehaviorSanitizer enabled")
endif()

if(ENABLE_LTO AND NOT ENABLE_ASAN)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
  if(ipo_supported)
    # Applied per-target below. The dlopen'd setup libraries call back into the
    # sim executable (see -rdynamic), so LTO must not internalize those
    # cross-module symbols; keep this opt-in and verify setups still load.
    message(STATUS "LTO/IPO supported and enabled")
  else()
    message(WARNING "LTO requested but not supported: ${ipo_output}")
    set(ENABLE_LTO OFF)
  endif()
endif()

# -----------------------
# Optional: specify setup library to build
# -----------------------
set(BUILD_SETUP "" CACHE STRING "Name of setup library to build (default: build all)")

# -----------------------
# Fetch dependencies
# -----------------------
include(FetchContent)

# SystemC: prefer the SYSTEMC_PATH env var (README/Makefile flow); otherwise
# fall back to an installed SystemC CMake package (SystemCLanguage).
set(SYSTEMC_PATH $ENV{SYSTEMC_PATH})
set(SYSTEMC_INCLUDE_DIR "")
if(EXISTS "${SYSTEMC_PATH}/lib/libsystemc.so")
  set(SYSTEMC_LIB "${SYSTEMC_PATH}/lib/libsystemc.so")
  set(SYSTEMC_INCLUDE_DIR "${SYSTEMC_PATH}/include")
elseif(EXISTS "${SYSTEMC_PATH}/lib64/libsystemc.so")
  set(SYSTEMC_LIB "${SYSTEMC_PATH}/lib64/libsystemc.so")
  set(SYSTEMC_INCLUDE_DIR "${SYSTEMC_PATH}/include")
else()
  find_package(SystemCLanguage QUIET)
  if(SystemCLanguage_FOUND)
    set(SYSTEMC_LIB SystemC::systemc)
    message(STATUS "Using SystemC from CMake package: ${SystemCLanguage_DIR}")
  else()
    message(FATAL_ERROR
      "SystemC not found. Set the SYSTEMC_PATH environment variable to a SystemC "
      "install (with lib/libsystemc.so or lib64/libsystemc.so), or make a SystemC "
      "CMake package discoverable via CMAKE_PREFIX_PATH.")
  endif()
endif()

# YAML (pinned to a tagged release rather than a moving master branch)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

# -----------------------
# Simulator executable
# -----------------------
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

  set_target_properties(sim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
  )

  if(ENABLE_LTO)
    set_target_properties(sim PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
endif()

# -----------------------
# Setup libraries
# -----------------------
file(GLOB SETUP_DIRS "${PROJECT_SOURCE_DIR}/setups/*")

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
      )

      if(ENABLE_LTO)
        set_target_properties(${LIB_NAME} PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
      endif()
    endif()
  endif()
endforeach()