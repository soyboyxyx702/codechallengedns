#include "alloc.h"
#include "cacheheader.h"
#include "tai.h"
#include "uint32.h"
#include "uint64.h"

/*
 * Request format SET
 * 1-byte req type (set); 4-byte keylen; 4-byte datalen; 8-byte expiry time; key; data
 * Request format GET
 * 1-byte req type (get); 4-byte keylen; key
 * Response format GET
 * 4-byte datalen; 8-byte expiry; data;
 */

static void cache_get(char* buffer, int reqlen, int keylen, char** response, int* responselen) {
  if(reqlen - 5 < keylen) {
    // Invalid request, expecing keylen bytes more
    return;
  }
  char* key = alloc(keylen);
  if(!key) {
    // could not allocate memory for key
    return;
  }
}

static void cache_set(char* buffer, int reqlen, int keylen) {
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

  if(reqlen - 9 < keylen + datalen + 8) {
    // Invalid request, not enough bytes to contain key, data and expiry
    return;
  }

  uint64 expiry;
  uint64_unpack(buffer + 9, &expiry);

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
