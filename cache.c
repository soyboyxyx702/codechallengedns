#include "alloc.h"
#include "byte.h"
#include "cache.h"
#include "distributedcache.h"
#include "exit.h"
#include "tai.h"
#include "uint32.h"

uint64 cache_motion = 0;

static char *x = 0;
static uint32 size;
static uint32 hsize;
static uint32 writer;
static uint32 oldest;
static uint32 unused;
static uint32 notfound;

static uint32 usedistributedcache;

/*
100 <= size <= 1000000000.
4 <= hsize <= size/16.
hsize is a power of 2.

hsize <= writer <= oldest <= unused <= size.
If oldest == unused then unused == size.

x is a hash table with the following structure:
x[0...hsize-1]: hsize/4 head links.
x[hsize...writer-1]: consecutive entries, newest entry on the right.
x[writer...oldest-1]: free space for new entries.
x[oldest...unused-1]: consecutive entries, oldest entry on the left.
x[unused...size-1]: unused.

Each hash bucket is a linked list containing the following items:
the head link, the newest entry, the second-newest entry, etc.
Each link is a 4-byte number giving the xor of
the positions of the adjacent items in the list.

Entries are always inserted immediately after the head and removed at the tail.

Each entry contains the following information:
4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
*/

#define MAXKEYLEN 1000
#define MAXDATALEN 1000000

static void cache_impossible(void)
{
  _exit(111);
}

static void set4(uint32 pos,uint32 u)
{
  if (pos > size - 4) cache_impossible();
  uint32_pack(x + pos, u);
}

static uint32 get4(uint32 pos)
{
  uint32 result;
  if (pos > size - 4) cache_impossible();
  uint32_unpack(x + pos,&result);
  return result;
}

static unsigned int hash(const char *key,unsigned int keylen)
{
  unsigned int result = 5381;

  while (keylen) {
    result = (result << 5) + result;
    result ^= (unsigned char) *key;
    ++key;
    --keylen;
  }
  result <<= 2;
  result &= hsize - 4;
  return result;
}

static uint32 cache_find(const char *key,unsigned int keylen) {
  struct tai expire;
  struct tai now;
  uint32 pos;
  uint32 prevpos;
  uint32 nextpos;
  uint32 u;
  unsigned int loop;
  double d;


  if (!x || keylen > MAXKEYLEN) {
    return notfound;
  }

  prevpos = hash(key,keylen);
  pos = get4(prevpos);
  loop = 0;

  /*
   * 4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
   */
  while (pos) {
    // Get key len and proceed only if keys are of the same length
    if (get4(pos + 4) == keylen) {
      // Boundary check before reading the key
      if (pos + 20 + keylen > size) cache_impossible();

      if (byte_equal(key, keylen, x + pos + 20)) {
        // Found the key at that position

        // Boundary check for data
        u = get4(pos + 8);
        if (u > size - pos - 20 - keylen) cache_impossible();

        tai_unpack(x + pos + 12, &expire);
        tai_now(&now);

        // key has already expired
        if (tai_less(&expire,&now)) {
          return notfound;
        }

        // return position of the entry for the key
        return pos;
      }
    }

    /*
     * Each link is a 4-byte number giving the xor of
     * the positions of the adjacent items in the list.
     */
    nextpos = prevpos ^ get4(pos);
    prevpos = pos;
    pos = nextpos;
    if (++loop > 100) {
      /* to protect against hash flooding */
      return notfound;
    }
  }

  return notfound;
}

void cache_delete(const char *key, unsigned int keylen) {
  if(usedistributedcache) {
    return;
  }

  struct tai diffpast;
  struct tai now;
  struct tai past;
  uint32 pos;
  double d;

  pos = cache_find(key, keylen);
  if(pos == notfound) {
    return;
  }

  /*
   * Expire the key by setting its expiry time in the past
   */
  tai_now(&now);
  tai_uint(&diffpast,10);
  tai_sub(&past, &now, &diffpast);

  tai_pack(x + pos + 12, &past);
}

char *cache_get(const char *key, unsigned int keylen, unsigned int *datalen, uint32 *ttl)
{
  if(usedistributedcache) {
    return distributed_cache_get(key, keylen, datalen, ttl);
  }

  struct tai expire;
  struct tai now;
  uint32 pos;
  double d;

  pos = cache_find(key, keylen);
  if(pos == notfound) {
    return 0;
  }

  /*
   * 4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
   */
  tai_unpack(x + pos + 12, &expire);
  tai_now(&now);
        
  tai_sub(&expire, &expire, &now);
  d = tai_approx(&expire);

  if (d > 604800) {
    // Cap TTL
    d = 604800;
  }
  *ttl = d;

  // Get datalen
  *datalen = get4(pos + 8);

  return x + pos + 20 + keylen;
}

void cache_set(const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl)
{
  if(usedistributedcache) {
    return distributed_cache_set(key, keylen, data, datalen, ttl);
  }

  struct tai now;
  struct tai expire;
  unsigned int entrylen;
  unsigned int keyhash;
  uint32 pos;

  // Parameter validation
  if (!x) return;
  if (keylen > MAXKEYLEN) return;
  if (datalen > MAXDATALEN) return;

  if (!ttl) return;
  if (ttl > 604800) ttl = 604800;

  /*
   * 4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
   */
  entrylen = keylen + datalen + 20;

  /*
   * hsize <= writer <= oldest <= unused <= size.
   * If oldest == unused then unused == size.
   */
  // Keep moving oldest until it's outside the boundary of the latest entry to be inserted
  while (writer + entrylen > oldest) {
    if (oldest == unused) {
      if (writer <= hsize) return;
      unused = writer;
      oldest = hsize;
      writer = hsize;
    }

    pos = get4(oldest);

    set4(pos, get4(pos) ^ oldest);
 
    // Move oldest by an entry 
    oldest += get4(oldest + 4) + get4(oldest + 8) + 20;
    if (oldest > unused) cache_impossible();
    if (oldest == unused) {
      unused = size;
      oldest = size;
    }
  }

  keyhash = hash(key, keylen);

  tai_now(&now);
  tai_uint(&expire,ttl);
  tai_add(&expire,&expire,&now);

  pos = get4(keyhash);
  if (pos) {
    set4(pos, get4(pos) ^ keyhash ^ writer);
  }
  set4(writer, pos ^ keyhash);
  set4(writer + 4,keylen);
  set4(writer + 8,datalen);
  tai_pack(x + writer + 12, &expire);
  byte_copy(x + writer + 20,keylen, key);
  byte_copy(x + writer + 20 + keylen, datalen, data);

  // value at pos keyhash will point to writer i.e. whether key has been written
  set4(keyhash, writer);
  writer += entrylen;
  cache_motion += entrylen;
}

int cache_init(unsigned int distributedcache, unsigned int cachesize, const char* cacheserversfile)
{
  usedistributedcache = distributedcache;
  if(usedistributedcache) {
    return distributed_cache_init(cacheserversfile);
  }

  if (x) {
    alloc_free(x);
    x = 0;
  }

  if (cachesize > 1000000000) cachesize = 1000000000;
  if (cachesize < 100) cachesize = 100;
  size = cachesize;
  // an out of bound index for hash array indicating a key could not be located
  notfound = size + 1;

  hsize = 4;
  while (hsize <= (size >> 5)) hsize <<= 1;

  x = alloc(size);
  if (!x) return 0;
  byte_zero(x,size);

  writer = hsize;
  oldest = size;
  unused = size;

  return 1;
}
