#include <cstdlib>

int main()
{
    int n, *ptr;
    for (int i = 0; i < 5; i++)
    {
        ptr  = (int *) malloc(20 * sizeof(int));
        for (int j = 0; j < 20; j++)
        {
            ptr[j] = 42;
            n = ptr[j];
        }
        free(ptr);
    }
    return 0;
}
