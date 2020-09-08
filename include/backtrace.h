#ifndef __BACKTRACE_H
#define __BACKTRACE_H

#include "pin.H"

#define MAX_DEPTH 3

typedef struct {
    std::string files[MAX_DEPTH];
    INT32 lines[MAX_DEPTH];
} Backtrace;

#endif
