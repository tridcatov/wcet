void linear_single(int single) {
    int a = single;
    a ++;
    a /= 2;
    a += 3;
    a *= 4;
}

void linear_double(int first, int second) {
    int a = first;
    int b = second;
    a += b;
    b -= 2;
    a = a / b;
    b *= a;
}

int linear_return(int input) {
    int a = input;
    a += 2;
    a *= 3;
    a -= 5;
    a /= 6;
    return a;
}
