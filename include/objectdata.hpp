#ifndef __OBJECT_DATA_HPP
#define __OBJECT_DATA_HPP

#include "pin.H"
#include <iostream>
#include <new>
#include "backtrace.hpp"

using namespace std;

class ObjectData
{
    public:
        ObjectData(ADDRINT addr, UINT32 size) : 
            addr(addr),
            size(size),
            numReads(0),
            numWrites(0),
            bytesRead(0),
            bytesWritten(0)
        {
            readBuf = new CHAR[size];
            memset(readBuf, initFiller, size);
            writeBuf = new CHAR[size];
            memset(writeBuf, initFiller, size);
        }

        pair<double,double> CalculateCoverage()
        {
            double readCoverage, writeCoverage;
            UINT32 read, written;

            read = written = 0;

            // Calculate read and write coverage
            // NOTE: coverage can be misleading on structs/classes that require extra space for alignment
            //
            for (UINT32 i = 0; i < size; i++)

            {
                if (readBuf[i] == finiFiller)
                {
                    read++;
                }
                if (writeBuf[i] == finiFiller)
                {
                    written++;
                }
            }
            readCoverage = (double) read / size;
            writeCoverage = (double) written / size;
            return make_pair<double,double>(readCoverage, writeCoverage);
        }

        double GetReadFactor() { return (double) numReads / bytesRead; }

        double GetWriteFactor() { return (double) numWrites / bytesWritten; }

        ADDRINT GetAddr() { return addr; }

        UINT32 GetSize() { return size; }

        VOID SetMallocTrace(Backtrace &b) { mallocTrace = b; }

        VOID SetFreeTrace(CONTEXT *ctxt) { freeTrace.SetTrace(ctxt); }

        UINT32 GetNumReads() { return numReads; }

        VOID SetNumReads(UINT32 numReads) { this->numReads = numReads; }

        UINT32 GetBytesRead() { return bytesRead; }

        VOID SetBytesRead(UINT32 bytesRead) { this->bytesRead = bytesRead; }

        UINT32 GetNumWrites() { return numWrites; }

        VOID SetNumWrites(UINT32 numWrites) { this->numWrites = numWrites; }

        UINT32 GetBytesWritten() { return bytesWritten; }

        VOID SetBytesWritten(UINT32 bytesWritten) { this->bytesWritten = bytesWritten; }

        VOID UpdateReadCoverage(ADDRINT addrRead, UINT32 readSize)
        {
            UpdateCoverage(addrRead, readSize, readBuf);
        }

        VOID UpdateWriteCoverage(ADDRINT addrWritten, UINT32 writeSize)
        {
            UpdateCoverage(addrWritten, writeSize, writeBuf);
        }

        pair<Backtrace,Backtrace> GetTrace()
        {
            return make_pair<Backtrace,Backtrace>(mallocTrace, freeTrace);
        }

    private:
        ADDRINT addr;
        UINT32 size, numReads, numWrites, bytesRead, bytesWritten;
        CHAR *readBuf, *writeBuf;
        Backtrace mallocTrace, freeTrace;
        static const CHAR initFiller = '\0', finiFiller = '\1';

        VOID UpdateCoverage(ADDRINT ptr, UINT32 size, CHAR *buf)
        {
            VOID *fillAddr;

            // Write to buf at the same offset the object is being read/written
            //
            fillAddr = buf + (ptr - addr);
            memset(fillAddr, finiFiller, size);
        }
};

ostream& operator<<(ostream& os, ObjectData& data) 
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
            "\t\t\t\"mallocBacktrace\" : " << trace.first << "," << endl <<
            "\t\t\t\"freeBacktrace\" : " << trace.second << endl <<
            "\t\t}";

    return os;
}

#endif
