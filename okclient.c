#include <sys/types.h>
#include <sys/stat.h>
#include "str.h"
#include "ip4.h"
#include "okclient.h"
#include "accesscontrol.h"

/*static char fn[3 + IP4_FMT];

int okclient(const char ip[4])
{
  struct stat st;
  int i;

  fn[0] = 'i';
  fn[1] = 'p';
  fn[2] = '/';
  fn[3 + ip4_fmt(fn + 3,ip)] = 0;

  for (;;) {
    
    if (stat(fn,&st) == 0) return 1;
    // treat temporary error as rejection
    i = str_rchr(fn,'.');
    if (!fn[i]) return 0;
    fn[i] = 0;
  }
}*/

int okclient(const char ip[4]) {
  char ipstr[IP4_FMT];
  int len = ip4_fmt(ipstr, ip);
  ipstr[len] = 0;
  return allowaccesstoip(ipstr);
}
