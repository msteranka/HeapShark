#ifndef __OBJECT_DATA_HPP
#define __OBJECT_DATA_HPP

#include "pin.H"
#include <iostream>
#include <new>
#include <vector>
#include <stdatomic.h>
#include "backtrace.hpp"

using namespace std;

class ObjectData
{
    public:
        ObjectData(ADDRINT addr, UINT32 size, THREADID mallocThread) : 
            addr(addr),
            size(size),
            mallocThread(mallocThread),
            freeThread(-1),
            readBitmap(size, 0),
            writeBitmap(size, 0)
        { 
            atomic_init(&numReads, 0);
            atomic_init(&numWrites, 0);
            atomic_init(&bytesRead, 0);
            atomic_init(&bytesWritten, 0);
            PIN_InitLock(&readBitmapLock);
            PIN_InitLock(&writeBitmapLock);
        }

        pair<double,double> CalculateCoverage() // NOT THREAD-SAFE
        {
            double readCoverage, writeCoverage;
            UINT32 bitsRead, bitsWritten;

            bitsRead = bitsWritten = 0;

            // Calculate read and write coverage
            // NOTE: coverage can be misleading on structs/classes that require extra space for alignment
            //
            for (UINT32 i = 0; i < size; i++)
            {
                if (readBitmap[i])
                {
                    bitsRead++;
                }
                if (writeBitmap[i])
                {
                    bitsWritten++;
                }
            }
            readCoverage = (double) bitsRead / size;
            writeCoverage = (double) bitsWritten / size;
            return make_pair<double,double>(readCoverage, writeCoverage);
        }

        ADDRINT GetAddr() { return addr; }

        UINT32 GetSize() { return size; }

        THREADID GetMallocThread() { return mallocThread; } // NOT THREAD-SAFE

        THREADID GetFreeThread() { return freeThread; } // NOT THREAD-SAFE

        VOID SetFreeThread(THREADID freeThread) { this->freeThread = freeThread; } // NOT THREAD-SAFE

        VOID SetMallocTrace(Backtrace &b) { mallocTrace = b; } // NOT THREAD-SAFE

        VOID SetFreeTrace(CONTEXT *ctxt) { freeTrace.SetTrace(ctxt); } // NOT THREAD-SAFE

        UINT32 GetNumReads() { return atomic_load(&numReads); }

        VOID IncrementNumReads() { atomic_fetch_add(&numReads, 1); }

        UINT32 GetBytesRead() { return atomic_load(&bytesRead); }

        VOID AddBytesRead(UINT32 bytesRead) { atomic_fetch_add(&(this->bytesRead), bytesRead); }

        UINT32 GetNumWrites() { return atomic_load(&numWrites); }

        VOID IncrementNumWrites() { atomic_fetch_add(&numWrites, 1); }

        UINT32 GetBytesWritten() { return atomic_load(&bytesWritten); }

        VOID AddBytesWritten(UINT32 bytesWritten) { atomic_fetch_add(&(this->bytesWritten), bytesWritten); }

        VOID UpdateReadCoverage(ADDRINT addrRead, UINT32 readSize)
        {
            ADDRINT offset;

            // Write to readBitmap at the same offset the object is being read
            // TODO: We actually may not have to use a lock to modify readBitmap
            // Maybe obtain an iterator to the desired element and modify it
            // atomically?
            //
            offset = addrRead - addr;
            for (ADDRINT i = offset; i < offset + readSize; i++)
            {
                PIN_GetLock(&readBitmapLock, -1);
                if (i >= readBitmap.size()) { // Break if read exceeds size of bitmap
                    PIN_ReleaseLock(&readBitmapLock);
                    break; 
                }
                readBitmap[i] = 1;
                PIN_ReleaseLock(&readBitmapLock);
            }
        }

        VOID UpdateWriteCoverage(ADDRINT addrWritten, UINT32 writeSize)
        {
            ADDRINT offset;
            offset = addrWritten - addr;
            for (ADDRINT i = offset; i < offset + writeSize; i++)
            {
                PIN_GetLock(&writeBitmapLock, -1);
                if (i >= writeBitmap.size()) {
                    PIN_ReleaseLock(&writeBitmapLock);
                    break; 
                }
                writeBitmap[i] = 1;
                PIN_ReleaseLock(&writeBitmapLock);
            }
        }

        pair<Backtrace,Backtrace> GetTrace() // NOT THREAD-SAFE
        {
            return make_pair<Backtrace,Backtrace>(mallocTrace, freeTrace);
        }

    private:
        const ADDRINT addr;
        const UINT32 size;
        atomic_int numReads, numWrites, bytesRead, bytesWritten;
        THREADID mallocThread, freeThread;
        Backtrace mallocTrace, freeTrace;
        vector<BOOL> readBitmap, writeBitmap;
        PIN_LOCK readBitmapLock, writeBitmapLock;
};

ostream& operator<<(ostream& os, ObjectData& data) // NOT THREAD-SAFE
{
    pair<double,double> coverage;
    pair<Backtrace,Backtrace> trace;

    coverage = data.CalculateCoverage();
    trace = data.GetTrace();

    os << "\t\t{" << endl <<
            "\t\t\t\"address\" : " << data.GetAddr() << "," << endl << 
            "\t\t\t\"size\" : " << data.GetSize() << "," << endl <<
            "\t\t\t\"numReads\" : " << data.GetNumReads() << "," << endl <<
            "\t\t\t\"numWrites\" : " << data.GetNumWrites() << "," << endl <<
            "\t\t\t\"bytesRead\" : " << data.GetBytesRead() << "," << endl <<
            "\t\t\t\"bytesWritten\" : " << data.GetBytesWritten() << "," << endl <<
            "\t\t\t\"readCoverage\" : " << coverage.first << "," << endl <<
            "\t\t\t\"writeCoverage\" : " << coverage.second << "," << endl <<
            "\t\t\t\"allocatingThread\" : " << (INT32) data.GetMallocThread() << "," << endl <<
            "\t\t\t\"freeingThread\" : " << (INT32) data.GetFreeThread() << "," << endl <<
            "\t\t\t\"mallocBacktrace\" : " << trace.first << "," << endl <<
            "\t\t\t\"freeBacktrace\" : " << trace.second << endl <<
            "\t\t}";

    return os;
}

#endif
