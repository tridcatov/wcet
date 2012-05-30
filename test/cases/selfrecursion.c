int selfrecursionfactorial(int i) {
    int result;
    if (i == 0) {
        result = 1;
    } else {
        result = i * selfrecursionfactorial(i - 1);
    }
    return result;
}
