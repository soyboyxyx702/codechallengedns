#ifndef CIRCULARSERVERHASH_H
#define CIRCULARSERVERHASH_H

extern void addserverstohashring(const char*);
extern int getserverforkey(const char*, const int, char**, unsigned int *);

#endif
