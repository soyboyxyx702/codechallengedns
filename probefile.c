#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "uint64.h"

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
