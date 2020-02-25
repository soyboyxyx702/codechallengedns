#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "alloc.h"
#include "byte.h"
#include "cacheheader.h"
#include "cacherequesthandler.h"
#include "exit.h"
#include "ip4.h"
#include "ndelay.h"
#include "scan.h"
#include "socket.h"
#include "strerr.h"

#define MAX_EPOLL_EVENTS 1000
#define FATAL "cacheserver: fatal: "
#define ERROR "cacheserver: error: "
#define SOMAXLISTENQUEUE 20

int keepRunning = 1;

/*
 * Handle SIGNAL interrupts to gracefully terminate the server
 */
static void sighandler(int sig) {
  if(sig == SIGINT) {
    keepRunning = 0;
  }
}

/*
 * Create a TCP socket listening to the server address to which it's bound
 * Return sockfd if successful, -1 in case of error
 */
static int initiateserversockfd(char ip[4], uint16 port) {
  int sockfd = socket_tcp();
  if(sockfd == -1) {
    strerr_die2sys(111, FATAL, "unable to create server TCP socket: ");
    return -1;
  }

  if(socket_bind4_reuse(sockfd, ip, port) == -1) {
    strerr_die2sys(111, FATAL,"unable to bind TCP socket: ");
    close(sockfd);
    return -1;
  }

  if(socket_listen(sockfd, SOMAXLISTENQUEUE) == -1) {
    strerr_die2sys(111, FATAL,"unable to listen on TCP socket: ");
    close(sockfd);
    return -1;
  }
  return sockfd;
}

/*
 * Add the sock fd to be monitored for events by epoll
 * Return 1 if successful, -1 in case of error
 */
static int addfdtoepoll(int epollfd, int sockfd) {
  struct epoll_event event;

  event.data.fd = sockfd;
  event.events = EPOLLIN;
  if(epoll_ctl(epollfd, EPOLL_CTL_ADD, sockfd, &event) == -1) {
    return -1;
  }

  return 1;
}

/*
 * Remove the sock fd from epoll monitoring pool
 * Return 1 if successful, -1 in case of error
 */
static int removefdfromepoll(int epollfd, int sockfd) {
  if(epoll_ctl(epollfd, EPOLL_CTL_DEL, sockfd, 0) == -1) {
    return -1;
  }

  return 1;
}

/*
 * Forward user request to be handled
 */
static void handlerequest(int sockfd, char** response, int* responselen) {
  int numreceived = 0;
  char buffer[MAXSOBUF];
  int ret = recv(sockfd, buffer, MAXSOBUF, 0);
  if(ret <= 0) {
    // 0 signifies TCP connection has been closed by the client
    return;
  }

  numreceived += ret;

  if(numreceived > 0) {
    cacherequesthandler(buffer, numreceived, response, responselen);
  }
}

/*
 * Send cache request response to the user
 */
static void sendresponse(int sockfd, char* response, int responselen) {
  int numsent = 0;
  while(numsent < responselen) {
    int ret = send(sockfd, response, responselen - numsent, 0);
    if(ret == -1) {
      return;
    }
    numsent += ret;
  }
}

static void createeventloop(int serverfd) {
  int epollfd = epoll_create1(0);
  struct epoll_event events[MAX_EPOLL_EVENTS];

  if(epollfd == -1)
  {
    strerr_die2x(111, FATAL, "Unable to create epoll fd ");
    return;
  }

  if(addfdtoepoll(epollfd, serverfd) == -1) {
    close(epollfd);
    return;
  }

  while(keepRunning == 1) {
    int numready = epoll_wait(epollfd, events, MAX_EPOLL_EVENTS, 200);
    if(numready == -1) {
      strerr_die2x(111, FATAL, "epoll_wait error ");
      return;
    }
    for(int i = 0; i < numready; i++) {
      if(events[i].events & EPOLLERR
          || events[i].events & EPOLLHUP
          || !(events[i].events & EPOLLIN)) {
        removefdfromepoll(epollfd, events[i].data.fd);
        close(events[i].data.fd);
      }
      else if(events[i].events & EPOLLIN) {
        if(events[i].data.fd == serverfd) {
          char ip[4];
          uint16 port;
          int newfd = socket_accept4(serverfd, ip, &port);
          if(newfd == -1) {
            strerr_die2x(111, ERROR, "Unable to accept new connection ");
          }

          socket_tryreservein(newfd, MAXSOBUF);

          if(ndelay_on(newfd) == -1 || addfdtoepoll(epollfd, newfd) == -1) {
            close(newfd);
          }
        }
        else {
          char *response = 0;
          int responselen = 0;
          handlerequest(events[i].data.fd, &response, &responselen);
          if(response) {
            sendresponse(events[i].data.fd, response, responselen);
            alloc_free(response);
          }
          removefdfromepoll(epollfd, events[i].data.fd);
          close(events[i].data.fd);
        }
      }
    }

  }
}

int main(int argc, char **argv) {
  char ip[4];
  uint16 port;

  if(argc != 3) {
    strerr_die2x(111, FATAL, "Invalid number of arguments");
    strerr_die3x(111, FATAL, "<Usage> %s <port number>", argv[0]);
    _exit(111);
  }

  if (!ip4_scan(argv[1], ip))
    strerr_die3x(111, FATAL, "unable to parse IP address ", argv[1]);

  scan_ushort(argv[2], &port);

  if(port < 1024 || port > 65535) {
    strerr_die3x(111, FATAL, "Invalid port number ", argv[1]);
    _exit(111);
  }

  int serverfd = initiateserversockfd(ip, port);
  if(serverfd == -1) {
    _exit(111);
  }

  struct sigaction act;
  act.sa_handler = sighandler;
  sigaction(SIGINT, &act, 0);

  cacherequesthandlerinit();

  createeventloop(serverfd);

  close(serverfd);
  return 0;
}
