set (case
        doublecorellatedloop.c
        doublenestedloop.c
        multiplebranch.c
        mutualrecursion.c
        nestedbranch.c
        quicksort.c
        selfrecursion.c
        simplebranch.c
        simplelinear.c
        singleloop.c
        switchbranch.c
    )

foreach (case ${cases})
    configure_file(${case} ${CMAKE_CURRENT_BINARY_DIR} COPYONLY)
endforeach(case)
