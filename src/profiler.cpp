#include "pin.H"
#include <unordered_map>
#include <vector>
#include <fstream>
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "objectdata.h"
#include "backtrace.h"
#include "misc.h"

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

using namespace std;

unordered_map<ADDRINT,ObjectData*> liveObjects;
unordered_map<size_t, vector<ObjectData*>> totalObjects;
ofstream traceFile;
KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "profiler.out", "specify profiling file name");
ADDRINT cachedSize;
Backtrace cachedTrace;

VOID GetTrace(Backtrace *trace, CONTEXT *ctxt)
{
    // buf contains MAX_DEPTH + 1 addresses because PIN_Backtrace also returns
    // the stack frame for malloc
    //
    VOID *buf[MAX_DEPTH + 1];
    INT32 depth;

    // Pin requires us to call Pin_LockClient() before calling PIN_Backtrace and
    // PIN_GetSourceLocation
    //
    PIN_LockClient();
    depth = PIN_Backtrace(ctxt, buf, MAX_DEPTH + 1);
    for (INT32 i = 1; i < depth; i++) // We set i = 1 because we don't want to include the stack frame for malloc
    {

        // NOTE: executable must be compiled with -g -gdwarf-2 -rdynamic to locate the invocation of malloc
        // Additionally, PIN_GetSourceLocation does not necessarily get the exact invocation point, but it's pretty close
        //
        PIN_GetSourceLocation((ADDRINT) buf[i], nullptr, trace->lines + i - 1, trace->files + i - 1);
    }
    PIN_UnlockClient();
}

// Function arguments and backtrace can only be accessed at the function entry point
// Thus, we must insert a routine before malloc and cache these values
//
VOID MallocBefore(CONTEXT *ctxt, ADDRINT size)
{
    GetTrace(&cachedTrace, ctxt); // NOT THREAD SAFE!
    cachedSize = size; // NOT THREAD SAFE!
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
    d = new ObjectData(retVal, cachedSize); // NOT THREAD SAFE!
    for (UINT32 i = 0; i < cachedSize; i++) // NOT THREAD SAFE!
    {
        nextAddr = (retVal + i);
        // Create a mapping from every address in this object's range to the same ObjectData
        //
        liveObjects.insert(make_pair<ADDRINT,ObjectData*>(nextAddr, d)); // NOT THREAD SAFE!
    }

    for (UINT32 i = 0; i < MAX_DEPTH; i++)
    {
        d->mallocTrace.files[i] = cachedTrace.files[i]; // NOT THREAD SAFE!
        d->mallocTrace.lines[i] = cachedTrace.lines[i]; // NOT THREAD SAFE!
    }

    PDEBUG("malloc(%u) = %p\n", (UINT32) cachedSize, (VOID *) retVal); // NOT THREAD SAFE!
}

VOID FreeHook(CONTEXT *ctxt, ADDRINT ptr) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    ADDRINT startAddr, endAddr;

    it = liveObjects.find(ptr); // NOT THREAD SAFE!
    // If this is an invalid/double free, then skip this routine (provided that the allocator doesn't already kill the process)
    //
    if (it == liveObjects.end()) // NOT THREAD SAFE!
    {
        return;
    }

    d = it->second;
    d->Freeze(); // Clean up the corresponding ObjectData, but keep its contents
    GetTrace(&d->freeTrace, ctxt);
    totalObjects[d->size].push_back(d); // Insert the ObjectData into totalObjects - NOT THREAD SAFE!

    startAddr = d->addr;
    endAddr = startAddr + d->size;
    while (startAddr != endAddr)
    {
        liveObjects.erase(startAddr++); // Remove all mappings corresponding to this object - NOT THREAD SAFE!
    }

    PDEBUG("free(%p)\n", (VOID *) ptr);
}

VOID ReadsMem(ADDRINT addrRead, UINT32 readSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    VOID *fillAddr;

    it = liveObjects.find(addrRead); // NOT THREAD SAFE
    if (it == liveObjects.end()) // If this is not an object returned by malloc, then skip this routine - NOT THREAD SAFE
    {
        return;
    }

    d = it->second; // NOTE: can a thread be reading/writing to an object that is currently being freed?
    d->numReads++; // NOT THREAD SAFE
    d->bytesRead += readSize; // NOT THREAD SAFE
    fillAddr = d->readBuf + (addrRead - d->addr);
    memset(fillAddr, FILLER, readSize); // Write to readBuf at the same offset the object is being read from - NOT THREAD SAFE

    PDEBUG("Read %d bytes @ %p\n", readSize, (VOID *) addrRead);
}

VOID WritesMem(ADDRINT addrWritten, UINT32 writeSize) 
{
    unordered_map<ADDRINT,ObjectData*>::iterator it;
    ObjectData *d;
    VOID *fillAddr;

    it = liveObjects.find(addrWritten); // NOT THREAD SAFE
    if (it == liveObjects.end()) // If this is not an object returned by malloc, then skip this routine - NOT THREAD SAFE
    {
        return;
    }

    d = it->second; // NOTE: can a thread be reading/writing to an object that is currently being freed?
    d->numWrites++; // NOT THREAD SAFE
    d->bytesWritten += writeSize; // NOT THREAD SAFE
    fillAddr = d->writeBuf + (addrWritten - d->addr);
    memset(fillAddr, FILLER, writeSize); // Write to writeBuf at the same offset the object is being written to - NOT THREAD SAFE

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
                        IARG_CONST_CONTEXT,
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
                        IARG_CONST_CONTEXT, 
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
