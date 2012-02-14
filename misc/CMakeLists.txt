add_definitions(-D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS)
add_library(logger STATIC logger.cpp)
set_target_properties(logger PROPERTIES COMPILE_FLAGS "-fPIC")
