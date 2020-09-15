#ifndef __OBJECT_MANAGER_HPP
#define __OBJECT_MANAGER_HPP

#include "pin.H"
#include <unordered_map>
#include <vector>

using namespace std;

class ObjectManager
{
    public:
        VOID AddObject(ADDRINT ptr, UINT32 size, Backtrace trace)
        {
            ObjectData *d;
            ADDRINT nextAddr;

            // We don't need to worry about recursive malloc calls since Pin doesn't instrument the PinTool itself
            //
            d = new ObjectData(ptr, size);
            d->SetMallocTrace(trace);
            for (UINT32 i = 0; i < size; i++)
            {
                nextAddr = (ptr + i);
                // Create a mapping from every address in this object's range to the same ObjectData
                //
                liveObjects.insert(make_pair<ADDRINT,ObjectData*>(nextAddr, d)); // NOT THREAD SAFE!
            }
        }

        VOID RemoveObject(ADDRINT ptr, CONTEXT *ctxt)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;
            ADDRINT startAddr, endAddr;

            it = liveObjects.find(ptr);
            if (it == liveObjects.end()) // If this is an invalid/double free, then skip this routine
            {
                return;
            }

            d = it->second;
            d->SetFreeTrace(ctxt);
            totalObjects[d->GetSize()].push_back(d); // Insert the ObjectData into totalObjects

            startAddr = d->GetAddr();
            endAddr = startAddr + d->GetSize();
            while (startAddr != endAddr)
            {
                liveObjects.erase(startAddr++); // Remove all mappings corresponding to this object
            }
        }

        BOOL ReadObject(ADDRINT addrRead, UINT32 readSize)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;

            it = liveObjects.find(addrRead);
            if (it == liveObjects.end()) // If this is not an object returned by malloc, then skip this routine
            {
                return false;
            }

            d = it->second; // NOTE: can a thread be reading/writing to an object that is currently being freed?
            d->SetNumReads(d->GetNumReads() + 1);
            d->SetBytesRead(d->GetBytesRead() + readSize);
            d->UpdateReadCoverage(addrRead, readSize);

            return true;
        }

        BOOL WriteObject(ADDRINT addrWritten, UINT32 writeSize)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;

            it = liveObjects.find(addrWritten);
            if (it == liveObjects.end())
            {
                return false;
            }

            d = it->second;
            d->SetNumWrites(d->GetNumWrites() + 1);
            d->SetBytesWritten(d->GetBytesWritten() + writeSize);
            d->UpdateWriteCoverage(addrWritten, writeSize);

            return true;
        }

        unordered_map<ADDRINT,ObjectData*> *GetLiveObjects() { return &liveObjects; }

        unordered_map<UINT32, vector<ObjectData*>> *GetTotalObjects() { return &totalObjects; }
        
    private:
        unordered_map<ADDRINT,ObjectData*> liveObjects;
        unordered_map<UINT32, vector<ObjectData*>> totalObjects;
};

ostream& operator<<(ostream &os, ObjectManager& manager) 
{
    unordered_map<ADDRINT,ObjectData*> seen, liveObjects;
    unordered_map<ADDRINT,ObjectData*>::iterator liveObjectsIter, seenIter;
    unordered_map<UINT32,vector<ObjectData*>>::iterator totalObjectsIter;
    unordered_map<UINT32, vector<ObjectData*>> totalObjects;
    vector<ObjectData*> *curAddrs;
    vector<ObjectData*>::iterator curAddrsIter;
    ObjectData *d;

    liveObjects = *manager.GetLiveObjects();
    totalObjects = *manager.GetTotalObjects();

    // Move objects that were never freed to totalObjects
    //
    for (liveObjectsIter = liveObjects.begin(); liveObjectsIter != liveObjects.end(); liveObjectsIter++)
    {
        d = liveObjectsIter->second;
        seenIter = seen.find(d->GetAddr());
        if (seenIter == seen.end()) // If this object hasn't been seen yet, then add it to totalObjects
        {
            seen.insert(make_pair<ADDRINT,ObjectData*>(d->GetAddr(), d));
            totalObjects[d->GetSize()].push_back(d);
        }
    }

    // Print out all data
    //
    for (totalObjectsIter = totalObjects.begin(); totalObjectsIter != totalObjects.end(); totalObjectsIter++)
    {
        curAddrs = &(totalObjectsIter->second);
        os << "OBJECT SIZE: " << totalObjectsIter->first << endl << 
                        "========================================" << endl;
        for (curAddrsIter = curAddrs->begin(); curAddrsIter != curAddrs->end(); curAddrsIter++)
        {
            // We must output data to a file instead of stdout or stderr because stdout and stderr have been closed at this point
            //
            os << **curAddrsIter << endl;
        }
    }

    return os;
}

#endif
