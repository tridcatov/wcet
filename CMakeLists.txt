project(WCET)
cmake_minimum_required(VERSION 2.6.4)

set (LLVM /usr/lib/llvm-2.8/)

set (PASSES_PATH ${CMAKE_CURRENT_BINARY_DIR}/pass/)
set (LLVM_INCLUDE ${LLVM}/include)
set (LLVM_TOOLS ${LLVM}/bin/)

include_directories( 
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}
        ${LLVM_INCLUDE}
        )


add_subdirectory(pass)
add_subdirectory(test)
add_subdirectory(misc)
