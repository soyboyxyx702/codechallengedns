#ifndef CACHECLIENT_H
#define CACHECLIENT_H

#include "uint16.h"
#include "uint32.h"

extern void sendcachetoserver(const char *, const uint16,
    const char *, unsigned int,
    const char *, const unsigned int,
    const uint32);

extern char *getcachefromserver(const char *, const uint16,
    const char *, const unsigned int,
    unsigned int *, uint32 *);

#endif
