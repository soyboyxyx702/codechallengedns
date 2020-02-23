#include <openssl/sha.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "globals.h"
#include "uint64.h"

int keepRunning = 1;

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

/*
 * probe file for changes and update its last modified time
 * return: 1 if file has been modified, else return 0
 */
int probefile(const char* path, time_t* lastmodified) {
  if(!lastmodified || !path) {
    return 0;
  }
  struct stat statbuf;
  int ret = stat(path, &statbuf);

  if(ret == -1) {
    return 0;
  }

  if(*lastmodified == statbuf.st_mtime) {
    return 0;
  }

  *lastmodified = statbuf.st_mtime;
  return 1;
}

/*
 * Sleep for a given duration
 */
void shortsleep(int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  select(0, 0, 0, 0, &tv);
}
