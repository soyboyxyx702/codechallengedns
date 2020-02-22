#include "scan.h"

unsigned int scan_uint(register const char *s,register unsigned int *u)
{
  register unsigned int pos = 0;
  register unsigned int result = 0;
  register unsigned long c;
  while ((c = (unsigned long) (unsigned char) (s[pos] - '0')) < 10) {
    result = result * 10 + c;
    ++pos;
  }
  *u = result;
  return pos;
}
