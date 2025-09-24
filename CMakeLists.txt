cmake_minimum_required(VERSION 3.10)
project(sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# SystemC library
find_library(SYSTEMC_LIB 
    NAMES libsystemc.a 
    PATHS $ENV{SYSTEMC_PATH}/lib64
    REQUIRED
)

# YAML library
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG master
)
FetchContent_MakeAvailable(yaml-cpp)

file(GLOB_RECURSE PROJECT_SOURCES ${PROJECT_SOURCE_DIR}/src/test/*.cpp)
add_executable(sim ${PROJECT_SOURCES})

target_include_directories(sim PRIVATE ${PROJECT_SOURCE_DIR}/include/test)
target_include_directories(sim PRIVATE $ENV{SYSTEMC_PATH}/include)

target_link_libraries(sim PRIVATE ${SYSTEMC_LIB})
target_link_libraries(sim PUBLIC yaml-cpp::yaml-cpp)

# link statically, and disable PIE (VSCodium flatpak enforces PIE by default)
set(CMAKE_EXE_LINKER_FLAGS "-static -no-pie")

set_target_properties(sim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)