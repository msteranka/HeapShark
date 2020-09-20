# Memory Profiler

A memory profiler for analyzing allocations that can be replaced with stack allocation or custom allocation routines

## Usage

Build the Pintool given the path to Pin:

    $ make PIN\_ROOT=path/to/Pin obj-intel64/profiler.so 

Run the Pintool on a given executable:

    $ path/to/Pin/pin -t obj-intel64/profiler.so -- path/to/executable
