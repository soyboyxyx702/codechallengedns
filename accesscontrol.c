#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "accesscontrol.h"
#include "alloc.h"
#include "byte.h"
#include "dnscache.h"
#include "error.h"
#include "str.h"

#define MAX_BUCKETS 10000
struct IPnode {
  char *ip;
  struct IPnode* next;
};

struct Hashbucket {
  struct IPnode* begin;
  struct IPnode* end;
};

static time_t lastModificationTime = 0;
static char* accesscontrolpath = 0;
static int alreadyinitialized = 0;
static struct Hashbucket h[MAX_BUCKETS];
static struct Hashbucket hauxillary[MAX_BUCKETS];
static pthread_mutex_t hashmutex[MAX_BUCKETS];

/*
 * Custom integer power function
 */
static unsigned long ipow(int base, int exponent) {
  unsigned long result = 1;
  int i;
  for(int i = 1; i <= exponent; i++) {
    result *= base;
  }

  return result;
}

/*
 * Thread sleep for a given duration
 */
static void shortSleep(int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  select(0, 0, 0, 0, &tv);
}

/*
 * probe access control file for change
 * return: 1 if file has been modified, else return 0
 */
static int probefile() {
  struct stat statbuf;
  int ret = stat(accesscontrolpath, &statbuf);

  if(ret == -1) {
    return 0;
  }
  if(lastModificationTime == statbuf.st_mtime) {
    return 0;
  }
  lastModificationTime = statbuf.st_mtime;
  return 1;
}

/*
 * return hash value of a string (IP) using a polynomial hash function
 */
static unsigned long hashcode(const char* ip) {
  const int len = str_len(ip);
  unsigned long hashval = 0;
  const int primeval = 31;

  int exponent = len - 1;
  unsigned long multiplier = ipow(primeval, exponent);
  int i;
  for(i = 0; i < len; i++) {
    hashval += multiplier * ip[i];
    multiplier /= primeval;
  }

  return hashval;
}

/*
 * create a new node for the IP address to be stored into the hash bucket
 */
static struct IPnode* newnode(const char* ip) {
  struct IPnode* ipnode = (struct IPnode*) malloc(sizeof(struct IPnode));
  if(ipnode) {
    ipnode->next = 0;
    int iplen = str_len(ip) + 1;
    ipnode->ip = (char *) alloc(iplen);
    if(ipnode->ip) {
      byte_copy(ipnode->ip, iplen, ip);
    }
    else {
      free(ipnode);
      ipnode = 0;
    }
  }

  return ipnode;
}

/*
 * Invoked whenever the access control list has been updated & update the hash bucket with IP entries
 */
static void moveEntriesFromAuxillaryToMainHashbucket() {
  int bucketnum;
  for(bucketnum = 0; bucketnum < MAX_BUCKETS; bucketnum++) {
    // critical section
    pthread_mutex_lock(&hashmutex[bucketnum]);

    // delete entries for current bucket
    struct IPnode* curr = h[bucketnum].begin;
    while(curr) {
      struct IPnode* next = curr->next;
      alloc_free(curr->ip);
      free(curr);
      curr = next;
    }

    // make current bucket point to new entries read from access control list stored in auxillary hash bucket
    h[bucketnum].begin = hauxillary[bucketnum].begin;
    h[bucketnum].end = hauxillary[bucketnum].end;

    // end critical section
    pthread_mutex_unlock(&hashmutex[bucketnum]);

    // clear out auxillary hash bucket, need not be in critical region
    hauxillary[bucketnum].begin = 0;
    hauxillary[bucketnum].end = 0;
  }
}

/*
 * add IP address into the auxillary hash bucket
 */
static void addIPtoauxillarybucket(const char* ip) {
  if(!ip) {
    return;
  }
  unsigned long hashval = hashcode(ip);
  int bucketnum = hashval % MAX_BUCKETS;

  struct IPnode* ipnode = newnode(ip);
  if(!ipnode) {
    return;
  }

  if(!hauxillary[bucketnum].begin) {
    hauxillary[bucketnum].begin = ipnode;
  }
  else {
    hauxillary[bucketnum].end->next = ipnode;
  }
  hauxillary[bucketnum].end = ipnode;
}

/*
 * Invoked whenever the access control list has been updated
 * Read from access control list and update which IPs are allowed
 */
static void getUpdatedAccessControlList() {
  FILE* fptr = fopen(accesscontrolpath, "r");
  if (!fptr) {
    return;
  }

  while(!feof(fptr)) {
    size_t len;
    char* ip;
    size_t ret = getline(&ip, &len, fptr);
    if(!feof(fptr)) {
      if(ip[ret - 1] == '\n') {
        ip[ret - 1] = '\0';
      }
      addIPtoauxillarybucket(ip);
    }
  }

  moveEntriesFromAuxillaryToMainHashbucket();
  
  fclose(fptr);
}

/*
 * Initialize auxillary and main hash tables buckets to be empty
 */
static void initializehashbuckets() {
  int i;
  for(int i = 0; i < MAX_BUCKETS; i++) {
    h[i].begin = 0;
    h[i].end = 0;
    hauxillary[i].begin = 0;
    hauxillary[i].end = 0;
  }
}

/*
 * Intiailize all mutexes to control concurrent access to hash buckets
 * Return 1 if initialization succeeds, -1 if it fails
 */
static int intializemutexes() {
  for(int i = 0; i < MAX_BUCKETS; i++) {
    if(pthread_mutex_init(&hashmutex[i], 0) != 0) {
      return -1;
    }
  }
  return 1;
}

/*
 * Replacement to stat system call in okclient
 * invoked to verify whether an IP has been greenlit (based on accesscontrol list)
 * Access critical section to access all entries in a given bucket
 * Returns 0 if IP should not be allowed, 1 otherwise
 */
int allowaccesstoip(const char* ip) {
  if(!ip || !alreadyinitialized) {
    return 0;
  }

  // Determine the bucket for the IP based on its hash code
  unsigned long hashval = hashcode(ip);
  int bucketnum = hashval % MAX_BUCKETS;
  int found = 0;

  // Critical section
  pthread_mutex_lock(&hashmutex[bucketnum]);

  // Search within the hashbucket if the IP exists
  struct IPnode* curr = h[bucketnum].begin;
  while(curr) {
    if(str_equal(curr->ip, ip)) {
      found = 1;
      break;
    }
    curr = curr->next;
  }

  // End of critical section
  pthread_mutex_unlock(&hashmutex[bucketnum]);

  return found;
}

/*
 * Thread invoked during dnscache startup to keep track of accesscontrol list updates
 * Determine whether an IP has been greenlit or not based on the accesscontrol list
 */
void* updateAccessControl(void *dummyparam) {
  if(!alreadyinitialized) {
    return 0;
  }
  while(keepRunning == 1) {
    shortSleep(2);
    if(probefile()) {
      getUpdatedAccessControlList();
    }
  }

  return 0;
}

/*
 * Initialize accesscontrol by providing accesscontrol file path and the hash buckets
 * Return 1 if successful -1 in case of failure
 */
int initializeaccesscontrol(const char *path) {
  if(!path) {
    return -1;
  }

  // Just a safety check, allow access control module to be initialized only once
  if(alreadyinitialized) {
    return -1;
  }

  if(intializemutexes() != 1) {
    return -1;
  }
  initializehashbuckets();

  int len = str_len(path) + 1;
  accesscontrolpath = alloc(len);
  if(!accesscontrolpath) {
    return -1;
  }

  byte_copy(accesscontrolpath, len, path);

  alreadyinitialized = 1;

  return 1;
}
