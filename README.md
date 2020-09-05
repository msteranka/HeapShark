# Memory Profiler

A memory profiler for analyzing allocations that can be replaced with stack allocation or custom allocation routines

# Usage

make PIN\_ROOT=path/to/Pin profiler.so
path/to/Pin/pin -t profiler.so -- path/to/executable
