#include <signal.h>
#include <sys/epoll.h>

#include "buffer.h"
#include "exit.h"
#include "scan.h"
#include "strerr.h"

#define MAXKEYLEN 1000
#define MAXDATALEN 1000000
#define MAX_EPOLL_EVENTS 64

#define FATAL "cacheserver: fatal: "

int keepRunning = 1;

static void sigHandler(int sig) {
  if(sig == SIGINT) {
    keepRunning = 0;
  }
}

static int createeventloop(unsigned int port) {
  int epoll_fd = epoll_create1(0);

  if(epoll_fd == -1)
  {
    strerr_die2x(111, FATAL, "Unable to create epoll fd ");
    return -1;
  }
}

int main(int argc,char **argv) {
  if(argc != 2) {
    strerr_die2x(111,FATAL,"Invalid number of arguments");
    strerr_die3x(111,FATAL,"<Usage> %s <port number>", argv[0]);
    _exit(111);
  }

  unsigned int port;
  scan_uint(argv[1], &port);

  if(port < 1024 || port > 65535) {
    strerr_die3x(111, FATAL, "Invalid port number ", argv[1]);
    _exit(111);
  }

  struct sigaction act;
  act.sa_handler = sigHandler;
  sigaction(SIGINT, &act, 0);

  return 0;
}
