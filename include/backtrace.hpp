#ifndef __BACKTRACE_HPP
#define __BACKTRACE_HPP

#include "pin.H"
#include <iostream>

using namespace std;

class Backtrace 
{
    public:
        // A Backtrace is initialized with the maximum number of stack frames
        // that it will go down
        //
        Backtrace() : depth(0)
        {
            trace = new pair<string,INT32>[maxDepth];
        }

        VOID SetTrace(CONTEXT *ctxt)
        {
            // buf contains maxDepth + 1 addresses because PIN_Backtrace also returns
            // the stack frame for malloc/free
            //
            VOID *buf[maxDepth + 1];
        
            // Pin requires us to call Pin_LockClient() before calling PIN_Backtrace
            // and PIN_GetSourceLocation
            //
            PIN_LockClient();
            depth = PIN_Backtrace(ctxt, buf, maxDepth + 1) - 1;

            // We set i = 1 because we don't want to include the stack frame 
            // for malloc/free
            //
            for (INT32 i = 1; i < depth + 1; i++)
            {
        
                // NOTE: executable must be compiled with -g -gdwarf-2 -rdynamic
                // to locate the invocation of malloc/free
                // NOTE: PIN_GetSourceLocation does not necessarily get the exact
                // invocation point, but it's pretty close
                //
                PIN_GetSourceLocation((ADDRINT) buf[i], nullptr, 
                                        &(trace[i - 1].second),
                                        &(trace[i - 1].first));
            }
            PIN_UnlockClient();
        }

        pair<string,INT32> *GetTrace()
        {
            return trace;
        }

        INT32 GetDepth()
        {
            return depth;
        }

        Backtrace &operator=(const Backtrace &b)
        {
            for (INT32 i = 0; i < b.depth; i++)
            {
                trace[i].first = b.trace[i].first;
                trace[i].second = b.trace[i].second;
            }
            depth = b.depth;

            return *this;
        }

    private:
        // trace consists of all invocation points of malloc/free, 
        // represented as a pairing of a file name and a line number
        //
        pair<string,INT32> *trace;
        INT32 depth;
        static const INT32 maxDepth = 3;
};

ostream& operator<<(ostream& os, Backtrace& bt)
{

    pair<string,INT32> *t;

    t = bt.GetTrace();
    if (bt.GetDepth() == 0)
    {
        os << "N/A" << endl;
        return os;
    }

    for (INT32 i = 0; i < bt.GetDepth(); i++)
    {
        // If PIN_GetSourceLocation failed to map the IP to a 
        // file + line number
        //
        if (t[i].first == "")
        {
            os << "(NIL)";
        }
        else
        {
            os << t[i].first << ":" << t[i].second;
        }
        if (i < bt.GetDepth() - 1)
        {
            os << endl;
        }
    }
    return os;
}

#endif
