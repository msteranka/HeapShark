# Memory Profiler

A memory profiler for analyzing allocations that can be replaced with stack allocation or custom allocation routines

# Usage

cd src

make PIN\_ROOT=path/to/Pin obj-intel64/profiler.so 

path/to/Pin/pin -t obj-intel64/profiler.so -- path/to/executable
