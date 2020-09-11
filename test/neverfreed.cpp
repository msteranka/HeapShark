#include <cstdlib>

int main()
{
    char c, *ptr;
    for (int i = 0; i < 5; i++)
    {
        ptr = (char *) malloc(20);
        for (int j = 0; j < 20; j++)
        {
            ptr[j] = 'a';
            c = ptr[j];
        }
    }
    return 0;
}
