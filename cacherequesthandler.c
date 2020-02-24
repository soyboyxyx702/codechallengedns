#include <stdlib.h>
#include "alloc.h"
#include "byte.h"
#include "cacheheader.h"
#include "hash.h"
#include "tai.h"
#include "uint32.h"
#include "uint64.h"

#define MAX_BUCKETS 10000
struct Cachenode {
  uint32 keylen;
  uint32 datalen;
  uint64 expiry;
  char *key;
  char* data;

  struct Cachenode* next;
};

struct Hashbucket {
  struct Cachenode* begin;
};

static struct Hashbucket h[MAX_BUCKETS];
static int initialized = 0;

static void addtocache(char* key, char* data, uint32 keylen, const uint32 datalen, uint32 ttl) {
  if(!ttl) {
    return;
  }
  if (ttl > 604800) {
    ttl = 604800;
  }
  uint64 hashval = hashcode(key, keylen);
  int bucketnum = hashval % MAX_BUCKETS;

  struct Cachenode* newnode = (struct Cachenode*) malloc(sizeof(struct Cachenode));
  if(!newnode) {
    // could not allocate memory
    return;
  }

  struct tai now;
  struct tai expire;
  tai_now(&now);
  tai_uint(&expire, ttl);
  tai_add(&expire, &expire, &now);

  newnode->key = key;
  newnode->data = data;
  newnode->keylen = keylen;
  newnode->datalen = datalen;
  newnode->expiry = expire.x;
  newnode->next = 0;

  struct Cachenode* curr = h[bucketnum].begin;
  struct Cachenode* prev = 0;
  while(curr) {
    struct Cachenode* next = curr->next;
    if(curr->keylen == keylen && byte_equal(curr->key, keylen, key)) {
      // Delete older entries of the key
      if(prev) {
        prev->next = next;
      }
      free(curr);
      curr = next;
    }
    else {
      prev = curr;
      curr = next;
    }
  }

  if(!prev) {
    h[bucketnum].begin = newnode;
  }
  else {
    prev->next = newnode;
  }
}

/*
 * Request format SET
 * 1-byte req type (set); 4-byte keylen; 4-byte datalen; 4-byte ttl; key; data
 * Request format GET
 * 1-byte req type (get); 4-byte keylen; key
 * Response format GET
 * 4-byte datalen; 4-byte ttl; data;
 */

static void cache_get(const char* buffer, const int reqlen, const int keylen, char** response, int* responselen) {
  if(reqlen - 5 < keylen) {
    // Invalid request, expecing keylen bytes more
    return;
  }

  const char *key = buffer + 5;
  uint64 hashval = hashcode(key, keylen);
  int bucketnum = hashval % MAX_BUCKETS;

  struct Cachenode* curr = h[bucketnum].begin;
  struct Cachenode* prev = 0;
  struct tai now;
  struct tai expire;
  tai_now(&now);

  // Look for non-expired copy of the key in the hash bucket
  // Delete stale copies of the key
  while(curr) {
    struct Cachenode* next = curr->next;
    if(curr->keylen == keylen && byte_equal(curr->key, keylen, key)) {
      expire.x = curr->expiry;

      // key has already expired
      if (tai_less(&expire,&now)) {
        // Delete older entries of the key
        if(prev) {
          prev->next = next;
        }
        if(curr == h[bucketnum].begin) {
          h[bucketnum].begin = next;
        }
        free(curr);
        curr = next;
      }
      else {
        break;
      }
    }
    else {
      prev = curr;
      curr = next;
    }
  }

  if(curr) {
    *response = alloc(curr->datalen + 8);
    if(!response) {
      // Memory allocation failed
      return;
    }

    expire.x = curr->expiry;
    tai_sub(&expire, &expire, &now);
    uint32 ttl = tai_approx(&expire);
    if(ttl > 604800) {
      ttl = 604800;
    }

    uint32_pack(*response, curr->datalen);
    uint32_pack(*response + 4, ttl);
    byte_copy(*response + 8, curr->datalen, curr->data);
    *responselen = curr->datalen + 8;
  }
  else {
    *response = alloc(8);
    if(!response) {
      // Memory allocation failed
      return;
    }
    uint32_pack(*response, 0);
    uint32_pack(*response + 4, 0);
    *responselen = 8;
  }
}

static void cache_set(const char* buffer, const int reqlen, const int keylen) {
  if(reqlen - 5 < 4) {
    // Invalid request, expecing 4 bytes of datalen
    return;
  }
  uint32 datalen;
  uint32_unpack(buffer + 5, &datalen);

  if(datalen > DISTRIBUTED_MAXDATALEN) {
    // data longer than the maximum length allowed
    return;
  }

  if(reqlen - 9 < keylen + datalen + 4) {
    // Invalid request, not enough bytes to contain key, data and expiry
    return;
  }

  uint32 ttl;
  uint32_unpack(buffer + 9, &ttl);

  char* key = alloc(keylen);
  if(!key) {
    // could not allocate memory for key
    return;
  }
  char* data = alloc(datalen);
  if(!data) {
    // could not allocate memory for data
    return;
  }

  addtocache(key, data, keylen, datalen, ttl);
}

void cacherequesthandler(char* buffer, int reqlen, char** response, int* responselen) {
  if(!buffer || reqlen < 5) {
    return;
  }

  char cacheoperation = buffer[0];

  uint32 keylen;
  uint32_unpack(buffer + 1, &keylen);
  if(keylen > DISTRIBUTED_MAXKEYLEN) {
    // Invalid request, key/data longer than the maximum length allowed
    return;
  }
  if(cacheoperation == CACHE_GET) {
    cache_get(buffer, reqlen, keylen, response, responselen);
  }
  else if(cacheoperation == CACHE_SET) {
    cache_set(buffer, reqlen,  keylen);
  }
}

void cacheinit() {
  if(initialized) {
    return;
  }

  for(int i = 0; i < MAX_BUCKETS; i++) {
    h[i].begin = 0;
  }

  initialized = 1;
}
