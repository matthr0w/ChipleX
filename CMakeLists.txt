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
# Optional: specify setup library to build
# -----------------------
set(BUILD_SETUP "" CACHE STRING "Name of setup library to build (default: build all)")

# -----------------------
# Fetch dependencies
# -----------------------
include(FetchContent)

# SystemC
set(SYSTEMC_PATH $ENV{SYSTEMC_PATH})
if(EXISTS "${SYSTEMC_PATH}/lib/libsystemc.so")
  set(SYSTEMC_LIB "${SYSTEMC_PATH}/lib/libsystemc.so")
elseif(EXISTS "${SYSTEMC_PATH}/lib64/libsystemc.so")
  set(SYSTEMC_LIB "${SYSTEMC_PATH}/lib64/libsystemc.so")
else()
  message(FATAL_ERROR "SystemC library not found in ${SYSTEMC_PATH}/lib or lib64")
endif()

# YAML
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master
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
    $ENV{SYSTEMC_PATH}/include
  )

  target_link_libraries(sim PRIVATE
    ${SYSTEMC_LIB}
    yaml-cpp::yaml-cpp
  )

  set_target_properties(sim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
  )
endif()

# -----------------------
# Setup libraries
# -----------------------
file(GLOB SETUP_DIRS "${PROJECT_SOURCE_DIR}/setups/bcdc")

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
        $ENV{SYSTEMC_PATH}/include
      )

      target_link_libraries(${LIB_NAME} PRIVATE
        ${SYSTEMC_LIB}
        yaml-cpp::yaml-cpp
      )

      set_target_properties(${LIB_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${SETUP_PATH}"
        OUTPUT_NAME "setup"
      )
    endif()
  endif()
endforeach()