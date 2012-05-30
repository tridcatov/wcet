int coworker(int i);

int worker(int input) {
    int result = 0;
    if (input % 2 != 0) {
        result += coworker(input);
    } else {
        result += input;
    }
    return result;
}

int coworker(int i) {
    int result = 4;
    if (input > 5) {
        result -= i + worker(i);
    } else {
        result += i;
    }

    return result;
}
