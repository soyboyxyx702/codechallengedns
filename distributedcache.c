#include <sys/types.h>

#include "alloc.h"
#include "byte.h"
#include "cacheclient.h"
#include "cacheheader.h"
#include "circularserverhash.h"
#include "distributedcache.h"
#include "probefile.h"
#include "serverstate.h"
#include "sleep.h"
#include "str.h"
#include "uint16.h"

static int initialized = 0;
static char* cacheserverspath = 0;
static time_t lastmodificationtime = 0;

// Part 4 - Distributed caching

/*
 * Extern entry point to set cache record for a given key
 */
void distributed_cache_set(const char *key, unsigned int keylen, const char *data, const unsigned int datalen, uint32 ttl) {
  if(!initialized || keylen > DISTRIBUTED_MAXKEYLEN || datalen > DISTRIBUTED_MAXDATALEN || !ttl) {
    return;
  }

  if(ttl > MAXTTL) {
    ttl = MAXTTL;
  }
  char* ip = 0;
  uint16 port;
  if(getserverforkey(key, keylen, &ip, &port) == 1) {
    if(ip) {
      sendcachetoserver(ip, port, key, keylen, data, datalen, ttl);
      alloc_free(ip);
    }
  }
}

/*
 * Extern entry point to get cache record for a given key
 * Return cache record on success, empty (0) on failure
 */
char *distributed_cache_get(const char *key, const unsigned int keylen, unsigned int *datalen, uint32 *ttl) {
  if(!initialized || keylen > DISTRIBUTED_MAXKEYLEN) {
    return 0;
  }

  char* ip = 0;
  uint16 port;
  if(getserverforkey(key, keylen, &ip, &port) == 1) {
    if(ip) {
      char* cached = getcachefromserver(ip, port, key, keylen, datalen, ttl);
      alloc_free(ip);
      return cached;
    }
    return 0;
  }
  return 0;
}

/*
 * Monitor cache servers list to keep track of newly added/removed servers
 * Ideally there will be some kind of external health check to keep this updated
 */
void* monitorserverlistforupdates(void *dummyparam) {
  if(!initialized) {
    return 0;
  }
  while(keepRunning == 1) {
    sleepinseconds(2);
    if(probefile(cacheserverspath, &lastmodificationtime)) {
      addserverstohashring(cacheserverspath);
    }
  }

  return 0;
}

int distributed_cache_init(const char* path) {
  if(initialized || !path) {
    return 0;
  }

  int len = str_len(path) + 1;
  cacheserverspath = alloc(len);
  if(!cacheserverspath) {
    return 0;
  }
  byte_copy(cacheserverspath, len, path);
  
  initialized = 1;
  
  // Get list of cache server during initialization for testing (cachetest)
  if(probefile(cacheserverspath, &lastmodificationtime)) {
    addserverstohashring(cacheserverspath);
  }
  return 1;
}
