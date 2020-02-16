#include <pthread.h>
#include <sys/select.h>
#include "accesscontrol.h"
#include "dnscache.h"
#include "buffer.h"
#include <stdio.h>
#include <stdlib.h>

static void shortSleep(int sec) {
  struct timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = 0;
  select(0, NULL, NULL, NULL, &tv);
}

void doStuff() {
  FILE* fptr = fopen("testing", "a");
  if (fptr != NULL) {
    fprintf(fptr, "time is on my side\n");
    fclose(fptr);
  }
}

void* updateAccessControl(void *param) {
  while(keepRunning == 1) {
    shortSleep(2);
    doStuff();
    buffer_puts(buffer_2,  "sleep\n");
  }
  buffer_puts(buffer_2,  "time to die\n");
}
