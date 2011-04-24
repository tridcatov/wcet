set (cases
    branch.cpp
    linear.cpp
    loop.cpp
    )

foreach (case ${cases})
    configure_file(${case} ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
endforeach(case)
