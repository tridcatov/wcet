int single_iterative_loop(int input) {
    int result = 0;
    for (int i = 0; i < input; i++) {
        result += i;
    }
    return result;
}

int double_nested_iterative_loop (int input) {
    int result = 0;
    for (int i = 0; i < input; i++) {
        for (int j = 0; j < input; j++) {
            result += i % j;
        }
    }

    return result;
}

int arbitrary_loop(int input) {
    int result = 0;
    while (input > 3) {
        result ++;
        input--;
    }
    return result;
}
