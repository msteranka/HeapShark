#ifndef __OBJECT_DATA_H
#define __OBJECT_DATA_H

#include "pin.H"
#include <iostream>
#include <new>
#include "backtrace.h"
#include "misc.h"

using namespace std;

class ObjectData
{
    public:
        ObjectData(ADDRINT addr, UINT32 size)
        {
            this->addr = addr;
            this->size = size;
            numReads = numWrites = bytesRead = bytesWritten = 0;
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

ostream& operator<<(ostream& os, ObjectData &data) 
{
    Backtrace *t;

    os << hex << data.addr << dec << ":" << endl <<
                 "\tnumReads: " << data.numReads << endl <<
                 "\tnumWrites: " << data.numWrites << endl <<
                 "\tbytesRead = " << data.bytesRead << endl <<
                 "\tbytesWritten = " << data.bytesWritten << endl <<
                 "\tRead Factor = " << data.GetReadFactor() << endl <<
                 "\tWrite Factor = " << data.GetWriteFactor() << endl <<
                 "\tRead Coverage = " << data.readCoverage << endl << 
                 "\tWrite Coverage = " << data.writeCoverage << endl;

    t = &(data.mallocTrace);
    os << "\tmalloc Backtrace:" << endl;
    for (INT32 i = 0; i < MAX_DEPTH; i++)
    {
        if (t->files[i] == "")
        {
            os << "\t\t(NIL)" << endl;
        }
        else
        {
            os << "\t\t" << t->files[i] << ":" << t->lines[i] << endl;
        }
    }

    t = &(data.freeTrace);
    os << "\tfree Backtrace:" << endl;
    for (INT32 i = 0; i < MAX_DEPTH; i++)
    {
        if (t->files[i] == "")
        {
            os << "\t\t(NIL)" << endl;
        }
        else
        {
            os << "\t\t" << t->files[i] << ":" << t->lines[i] << endl;
        }
    }

    return os;
}

#endif
