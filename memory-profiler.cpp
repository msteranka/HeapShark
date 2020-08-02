#include "pin.H"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>

#ifdef TARGET_MAC
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif // TARGET_MAC

#define PROF_DEBUG
#ifdef PROF_DEBUG
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif // PROF_DEBUG

/*
    * TODO: Take into consideration when objects are reused.
    * Upon calling free(), move the corresponding entry in m
    * to a new unordered_map that keeps track of all objects
    * ever spewed out by the allocator and print its contents
    * upon termination.
*/

static ADDRINT nextSize;
std::unordered_map<ADDRINT, std::pair<int,int>> m;
static bool isAllocating;
std::ofstream traceFile;
KNOB<std::string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

VOID MallocBefore(ADDRINT size) 
{
    nextSize = size;
}

VOID MallocAfter(ADDRINT ret) 
{
    std::pair<int,int> *p;
    if (!isAllocating) 
    {
        isAllocating = true;
        p = &(m[ret]);
        isAllocating = false;
        p->first = 0;
        p->second = 0;
        PDEBUG("malloc(%ld) = %lx\n", nextSize, ret);
    }
}

VOID FreeHook(ADDRINT ptr) 
{
    PDEBUG("free(%lx)\n", ptr);
}

VOID ReadsMem(ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    if (m.find(memoryAddressRead) != m.end()) 
    {
        m[memoryAddressRead].first++;
        PDEBUG("Read %d bytes @ 0x%lx\n", memoryReadSize, memoryAddressRead);
    }
}

VOID WritesMem(ADDRINT memoryAddressWritten, UINT32 memoryWriteSize) {
    if (m.find(memoryAddressWritten) != m.end()) 
    {
        m[memoryAddressWritten].second++;
        PDEBUG("Wrote %d bytes @ 0x%lx\n", memoryWriteSize, memoryAddressWritten);
    }
}

VOID Instruction(INS ins, VOID *v) 
{
    if (INS_IsMemoryRead(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem,
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_END);
    }

    if (INS_IsMemoryWrite(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) WritesMem,
                        IARG_MEMORYWRITE_EA,
                        IARG_MEMORYWRITE_SIZE,
                        IARG_END);
    }

}

VOID Image(IMG img, VOID *v) 
{
    RTN mallocRtn = RTN_FindByName(img, MALLOC), freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(mallocRtn)) 
    {
        RTN_Open(mallocRtn);
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR) MallocBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR) MallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
        RTN_Close(mallocRtn);
    }
    if (RTN_Valid(freeRtn)) 
    {
        RTN_Open(freeRtn);
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR) FreeHook, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_Close(freeRtn);
    }
}

VOID Fini(INT32 code, VOID *v) 
{
    for (auto it = m.begin(); it != m.end(); it++) 
    {
        traceFile << std::hex << it->first << ": " << std::dec << it->second.first << " Reads, " << it->second.second << " Writes" << std::endl;
    }
}

INT32 Usage() 
{
    std::cerr << "This tool tracks the number of reads and writes to allocated heap objects." << std::endl;
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) 
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) 
    {
        return Usage();
    }
    isAllocating = false;
    traceFile.open(knobOutputFile.Value().c_str());
    traceFile.setf(std::ios::showbase);
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
}
