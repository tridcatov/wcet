int switchcase(int input) {
    int result;
    switch(input) {
    case 0:
        result = input + 1;
        break;
    case 1:
        result = input * 2;
        break;
    case 2:
        result = input - 8;
        break;
    default:
        result = input / 3;
        break;
    }

    return result;:
}
