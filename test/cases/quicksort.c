void qSort(int * A, int low, int high) {
    int i = low;
    int j = high;
    int x = A[(low+high)/2];
    do {
        while(A[i] < x) ++i;
        while(A[j] > x) --j;
        if(i <= j){
            int temp = A[i];
            A[i] = A[j];
            A[j] = temp;
            i++; j--;
        }
    } while(i <= j);

    if(low < j) qSort(A, low, j);
    if(i < high) qSort(A, i, high);
}
