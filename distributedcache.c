#include <sys/types.h>

#include "alloc.h"
#include "byte.h"
#include "circularserverhash.h"
#include "distributedcache.h"
#include "globals.h"
#include "str.h"
#include "buffer.h"

static int initialized = 0;
static char* cacheserverspath = 0;
static time_t lastmodificationtime = 0;

void distributed_cache_set(const char *key, unsigned int keylen, const char *data, unsigned int datalen, uint32 ttl) {
  if(!initialized) {
    return;
  }
}

char *distributed_cache_get(const char *key, unsigned int keylen, unsigned int *datalen, uint32 *ttl) {
  if(!initialized) {
    return 0;
  }
}

void* monitorserverlistforupdates(void *dummyparam) {
  buffer_puts(buffer_2, "monitor file\n");
  if(!initialized) {
    return 0;
  }
  while(keepRunning == 1) {
    buffer_puts(buffer_2, "monitor file\n");
    shortsleep(2);
    if(probefile(cacheserverspath, &lastmodificationtime)) {
      buffer_puts(buffer_2, "cache servers list updated\n");
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
  return 1;
}
