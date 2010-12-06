project(WCET)
cmake_minimum_required(VERSION 2.6.4)

set (LLVM ~/term/llvm28)
set (LLVM_INCLUDE
        ${LLVM}/include
        ${LLVM}/build/include
        )

include_directories( ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE}
        )

add_subdirectory(pass)
