#include <sys/select.h>

/*
 * Sleep for a given duration in milliseconds
 */
void sleepinmilliseconds(int msec) {
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = msec * 1000;
  select(0, 0, 0, 0, &tv);
}

/*
 * Sleep for a given duration in seconds
 */
void sleepinseconds(int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  select(0, 0, 0, 0, &tv);
}
