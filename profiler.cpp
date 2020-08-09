#include "pin.H"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>
// #include "hoardtlab.h" - HOARD

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
    * Take into consideration what happens when you
    * write in the middle of an object instead of
    * just the pointer dished out by malloc().
*/

/*
    * Consider storing data before allocated heap
    * objects instead of storing everything in an
    * unordered_map.
*/

using namespace std;

class Data
{
    public:
        Data()
        {
            numAllocs = numReads = numWrites = bytesRead = bytesWritten = 0;
        }

        int numAllocs, numReads, numWrites, bytesRead, bytesWritten;
        bool isLive; // isLive is necessary to not keep track of reads and writes internal to the allocator
};

ostream& operator<<(ostream& os, const Data &data) 
{
    double avgReads, avgWrites, readFactor, writeFactor;
    avgReads = (double) data.numReads / data.numAllocs;
    avgWrites = (double) data.numWrites / data.numAllocs;
    readFactor = (double) data.numReads / data.bytesRead;
    writeFactor = (double) data.numWrites / data.bytesWritten;
    return os << "\tnumAllocs: " << data.numAllocs << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tavgReads = " << avgReads << endl <<
                 "\tavgWrites = " << avgWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << readFactor << endl <<
                 "\tWrite Factor = " << writeFactor << endl;
}

static ADDRINT nextSize;
unordered_map<ADDRINT, Data> m;
static bool isAllocating;
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

// TheCustomHeapType *getCustomHeap(); - HOARD

// void *GetAddrStart(void *addr) - HOARD
// {
// 
//     auto superblock = getCustomHeap()->getSuperblock(addr);
//     size_t size = getCustomHeap()->getSize(addr), objSize;
//     if (size == 0 || superblock == nullptr)
//     {
//         return nullptr;
//     }
//     objSize = superblock->getObjectSize();
//     return (char *) addr - (objSize - size);
// }

VOID MallocBefore(ADDRINT size) 
{
    nextSize = size;
}

VOID MallocAfter(ADDRINT ret) 
{
    unordered_map<ADDRINT, Data>::iterator it;
    if (!isAllocating) 
    {
        isAllocating = true;
        it = m.find(ret);
        if (it == m.end())
        {
            it = m.insert(make_pair<ADDRINT,Data>(ret,Data())).first;
        }
        it->second.numAllocs++;
        it->second.isLive = true;
        isAllocating = false;
        PDEBUG("malloc(%lu) = %lx\n", nextSize, ret);
    }
}

VOID FreeHook(ADDRINT ptr) 
{
    unordered_map<ADDRINT, Data>::iterator it;
    it = m.find(ptr);
    if (it != m.end())
    {
        it->second.isLive = false;
    }
    PDEBUG("free(%lx)\n", ptr);
}

VOID ReadsMem(ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    unordered_map<ADDRINT, Data>::iterator it;
    it = m.find(memoryAddressRead);
    if (it != m.end() && it->second.isLive) 
    {
        it->second.numReads++;
        it->second.bytesRead += memoryReadSize;
        PDEBUG("Read %d bytes @ 0x%lx\n", memoryReadSize, memoryAddressRead);
    }
}

VOID WritesMem(ADDRINT memoryAddressWritten, UINT32 memoryWriteSize) 
{
    unordered_map<ADDRINT, Data>::iterator it;
    it = m.find(memoryAddressWritten);
    if (it != m.end() && it->second.isLive) 
    {
        it->second.numWrites++;
        it->second.bytesWritten += memoryWriteSize;
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
    RTN mallocRtn, freeRtn;
    mallocRtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(mallocRtn)) 
    {
        RTN_Open(mallocRtn);
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR) MallocBefore, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR) MallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
        RTN_Close(mallocRtn);
    }
    freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(freeRtn)) 
    {
        RTN_Open(freeRtn);
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR) FreeHook, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_Close(freeRtn);
    }
}

VOID Fini(INT32 code, VOID *v) 
{
    unordered_map<ADDRINT,Data>::iterator it;
    for (it = m.begin(); it != m.end(); it++) 
    {
        traceFile << hex << it->first << ": " << endl << dec << it->second << endl;
    }
}

INT32 Usage() 
{
    cerr << "This tool tracks the number of reads and writes to allocated heap objects." << endl;
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
    traceFile.setf(ios::showbase);
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
}
