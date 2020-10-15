# HeapShark

HeapShark is a memory profiler for analyzing allocations that can be replaced with stack allocation or custom allocation routines

## Usage

Build HeapShark given the path to Pin:

    $ make PIN_ROOT=/path/to/Pin obj-intel64/heapshark.so 

Run HeapShark on a given executable:

    $ /path/to/Pin/pin -t obj-intel64/heapshark.so -- /path/to/executable
