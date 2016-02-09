/*
 *	Copyright (c) 2016 Aki Nyrhinen
 *
 *	Permission is hereby granted, free of charge, to any person obtaining a copy
 *	of this software and associated documentation files (the "Software"), to deal
 *	in the Software without restriction, including without limitation the rights
 *	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *	copies of the Software, and to permit persons to whom the Software is
 *	furnished to do so, subject to the following conditions:
 *
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *	THE SOFTWARE.
 */
#include "os.h"
#include "tun.h"

int
ifconfig(char *devname, char *addr)
{
	char *p, addrbuf[32];
	struct sockaddr_in *addrp;
	struct ifreq ifr;
	int cfgfd;
	int maskbits;

	maskbits = -1;
	cfgfd = -1;
	memset(&ifr, 0, sizeof ifr);

	strncpy(addrbuf, addr, sizeof addrbuf);
	addrbuf[sizeof addrbuf-1] = '\0';
	if((p = strchr(addrbuf, '/')) != NULL){
		*p = '\0';
		maskbits = strtol(p+1, NULL, 10);

	}

	if((cfgfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP)) == -1){
		fprintf(stderr, "socket SOCK_DGRAM: %s\n", strerror(errno));
		goto error_out;
	}

	strncpy(ifr.ifr_name, devname, sizeof ifr.ifr_name-1);

	ifr.ifr_addr.sa_family = AF_INET;
	addrp = (struct sockaddr_in *)&ifr.ifr_addr;
	if(inet_pton(AF_INET, addrbuf, &addrp->sin_addr) == -1){
		fprintf(stderr, "inet_aton %s: %s\n", addr, strerror(errno));
		goto error_out;
	}
	if(ioctl(cfgfd, SIOCSIFADDR, (void *)&ifr) == -1){
		fprintf(stderr, "ioctl SIOCSIFADDR %s: %s\n", ifr.ifr_name, strerror(errno));
		goto error_out;
	}

	if(maskbits != -1){
		ifr.ifr_netmask.sa_family = AF_INET;
		addrp = (struct sockaddr_in *)&ifr.ifr_netmask;
		addrp->sin_addr.s_addr = htonl(~(0xfffffffful >> maskbits));
		if(ioctl(cfgfd, SIOCSIFNETMASK, (void *)&ifr) == -1){
			fprintf(stderr, "ioctl SIOCGIFNETMASK %s: %s\n", ifr.ifr_name, strerror(errno));
			goto error_out;
		}
	}

	ifr.ifr_flags = IFF_UP|IFF_BROADCAST|IFF_RUNNING; //|MULTICAST 
	if(ioctl(cfgfd, SIOCSIFFLAGS, (void *)&ifr) == -1){
		fprintf(stderr, "ioctl SIOCSIFFLAGS %s: %s\n", ifr.ifr_name, strerror(errno));
		goto error_out;
	}

	close(cfgfd);

	return 0;


error_out:
	if(cfgfd != -1)
		close(cfgfd);
	return -1;
}

int
tunopen(char *gotdev, char *wantdev, char *addr)
{
	struct ifreq ifr;
	int tunfd;

	tunfd = -1;
	memset(&ifr, 0, sizeof ifr);

	ifr.ifr_flags = IFF_TAP;
	//ifr.ifr_flags = IFF_TUN;
	//ifr.ifr_flags = IFF_NO_PI;
	if(wantdev != NULL)
		strncpy(ifr.ifr_name, wantdev, IFNAMSIZ);

	if((tunfd = open("/dev/net/tun", O_RDWR)) == -1){
		fprintf(stderr, "open %s: %s\n", ifr.ifr_name, strerror(errno));
		goto error_out;
	}
	if(ioctl(tunfd, TUNSETIFF, (void *)&ifr) == -1){
		fprintf(stderr, "ioctl TUNSETIFF %s: %s\n", ifr.ifr_name, strerror(errno));
		goto error_out;
	}

	ifconfig(ifr.ifr_name, addr);

	if(gotdev != NULL)
		strcpy(gotdev, ifr.ifr_name);

	return tunfd;

error_out:
	if(tunfd != -1)
		close(tunfd);
	return -1;
}
