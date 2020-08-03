#include "pin.H"
#include <unordered_map>
#include <vector>
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
    * Take into consideration what happens when you
    * write in the middle of an object instead of
    * just the pointer dished out by malloc().
*/

using namespace std;

class Data
{
    public:
        Data(ADDRINT addr, int numReads, int numWrites)
        {
            this->addr = addr;
            this->numReads = numReads;
            this->numWrites = numWrites;
        }

        ADDRINT addr;
        int numReads, numWrites;
};

ostream& operator<<(ostream& os, const Data &data) 
{
    return os << hex << data.addr << ": " << dec << data.numReads << " Read(s), " << data.numWrites << " Write(s)";
}

static ADDRINT nextSize;
unordered_map<ADDRINT, pair<int,int>> live;
vector<Data> total;
static bool isAllocating;
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

VOID MallocBefore(ADDRINT size) 
{
    nextSize = size;
}

VOID MallocAfter(ADDRINT ret) 
{
    pair<int,int> *p;
    if (!isAllocating) 
    {
        isAllocating = true;
        p = &(live[ret]);
        isAllocating = false;
        p->first = 0;
        p->second = 0;
        PDEBUG("malloc(%ld) = %lx\n", nextSize, ret);
    }
}

VOID FreeHook(ADDRINT ptr) 
{
    pair<int,int> *p;
    isAllocating = true;
    p = &(live[ptr]);
    total.emplace_back(ptr, p->first, p->second);
    isAllocating = false;
    PDEBUG("free(%lx)\n", ptr);
}

VOID ReadsMem(ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    if (live.find(memoryAddressRead) != live.end()) 
    {
        live[memoryAddressRead].first++;
        PDEBUG("Read %d bytes @ 0x%lx\n", memoryReadSize, memoryAddressRead);
    }
}

VOID WritesMem(ADDRINT memoryAddressWritten, UINT32 memoryWriteSize) {
    if (live.find(memoryAddressWritten) != live.end()) 
    {
        live[memoryAddressWritten].second++;
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
    traceFile << "LIVE OBJECTS" << endl;
    for (auto it = live.begin(); it != live.end(); it++) 
    {
        traceFile << hex << it->first << ": " << dec << it->second.first << " Read(s), " << it->second.second << " Write(s)" << endl;
    }
    traceFile << endl << "DEALLOCATED OBJECTS" << endl;
    for (auto it = total.begin(); it != total.end(); it++) 
    {
        traceFile << *it << endl;
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
