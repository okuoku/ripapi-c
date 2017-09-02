cmake_minimum_required(VERSION 3.0)
project(ripapi-c)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM = ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if(CYGWIN)
    message(STATUS "WAR on Cygwin LLVM...")
    add_definitions(
        "-DHOSTINCDIR=\"/usr/lib/clang/${LLVM_PACKAGE_VERSION}/include\"")
endif()

add_executable(ripapi
    ripapi.c)

target_link_libraries(ripapi clang)
