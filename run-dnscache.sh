#!/bin/sh

[ "$USER" = root ] || { echo "You must run $0 as root."; exit 1; }

ACCESS_CONTROL_FILE_PATH="ip/accesscontrol.global"
CACHE_SERVERS_LIST_FILE_PATH="cacheservers"
[ -d root ] || mkdir root
[ -d root/ip ] || mkdir root/ip
[ -f $ACCESS_CONTROL_FILE_PATH ] || cp accesscontrol.global root/ip/.
[ -f $CACHE_SERVERS_LIST_FILE_PATH ] || cp cacheservers root/.
[ -d root/servers ] || mkdir root/servers
cp dnsroots.global root/servers/@

export ROOT=./root
export IP=127.0.0.1
export IPSEND=0.0.0.0
export DISTRIBUTEDCACHE=1
export DISTRIBUTEDCACHESERVERSFILE=$CACHE_SERVERS_LIST_FILE_PATH
export CACHESIZE=1024576
export CUSTOMDOMAIN=myip.opendns.com
# domain length when encoded 4myip7opendns3com + null char
export CUSTOMDNSDOMAINLEN=18
export CUSTOMDNS=208.67.222.222
export GID=0
export ACCESSCONTROL=$ACCESS_CONTROL_FILE_PATH

# UID is read-only in bash, so we set it with env.
#
# Dnscache requires 128 bytes from standard input to start up; we use
# the dnscache binary itself for that.
exec env UID=0 ./dnscache <dnscache

