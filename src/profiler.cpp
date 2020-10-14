#include "pin.H"
#include <fstream>
#include <iostream>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include "objectdata.hpp"
#include "backtrace.hpp"
#include "objectmanager.hpp"

#ifdef TARGET_MAC
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif // TARGET_MAC

#ifdef PROF_DEBUG
#define PDEBUG(fmt, args...) printf(fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif // PROF_DEBUG

#ifdef PROF_UPDATE
static UINT32 updateNumAllocs, updateNextThreshold;
static PIN_LOCK updateOutputLock;
#endif // PROF_UPDATE

using namespace std;

static ofstream traceFile;
static KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "data.json", "specify profiling file name");
static ObjectManager manager;
static unordered_map<THREADID, pair<ADDRINT, Backtrace>> cache;
static PIN_LOCK cacheLock; // CACHE_LOCK

VOID ThreadStart(THREADID threadId, CONTEXT *ctxt, INT32 flags, VOID* v)
{
    // This will be called each time a thread is created.
    // We don't need it at the moment but I'm leaving it in for future reference.
}

VOID ThreadFini(THREADID threadId, const CONTEXT *ctxt, INT32 code, VOID* v)
{
    // This will be called each time a thread is destroyed.
    // We don't need it at the moment but I'm leaving it in for future reference.
}

// Function arguments and backtrace can only be accessed at the function entry point
// Thus, we must insert a routine before malloc and cache these values
//
VOID MallocBefore(THREADID threadId, CONTEXT *ctxt, ADDRINT size)
{
    #ifdef PROF_UPDATE
    PIN_GetLock(&updateOutputLock, threadId);
    updateNumAllocs++;
    if (updateNumAllocs >= updateNextThreshold)
    {
        cout << "Number of Allocations: " << updateNumAllocs << endl;
        updateNextThreshold <<= 1;
    }
    PIN_ReleaseLock(&updateOutputLock);
    #endif

    Backtrace b;
    b.SetTrace(ctxt);
    PIN_GetLock(&cacheLock, threadId); // CACHE_LOCK
    cache[threadId] = make_pair(size, b);
    PIN_ReleaseLock(&cacheLock);
}

VOID MallocAfter(THREADID threadId, ADDRINT retVal)
{
    UINT32 size; // CACHE_LOCK
    Backtrace b;
    PIN_GetLock(&cacheLock, threadId);
    size = (UINT32) cache[threadId].first;
    b = cache[threadId].second;
    PIN_ReleaseLock(&cacheLock);

    // Check for success since we don't want to track null pointers
    //
    if ((VOID *) retVal == nullptr) { return; }
    manager.AddObject(retVal, size, b, threadId);
}

VOID FreeHook(THREADID threadId, CONTEXT* ctxt, ADDRINT ptr)
{
    // Value of sizeThreshold is somewhat arbitrary, just using 2^20 for now
    //
    static const UINT32 sizeThreshold = 1048576;

    manager.RemoveObject(ptr, ctxt, threadId);

    // Write out all data to output file every sizeThreshold in the event that the 
    // application makes a lot of allocations
    //
    if (manager.GetNumDeadObjects() >= sizeThreshold)
    {
        manager.ClearDeadObjects(traceFile);
    }
}

VOID ReadsMem(THREADID threadId, ADDRINT addrRead, UINT32 readSize)
{
    manager.ReadObject(addrRead, readSize, threadId);
}

VOID WritesMem(THREADID threadId, ADDRINT addrWritten, UINT32 writeSize)
{
    manager.WriteObject(addrWritten, writeSize, threadId);
}

VOID Instruction(INS ins, VOID *v) 
{
    if (INS_IsMemoryRead(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem, // Intercept all read instructions with ReadsMem
                        IARG_THREAD_ID,
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_END);
    }

    if (INS_IsMemoryWrite(ins)) 
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) WritesMem, // Intercept all write instructions with WritesMem
                        IARG_THREAD_ID,
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
                        IARG_THREAD_ID,
                        IARG_CONST_CONTEXT,
                        IARG_FUNCARG_ENTRYPOINT_VALUE, 
                        0, IARG_END);
        RTN_InsertCall(rtn, IPOINT_AFTER, (AFUNPTR) MallocAfter, // Hook calls to malloc with MallocAfter
                        IARG_THREAD_ID,
                        IARG_FUNCRET_EXITPOINT_VALUE, 
                        IARG_END);
        RTN_Close(rtn);
    }

    rtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(rtn)) 
    {
        RTN_Open(rtn);
        RTN_InsertCall(rtn, IPOINT_BEFORE, (AFUNPTR) FreeHook, // Hook calls to free with FreeHook
                        IARG_THREAD_ID,
                        IARG_CONST_CONTEXT, 
                        IARG_FUNCARG_ENTRYPOINT_VALUE, 
                        0, IARG_END);
        RTN_Close(rtn);
    }
}

VOID Fini(INT32 code, VOID *v)
{
    // << operator on ObjectManager only prints out freed objects, so
    // we need to free all objects that are still live
    //
    manager.KillLiveObjects();

    // NOTE: This will break the JSON format if FreeHook has printed out
    // entries to traceFile and manager is empty
    // Can manager ever be empty? i.e. Is it possible for an application to 
    // ever terminate without any memory leaks?
    //
    traceFile << manager;
    traceFile << endl << "\t]" << endl << "}"; // Terminate JSON
}

INT32 Usage() 
{
    cerr << "This tool tracks the usage of objects returned by malloc." << endl;
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) 
{
    PIN_InitLock(&cacheLock);
    #ifdef PROF_UPDATE
    updateNumAllocs = 0;
    updateNextThreshold = 1;
    PIN_InitLock(&updateOutputLock);
    #endif

    if (PIN_Init(argc, argv)) 
    {
        return Usage();
    }
    PIN_InitSymbols();

    traceFile.open(knobOutputFile.Value().c_str());
    traceFile.setf(ios::showbase);
    traceFile << "{" << endl << "\t\"objects\" : [" << endl; // Begin JSON

    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddThreadStartFunction(ThreadStart, 0);
    PIN_AddThreadFiniFunction(ThreadFini, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
}
