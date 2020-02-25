#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "accesscontrol.h"
#include "alloc.h"
#include "byte.h"
#include "error.h"
#include "hash.h"
#include "probefile.h"
#include "serverstate.h"
#include "sleep.h"
#include "str.h"
#include "uint64.h"

#define MAX_BUCKETS 10000
struct IPnode {
  char *ip;
  struct IPnode* next;
};

struct Hashbucket {
  struct IPnode* begin;
  struct IPnode* end;
};

static time_t lastmodificationtime = 0;
static char* accesscontrolpath = 0;
static int alreadyinitialized = 0;
static struct Hashbucket h[MAX_BUCKETS];
static struct Hashbucket hauxillary[MAX_BUCKETS];
static pthread_mutex_t hashmutex[MAX_BUCKETS];

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
static void moveentriestomainbucket() {
  int bucketnum;
  for(bucketnum = 0; bucketnum < MAX_BUCKETS; bucketnum++) {
    // critical section, swap out auxillary bucket and main bucket pointers
    pthread_mutex_lock(&hashmutex[bucketnum]);

    struct IPnode* temp = h[bucketnum].begin;
    h[bucketnum].begin = hauxillary[bucketnum].begin;
    hauxillary[bucketnum].begin = temp;
    
    temp = h[bucketnum].end;
    h[bucketnum].end = hauxillary[bucketnum].end;
    hauxillary[bucketnum].end = temp;

    // end critical section
    pthread_mutex_unlock(&hashmutex[bucketnum]);

    // clear out auxillary hash bucket, need not be in critical region
    struct IPnode* curr = hauxillary[bucketnum].begin;
    while(curr) {
      struct IPnode* next = curr->next;
      alloc_free(curr->ip);
      free(curr);
      curr = next;
    }

    // make current bucket point to new entries read from access control list stored in auxillary hash bucket
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
  uint64 hashval = hashcode(ip, str_len(ip));
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
    char* ip = 0;
    size_t ret = getline(&ip, &len, fptr);
    if(!feof(fptr)) {
      if(ip) {
        if(ip[ret - 1] == '\n') {
          ip[ret - 1] = '\0';
        }
        addIPtoauxillarybucket(ip);
        free(ip);
      }
    }
  }

  moveentriestomainbucket();
  
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
  uint64 hashval = hashcode(ip, str_len(ip));
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
    sleepinseconds(2);
    if(probefile(accesscontrolpath, &lastmodificationtime)) {
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
