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
            totalObjects.push_back(d); // Insert the ObjectData into totalObjects

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

        // Move objects that were never freed to totalObjects
        // Only reason we even need this is because we may have to print out data
        // before the application has ended if there are many allocations
        // 
        VOID KillLiveObjects()
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;

            while (liveObjects.size() > 0)
            {
                it = liveObjects.begin();
                // Remember that liveObjects contains pointers within objects as well,
                // so all we really need to free is the base address of the object
                //
                RemoveObject(it->second->GetAddr(), nullptr);
            }
        }

        // Write out contents of totalObjects to os 
        //
        VOID ClearDeadObjects(ostream& os, UINT32 sizeThreshold)
        {
            vector<ObjectData*>::iterator it;

            while (totalObjects.size() > 0)
            {
                // Written out in reverse order to take advantage of pop_back
                //
                it = totalObjects.end() - 1;
                os << **it << "," << endl;
                delete *it;
                totalObjects.pop_back();
            }
        }

        unordered_map<ADDRINT,ObjectData*> *GetLiveObjects() { return &liveObjects; }

        vector<ObjectData*> *GetTotalObjects() { return &totalObjects; }
        
    private:
        unordered_map<ADDRINT,ObjectData*> liveObjects;
        vector<ObjectData*> totalObjects;
};

ostream& operator<<(ostream &os, ObjectManager& manager) 
{
    vector<ObjectData*> *totalObjects;
    vector<ObjectData*>::iterator totalIt;

    totalObjects = manager.GetTotalObjects();
    for (totalIt = totalObjects->begin(); totalIt != totalObjects->end() - 1; totalIt++)
    {
        os << **totalIt << "," << endl;
    }
    os << **totalIt;

    return os;
}

#endif
