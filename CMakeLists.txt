project(WCET)
cmake_minimum_required(VERSION 2.6.4)

set (LLVM ~/term/llvm)
set (LLVM_BUILD ${LLVM}/build)
set (LLVM_RELEASE ${LLVM_BUILD}/Release+Asserts)
set (LLVM_INCLUDE ${LLVM}/include ${LLVM_BUILD}/include)
set (LLVM_LIBS ${LLVM_RELEASE}/lib)

include_directories( 
        ${CMAKE_SOURCE_DIR}/include
        ${LLVM_INCLUDE}
        )

find_library (llvmSupport NAMES libLLVMSupport.a PATHS ${LLVM_LIBS})


add_subdirectory(pass)
add_subdirectory(test)
