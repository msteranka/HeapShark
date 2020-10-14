#include <iostream>
#include <thread>

void routine(char *buf, size_t size, int iters) {
    char c;
    for (int i = 0; i < iters; i++) {
        for (int i = 0; i < size / 2; i++) {
            buf[i] = 'a';
            c = buf[i];
        }
    }
}

int main() {
    std::cout << "Starting test..." << std::endl;
    const int NUM_THREADS = 8, PARTITION_SIZE = 1024, ITERS = 100;
    char *buf = (char *) malloc(NUM_THREADS * PARTITION_SIZE);
    std::thread threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = std::thread(routine, buf + i * PARTITION_SIZE, PARTITION_SIZE, ITERS);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i].join();
    }
    free(buf);
    std::cout << "Ending test..." << std::endl;
    return 0;
}
