#include "alloc.h"
#include "byte.h"
#include "uint32.h"
#include "exit.h"
#include "tai.h"
#include "cache.h"
#include "buffer.h"
#include <stdio.h>

uint64 cache_motion = 0;

static char *x = 0;
static uint32 size;
static uint32 hsize;
static uint32 writer;
static uint32 oldest;
static uint32 unused;

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

char *cache_get(const char *key,unsigned int keylen,unsigned int *datalen,uint32 *ttl)
{
  struct tai expire;
  struct tai now;
  uint32 pos;
  uint32 prevpos;
  uint32 nextpos;
  uint32 u;
  unsigned int loop;
  double d;

  if (!x) return 0;
  if (keylen > MAXKEYLEN) return 0;

  buffer_puts(buffer_2, "cacheget key  ");
  buffer_put(buffer_2, key, keylen);
  char temp[1024];
  sprintf(temp, "   cacheget keylen %d\n", keylen);
  buffer_puts(buffer_2, temp);

  prevpos = hash(key,keylen);
  pos = get4(prevpos);
  sprintf(temp, "cacheget prevpos (keyhash) %d pos %d\n", prevpos, pos);
  buffer_puts(buffer_2, temp);
  loop = 0;

  /*
   * 4-byte link; 4-byte keylen; 4-byte datalen; 8-byte expire time; key; data.
   */
  while (pos) {
    sprintf(temp, "cacheget pos %d\n", pos);
    buffer_puts(buffer_2, temp);
    sprintf(temp, "cacheget pos  + 4 (keylen)   %d\n", get4(pos + 4));
    buffer_puts(buffer_2, temp);

    // Get key len and proceed only if keys are of the same length
    if (get4(pos + 4) == keylen) {
      // Boundary check before reading the key
      if (pos + 20 + keylen > size) cache_impossible();
      // Found the key at that position
      if (byte_equal(key, keylen, x + pos + 20)) {
        tai_unpack(x + pos + 12, &expire);
        tai_now(&now);
        
        sprintf(temp, "expires: %ld now: %ld\n", expire.x, now.x);
        buffer_puts(buffer_2, temp);

        // key has already expired
        if (tai_less(&expire,&now)) return 0;

        tai_sub(&expire,&expire,&now);
        d = tai_approx(&expire);
        if (d > 604800) d = 604800;
        *ttl = d;

        // Get datalen
        u = get4(pos + 8);
        // Boundary check before reading the data
        if (u > size - pos - 20 - keylen) cache_impossible();
        *datalen = u;

        return x + pos + 20 + keylen;
      }
    }

    /*
     * Each link is a 4-byte number giving the xor of
     * the positions of the adjacent items in the list.
     */
    nextpos = prevpos ^ get4(pos);
    prevpos = pos;
    pos = nextpos;
    if (++loop > 100) return 0; /* to protect against hash flooding */
  }

  return 0;
}

void cache_set(const char *key,unsigned int keylen,const char *data,unsigned int datalen,uint32 ttl)
{
  struct tai now;
  struct tai expire;
  unsigned int entrylen;
  unsigned int keyhash;
  uint32 pos;

  char temp[1024];
  sprintf(temp, "\n\nCACHESET keylen %d datalen %d ttl %d\n", keylen, datalen, ttl);
  buffer_puts(buffer_2, temp);
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
  sprintf(temp, "cacheset writer %d oldest %d\n", writer, oldest);
  buffer_puts(buffer_2, temp);
  while (writer + entrylen > oldest) {
    if (oldest == unused) {
      if (writer <= hsize) return;
      unused = writer;
      oldest = hsize;
      writer = hsize;
      sprintf(temp, "cacheset reset to oldest %d writer %d unused %d\n", oldest, writer, unused);
      buffer_puts(buffer_2, temp);
    }

    // Overwrite x + pos with XOR(existing val of 4 bytes at pos, oldest)
    sprintf(temp, "cacheset oldest contains %d to be assigned to pos\n", get4(oldest));
    buffer_puts(buffer_2, temp);

    pos = get4(oldest);

    sprintf(temp, "cacheset pos contains %d to be overwritten with get4(pos) ^ oldest: %d\n", get4(pos), get4(pos) ^ oldest);
    buffer_puts(buffer_2, temp);
    set4(pos, get4(pos) ^ oldest);
    sprintf(temp, "cacheset pos overwritten with %d\n", get4(pos));
    buffer_puts(buffer_2, temp);
 
    // Move oldest by an entry 
    oldest += get4(oldest + 4) + get4(oldest + 8) + 20;
    sprintf(temp, "cacheset oldest moved to %d\n", oldest);
    buffer_puts(buffer_2, temp);
    if (oldest > unused) cache_impossible();
    if (oldest == unused) {
      unused = size;
      oldest = size;
      sprintf(temp, "cacheset reset oldest & unused to %d\n", size);
      buffer_puts(buffer_2, temp);
    }
  }

  keyhash = hash(key, keylen);

  sprintf(temp, "cacheset keyhash: %d\n", keyhash);
  buffer_puts(buffer_2, temp);

  tai_now(&now);
  tai_uint(&expire,ttl);
  tai_add(&expire,&expire,&now);

  pos = get4(keyhash);
  sprintf(temp, "cacheset 4 bytes at keyhash to be assigned to pos: %d\n", pos);
  buffer_puts(buffer_2, temp);
  if (pos) {
    sprintf(temp, "cacheset 4 bytes at x(pos) are : %d  writer %d\n", get4(pos), writer);
    buffer_puts(buffer_2, temp);

    set4(pos, get4(pos) ^ keyhash ^ writer);

    sprintf(temp, "cacheset 4 bytes at x(pos) set to get4(pos) ^ keyhash ^ writer: %d\n", get4(pos));
    buffer_puts(buffer_2, temp);
  }
  sprintf(temp, "cacheset writer %d, 4 bytes at writer %d, to be overwritten with  (pos ^ keyhash) : %d\n", writer, get4(writer), pos ^ keyhash);
  buffer_puts(buffer_2, temp);
  set4(writer, pos ^ keyhash);
  set4(writer + 4,keylen);
  set4(writer + 8,datalen);
  tai_pack(x + writer + 12, &expire);
  byte_copy(x + writer + 20,keylen, key);
  byte_copy(x + writer + 20 + keylen, datalen, data);

  // value at pos keyhash will point to writer i.e. whether key has been written
  sprintf(temp, "cacheset keyhash %d, 4 bytes at keyhash %d, to be overwritten with writer: %d\n", keyhash, get4(keyhash), writer);
  buffer_puts(buffer_2, temp);
  set4(keyhash, writer);
  writer += entrylen;
  cache_motion += entrylen;
}

int cache_init(unsigned int cachesize)
{
  if (x) {
    alloc_free(x);
    x = 0;
  }

  if (cachesize > 1000000000) cachesize = 1000000000;
  if (cachesize < 100) cachesize = 100;
  size = cachesize;

  hsize = 4;
  while (hsize <= (size >> 5)) hsize <<= 1;

  x = alloc(size);
  if (!x) return 0;
  byte_zero(x,size);

  char temp[1024];
  sprintf(temp, "hsize %d size %d  size/32 %d\n", hsize, size, (size >> 5)); 
  buffer_puts(buffer_2, temp);

  writer = hsize;
  oldest = size;
  unused = size;

  return 1;
}
