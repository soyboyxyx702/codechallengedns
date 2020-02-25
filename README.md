Programming Exercises
=======================

Setup
------
Compiled with POSIX threads and crypto libraries (-lpthread -lcrypto)

The code for these exercises originally came from:
> http://cr.yp.to/djbdns/djbdns-1.05.tar.gz

For a quick way to set up a build environment on Debian Linux, follow these instructions:

    # Make sure you have some standard libs and a compiler:
    $ sudo apt-get install make gcc libssl-dev
    
    # Now to compile (but not install) djbdns
    $ git clone <BitBucket git URL>
    $ cd coding-challenge
    $ echo "cc -O2 -include /usr/include/errno.h" > conf-cc
    $ make

    # Rather than setting up a service, you can use our run-script to run it in foreground.
    $ ./run-dnscache.sh
    # You are now running dnscache.  Run make to re-compile with your code changes.
    # Just run ./run-dnscache.sh whenever you want to start it up.
    # You can test by issuing: "dig @127.0.0.1 yahoo.com" from another terminal on the same machine.


Programming Challenges:

For the project to successfully compile on Ubuntu or equivalent Linux distribution libpthread and libssl-dev or equivalent are required.
Optionally install the following packages for manpages - manpages-posix manpages-posix-dev libssl-doc

1. myip.opendns.com
----------------

The OpenDNS resolvers treat the myip.opendns.com domain name
specially.  They respond to a query for myip.opendns.com with the IP
address of the client that sent the query.  For example, my
development host's IP address address is `67.215.67.199`.  Here's what I
see when I query for `myip.opendns.com`:

    $ dig @208.67.222.222 myip.opendns.com
    
    ; <<>> DiG 9.3.4-P1.2 <<>> @208.67.222.222 myip.opendns.com
    ; (1 server found)
    ;; global options:  printcmd
    ;; Got answer:
    ;; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 2559
    ;; flags: qr rd ra; QUERY: 1, ANSWER: 1, AUTHORITY: 0, ADDITIONAL: 0
    
    ;; QUESTION SECTION:
    ;myip.opendns.com.              IN      A
    
    ;; ANSWER SECTION:
    myip.opendns.com.       0       IN      A       67.215.68.199

    ;; Query time: 13 msec
    ;; SERVER: 208.67.222.222#53(208.67.222.222)
    ;; WHEN: Fri May  7 23:25:30 2010
    ;; MSG SIZE  rcvd: 50

Implement this behavior in dnscache.

Implementation:

The approach was to figure out where are we getting the list of DNS servers from.
By specifying the DNS server that performs the custom behavior of returning public UP
for myip.opendns.com, as a configuration setting/ environment variable, we return the
custom DNS server instead of specified DNS servers list at `root/@` for `myip.opendns.com`

Configuration:

The following entries in run-dnscache.sh :

    $ export CUSTOMDOMAIN=myip.opendns.com
    $ export CUSTOMDNSDOMAINLEN=18
    $ export CUSTOMDNS=208.67.222.222


2. okclient()
----------------

The `okclient()` function (in okclient.c) performs IP address-based
access control for dnscache.  It does so using the `stat()` system call,
the overhead of which can limit the throughput of a busy server.
Modify dnscache to perform IP address-based access control without
using system calls for each DNS request.

Implementation:

Ideally the access control list will be specified in a database and cached by DNS.
However in order to avoid performing a stat system call on every request, & still be
able to keep track of access control list, we do so by creating a separate dedicated thread,
whose job is to simply perform a stat call on regular intervals and if access control list has
been modified, then take the appropriate action.
Also I have done away with file name approach for access control, instead a new file `accesscontrol.global`
will have a list of authorized IPs.

`accesscontrol.c` monitors this file for updates in a separate thread.

Configuration:

The following entries in run-dnscache.sh :

    $ ACCESS_CONTROL_FILE_PATH="ip/accesscontrol.global"
    $ [ -f root/ip/accesscontrol.global  ] || cp accesscontrol.global root/ip/.



3. Cache Delete
-----------------

Modify cache.c to add a method to delete an entry from the cache.

Implementation:

`cache_find` is a helper method, which will keep looking for a key in the byte array, called by `cache_get` & `cache_delete`.
If an expired copy of the key is found, instead of following the previous behavior and return key to not be found,
we invalidate the key record, by setting the key field to all 0s. The assumption here being, the real set of keys
will not be taking a value of all 0s. And we keep looking in the byte array if there are other versions of the key.

This will also modify how cache_get, as the search for a key does not terminate just because we find an expired copy of the same.

As far as cache_delete goes, once a valid copy of the key is found, set its key field to all 0s.
The deleted key marked as invalid will eventually be overwritten.

(Alternatively we could simply expire the key to mark it as deleted.
it did not make sense to me to move entries around the byte array when a key is being deleted,
that will be a O(n) time/space operation.).

Testing:

Modified `cachetest` to test `cache_delete`.
`key name:delete` indicates key has to be deleted
NOTE: Have not implemented delete key for Distributed Cache (section 4), make sure you disable
distributed caching in run-dnscache.sh :

    $ export DISTRIBUTEDCACHE=0  # set this to 0


Try out :

    $ ./cachetest www.google.com:172.217.3.164 www.google.com www.google.com:delete www.google.com
    $ ./cachetest www.google.com:172.217.3.164 www.google.com www.google.com:delete www.google.com

4. New Features
----------------
Modify cache.c, or provide an
entirely new cache.c, to implement some interesting new functionality.
For example, you might provide a way to delete the cache entries for
all subdomains of a domain, or to do distributed caching across a
cluster, or to save and restore the contents of the cache.  Perhaps
there is a better way to structure the contents of the cache in a
hierarchy of sorts.  This is usually the most interesting and fun of
the problems in this challenge;  we know this question is the one we
look forward to seeing the most. If you are short on time we would
still like to see your thoughts on how you will approach this problem. :-)


Implementation:

I have borrowed from consistent hashing to map cache keys to cache server.
circularserverhash.c monitors cache server list specifying list of cache servers for updates
and maps servers to a deterministic hash position, regardless of the number of servers.
The cache keys are mapped to a deterministic hash position too, independent of number of servers.
Thus less keys will have to be remapped if the number of servers change, instead of rehashing
the whole key space in case of conventional hashing.

In production, list of cache servers will probably be maintained in a DB and cached somewhere and
there will be separate health periodic checks to take faulty cache servers out of the cluster.

Also, an enhancement to circular hashing could also be mapping a given server to multiple virtual positions
within the circular hash space.

`cacheclient.c/cacheserver.c` are TCP client/servers. This could have been done in UDP too.


Configuration:

The following entry in run-dnscache.sh specifies the list of cache servers
and whether we are running a distributed cache service or the default one:
    $ CACHE_SERVERS_LIST_FILE_PATH="cacheservers.list"
    $ export DISTRIBUTEDCACHE=1
    $ export DISTRIBUTEDCACHESERVERSFILE=$CACHE_SERVERS_LIST_FILE_PATH


Testing:

Make Distributed cache has been enabled in run-dnscache.sh

Start all cache servers specified in `root/cacheservers.list`

    $ ./cacheserver 127.0.0.1 6001
    $ ./cacheserver 127.0.0.1 6002
    $ ./cacheserver 127.0.0.1 6003
    $ ./cacheserver 127.0.0.1 6004

Start the dnscache 
    $ sudo ./run-dnscache.sh

Test out with DNS requests
    $ dig @127.0.0.1 www.facebook.com
    $ dig @127.0.0.1 www.google.com
