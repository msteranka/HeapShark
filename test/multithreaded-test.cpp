#include <iostream>
#include <cstdlib>
#include <thread>

inline void *ec_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == nullptr) {
        std::cerr << "multithreaded-test ERROR: malloc failed" << std::endl;
        exit(EXIT_FAILURE);
    }
    return ptr;
}

// Using custom memset here because regular memset doesn't
// trigger coverage quite right
//
inline void my_memset(void *ptr, size_t size) {
    for (int i = 0; i < size; i++) {
        *((char *) ptr + i) = 'a';
    }
}

void routine(const int NUM_ITERS, const int OBJ_SIZE) {
    void *ptr;
    for (int i = 0; i < NUM_ITERS; i++) {
        ptr = ec_malloc(OBJ_SIZE);
        my_memset(ptr, OBJ_SIZE);
        free(ptr);
     }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "usage: <num_threads> <num_iters> <obj_size>" << std::endl;
        return EXIT_FAILURE;
    }
    const int NUM_THREADS = std::stoi(argv[1]), NUM_ITERS = std::stoi(argv[2]), OBJ_SIZE = std::stoi(argv[3]);
    std::thread *threads = new std::thread[NUM_THREADS];
    std::cout << "Starting test..." << std::endl;
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i] = std::thread(routine, NUM_ITERS, OBJ_SIZE);
    }
    for (int i = 0; i < NUM_THREADS; i++) {
        threads[i].join();
    }
    std::cout << "Ending test..." << std::endl;
    return 0;
 }
