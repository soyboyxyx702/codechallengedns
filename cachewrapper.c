#include "cache.h"
#include "distributedcache.h"
#include "uint32.h"
#include "uint64.h"

static int usedistributedcache = 0;

char *cache_get_wrapper(const char *key, unsigned int keylen, unsigned int *datalen, uint32 *ttl) {
  if(usedistributedcache) {
    return distributed_cache_get(key, keylen, datalen, ttl);
  }
  else {
    return cache_get(key, keylen, datalen, ttl);
  }
}

void cache_delete_wrapper(const char *key, unsigned int keylen) {
  // delete not implemented for distributed cache
  if(!usedistributedcache) {
    cache_delete(key, keylen);
  }
}

void cache_set_wrapper(const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl) {
  if(usedistributedcache) {
    distributed_cache_set(key, keylen, data, datalen, ttl);
  }
  else {
    cache_set(key, keylen, data, datalen, ttl);
  }
}

int cache_init_wrapper(unsigned int distributedcache, unsigned int cachesize, const char* cacheserversfile) {
  if(distributedcache != 0 && distributedcache != 1) {
    return -1;
  }
  usedistributedcache = distributedcache;
  if(distributedcache) {
    return distributed_cache_init(cacheserversfile);
  }
  else {
    return cache_init(cachesize);
  }
}
