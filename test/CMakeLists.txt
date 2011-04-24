configure_file(pipeline ${CMAKE_CURRENT_BINARY_DIR}/pipeline @ONLY)
configure_file(test.cpp ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)

add_subdirectory(cases)
