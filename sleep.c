#include <sys/select.h>

/*
 * Sleep for a given duration
 */
void shortsleep(int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  select(0, 0, 0, 0, &tv);
}
