int doublenestediterativeloop (int input) {
    int result = 0;
    for (int i = 0; i < input; i++) {
        for (int j = 0; j < input; j++) {
            result += i % j;
        }
    }

    return result;
}
