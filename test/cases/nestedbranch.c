int nestedbranch(int input) {
    int result;
    if (input < 0) {
        if (input > -10) {
            result = input - 8;
        } else {
            result = input + 50;
        }
    } else {
        if (input < 10) {
            result = input + 8;
        } else {
            result = input - 50;
        }
    }

    return result;
}
