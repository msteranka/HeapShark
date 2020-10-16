// stackalloc compares the performance difference bewteen using stack allocation
// vs using heap allocation

#include <iostream>
#include <thread>
#include <cstdlib>

using namespace std;

void routine() {
    char buf[1000]; // HERE
    // char *buf = (char *) malloc(1000); // HERE
    for (int i = 0; i < 100000; i++) {
        for (int j = 0; j < 1000; j++) {
            buf[j] = 'a';
        }
    }
}

int main()
{
    const int NUM_THREADS = 8;
    thread threads[NUM_THREADS];
    cout << "Starting test..." << endl;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = thread(routine);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i].join();
    }
    cout << "Ending test..." << endl;
    return 0;
}
