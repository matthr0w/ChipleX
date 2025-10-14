cmake_minimum_required(VERSION 3.10)
project(sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
message(STATUS "Using SystemC library: ${SYSTEMC_LIB}")

# YAML
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master
)
FetchContent_MakeAvailable(yaml-cpp)

file(GLOB_RECURSE PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
add_executable(sim ${PROJECT_SOURCES})

target_include_directories(sim PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_include_directories(sim PRIVATE $ENV{SYSTEMC_PATH}/include)

target_link_libraries(sim PRIVATE ${SYSTEMC_LIB})
target_link_libraries(sim PUBLIC yaml-cpp::yaml-cpp)

set_target_properties(sim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)