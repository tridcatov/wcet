int multiplebranch(int input) {
    int result;
    if (input < 8) {
        result = input + 1;
    } else if (input < 50) {
        result = input + 2;
    } else {
        result = input - 1;
    }

    return result;:
}
