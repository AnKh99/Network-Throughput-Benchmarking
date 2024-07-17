cmake_minimum_required(VERSION 3.10)
project(DPDKTest)

set(CMAKE_CXX_STANDARD 20)

# Add the required flag for SSSE3
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mssse3")

# Enable Clang-Tidy
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    find_program(CLANG_TIDY_EXE NAMES "clang-tidy" REQUIRED)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
    endif()
endif()

# Find DPDK
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK REQUIRED libdpdk)

# Include DPDK directories
include_directories(${DPDK_INCLUDE_DIRS})

# Link DPDK libraries
link_directories(${DPDK_LIBRARY_DIRS})

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

add_executable(get_mac get_mac.cpp)
add_executable(socket_single_send socket_single_send.cpp)
add_executable(socket_mt_send socket_mt_send.cpp)
add_executable(dpdk_receiver dpdk_receiver.cpp)
add_executable(dpdk_sender dpdk_sender.cpp)
add_executable(socket_receiver socket_receiver.cpp)

target_link_libraries(get_mac ${DPDK_LIBRARIES})
target_link_libraries(dpdk_receiver ${DPDK_LIBRARIES})
target_link_libraries(dpdk_sender ${DPDK_LIBRARIES})