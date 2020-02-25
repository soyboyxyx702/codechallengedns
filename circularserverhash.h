#ifndef CIRCULARSERVERHASH_H
#define CIRCULARSERVERHASH_H

#include "uint16.h"

extern void addserverstohashring(const char*);
extern int getserverforkey(const char*, const int, char**, uint16 *);

#endif
