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
#include "file.h"
#include "strsplit.h"
#include "unsocket.h"
#include "smprintf.h"
#include "seccomp.h"

#include <sys/syscall.h> /* For SYS_xxx definitions, pivot_root.. */

// not sure this is in the standard, but it is too handy for json templating to ignore.
#define json(...) #__VA_ARGS__

typedef struct Args Args;
struct Args {
	int argc;
	char **argv;
	char *root;
	char *toproot;
	char *topwork;
	char *ip4addr;
	char *identity;
	int dsock; // domain socket to switch
};

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


// printf needs a lot of stack.
static char childstack[16384];
static char buf[8192];

static void
detachbut(char *path)
{
	int i, nrd;
	if((nrd = readfile("/proc/mounts", buf, sizeof buf-1)) == -1){
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
}

static int
enterchild(void *arg){
	Args *ap = (Args *)arg;
	char ifname[256];
	int i, fd;

	ifconfig("lo", "127.0.0.1/8");
	if(ap->dsock != -1){
		char *buf;
		if((fd = tunopen(ifname, "eth0", ap->ip4addr)) == -1)
			exit(1);
		buf = smprintf(
			json({
				"add":{
					"ifname":"%s",
					"id":"%s"
				}
			}),
			ifname,
			ap->identity
		);
		if(sendfd(ap->dsock, fd, buf, strlen(buf)) == -1)
			fprintf(stderr, "sendfd fail\n");
		free(buf);
		close(ap->dsock);
		close(fd);
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

	// wow, linux programs make a lot of weird system calls, many of which I'd prefer didn't exist,
	// this part will have to wait until a lot later.
	//seccomp();

	execv(ap->argv[0], ap->argv);
	fprintf(stderr, "exec(\"%s\"): %s\n", ap->argv[0], strerror(errno));
	exit(1);
}

char *identity;
char *swtchname;


void
die(int sig)
{
	int dsock;
	if((dsock = unsocket(SOCK_STREAM, NULL, swtchname)) == -1){
		fprintf(stderr, "could not connect to switch %s\n", swtchname);
		exit(1);
	}

	char *buf;
	int len;
	buf = smprintf(
		json({
			"remove":{
				"id":"%s"
			}
		}),
		identity
	);
	len = strlen(buf);
	if(write(dsock, buf, len) != len){
		fprintf(stderr, "failed to send: '%s' to switch: %s\n", buf, strerror(errno));
	}
	free(buf);
	close(dsock);
	exit(sig);
}

static char *base62 =
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789";

char *
idstr(uint64_t id)
{

	char *str;
	int i;

	str = malloc(12);
	for(i = 10; i >= 0; i--){
		str[i] = base62[id%62];
		id /= 62;
	}
	str[11] = '\0';
	return str;
}

uint64_t
strid(char *str)
{
	uint64_t id;
	int i, j;

	id = 0;
	for(i = 0; i < 11; i++){
		for(j = 0; j < 62; j++){
			if(base62[j] == str[i])
				break;
		}
		if(j == 62){
			fprintf(stderr, "strid: bad id string\n");
			return ~(uint64_t)0;
		}
		id *= 62;
		id += j;
	}

	return id;
}

int
main(int argc, char *argv[])
{
	Args args;
	char *root;
	char *toproot;
	char *topwork;
	char *ip4addr;
	int opt, pid, status;
	int dsock;

	root = NULL;
	toproot = NULL;
	topwork = NULL;
	ip4addr = NULL;
	dsock = -1;
	while((opt = getopt(argc, argv, "r:t:w:4:s:i:")) != -1) {
		switch(opt){
		case 's':
			swtchname = optarg;
			if((dsock = unsocket(SOCK_STREAM, NULL, swtchname)) == -1){
				fprintf(stderr, "could not connect to switch %s\n", swtchname);
				exit(1);
			}
			break;
		case '4':
			ip4addr = optarg;
			break;
		case 'r':
			root = optarg;
			break;
		case 't':
			toproot = optarg;
			break;
		case 'w':
			topwork = optarg;
			break;
		case 'i':
			identity = optarg;
			break;
		default:
			fprintf(stderr, "usage: %s [-r path/to/root] [-t path/to/top-dir] [-w path/to/work-dir] [-4 ip4 address] [-s path/to/switch-sock]\n", argv[0]);
			exit(1);
		}
	}

	if(identity == NULL){
		uint64_t id;
		int rndfd;
		rndfd = open("/dev/urandom", O_RDONLY);
		if(rndfd == -1){
			fprintf(stderr, "open /dev/urandom: %s\n", strerror(errno));
			exit(1);
		}
		if(read(rndfd, &id, sizeof id) != sizeof id){
			fprintf(stderr, "read /dev/urandom: %s\n", strerror(errno));
			exit(1);
		}
		identity = idstr(id);
		if(strid(identity) != id){
			fprintf(stderr, "strid-idstr scheme is broken\n");
			exit(1);
		}
		close(rndfd);
	}

	// gives a perf kick for namespace creation and teardown.
	// having iptables around at all is a major time suck too, but we can't fix that here.
	writefile("/sys/kernel/rcu_expedited", "1", 1);

	args = (Args){argc-optind, argv+optind, root, toproot, topwork, ip4addr, identity, dsock};

	pid = clone(
		enterchild, stackalign(childstack + sizeof childstack),
		SIGCHLD |	// new process
		CLONE_NEWNS|	// new mount space
		CLONE_NEWPID|	// new pid space
		CLONE_NEWUTS|	// new uname(2), setdomainname(2) and sethostname(2)
		// these are slow to create and tear down, so try to do without them.
		CLONE_NEWIPC|	// new sysvipc name space 
		CLONE_NEWNET|	// new network namespace.
		0,
		(void*)&args
	);

	if(pid == -1){
		fprintf(stderr, "clone: %s\n", strerror(errno));
		exit(1);
	}
	if(waitpid(pid, &status, 0) == -1) {
		fprintf(stderr, "waitpid: %s\n", strerror(errno));
		die(1);
	}
	if(!WIFEXITED(status)){
		fprintf(stderr, "error: child did not exit normally\n");
		die(1);
	}
	die(0);
	return 0;
}
