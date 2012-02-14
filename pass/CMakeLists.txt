add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)

add_library(basicBlocksNumber SHARED basicBlocksNumber.cpp)

add_library(singleExit SHARED singleExit.cpp)

add_library(linearize SHARED linearize.cpp)
set_target_properties(linearize PROPERTIES COMPILE_FLAGS "-fPIC")

set (llvmSupport /usr/lib/llvm-2.8/lib/libLLVMSupport.a)

target_link_libraries(linearize
        ${llvmSupport}
        )


add_library(abstractSemantics SHARED abstractSemantics.cpp)

add_library(helloWorld SHARED helloPass.cpp)

add_library(recursionBrakes SHARED recursionBrakes.cpp)

add_library(initUnreachable SHARED initUnreachable.cpp)
