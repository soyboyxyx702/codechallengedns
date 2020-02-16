#include <unistd.h>
#include "open.h"
#include "error.h"
#include "str.h"
#include "byte.h"
#include "error.h"
#include "direntry.h"
#include "ip4.h"
#include "dns.h"
#include "openreadclose.h"
#include "roots.h"
#include "alloc.h"
#include <string.h>

static stralloc data;
static char* customdnsdomain;
static unsigned long customdnsdomainlen;
static char customserverip[4];

static int checkCustomDomainName(const char *domainname)
{
  char ch;
  int state;

  if (domainname == NULL || !*domainname) {
    return 0;
  }

  int domainlen = dns_domain_length(domainname);
  if(domainlen != customdnsdomainlen) {
    return 0;
  }

  int i = 0;
  int strlenDomain = strlen(customdnsdomain);
  while (state = *domainname++) {
    while (state) {
      ch = *domainname++;
      --state;
      if ((ch <= 32) || (ch > 126)) {
        ch = '?';
      }
      if ((ch >= 'A') && (ch <= 'Z')) {
        ch += 32;
      }

      if(i == strlenDomain || customdnsdomain[i++] != ch) {
        return 0;
      }
    }
    if(i < strlenDomain && customdnsdomain[i++] != '.') {
      return 0;
    }
  }
  
  return 1;
}

static int roots_find(const char *q)
{
  int i;
  int j;

  i = 0;
  while (i < data.len) {
    j = dns_domain_length(data.s + i);
    if (dns_domain_equal(data.s + i,q)) return i + j;
    i += j;
    i += 64;
  }
  return -1;
}

static int roots_search(const char *q)
{
  int r;

  for (;;) {
    r = roots_find(q);
    if (r >= 0) return r;
    if (!*q) return -1; /* user misconfiguration */
    q += *q;
    q += 1;
  }
}

int roots(char servers[64], const char *q, const char* domainname)
{
  int r;

  // if user domain query is the custom domain name to retrieve public IP
  if(checkCustomDomainName(domainname) == 1) {
    // overwrite list of servers with the 1 custom OpenDNS server that returns public IP address for custom user DNS query
    byte_zero(servers, 64);
    byte_copy(servers, 4, customserverip);
    return 1;
  }

  r = roots_find(q);
  if (r == -1) return 0;
  byte_copy(servers,64,data.s + r);
  return 1;
}

int roots_same(char *q,char *q2)
{
  return roots_search(q) == roots_search(q2);
}

static int init2(DIR *dir)
{
  direntry *d;
  const char *fqdn;
  static char *q;
  static stralloc text;
  char servers[64];
  int serverslen;
  int i;
  int j;

  for (;;) {
    errno = 0;
    d = readdir(dir);
    if (!d) {
      if (errno) return 0;
      return 1;
    }

    if (d->d_name[0] != '.') {
      if (openreadclose(d->d_name,&text,32) != 1) return 0;
      if (!stralloc_append(&text,"\n")) return 0;

      fqdn = d->d_name;
      if (str_equal(fqdn,"@")) fqdn = ".";
      if (!dns_domain_fromdot(&q,fqdn,str_len(fqdn))) return 0;

      serverslen = 0;
      j = 0;
      for (i = 0;i < text.len;++i)
	if (text.s[i] == '\n') {
	  if (serverslen <= 60)
	    if (ip4_scan(text.s + j,servers + serverslen))
	      serverslen += 4;
	  j = i + 1;
	}
      byte_zero(servers + serverslen,64 - serverslen);

      if (!stralloc_catb(&data,q,dns_domain_length(q))) return 0;
      if (!stralloc_catb(&data,servers,64)) return 0;
    }
  }
}

static int init1(void)
{
  DIR *dir;
  int r;

  if (chdir("servers") == -1) return 0;
  dir = opendir(".");
  if (!dir) return 0;
  r = init2(dir);
  closedir(dir);
  return r;
}

int roots_init(const char *customdomain, const char customdnsip[4], const unsigned long customdomainlen)
{
  int fddir;
  int r;

  int numbytestocopy = strlen(customdomain) + 1;
  customdnsdomain = alloc(numbytestocopy);
  byte_copy(customdnsdomain, numbytestocopy, customdomain);
  byte_copy(customserverip, 4, customdnsip);
  customdnsdomainlen = customdomainlen;

  if (!stralloc_copys(&data,"")) return 0;

  fddir = open_read(".");
  if (fddir == -1) return 0;
  r = init1();
  if (fchdir(fddir) == -1) r = 0;
  close(fddir);
  return r;
}
