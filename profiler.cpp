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

using namespace std;

class ObjectData
{
    public:
        ObjectData(ADDRINT addr, UINT32 size)
        {
            this->addr = addr;
            this->size = size;
            numReads = numWrites = bytesRead = bytesWritten = 0;
            readBuf = (CHAR *) calloc(size, 1);
            writeBuf = (CHAR *) calloc(size, 1);
        }

        VOID Freeze()
        {
            UINT32 read = 0, written = 0;

            // Calculate read and write coverage
            // NOTE: coverage can be misleading on structs/classes that require extra space for alignment
            //
            for (UINT32 i = 0; i < size; i++)

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
        UINT32 size, numReads, numWrites, bytesRead, bytesWritten;
        double readCoverage, writeCoverage;
        CHAR *readBuf, *writeBuf;
        INT32 mallocLine, freeLine;
        string mallocFile, freeFile;
};

ostream& operator<<(ostream& os, ObjectData &data) 
{
    os << hex << data.addr << dec << ":" << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << data.GetReadFactor() << endl <<
                 "\tWrite Factor = " << data.GetWriteFactor() << endl <<
                 "\tRead Coverage = " << data.readCoverage << endl << 
                 "\tWrite Coverage = " << data.writeCoverage << endl;

    if (data.mallocFile == "")
    {
        os << "\tCould not locate malloc invocation point" << endl;
    }
    else
    {
        os << "\tAllocated in " << data.mallocFile << ":" << data.mallocLine << endl;
    }

    if (data.freeFile == "")
    {
        os << "\tCould not locate free invocation point" << endl;
    }
    else
    {
        os << "\tDeallocated in " << data.freeFile << ":" << data.freeLine << endl;
    }

    return os;
}

unordered_map<ADDRINT,ObjectData*> liveObjects;
unordered_map<size_t, vector<ObjectData*>> totalObjects;
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "profiler.out", "specify profiling file name");
ADDRINT nextSize, nextRetIp;

// Function arguments and return IP can only be accessed at the function entry point
// Thus, we must insert a routine before malloc and cache these values
//
VOID MallocBefore(ADDRINT retIp, ADDRINT size)
{
    nextRetIp = retIp;
    nextSize = size;
}

VOID MallocAfter(ADDRINT retVal)
{
    ObjectData *d;
    ADDRINT nextAddr;

    if ((VOID *) retVal == nullptr) // Check for success since we don't want to track null pointers
    {
        return;
    }

    // We don't need to worry about recursive malloc calls since Pin doesn't instrument the PinTool itself
    //
    d = new ObjectData(retVal, nextSize);
    for (UINT32 i = 0; i < nextSize; i++)
    {
        nextAddr = (retVal + i);
        // Create a mapping from every address in this object's range to the same ObjectData
        //
        liveObjects.insert(make_pair<ADDRINT,ObjectData*>(nextAddr, d));
    }

    PIN_LockClient(); // Pin requires that PIN_LockClient be called before calling PIN_GetSourceLocation
    // Given an address, fetch the line and file name
    // NOTE: executable must be compiled with -g -gdwarf-2 -rdynamic to locate the invocation of malloc
    // Additionally, PIN_GetSourceLocation does not necessarily get the exact invocation point, but it's pretty close
    //
    PIN_GetSourceLocation(nextRetIp, nullptr, &d->mallocLine, &d->mallocFile);
    PIN_UnlockClient();

    PDEBUG("malloc(%u) = %p\n", (UINT32) nextSize, (VOID *) retVal);
}

VOID FreeHook(ADDRINT retIp, ADDRINT ptr) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    ADDRINT startAddr, endAddr;

    it = liveObjects.find(ptr);
    // If this is an invalid/double free, then skip this routine (provided that the allocator doesn't already kill the process)
    //
    if (it == liveObjects.end())
    {
        return;
    }

    d = it->second;
    d->Freeze(); // Clean up the corresponding ObjectData, but keep its contents
    PIN_LockClient();
    PIN_GetSourceLocation(retIp, nullptr, &d->freeLine, &d->freeFile);
    PIN_UnlockClient();
    totalObjects[d->size].push_back(d); // Insert the ObjectData into totalObjects

    startAddr = d->addr;
    endAddr = startAddr + d->size;
    while (startAddr != endAddr)
    {
        liveObjects.erase(startAddr++); // Remove all mappings corresponding to this object
    }

    PDEBUG("free(%p)\n", (VOID *) ptr);
}

VOID ReadsMem(ADDRINT addrRead, UINT32 readSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    VOID *fillAddr;

    it = liveObjects.find(addrRead);
    if (it == liveObjects.end()) // If this is not an object returned by malloc, then skip this routine
    {
        return;
    }

    d = it->second;
    d->numReads++;
    d->bytesRead += readSize;
    fillAddr = d->readBuf + (addrRead - d->addr);
    memset(fillAddr, FILLER, readSize); // Write to readBuf at the same offset the object is being read from

    PDEBUG("Read %d bytes @ %p\n", readSize, (VOID *) addrRead);
}

VOID WritesMem(ADDRINT addrWritten, UINT32 writeSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    VOID *fillAddr;

    it = liveObjects.find(addrWritten);
    if (it == liveObjects.end()) // If this is not an object returned by malloc, then skip this routine
    {
        return;
    }

    d = it->second;
    d->numWrites++;
    d->bytesWritten += writeSize;
    fillAddr = d->writeBuf + (addrWritten - d->addr);
    memset(fillAddr, FILLER, writeSize); // Write to writeBuf at the same offset the object is being written to

    PDEBUG("Wrote %d bytes @ %p\n", writeSize, (VOID *) addrWritten);
}

VOID Instruction(INS ins, VOID *v) 
{
    if (INS_IsMemoryRead(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem, // Intercept all read instructions with ReadsMem
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_END);
    }

    if (INS_IsMemoryWrite(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) WritesMem, // Intercept all write instructions with WritesMem
                        IARG_MEMORYWRITE_EA,
                        IARG_MEMORYWRITE_SIZE,
                        IARG_END);
    }
}

VOID Image(IMG img, VOID *v) 
{
    RTN rtn;

    rtn = RTN_FindByName(img, MALLOC);
    if (RTN_Valid(rtn)) 
    {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) MallocBefore, // Hook calls to malloc with MallocBefore
                        IARG_RETURN_IP, 
                        IARG_FUNCARG_ENTRYPOINT_VALUE, 
                        0, IARG_END);
        RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) MallocAfter, // Hook calls to malloc with MallocAfter
                        IARG_FUNCRET_EXITPOINT_VALUE, 
                        IARG_END);
        RTN_Close(rtn);
    }

    rtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(rtn)) 
    {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) FreeHook, // Hook calls to free with FreeHook
                        IARG_RETURN_IP, 
                        IARG_FUNCARG_ENTRYPOINT_VALUE, 
                        0, IARG_END);
        RTN_Close(rtn);
    }
}

VOID Fini(INT32 code, VOID *v)
{
    unordered_map<ADDRINT,ObjectData*> seen;
    unordered_map<ADDRINT,ObjectData*>::iterator liveObjectsIter, seenIter;
    unordered_map<size_t,vector<ObjectData*>>::iterator totalObjectsIter;
    vector<ObjectData*> *curAddrs;
    vector<ObjectData*>::iterator curAddrsIter;
    ObjectData *d;

    // Move objects that were never freed to totalObjects
    //
    for (liveObjectsIter = liveObjects.begin(); liveObjectsIter != liveObjects.end(); liveObjectsIter++)
    {
        d = liveObjectsIter->second;
        seenIter = seen.find(d->addr);
        if (seenIter == seen.end()) // If this object hasn't been seen yet, then add it to totalObjects
        {
            seen.insert(make_pair<ADDRINT,ObjectData*>(d->addr, d));
            d->Freeze(); // Freeze data first so that coverage is calculated
            totalObjects[d->size].push_back(d);
        }
    }

    // Print out all data
    //
    for (totalObjectsIter = totalObjects.begin(); totalObjectsIter != totalObjects.end(); totalObjectsIter++)
    {
        curAddrs = &(totalObjectsIter->second);
        traceFile << "OBJECT SIZE: " << totalObjectsIter->first << endl << 
                        "========================================" << endl;
        for (curAddrsIter = curAddrs->begin(); curAddrsIter != curAddrs->end(); curAddrsIter++)
        {
            // We must output data to a file instead of stdout or stderr because stdout and stderr have been closed at this point
            //
            traceFile << **curAddrsIter << endl;
        }
    }
}

INT32 Usage() 
{
    cerr << "This tool tracks the usage of objects returned by malloc." << endl;
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) 
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) 
    {
        return Usage();
    }
    traceFile.open(knobOutputFile.Value().c_str());
    traceFile.setf(ios::showbase);
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
}
