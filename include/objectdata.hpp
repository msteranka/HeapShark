#ifndef __OBJECT_DATA_H
#define __OBJECT_DATA_H

#include "pin.H"
#include <iostream>
#include <new>
#include "backtrace.hpp"
#include "misc.hpp"

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
            bytesWritten(0),
            mallocTrace(3),
            freeTrace(3)
        {
            readBuf = new CHAR[size];
            memset(readBuf, 0, size);
            writeBuf = new CHAR[size];
            memset(writeBuf, 0, size);
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
            delete[] readBuf;
            delete[] writeBuf;
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
        Backtrace mallocTrace, freeTrace;
};

ostream& operator<<(ostream& os, ObjectData& data) 
{
    os << hex << data.addr << dec << ":" << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << data.GetReadFactor() << endl <<
                 "\tWrite Factor = " << data.GetWriteFactor() << endl <<
                 "\tRead Coverage = " << data.readCoverage << endl << 
                 "\tWrite Coverage = " << data.writeCoverage << endl <<
                 "malloc Backtrace:" << endl << data.mallocTrace << endl <<
                 "free Backtrace:" << endl << data.freeTrace << endl;

    return os;
}

#endif
