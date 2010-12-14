
int f (int a, int b) {
    return a*b*2;
}

int main(int argc, char ** )
{
    int result = -1;
    int p = argc;
    if (p > 1) {
        result += 2;
    } else {
        result += 1;
    }
    return result;
}

