#!/bin/sh

[ "$USER" = root ] || { echo "You must run $0 as root."; exit 1; }

[ -d root ] || mkdir root
[ -d root/ip ] || mkdir root/ip
[ -f root/ip/accesscontrol.global  ] || cp accesscontrol.global root/ip/.
[ -f root/cacheservers.list ] || cp cacheservers.list root/.
[ -d root/servers ] || mkdir root/servers
cp dnsroots.global root/servers/@

ACCESS_CONTROL_FILE_PATH="ip/accesscontrol.global"
CACHE_SERVERS_LIST_FILE_PATH="cacheservers.list"

export ACCESSCONTROL=$ACCESS_CONTROL_FILE_PATH
export DISTRIBUTEDCACHE=1
export DISTRIBUTEDCACHESERVERSFILE=$CACHE_SERVERS_LIST_FILE_PATH
export ROOT=./root
export IP=127.0.0.1
export IPSEND=0.0.0.0
export CACHESIZE=1024576
export CUSTOMDOMAIN=myip.opendns.com
# domain length when encoded 4myip7opendns3com + null char
export CUSTOMDNSDOMAINLEN=18
export CUSTOMDNS=208.67.222.222
export GID=0

# UID is read-only in bash, so we set it with env.
#
# Dnscache requires 128 bytes from standard input to start up; we use
# the dnscache binary itself for that.
exec env UID=0 ./dnscache <dnscache

