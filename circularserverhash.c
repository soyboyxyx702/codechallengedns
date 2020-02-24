#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"
#include "byte.h"
#include "circularserverhash.h"
#include "hash.h"
#include "scan.h"
#include "str.h"
#include "uint64.h"
#include "buffer.h"

struct CircularListNode {
  unsigned int hashposition;
  char* ip;
  unsigned int port;
  struct CircularListNode* next;
};

static struct CircularListNode* head = 0;
static struct CircularListNode* headauxillary = 0;

static pthread_mutex_t hashmutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Modulo division by a number independent of number of servers in the system
 */
static unsigned int gethashposition(const char* key, int keylen) {
  return hashcode(key, keylen) % 9999;
}

/*
 * Add servernode to auxillary circular hash list while maintaining sorted circular hash position order
 */
static void addservertoauxillarylist(struct CircularListNode* servernode) {
  if(!headauxillary) {
    headauxillary = servernode;
    return;
  }

  struct CircularListNode* prev = 0;
  struct CircularListNode* curr = headauxillary;

  /*
   * Find the earliest entry in the circular hash list with hash position > new node to be inserted
   * Can be optimized a bit by using binary search to determine the point of insertion
   */
  /*
   * Does not handle the case when there is a collision i.e. another server occupying the same hash position
   */
  while(curr && curr->hashposition < servernode->hashposition) {
    prev = curr;
    curr = curr->next;
  }

  if(prev) {
    servernode->next = prev->next;
    prev->next = servernode;
  }
  else {
    servernode->next = headauxillary;
    servernode = headauxillary;
  }
}

static struct CircularListNode* getnewservernode(const char *serverentry) {
  if(!serverentry) {
    return 0;
  }
  struct CircularListNode* newnode = (struct CircularListNode*) malloc(sizeof(struct CircularListNode));
  if(!newnode) {
    return 0;
  }
  newnode->next = 0;

  int len = str_len(serverentry);
  int pos = str_chr(serverentry, ':');
  if(pos == len) {
    // invalid entry, entries are expected to be in the format serverip:port
    free(newnode);
    return 0;
  }

  newnode->ip = alloc(pos);
  if(!newnode->ip) {
    free(newnode);
    return 0;
  }
  byte_copy(newnode->ip, pos, serverentry);
  newnode->ip[pos] = 0;
  
  scan_uint(serverentry + pos + 1, &(newnode->port));
  if(newnode->port < 1024 || newnode->port > 65535) {
    // Reserved or out of range port number
    alloc_free(newnode->ip);
    free(newnode);
    return 0;
  }

  newnode->hashposition = gethashposition(serverentry, len);
  return newnode;
}

static void moveentriestomaincircularlist() {
  /*
   * Critical section
   * Swap out head and auxillary head entry pointers
   */
  pthread_mutex_lock(&hashmutex);

  struct CircularListNode* temp = head;
  head = headauxillary;
  headauxillary = temp;

  /* end of critical section */
  pthread_mutex_unlock(&hashmutex);

  // clear out auxillary hash list, need not be in critical region
  struct CircularListNode* curr = headauxillary;
  while(curr) {
    struct CircularListNode* next = curr->next;
    alloc_free(curr->ip);
    free(curr);
    curr = next;
  }
  
  headauxillary = 0;
}

/*
 * Update circular hash ring with list of cache servers specified in the file
 */
void addserverstohashring(const char* cacheserverspath) {
  if(!cacheserverspath) {
    return;
  }

  FILE* fptr = fopen(cacheserverspath, "r");
  if (!fptr) {
    return;
  }

  while(!feof(fptr)) {
    size_t len;
    char* serverentry = 0;
    size_t ret = getline(&serverentry, &len, fptr);
    if(!feof(fptr)) {
      if(serverentry) {
        if(serverentry[ret - 1] == '\n') {
          serverentry[ret - 1] = '\0';
        }
        struct CircularListNode* servernode = getnewservernode(serverentry);
        char temp[1024];
        sprintf(temp, "adding IP %s port %d hashposition %d\n", servernode->ip, servernode->port, servernode->hashposition);
        buffer_puts(buffer_2, temp);
        if(servernode) {
          addservertoauxillarylist(servernode);
        }
        free(serverentry);
      }
    }
  }

  moveentriestomaincircularlist();

  fclose(fptr);
}

/*
 * Accesses critical section
 * Find server responsible for handling the given key
 * Return 1 on success, -1 on failure and set ip and port numbers to the server that should handle the key
 * The memory area pointed to by IP must be freed by the caller
 */
int getserverforkey(const char* key, const int keylen, char** ip, unsigned int* port) {
  if(!key || !port) {
    return -1;
  }
  int hashposition = gethashposition(key, keylen);

  char temp[1024];
  sprintf(temp, "hashcode for key %lu %d\n", hashcode(key, keylen), hashposition);
  buffer_puts(buffer_2, temp);

  /*
   * Critical section
   */
  pthread_mutex_lock(&hashmutex);
  if(!head) {
    pthread_mutex_unlock(&hashmutex);
    return -1;
  }

  struct CircularListNode* curr = head;

  /*
   * Find the earliest entry in the circular hash list with hash position >= hash position of key to be processed
   * Can be optimized a bit by using binary search
   */
  while(curr && curr->hashposition < hashposition) {
    curr = curr->next;
  }

  // Reached the end of the list, circle back to the beginning
  if(!curr) {
    curr = head;
  }

  int len = str_len(curr->ip) + 1;
  *ip = alloc(len);
  if(!*ip) {
    pthread_mutex_unlock(&hashmutex);
    return -1;
  }
  byte_copy(*ip, len, curr->ip);

  *port = curr->port;
  pthread_mutex_unlock(&hashmutex);

  sprintf(temp, "selecting server ip %s port %d\n", *ip, *port);
  buffer_puts(buffer_2, temp);
  return 1;
}
