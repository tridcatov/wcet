int simple_branch(int input) {
    int result;
    if (input < 5) {
        result = input * 5;
    } else {
        result = input - 8;
    }

    return input;
}

int extended_branch(int input) {
    int result;
    if (input < 8) {
        result = input + 1;
    } else if (input < 50) {
        result = input + 2;
    } else {
        result = input - 1;
    }
}

int nested_branch(int input) {
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
