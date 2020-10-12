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
#define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif // PROF_DEBUG

using namespace std;

static ofstream traceFile;
static KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "data.json", "specify profiling file name");
static ObjectManager manager;
static PIN_LOCK lock;
static unordered_map<THREADID, pair<ADDRINT, Backtrace>> cache;

VOID ThreadStart(THREADID threadid, CONTEXT* ctxt, INT32 flags, VOID* v)
{
    // This will be called each time a thread is created.
    // We don't need it at the moment but I'm leaving it in for future reference.
}

VOID ThreadFini(THREADID threadid, const CONTEXT* ctxt, INT32 code, VOID* v)
{
    // This will be called each time a thread is destroyed.
    // We don't need it at the moment but I'm leaving it in for future reference.
}

// Function arguments and backtrace can only be accessed at the function entry point
// Thus, we must insert a routine before malloc and cache these values
//
VOID MallocBefore(THREADID threadid, CONTEXT* ctxt, ADDRINT size)
{
    PIN_GetLock(&lock, threadid + 1);
    PDEBUG("Locking from thread %lu before a malloc...\n", threadid);
    Backtrace b;
    b.SetTrace(ctxt);
    cache[threadid] = make_pair(size, b);
    PDEBUG("Unlocked from thread %lu.\n", threadid);
    PIN_ReleaseLock(&lock);
}

VOID MallocAfter(THREADID threadid, ADDRINT retVal)
{
    if ((VOID *) retVal == nullptr) // Check for success since we don't want to track null pointers
    {
        return;
    }
    PIN_GetLock(&lock, threadid + 1);
    PDEBUG("Locking from thread %lu after a malloc...\n", threadid);
    manager.AddObject(retVal, (UINT32) cache[threadid].first, cache[threadid].second);
    PDEBUG("Unlocked from thread %lu.\n", threadid);
    PIN_ReleaseLock(&lock);
}

VOID FreeHook(THREADID threadid, CONTEXT* ctxt, ADDRINT ptr)
{
    // Value of sizeThreshold is somewhat arbitrary, just using 2^20 for now
    //
    static const UINT32 sizeThreshold = 1048576;

    PIN_GetLock(&lock, threadid + 1);
    PDEBUG("Locking from thread %lu on a free...\n", threadid);

    manager.RemoveObject(ptr, ctxt);

    // Write out all data to output file every sizeThreshold in the event that the 
    // application makes a lot of allocations
    //
    if (manager.GetTotalObjects()->size() >= sizeThreshold)
    {
        manager.ClearDeadObjects(traceFile, sizeThreshold);
    }

    PDEBUG("Unlocked from thread %lu.\n", threadid);
    PIN_ReleaseLock(&lock);
}

VOID ReadsMem(THREADID threadid, ADDRINT addrRead, UINT32 readSize)
{
    PIN_GetLock(&lock, threadid + 1);
    PDEBUG("Locking from thread %lu on a read...\n", threadid);
    manager.ReadObject(addrRead, readSize);
    PDEBUG("Unlocked from thread %lu.\n", threadid);
    PIN_ReleaseLock(&lock);
}

VOID WritesMem(THREADID threadid, ADDRINT addrWritten, UINT32 writeSize)
{
    PIN_GetLock(&lock, threadid + 1);
    PDEBUG("Locking from thread %lu on a write.\n", threadid);
    manager.WriteObject(addrWritten, writeSize);
    PDEBUG("Unlocked from thread %lu.\n", threadid);
    PIN_ReleaseLock(&lock);
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
    PIN_InitLock(&lock);
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
