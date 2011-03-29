project(WCET)
cmake_minimum_required(VERSION 2.6.4)

set (LLVM /home/tridcatov/term/llvm)
set (LLVM_BUILD ${LLVM}/build)
set (LLVM_RELEASE ${LLVM_BUILD}/Release/Release)
set (LLVM_INCLUDE ${LLVM}/include ${LLVM_BUILD}/include)
set (LLVM_LIBS ${LLVM_RELEASE}/lib/)

set (PASSES_PATH ${CMAKE_CURRENT_BINARY_DIR}/pass/)
set (LLVM_TOOLS ${LLVM_RELEASE}/bin/)
set (LLVM_GCC /home/tridcatov/term/llvm-gcc-bin/bin/)

include_directories( 
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE}
        )


add_subdirectory(pass)
add_subdirectory(test)
