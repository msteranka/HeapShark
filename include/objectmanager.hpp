#ifndef __OBJECT_MANAGER_HPP
#define __OBJECT_MANAGER_HPP

#include "pin.H"
#include <unordered_map>
#include <vector>

using namespace std;

// All of ObjectManager's methods are thread-safe unless specified otherwise
//
class ObjectManager
{
    public:
        ObjectManager()
        {
            PIN_InitLock(&liveObjectsLock);
            PIN_InitLock(&deadObjectsLock);
        }

        VOID AddObject(ADDRINT ptr, UINT32 size, Backtrace trace, THREADID threadId)
        {
            ObjectData *d;
            ADDRINT nextAddr;

            // We don't need to worry about recursive malloc calls since Pin doesn't instrument the Pintool itself
            //
            d = new ObjectData(ptr, size, threadId);
            d->SetMallocTrace(trace);

            // Create a mapping from every address in this object's range to the same ObjectData
            // TODO: Is it more efficient to just grab the lock once and execute the loop
            // than it is to grab the lock every iteration?
            //
            for (UINT32 i = 0; i < size; i++)
            {
                nextAddr = (ptr + i);
                PIN_GetLock(&liveObjectsLock, threadId);
                liveObjects.insert(make_pair<ADDRINT,ObjectData*>(nextAddr, d));
                PIN_ReleaseLock(&liveObjectsLock);
            }
        }

        VOID RemoveObject(ADDRINT ptr, CONTEXT *ctxt, THREADID threadId)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;
            ADDRINT startAddr, endAddr;

            // Determine if this is an invalid/double free, and if it is, then 
            // skip this routine
            //
            PIN_GetLock(&liveObjectsLock, threadId);
            it = liveObjects.find(ptr);
            if (it == liveObjects.end())
            {
                PIN_ReleaseLock(&liveObjectsLock);
                return;
            }
            PIN_ReleaseLock(&liveObjectsLock);
    
            // Set the backtrace for free() in the corresponding object, and 
            // insert the object into deadObjects
            //
            d = it->second;
            d->SetFreeThread(threadId);
            d->SetFreeTrace(ctxt);
            PIN_GetLock(&deadObjectsLock, threadId);
            deadObjects.push_back(d);
            PIN_ReleaseLock(&deadObjectsLock);

            // Remove all mappings corresponding to this object
            //
            startAddr = d->GetAddr();
            endAddr = startAddr + d->GetSize();
            while (startAddr != endAddr)
            {
                PIN_GetLock(&liveObjectsLock, threadId);
                liveObjects.erase(startAddr++);
                PIN_ReleaseLock(&liveObjectsLock);
            }
        }

        BOOL ReadObject(ADDRINT addrRead, UINT32 readSize, THREADID threadId)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;

            // Determine whether addrRead corresponds to an object returned by malloc, 
            // and if it isn't, then skip this routine
            //
            // TODO: Grabbing a lock on every read/write instruction absolutely sucks
            // Need a better way of doing this without using an unordered_map...
            //
            PIN_GetLock(&liveObjectsLock, threadId);
            it = liveObjects.find(addrRead);
            if (it == liveObjects.end())
            {
                PIN_ReleaseLock(&liveObjectsLock);
                return false;
            }
            PIN_ReleaseLock(&liveObjectsLock);

            // Update corresponding object's data (no locks are needed here since
            // all of ObjectData's methods are thread-safe
            //
            d = it->second;
            d->IncrementNumReads();
            d->AddBytesRead(readSize);
            d->UpdateReadCoverage(addrRead, readSize);

            return true;
        }

        BOOL WriteObject(ADDRINT addrWritten, UINT32 writeSize, THREADID threadId)
        {
            unordered_map<ADDRINT,ObjectData*>::iterator it;
            ObjectData *d;

            PIN_GetLock(&liveObjectsLock, threadId);
            it = liveObjects.find(addrWritten);
            if (it == liveObjects.end())
            {
                PIN_ReleaseLock(&liveObjectsLock);
                return false;
            }
            PIN_ReleaseLock(&liveObjectsLock);

            d = it->second;
            d->IncrementNumWrites();
            d->AddBytesWritten(writeSize);
            d->UpdateWriteCoverage(addrWritten, writeSize);

            return true;
        }

        // NOT THREAD-SAFE
        // Move objects that were never freed to totalObjects
        // Only called at the end of the application in case objects were
        // never freed
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
                RemoveObject(it->second->GetAddr(), nullptr, -1);
            }
        }

        // Write out contents of deadObjects to os and empty deadObjects
        //
        VOID ClearDeadObjects(ostream& os)
        {
            vector<ObjectData*>::iterator it;

            PIN_GetLock(&deadObjectsLock, -1);
            while (deadObjects.size() > 0)
            {
                // Written out in reverse order to take advantage of pop_back
                //
                it = deadObjects.end() - 1;
                os << **it << "," << endl;
                delete *it;
                deadObjects.pop_back();
            }
            PIN_ReleaseLock(&deadObjectsLock);
        }

        // NOT THREAD-SAFE
        // This method is only called upon program termination
        //
        vector<ObjectData*> *GetDeadObjects() { return &deadObjects; }

        UINT32 GetNumDeadObjects()
        {
            UINT32 size;
            PIN_GetLock(&deadObjectsLock, -1);
            size = deadObjects.size();
            PIN_ReleaseLock(&deadObjectsLock);
            return size;
        }
        
    private:
        unordered_map<ADDRINT,ObjectData*> liveObjects;
        vector<ObjectData*> deadObjects;
        PIN_LOCK liveObjectsLock, deadObjectsLock;
};

ostream& operator<<(ostream &os, ObjectManager& manager) // NOT THREAD-SAFE
{
    vector<ObjectData*> *deadObjects;
    vector<ObjectData*>::iterator it;

    deadObjects = manager.GetDeadObjects();
    for (it = deadObjects->begin(); it != deadObjects->end() - 1; it++)
    {
        os << **it << "," << endl;
    }
    os << **it;

    return os;
}

#endif
