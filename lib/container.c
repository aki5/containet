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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <linux/ioctl.h>
#include <linux/if_tun.h>
#include <linux/if.h>
#include <linux/sockios.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/syscall.h> /* For SYS_xxx definitions, pivot_root.. */

#define stackalign(x) (void*)((uintptr_t)(x) & ~(uintptr_t)0xf)
#define nelem(x) (int)(sizeof(x)/sizeof(x[0]))

#include "file.h"
#include "unsocket.h"
#include "strsplit.h"
#include "container.h"
#include "tun.h"
#include "smprintf.h"

// not sure this is in the standard, but it is too handy for json templating to ignore.
#define json(...) #__VA_ARGS__


static struct {
	char *path;
	mode_t mode;
	int major;
	int minor;
} devices[] = {
	{"/dev/console", S_IFCHR | 0622, 5, 1},
	{"/dev/null", S_IFCHR | 0666, 1, 3},
	{"/dev/zero", S_IFCHR | 0666, 1, 5},
	{"/dev/ptmx", S_IFCHR | 0666, 5, 2},
	{"/dev/tty", S_IFCHR | 0666, 5, 0},
	{"/dev/random", S_IFCHR | 0444, 1, 8},
	{"/dev/urandom", S_IFCHR | 0444, 1, 9},
};

// manpages are out of date, /usr/include/sys/mount.h is a good resource
// for available mount flags.
static struct {
	char *from;
	char *to;
	char *type;
	long flags;
} mounts[] = {
	{"proc", "/proc", "proc", 0},
	{"tmpfs", "/dev", "tmpfs", 0},
	{"devpts", "/dev/pts", "devpts", 0},
};



static void
detachbut(char *path)
{
	char *buf;
	int i, nrd, bufsize;

	bufsize = 16384;
	buf = malloc(bufsize);
	if((nrd = readfile("/proc/mounts", buf, bufsize-1)) == -1){
		fprintf(stderr, "read /proc/mounts: %s\n", strerror(errno));
		exit(1);
	}

	buf[nrd] = '\0';
	char *lines[128];
	int nlin;
	nlin = strsplit(buf, '\n', lines, nelem(lines));
	for(i = nlin-1; i >= 0; i--){
		char *toks[16];
		int ntok;
		ntok = strsplit(lines[i], ' ', toks, nelem(toks));
		if(ntok >= 2 && strcmp(toks[1], path) != 0){
			if(umount2(toks[1], MNT_DETACH) == -1){
				fprintf(stderr, "umount %s: %s\n", toks[1], strerror(errno));
				exit(1);
			}
		}
	}
	free(buf);
}

static int
enterchild(void *arg){
	Args *ap = (Args *)arg;
	char ifname[256];
	int i;

	sethostname(ap->identity, strlen(ap->identity));

	ifconfig("lo", "127.0.0.1/8");

	if(ap->ctrlsock != -1){
		char *buf;
		int tunfd;

		if((tunfd = tunopen(ifname, "eth0", ap->ip4addr)) == -1)
			exit(1);
		buf = smprintf(
			json({
				"add-etherfd":{
					"ifname":"%s",
					"nodeid":"%s"
				}
			}),
			ifname,
			ap->identity
		);
		if(sendfd(ap->ctrlsock, tunfd, buf, strlen(buf)) == -1)
			fprintf(stderr, "sendfd fail\n");
		close(tunfd);
		free(buf);

		// read response
		buf = malloc(256);
		read(ap->ctrlsock, buf, 256);
		free(buf);

	}

	/*
	 *	remount root as a slave before we do anything. stupid systemd made it shared
	 *	by default so namespaces don't really work without this at all.
	 */
	if(mount("none", "/", "", MS_REC | MS_SLAVE, NULL) == -1){
		fprintf(stderr, "mount(\"/\"): %s\n", strerror(errno));
		exit(1);
	}

	if(ap->root != NULL){
		/*
		 *	use overlayfs when topwork and toproot are supplied,
		 *	use aufs when only toproot is supplied.
		 *	use chroot when neither is supplied
		 */
		if(ap->toproot != NULL){
			char tmp[256];
			if(mkdir(ap->toproot, 0777) == -1 && errno != EEXIST){
				fprintf(stderr, "mkdir(\"%s\"): %s\n", ap->toproot, strerror(errno));
				exit(1);
			}
			if(ap->topwork != NULL){
				if(mkdir(ap->topwork, 0777) == -1 && errno != EEXIST){
					fprintf(stderr, "mkdir(\"%s\"): %s\n", ap->topwork, strerror(errno));
					exit(1);
				}
				snprintf(tmp, sizeof tmp,"lowerdir=%s,upperdir=%s,workdir=%s", ap->root, ap->toproot, ap->topwork);
				if(mount("overlay", ap->root, "overlay", 0, tmp) == -1){
					fprintf(stderr, "mount(\"%s\") overlay: %s\n", ap->root, strerror(errno));
					exit(1);
				}
			} else {
				snprintf(tmp, sizeof tmp,"br:%s:%s=ro", ap->toproot, ap->root);
				if(mount("none", ap->root, "aufs", 0, tmp) == -1){
					fprintf(stderr, "mount(\"%s\") aufs: %s\n", ap->root, strerror(errno));
					exit(1);
				}
			}
			snprintf(tmp, sizeof tmp, "%s/mnt", ap->root);
			if(mkdir(tmp, 0777) == -1 && errno != EEXIST){
				fprintf(stderr, "mkdir(\"%s\"): %s\n", tmp, strerror(errno));
				exit(1);
			}
			if(syscall(SYS_pivot_root, ap->root, tmp) == -1){
				fprintf(stderr, "pivot_root(\"%s\", \"%s\"): %s\n", ap->root, tmp, strerror(errno));
				exit(1);
			}
			if(umount2("/mnt", MNT_DETACH) == -1){
				fprintf(stderr, "umount %s: %s\n", "/mnt", strerror(errno));
				exit(1);
			}
		} else {
			detachbut("/");
			if(chroot(ap->root) == -1){
				fprintf(stderr, "chroot(\"%s\"): %s\n", ap->root, strerror(errno));
				exit(1);
			}
		}
		if(chdir("/") == -1){
			fprintf(stderr, "could not chdir to /: %s\n", strerror(errno));
			exit(1);
		}
	}

	for(i = 0; i < nelem(mounts); i++){
		if(mkdir(mounts[i].to, 0777) == -1 && errno != EEXIST){
			fprintf(stderr, "mkdir(\"%s\"): %s\n", mounts[i].to, strerror(errno));
			exit(1);
		}
		if(mount(mounts[i].from, mounts[i].to, mounts[i].type, mounts[i].flags, NULL) == -1){
			fprintf(stderr, "mount(\"%s\"): %s\n", mounts[i].to, strerror(errno));
			exit(1);
		}
	}

	for(i = 0; i < nelem(devices); i++){
		if(mknod(devices[i].path, devices[i].mode, makedev(devices[i].major, devices[i].minor)) == -1){
			fprintf(stderr, "mknod(\"%s\"): %s\n", devices[i].path, strerror(errno));
			exit(1);
		}
	}


	if(ap->postname != NULL){
		char *buf;
		int postfd;

		if((postfd = unsocket(SOCK_STREAM, ap->postname, NULL)) == -1){
			fprintf(stderr, "could not post %s\n", ap->postname);
			exit(1);
		}

		buf = smprintf(
			json({
				"add-ctrlsock":{
					"nodeid":"%s"
				}
			}),
			ap->identity
		);
		if(sendfd(ap->ctrlsock, postfd, buf, strlen(buf)) == -1)
			fprintf(stderr, "sendfd fail\n");
		free(buf);

		// read response
		buf = malloc(256);
		read(ap->ctrlsock, buf, 256);
		free(buf);

		close(postfd);
	}

	if(ap->ctrlsock != -1)
		close(ap->ctrlsock);


	// wow, linux programs make a lot of weird system calls, many of which I'd prefer didn't exist,
	// this part will have to wait until a lot later.
	//seccomp();

	execv(ap->argv[0], ap->argv);
	fprintf(stderr, "exec(\"%s\"): %s\n", ap->argv[0], strerror(errno));
	exit(1);
}

int
runcontainer(Args *args, int cloneflags)
{
	char *childstack;
	int stacksize;
	int pid;

	stacksize = 16384;
	childstack = malloc(stacksize);
	pid = clone(
		enterchild, stackalign(childstack + stacksize),
		cloneflags,
		(void*)args
	);

	return pid;
}
