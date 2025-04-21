cmake_minimum_required(VERSION 3.10)
project(sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_library(SYSTEMC_LIB 
    NAMES libsystemc.a 
    PATHS $ENV{SYSTEMC_PATH}/lib64
    REQUIRED
)

# find source files
# chiplet sources
file(GLOB CHIPLET_SOURCES ${PROJECT_SOURCE_DIR}/chiplet/*.cpp)
# common sources
file(GLOB COMMON_SOURCES ${PROJECT_SOURCE_DIR}/common/*.cpp)

# add source files
add_executable(sim main.cpp ${CHIPLET_SOURCES} ${COMMON_SOURCES})

# include SystemC headers
target_include_directories(sim PRIVATE $ENV{SYSTEMC_PATH}/include)

# include project headers
target_include_directories(sim PRIVATE ${PROJECT_SOURCE_DIR})

# link SystemC library
target_link_libraries(sim PRIVATE ${SYSTEMC_LIB})

# link statically, and disable PIE (VSCodium flatpak enforces PIE by default)
set(CMAKE_EXE_LINKER_FLAGS "-static -no-pie")

# set the runtime output directory
set_target_properties(sim
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}"
)