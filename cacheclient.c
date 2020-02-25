#include <arpa/inet.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "alloc.h"
#include "byte.h"
#include "cacheheader.h"
#include "uint16.h"
#include "uint32.h"
#include "sleep.h"
#include "socket.h"

// Part 4 - Distributed caching

/*
 * Marshal the cache set parameters for the request
 */
static char *preparecachesetpayload(
    const char *key, unsigned int keylen,
    const char *data, const unsigned int datalen,
    const uint32 ttl, int* payloadlen) {
  /*
   * Request format SET
   * 1-byte req type (set); 4-byte keylen; 4-byte datalen; 4-byte ttl; key; data
   */
  *payloadlen = keylen + datalen + 13;
  char* buf = alloc(*payloadlen);
  if(!buf) {
    // Memory allocation failed
    return 0;
  }

  buf[0] = CACHE_SET;
  uint32_pack(buf + 1, keylen);
  uint32_pack(buf + 5, datalen);
  uint32_pack(buf + 9, ttl);

  byte_copy(buf + 13, keylen, key);
  byte_copy(buf + keylen + 13, datalen, data);
  return buf;
}

/*
 * Marshal the cache get parameters for the request
 */
static char *preparecachegetpayload(const char* key, unsigned int keylen, int* payloadlen) {
  /*
   * Request format GET
   * 1-byte req type (get); 4-byte keylen; key
   */
  *payloadlen = keylen + 5;

  char* buf = alloc(*payloadlen);
  if(!buf) {
    // Memory allocation failed
    return 0;
  }

  buf[0] = CACHE_GET;
  uint32_pack(buf + 1, keylen);
  byte_copy(buf + 5, keylen, key);

  return buf;
}

/*
 * Unmarshal the cache get response and return the result
 */
static char *processserverresponse(const char* buff, const int responselen, uint32* datalen, uint32* ttl) {
  /*
   * Response format GET
   * 4-byte datalen; 4-byte ttl; data;
   */
  if(responselen <= 8) {
    return 0;
  }

  uint32_unpack(buff, datalen);
  uint32_unpack(buff + 4, ttl);

  if(*ttl > MAXTTL) {
    *ttl = MAXTTL;
  }

  char *cached = alloc(*datalen);
  if(!cached) {
    // Memory allocation error
    *ttl = 0;
    *datalen = 0;
    return 0;
  }

  byte_copy(cached, *datalen, buff + 8);
  return cached;
}

/*
 * Send the marshalled GET/SET request to the server
 */
static void sendrequest(int sockfd, char* req, int reqlen) {
  int numsent = 0;
  while(numsent < reqlen) {
    int ret = send(sockfd, req, reqlen - numsent, 0);
    if(ret == -1) {
      return;
    }
    numsent += ret;
  }
}

/*
 * Receive the response from server for GET request
 */
static void getresponse(int sockfd, char *buf, int maxbuflen, int *responselen) {
  struct timeval tv;
  fd_set readfds;
  char buffer[MAXSOBUF];

  // timeout of 500 ms
  tv.tv_sec = 0;
  tv.tv_usec = 500000;

  FD_ZERO(&readfds);
  FD_SET(sockfd, &readfds);

  select(sockfd + 1, &readfds, 0, 0, &tv);

  if(FD_ISSET(sockfd, &readfds)) {
    int ret = recv(sockfd, buffer, MAXSOBUF, 0);
    if(ret > 0) {
      *responselen += ret;
    }
  }

  return; 
}

/*
 * Since we are using nonblocking socket, connect returns right away
 * Check if connect succeeds within the timeout
 * socket_connected didn't work for me, to get things moving I used select to monitor whether connect succeeded
 * Return 1 on succees, -1 on failure
 */
static int checkconnectsucceeded(int sockfd) {
  fd_set wfd, efd;

  FD_ZERO(&wfd);
  FD_SET(sockfd, &wfd);

  FD_ZERO(&efd);
  FD_SET(sockfd, &efd);

  struct timeval tv;
  // timeout of 500 ms
  tv.tv_sec = 0;
  tv.tv_usec = 500000;

  int ret = select(sockfd+1, NULL, &wfd, &efd, &tv);
  if (ret == -1)
  {
    return -1;
  }

  if (ret == 0)
  {
    // select timelimit exceeded
    close(sockfd);
    return -1;
  }

  if (FD_ISSET(sockfd, &efd))
  {
    // connect failed
    return -1;
  }

  // fd is writeable, connect succeeded
  return 1;
}

/*
 * Attempt a non-blocking connection to the server
 * Return connected sockfd on success, -1 on failure
 */ 
static int connecttoserver(const char* ip, const uint16 port) {
  int sockfd = socket_tcp();
  if(sockfd == -1) {
    return -1;
  }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);

  // Convert IPv4 and IPv6 addresses from text to binary form
  if(inet_pton(AF_INET, ip, &serv_addr.sin_addr) != 1)
  {
    close(sockfd);
    return -1;
  }

  if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
  {
    if(errno != EINPROGRESS) {
      close(sockfd);
      return -1;
    }

    if(checkconnectsucceeded(sockfd) == -1) {
      close(sockfd);
      return -1;
    }
  }

  return sockfd;
}

/*
 * Extern entry point method to update cache record for a given key
 */ 
void sendcachetoserver(
    const char *ip, const uint16 port,
    const char *key, unsigned int keylen,
    const char *data, const unsigned int datalen,
    const uint32 ttl) {
  int sockfd = connecttoserver(ip, port);
  if(sockfd == -1) {
    return;
  }
  int reqlen = 0;
  char *buf = preparecachesetpayload(key, keylen, data, datalen, ttl, &reqlen);
  if(!buf) {
    close(sockfd);
    return;
  }

  sendrequest(sockfd, buf, reqlen);
  alloc_free(buf);
  close(sockfd);
}

/*
 * Extern entry point method to get cache record for a given key
 * Return cache entry on success, empty (0) on failure
 */ 
char *getcachefromserver(
    const char *ip, const uint16 port,
    const char *key, const unsigned int keylen,
    unsigned int *datalen, uint32 *ttl) {
  int sockfd = connecttoserver(ip, port);
  if(sockfd == -1) {
    return 0;
  }
  /*
   * Response format GET
   * 4-byte datalen; 4-byte ttl; data;
   */
  int reqlen = 0;
  char *buf = preparecachegetpayload(key, keylen, &reqlen);
  if(!buf) {
    close(sockfd);
    return 0;
  }
  sendrequest(sockfd, buf, reqlen);
  alloc_free(buf);

  char buff[MAXSOBUF];
  int responselen = 0;
  getresponse(sockfd, buff, MAXSOBUF, &responselen);

  char *cached = processserverresponse(buff, responselen, datalen, ttl);

  close(sockfd);
  return cached;
}
