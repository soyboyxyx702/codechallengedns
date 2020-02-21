#include "buffer.h"
#include "exit.h"
#include "cache.h"
#include "str.h"

/*
 * ./cachetest www.google.com:172.217.3.164 www.google.com www.google.com:delete www.google.com
 */
int main(int argc,char **argv)
{
  int i;
  char *x;
  char *y;
  unsigned int u;
  uint32 ttl;

  if (!cache_init(200)) _exit(111);

  if (*argv) ++argv;

  while (x = *argv++) {
    i = str_chr(x,':');
    if (x[i]) {
      if(str_equal(x + i + 1, "delete")) {
        buffer_puts(buffer_1, "delete ");
        buffer_puts(buffer_1, x);
        buffer_puts(buffer_1,"\n");
        cache_delete(x, i);
      }
      else {
        buffer_puts(buffer_1, "set ");
        buffer_puts(buffer_1, x);
        buffer_puts(buffer_1,"\n");
        cache_set(x, i, x + i + 1, str_len(x) - i - 1, 86400);
      }
    }
    else {
      buffer_puts(buffer_1, "get ");
      buffer_puts(buffer_1, x);
      buffer_puts(buffer_1, " ");
      y = cache_get(x,i,&u,&ttl);
      if (y)
        buffer_put(buffer_1, y, u);
      buffer_puts(buffer_1,"\n");
    }
  }

  buffer_flush(buffer_1);
  _exit(0);
}
