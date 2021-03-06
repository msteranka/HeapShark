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

#ifdef HEAP_SHARK_DEBUG
#define PDEBUG(fmt, args...) printf(fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif // HEAP_SHARK_DEBUG

#ifdef HEAP_SHARK_UPDATE
static UINT32 updateNumAllocs, updateNextThreshold;
static PIN_LOCK updateOutputLock;
#endif // HEAP_SHARK_UPDATE

using namespace std;

static ofstream traceFile;
static KNOB<string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "heapshark.json", "specify profiling file name");
static ObjectManager manager;
static INT32 numThreads = 0;
static TLS_KEY tls_key = INVALID_TLS_KEY; // Thread Local Storage

VOID ThreadStart(THREADID threadId, CONTEXT *ctxt, INT32 flags, VOID* v)
{
    numThreads++;
    pair<ADDRINT, Backtrace> *threadCache = new pair<ADDRINT, Backtrace>();
    if (PIN_SetThreadData(tls_key, threadCache, threadId) == FALSE)
    {
        cerr << "PIN_SetThreadData failed." << endl;
        PIN_ExitProcess(1);
    }
}

VOID ThreadFini(THREADID threadId, const CONTEXT *ctxt, INT32 code, VOID* v)
{
    pair<ADDRINT, Backtrace> *threadCache = static_cast<pair<ADDRINT, Backtrace>*>(PIN_GetThreadData(tls_key, threadId));
    delete threadCache;
}

// Function arguments and backtrace can only be accessed at the function entry point
// Thus, we must insert a routine before malloc and cache these values
//
VOID MallocBefore(THREADID threadId, CONTEXT *ctxt, ADDRINT size)
{
    #ifdef HEAP_SHARK_UPDATE
    PIN_GetLock(&updateOutputLock, threadId);
    updateNumAllocs++;
    if (updateNumAllocs >= updateNextThreshold)
    {
        cout << "Number of Allocations: " << updateNumAllocs << endl;
        updateNextThreshold <<= 1;
    }
    PIN_ReleaseLock(&updateOutputLock);
    #endif

    pair<ADDRINT, Backtrace> *threadCache = static_cast<pair<ADDRINT, Backtrace>*>(PIN_GetThreadData(tls_key, threadId));
    threadCache->second.SetTrace(ctxt);
    threadCache->first = size;
}

VOID MallocAfter(THREADID threadId, ADDRINT retVal)
{
    // Check for success since we don't want to track null pointers
    //
    if ((VOID *) retVal == nullptr) { return; }

    pair<ADDRINT, Backtrace> *threadCache = static_cast<pair<ADDRINT, Backtrace>*>(PIN_GetThreadData(tls_key, threadId));
    UINT32 size = threadCache->first;
    Backtrace b = threadCache->second;
    manager.AddObject(retVal, size, b, threadId);
}

VOID FreeHook(THREADID threadId, CONTEXT *ctxt, ADDRINT ptr)
{
    // Value of sizeThreshold is somewhat arbitrary, just using 2^20 for now
    //
    static const UINT32 sizeThreshold = 1048576;

    manager.RemoveObject(ptr, ctxt, threadId);

    // Write out all data to output file every sizeThreshold in the event that the 
    // application makes a lot of allocations
    //
    manager.ClearDeadObjects(traceFile, sizeThreshold);
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
    if (INS_IsMemoryRead(ins) && !INS_IsStackRead(ins)) 
    {
        // Intercept read instructions that don't read from the stack with ReadsMem
        //
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem,
                        IARG_THREAD_ID,
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_END);
    }

    if (INS_IsMemoryWrite(ins) && !INS_IsStackWrite(ins)) 
    {
        // Intercept write instructions that don't write to the stack with WritesMem
        //
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) WritesMem,
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
    cerr << "HeapShark identifies allocations that can be replaced "
            "with stack allocation or custom allocation routines" << endl;
    return EXIT_FAILURE;
}

int main(int argc, char *argv[]) 
{
    PIN_InitSymbols();

    #ifdef HEAP_SHARK_UPDATE
    updateNumAllocs = 0;
    updateNextThreshold = 1;
    PIN_InitLock(&updateOutputLock);
    #endif

    if (PIN_Init(argc, argv)) 
    {
        return Usage();
    }

    tls_key = PIN_CreateThreadDataKey(NULL);
    if (tls_key == INVALID_TLS_KEY)
    {
        cerr << "number of already allocated keys reached the MAX_CLIENT_TLS_KEYS limit" << endl;
        PIN_ExitProcess(1);
    }

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
