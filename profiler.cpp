#include "pin.H"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>

#ifdef TARGET_MAC
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif // TARGET_MAC

#ifdef PROF_DEBUG
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif // PROF_DEBUG

#define FILLER 'a'

// NOTE: coverage can be misleading on structs/classes that require extra space for alignment

using namespace std;

class Data
{
    public:
        Data(ADDRINT base, size_t size)
        {
            this->base = base;
            this->size = size;
            readBuf = (char *) calloc(size, 1);
            writeBuf = (char *) calloc(size, 1);
            numAllocs = numFrees = numReads = numWrites = bytesRead = bytesWritten = 0;
            minCoverage.first = minCoverage.second = 1;
        }

        pair<double,double> GetCoverage()
        {
            int read = 0, written = 0;
            for (size_t i = 0; i < size; i++)
            {
                if (readBuf[i] == FILLER)
                {
                    read++;
                }
                if (writeBuf[i] == FILLER)
                {
                    written++;
                }
            }
            return make_pair<double,double>((double) read / size, (double) written / size);
        }

        ADDRINT base;
        char *readBuf, *writeBuf;
        size_t size;
        int numAllocs, numFrees, numReads, numWrites, bytesRead, bytesWritten;
        pair<double,double> minCoverage;
        bool isLive; // isLive is necessary to not keep track of reads and writes internal to the allocator
};

ostream& operator<<(ostream& os, Data &data) 
{
    double avgReads, avgWrites, readFactor, writeFactor;
    pair<double,double> coverage;
    avgReads = (double) data.numReads / data.numAllocs;
    avgWrites = (double) data.numWrites / data.numAllocs;
    readFactor = (double) data.numReads / data.bytesRead;
    writeFactor = (double) data.numWrites / data.bytesWritten;
    coverage = data.GetCoverage();
    return os << "malloc(" << hex << data.base << "):" << dec << endl <<
                 "\tnumAllocs: " << data.numAllocs << endl <<
                 "\tnumFrees: " << data.numFrees << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tavgReads = " << avgReads << endl <<
                 "\tavgWrites = " << avgWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << readFactor << endl <<
                 "\tWrite Factor = " << writeFactor << endl <<
                 "\tMin Read Coverage = " << min(data.minCoverage.first, coverage.first) << endl << 
                 "\tMin Write Coverage = " << min(data.minCoverage.second, coverage.second) << endl;
}

static ADDRINT nextSize;
unordered_map<ADDRINT, Data*> m;
static bool isAllocating;
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

VOID MallocBefore(ADDRINT size) 
{
    nextSize = size;
}

VOID MallocAfter(ADDRINT ret) 
{
    unordered_map<ADDRINT, Data*>::iterator it;
    Data *d;
    ADDRINT nextAddr;
    if (isAllocating)
    {
        return;
    }
    isAllocating = true;
    it = m.find(ret);
    if (it == m.end())
    {
        d = new Data(ret, nextSize);
        for (ADDRINT i = 0; i < nextSize; i++)
        {
            nextAddr = (ADDRINT) ((char *) ret + i);
            m.insert(make_pair<ADDRINT,Data*>(nextAddr, d));
        }
        it = m.find(ret);
    }
    it->second->numAllocs++;
    it->second->isLive = true;
    it->second->size = nextSize;
    memset(it->second->readBuf, 0, nextSize);
    memset(it->second->writeBuf, 0, nextSize);
    isAllocating = false;
    PDEBUG("malloc(%lu) = %lx\n", nextSize, ret);
}

VOID FreeHook(ADDRINT ptr) 
{
    unordered_map<ADDRINT, Data*>::iterator it;
    pair<double,double> coverage;
    isAllocating = true;
    it = m.find(ptr);
    if (it != m.end())
    {
        it->second->isLive = false;
        coverage = it->second->GetCoverage();
        it->second->minCoverage.first = min(coverage.first, it->second->minCoverage.first);
        it->second->minCoverage.second = min(coverage.second, it->second->minCoverage.second);
        it->second->numFrees++;
    }
    isAllocating = false;
    PDEBUG("free(%lx)\n", ptr);
}

VOID ReadsMem(ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    unordered_map<ADDRINT, Data*>::iterator it;
    char *start;
    isAllocating = true;
    it = m.find(memoryAddressRead);
    isAllocating = false;
    if (it != m.end() && it->second->isLive) 
    {
        it->second->numReads++;
        it->second->bytesRead += memoryReadSize;
        start = (char *) it->second->readBuf + (memoryAddressRead - it->second->base);
        memset(start, FILLER, memoryReadSize);
        PDEBUG("Read %d bytes @ 0x%lx\n", memoryReadSize, memoryAddressRead);
    }
}

VOID WritesMem(ADDRINT memoryAddressWritten, UINT32 memoryWriteSize) 
{
    unordered_map<ADDRINT, Data*>::iterator it;
    char *start;
    isAllocating = true;
    it = m.find(memoryAddressWritten);
    isAllocating = false;
    if (it != m.end() && it->second->isLive) 
    {
        it->second->numWrites++;
        it->second->bytesWritten += memoryWriteSize;
        start = (char *) it->second->writeBuf + (memoryAddressWritten - it->second->base);
        memset(start, FILLER, memoryWriteSize);
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
    freeRtn = RTN_FindByName(img, FREE);

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
    isAllocating = true;
    unordered_map<ADDRINT,Data*> seen;
    unordered_map<ADDRINT,Data*>::iterator mIter, seenIter;
    for (mIter = m.begin(); mIter != m.end(); mIter++) 
    {
        seenIter = seen.find(mIter->second->base);
        if (seenIter == seen.end())
        {
            traceFile << *(mIter->second) << endl;
            seen.insert(make_pair<ADDRINT,Data*>(mIter->second->base, mIter->second));
        }
    }
    isAllocating = false;
}

INT32 Usage() 
{
    cerr << "This tool tracks the number of reads and writes to allocated heap objects." << endl;
    return EXIT_FAILURE;
}

int main(INT32 argc, CHAR *argv[]) 
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
