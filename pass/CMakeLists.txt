add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)

add_library(basicBlocksNumber SHARED basicBlocksNumber.cpp)

add_library(singleExit SHARED singleExit.cpp)

#add_library(localize SHARED localize.cpp)

add_library(linearize SHARED linearize.cpp)
set_target_properties(linearize PROPERTIES COMPILE_FLAGS "-fPIC")
target_link_libraries(linearize ${llvmAnalysis} ${llvmSupport})

add_library(helloWorld SHARED helloPass.cpp)
