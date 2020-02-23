#ifndef DISTRIBUTEDCACHE_H
#define DISTRIBUTEDCACHE_H

#include "uint32.h"

extern int distributed_cache_init(const char*);
extern void distributed_cache_set(const char *, unsigned int, const char *, unsigned int,uint32);
extern char *distributed_cache_get(const char *, unsigned int, unsigned int *, uint32 *);
extern void* monitorserverlistforupdates(void *);

#endif
