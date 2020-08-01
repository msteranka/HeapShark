#include "pin.H"
#include <unordered_map>
#include <fstream>
#include <iostream>

#if defined(TARGET_MAC)
#define MALLOC "_malloc"
#define FREE "_free"
#else
#define MALLOC "malloc"
#define FREE "free"
#endif

std::map<ADDRINT, std::string> disAssemblyMap;

std::unordered_map<ADDRINT, int> m;
static bool isEmplace;
std::ofstream traceFile;
KNOB<std::string> knobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "my-profiler.out", "specify profiling file name");

VOID MallocBefore(CHAR *name, ADDRINT size) 
{
    return;
}

VOID MallocAfter(ADDRINT ret) 
{
    if (!isEmplace) 
    {
        isEmplace = true;
        m[ret] = 0;
        isEmplace = false;
    }
}

VOID FreeHook(CHAR *name, ADDRINT ptr) 
{
    return;
}

VOID ReadsMem(ADDRINT applicationIp, ADDRINT memoryAddressRead, UINT32 memoryReadSize) 
{
    if (m.find(memoryAddressRead) != m.end()) 
    {
        m[memoryAddressRead]++;
    }
    // printf("0x%lx %s reads %d bytes of memory at 0x%lx\n", applicationIp, disAssemblyMap[applicationIp].c_str(), memoryReadSize, memoryAddressRead);
}

VOID Instruction(INS ins, VOID *v) 
{
    if (INS_IsMemoryRead(ins)) 
    {
        disAssemblyMap[INS_Address(ins)] = INS_Disassemble(ins);
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) ReadsMem,
                        IARG_INST_PTR,
                        IARG_MEMORYREAD_EA,
                        IARG_MEMORYREAD_SIZE,
                        IARG_END);
    }
}

VOID Image(IMG img, VOID *v) 
{
    RTN mallocRtn = RTN_FindByName(img, MALLOC), freeRtn = RTN_FindByName(img, FREE);
    if (RTN_Valid(mallocRtn)) 
    {
        RTN_Open(mallocRtn);
        RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR) MallocBefore, IARG_ADDRINT, MALLOC, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR) MallocAfter, IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
        RTN_Close(mallocRtn);
    }
    if (RTN_Valid(freeRtn)) 
    {
        RTN_Open(freeRtn);
        RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR) FreeHook, IARG_ADDRINT, FREE, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
        RTN_Close(freeRtn);
    }
}

VOID Fini(INT32 code, VOID *v) 
{
    for (auto it = m.begin(); it != m.end(); it++) 
    {
        traceFile << std::hex << it->first << ": " << std::dec << it->second << std::endl;
    }
}

INT32 Usage() 
{
    // std::cerr << "This tool produces a trace of calls to malloc." << std::endl;
    return -1;
}

int main(int argc, char *argv[]) 
{
    PIN_InitSymbols();
    if (PIN_Init(argc, argv)) 
    {
        return Usage();
    }
    isEmplace = false;
    traceFile.open(knobOutputFile.Value().c_str());
    traceFile.setf(std::ios::showbase);
    IMG_AddInstrumentFunction(Image, 0);
    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);
    PIN_StartProgram();
}
