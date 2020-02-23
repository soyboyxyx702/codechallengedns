#ifndef GLOBALS_H
#define GLOBALS_H

#include <sys/types.h>
#include "uint64.h"

extern int keepRunning;
extern uint64 hashcode(const char*, const int);
extern int probefile(const char*, time_t*);
extern void shortsleep(int);

#endif
