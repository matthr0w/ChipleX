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

# find source files
# common sources
file(GLOB COMMON_SOURCES ${PROJECT_SOURCE_DIR}/common/*.cpp)
# include sources
file(GLOB INCLUDE_SOURCES ${PROJECT_SOURCE_DIR}/include/*.cpp)
# modules sources
file(GLOB MODULES_SOURCES ${PROJECT_SOURCE_DIR}/modules/*.cpp)
# usercode sources
file(GLOB USERCODE_SOURCES ${PROJECT_SOURCE_DIR}/usercode/*.c ${PROJECT_SOURCE_DIR}/usercode/*.cpp)

# add source files
add_executable(sim main.cpp ${COMMON_SOURCES} ${INCLUDE_SOURCES} ${MODULES_SOURCES} ${USERCODE_SOURCES})

# include SystemC headers
target_include_directories(sim PRIVATE $ENV{SYSTEMC_PATH}/include)

# include project headers
target_include_directories(sim PRIVATE ${PROJECT_SOURCE_DIR})

# link SystemC library
target_link_libraries(sim PRIVATE ${SYSTEMC_LIB})
# link YAML library
target_link_libraries(sim PUBLIC yaml-cpp::yaml-cpp)

# link statically, and disable PIE (VSCodium flatpak enforces PIE by default)
set(CMAKE_EXE_LINKER_FLAGS "-static -no-pie")

# set the runtime output directory
set_target_properties(sim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)