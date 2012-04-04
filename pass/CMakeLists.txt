add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)

set (PASSES
    hello
    basicBlocksNumber
    singleExit
    linearize
    abstractSemantics
    recursionBrakes
    brandNewLinearize
    loopInterpreter
)

foreach(PASS ${PASSES})
    add_library(${PASS} SHARED ${PASS}.cpp)
    set_target_properties(${PASS} PROPERTIES COMPILE_FLAGS "-fPIC")
    target_link_libraries(${PASS} logger)
endforeach(PASS)
