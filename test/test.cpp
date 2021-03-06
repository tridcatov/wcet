int a(int i) {
    int b = i % 10;
    if (i < 0)
        b += 10 * i;
    else
        b = 12 - i;
    return b;
}

int loop(int count) {
    int result = count * 2;
    for (int i = 0; i < count; i++){
        for (int j = i; j < count; j ++) {
            result += i + 8 + j;
            result -= 2 * i + 24 * j;
            result %= 128 - j - i;
        }
    }
    return result;
}

int main(int argc, char ** )
{
    int result = -1;
    int p = argc;

    if (p > 1) {
        int a = 24;
        a += p * a;
        a -= 12 * p;
        result = a;
    } else {
        int b = 42;
        b += (p + 12) % 5;
        b -= b * b - 2;
        result = b;
    }
    result += loop(result);
    return result;
}

