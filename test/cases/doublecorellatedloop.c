int doublecorellatedloop (int input) {
    int result = 0;
    for (int i = 0; i < input; i++) {
        for (int j = i; j < input; j++) {
            result += i % j;
        }
    }

    return result;
}
