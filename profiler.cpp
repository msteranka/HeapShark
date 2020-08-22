#include "pin.H"
#include <unordered_map>
#include <vector>
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

class ObjectData
{
    public:
        ObjectData(ADDRINT addr, size_t size)
        {
            this->addr = addr;
            this->size = size;
            numReads = numWrites = bytesRead = bytesWritten = 0;
            readBuf = (char *) calloc(size, 1);
            writeBuf = (char *) calloc(size, 1);
        }

        void Freeze()
        {
            int read = 0, written = 0;
            for (int i = 0; i < (int) size; i++) // Calculate read and write coverage
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
            readCoverage = (double) read / size;
            writeCoverage = (double) written / size;
            free(readBuf);
            free(writeBuf);
        }

        double GetReadFactor()
        {
            return (double) numReads / bytesRead;
        }

        double GetWriteFactor()
        {
            return (double) numWrites / bytesWritten;
        }

        ADDRINT addr;
        size_t size;
        int numReads, numWrites, bytesRead, bytesWritten;
        double readCoverage, writeCoverage;
        char *readBuf, *writeBuf;
};

ostream& operator<<(ostream& os, ObjectData &data) 
{
    return os << "malloc(" << hex << data.addr << "):" << dec << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << data.GetReadFactor() << endl <<
                 "\tWrite Factor = " << data.GetWriteFactor() << endl <<
                 "\tRead Coverage = " << data.readCoverage << endl << 
                 "\tWrite Coverage = " << data.writeCoverage << endl;
}

static ADDRINT nextSize;
unordered_map<ADDRINT,ObjectData*> m;
unordered_map<ADDRINT, vector<ObjectData*>> totalObjects;
static bool isAllocating; // If isAllocating is true, then an allocation internal to the profiler is taking place
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

VOID MallocBefore(ADDRINT size) 
{
    nextSize = size;
}

VOID MallocAfter(ADDRINT ret) 
{
    ObjectData *d;
    ADDRINT nextAddr;

    if (isAllocating || (void *) ret == nullptr) // If this allocation is internal to the profiler or the allocation failed, then skip this routine
    {
        return;
    }
    isAllocating = true;
    d = new ObjectData(ret, nextSize);
    for (ADDRINT i = 0; i < nextSize; i++)
    {
        nextAddr = (ADDRINT) ((char *) ret + i);
        m.insert(make_pair<ADDRINT,ObjectData*>(nextAddr, d)); // Create a mapping from every address in this object's range to the same Data structure
    }
    isAllocating = false;
    PDEBUG("malloc(%lu) = 0x%lx\n", nextSize, ret);
}

VOID FreeHook(ADDRINT ptr) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    ADDRINT start, end;
    isAllocating = true;
    it = m.find(ptr);
    if (it == m.end()) // If this is an invalid/double/internal free, then skip this routine
    {
        isAllocating = false;
        return;
    }
    d = it->second;
    d->Freeze();
    totalObjects[d->addr].push_back(d);
    start = d->addr;
    end = start + d->size;
    while (start != end) { // Must erase all pointers to the same ObjectData
        m.erase(start++);
    }
    isAllocating = false;
    PDEBUG("free(0x%lx)\n", ptr);
}

VOID ReadsMem(ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    char *start;
    isAllocating = true;
    it = m.find(memoryAddressRead);
    isAllocating = false;
    if (it != m.end()) 
    {
        it->second->numReads++;
        it->second->bytesRead += memoryReadSize;
        start = (char *) it->second->readBuf + (memoryAddressRead - it->second->addr);
        memset(start, FILLER, memoryReadSize); // Write into readBuf the same offset the object is being read from
        PDEBUG("Read %d bytes @ 0x%lx\n", memoryReadSize, memoryAddressRead);
    }
}

VOID WritesMem(ADDRINT memoryAddressWritten, UINT32 memoryWriteSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    char *start;
    isAllocating = true;
    it = m.find(memoryAddressWritten);
    isAllocating = false;
    if (it != m.end()) 
    {
        it->second->numWrites++;
        it->second->bytesWritten += memoryWriteSize;
        start = (char *) it->second->writeBuf + (memoryAddressWritten - it->second->addr);
        memset(start, FILLER, memoryWriteSize); // Write into writeBuf the same offset the object is being written to
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
    unordered_map<ADDRINT,ObjectData*> seen;
    unordered_map<ADDRINT,ObjectData*>::iterator mIter, seenIter;
    unordered_map<ADDRINT,vector<ObjectData*>>::iterator totalObjectsIter;
    vector<ObjectData*> *curAddrs;
    vector<ObjectData*>::iterator curAddrsIter;

    for (mIter = m.begin(); mIter != m.end(); mIter++) // Print out objects that were never freed
    {
        seenIter = seen.find(mIter->second->addr);
        if (seenIter == seen.end())
        {
            traceFile << *(mIter->second) << endl;
            seen.insert(make_pair<ADDRINT,ObjectData*>(mIter->second->addr, mIter->second));
        }
    }
    for (totalObjectsIter = totalObjects.begin(); totalObjectsIter != totalObjects.end(); totalObjectsIter++) // Print out objects that were freed
    {
        curAddrs = &(totalObjectsIter->second);
        for (curAddrsIter = curAddrs->begin(); curAddrsIter != curAddrs->end(); curAddrsIter++)
        {
            traceFile << **curAddrsIter << endl;
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
