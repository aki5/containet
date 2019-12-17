
# Containet

[![Build Status](https://travis-ci.org/aki5/containet.svg?branch=master)](https://travis-ci.org/aki5/containet)
[![Coverity Scan Build Status](https://scan.coverity.com/projects/7971/badge.svg)](https://scan.coverity.com/projects/containet)

This repository is an attempt at creating clean environment for cluster
computing, in a way this is an attempt to make the network addressing not
only trivially configurable, but completely programmable.

Currently, Containet consists of three programs: containode, containet and mocker.

## Containode

Containode has the following command line options

```
-s path/to/containet.sock
	domain socket to post our tun/tap interface file descriptor into, for
	packet forwarding between containers.
-4 ip4addr/mask
	ipv4 address to assing to our tun/tap interface locally. If mask
	is not supplied, the default is based on class.
-r path/to/root
	path to pivot into before executing the program
-t path/to/top
	the supplied directory is used to store changes to the root filesystem,
	instead of automatically creating a temporary directory.
-i identity
	Identity of the container (needs to be unique within session), also
	hostname. If not supplied, a random 64-bit number is generated and
	base62 encoded into a 11 character identity.
-N
	if supplied, a new network namespace is not created, the container
	executes with the host network stack instead
-I
	if supplied, a new IPC namespace is not created, the container
	executes with the host IPC namespace instead.
```

All the mount name space paramters (-r, -t) can be omitted, in which case
no changes are made to the mount name space. If -r is supplied but -t is
omitted, a new directory is created. Its name is just what got passed to -r
but with the container identity added as a suffix.

If no -s flag is supplied, no network interface will be created for the container.
The -4 switch can also be omitted, in which case no address will be assigned up
front (but code in the container can still assign any address it wants).

Regardless of the mount name space configuration, containode will mount the
following file systems before executing the program

```
	/proc
	/dev
	/dev/pts
```

It will also create the following device files

```
	/dev/console
	/dev/null
	/dev/zero
	/dev/ptmx
	/dev/tty
	/dev/random
	/dev/urandom
```

Which goes a surprisingly long way.

## Containet

Containet does ethernet switching between multiple containers. Containet has the
following command line options

```
-s path/to/containet.sock
	Where to listen for incoming calls from containode, to inject new
	containers into the forwarder
```

## Mocker

mocker currently just pulls images from dockerhub. it was written mostly to try out
the json parser and to check that libcurl works ok..

## Demo

First build the programs
```
make
```

Now remove any old switch socket and start the switch.

```
sudo rm /tmp/containet.sock
sudo ./containet -s /tmp/containet.sock
```

Open up another terminal, and write

```
$ sudo ./containode -4 10.0.0.2/24 -s /tmp/containet.sock /bin/sh
# ifconfig
eth0      Link encap:Ethernet  HWaddr 92:35:d0:4c:78:10  
          inet addr:10.0.0.2  Bcast:10.0.0.255  Mask:255.255.255.0
          inet6 addr: fe80::9035:d0ff:fe4c:7810/64 Scope:Link
          UP BROADCAST RUNNING  MTU:1500  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:3 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:500 
          RX bytes:0 (0.0 B)  TX bytes:258 (258.0 B)
```

In yet another terminal, write 

```
$ sudo ./containode -4 10.0.0.1/24 -s /tmp/containet.sock /bin/sh
# ifconfig
eth0      Link encap:Ethernet  HWaddr 02:d7:47:8e:18:29  
          inet addr:10.0.0.1  Bcast:10.0.0.255  Mask:255.255.255.0
          inet6 addr: fe80::d7:47ff:fe8e:1829/64 Scope:Link
          UP BROADCAST RUNNING  MTU:1500  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:6 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:500 
          RX bytes:0 (0.0 B)  TX bytes:508 (508.0 B)

# ping 10.0.0.2
PING 10.0.0.2 (10.0.0.2) 56(84) bytes of data.
64 bytes from 10.0.0.2: icmp_seq=1 ttl=64 time=0.183 ms
64 bytes from 10.0.0.2: icmp_seq=2 ttl=64 time=0.122 ms
64 bytes from 10.0.0.2: icmp_seq=3 ttl=64 time=0.119 ms
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=0.156 ms
^C
--- 10.0.0.2 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 2998ms
rtt min/avg/max/mdev = 0.119/0.145/0.183/0.026 ms
```
