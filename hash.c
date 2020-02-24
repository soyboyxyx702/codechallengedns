#include <openssl/sha.h>

#include "uint64.h"

/*
 * Hash a key
 */
uint64 hashcode(const char* key, const int len) {
  unsigned char shadigest[SHA_DIGEST_LENGTH];
  uint64 hashval = 0;
  const int primeval = 7;

  SHA1(key, len, shadigest);

  uint64 multiplier = primeval;
  int i;
  for(i = 0; i < SHA_DIGEST_LENGTH; i++) {
    hashval += multiplier * shadigest[i];
    multiplier *= primeval;
  }

  return hashval;
}
