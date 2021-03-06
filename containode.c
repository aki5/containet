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
#include "container.h"

#include <sys/syscall.h> /* For SYS_xxx definitions, pivot_root.. */

// not sure this is in the standard, but it is too handy for json templating to ignore.
#define json(...) #__VA_ARGS__

static char *authtoken;
static char *swtchname;
static char *identity;

static void
die(int sig)
{
	if(swtchname != NULL){
		int ctrlsock;
		if((ctrlsock = unsocket(SOCK_STREAM, NULL, swtchname)) == -1){
			fprintf(stderr, "could not connect to switch %s\n", swtchname);
			exit(1);
		}

		char *buf;
		int len;
		buf = smprintf(
			json({
				"authtoken": "%s",
				"remove-etherfd":{
					"nodeid":"%s"
				}
			}),
			authtoken,
			identity
		);
		len = strlen(buf);
		if(write(ctrlsock, buf, len) != len){
			fprintf(stderr, "failed to send: '%s' to switch: %s\n", buf, strerror(errno));
		}
		free(buf);

		// read response
		buf = malloc(256);
		if(read(ctrlsock, buf, 256) == -1)
			fprintf(stderr, "failed to read response: %s\n", strerror(errno));
		free(buf);

		close(ctrlsock);
	}
	exit(sig);
}

static char *base62 =
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789";

static char *
idstr(uint64_t id)
{
	char *str = malloc(12);
	for(int i = 10; i >= 0; i--){
		str[i] = base62[id%62];
		id /= 62;
	}
	str[11] = '\0';
	return str;
}

static uint64_t
strid(char *str)
{
	uint64_t id = 0;
	for(size_t i = 0; i < 11; i++){
		size_t j;
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
	char *root = NULL;
	char *toproot = NULL;
	char *ip4addr = NULL;
	char *postname = NULL;
	int ctrlsock = -1;
	int Cflag = 0;

	int cloneflags =
		SIGCHLD |	// new process
		CLONE_NEWNS|	// new mount space
		CLONE_NEWPID|	// new pid space
		CLONE_NEWUTS|	// new uname(2), setdomainname(2) and sethostname(2)
		CLONE_NEWIPC|	// new sysvipc name space
		CLONE_NEWNET;	// new network namespace

	int opt, status;
	while((opt = getopt(argc, argv, "r:t:w:4:s:i:NIp:a:")) != -1) {
		switch(opt){
		case 's':
			swtchname = optarg;
			if((ctrlsock = unsocket(SOCK_STREAM, NULL, swtchname)) == -1){
				fprintf(stderr, "could not connect to switch %s\n", swtchname);
				exit(1);
			}
			break;
		case 'p':
			postname = optarg;
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
		case 'i':
			identity = optarg;
			break;
		case 'N':
			cloneflags &= ~CLONE_NEWNET;
			break;
		case 'I':
			cloneflags &= ~CLONE_NEWIPC;
			break;
		case 'C':
			Cflag++;
			break;
		case 'a':
			authtoken = optarg;
			break;
		default:
			fprintf(stderr, "usage: %s [-a authtoken] [-i identity] [-r path/to/root] [-t path/to/top-dir] [-4 ip4 address] [-s path/to/switch-sock] [-p where/to/post/ctrl-sock] [-I] [-N] [-C]\n", argv[0]);
			exit(1);
		}
	}

	// if toproot specified and is of root.foobar format, take the part after '.' as identity.
	if(identity == NULL && root != NULL && toproot != NULL){
		identity = strchr(toproot, '.');
		if(identity != NULL && (identity-toproot == (ptrdiff_t)strlen(root)) && !memcmp(toproot, root, identity-toproot)){
			identity++;
		} else {
			identity = NULL;
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

	if(Cflag == 0 && root != NULL && toproot == NULL){
		toproot = smprintf("%s.%s", root, identity);
	}

	// hack..
	if(authtoken == NULL)
		authtoken = identity;

	// gives a perf kick for namespace creation and teardown.
	// having iptables around at all is a major time suck too, but we can't fix that here.
	writefile("/sys/kernel/rcu_expedited", "1", 1);

	Args args = (Args){
		.argc = argc-optind,
		.argv = argv+optind,
		.environ = NULL,
		.root = root,
		.toproot = toproot,
		.ip4addr = ip4addr,
		.identity = identity,
		.ctrlsock = ctrlsock,
		.postname = postname,
		.authtoken = authtoken,
	};

	int pid = runcontainer(&args, cloneflags);
	if(pid == -1){
		fprintf(stderr, "runcontainer: %s\n", strerror(errno));
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
	cleancontainer(&args);
	fprintf(stderr, "fs modifications saved to %s\n", args.toproot);
	die(0);
	return 0;
}
