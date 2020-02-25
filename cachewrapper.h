#ifndef CACHE_H
#define CACHE_H

#include "uint32.h"
#include "uint64.h"

extern int cache_init_wrapper(unsigned int, unsigned int, const char*);
extern void cache_set_wrapper(const char *,unsigned int,const char *,unsigned int,uint32);
extern char *cache_get_wrapper(const char *,unsigned int,unsigned int *,uint32 *);
extern void cache_delete_wrapper(const char *, unsigned int);

#endif
