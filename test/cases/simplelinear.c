int simplelinear(int a; int b)
{
    int i = a + b;
    int j = a * b;
    int result = (i + j) * (i - j) / (4 * i * j);
    result += a * b;
    return result;
}
