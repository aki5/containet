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
#include <pthread.h>
#include "unsocket.h"

typedef struct Port Port;
struct Port {
	pthread_t thr;
	int fd;
	char buf[16384];
};

Port ports[128];
int aports = nelem(ports);
int nports;
pthread_t accepthr;

void *
reader(void *aport)
{
	Port *port = (Port *)aport;
	int i, nrd;

	fprintf(stderr, "reader %ld starting\n", port-ports);
	while((nrd = read(port->fd, port->buf, sizeof port->buf)) > 0){
		for(i = 0; i < nports; i++){
			if(port == ports+i)
				continue;
			if(write(ports[i].fd, port->buf, nrd) != nrd)
				fprintf(stderr, "short write to port %d: wanted %d: %s\n", i, nrd, strerror(errno));
		}
	}

	return NULL;
}

void *
acceptor(void *dsockp)
{
	char buf[32];
	int fd, newfd, dsock = *(int*)dsockp;
	int nrd;

	for(;;){
		if((fd = accept(dsock, NULL, NULL)) == -1){
			fprintf(stderr, "accept: %s\n", strerror(errno));
			continue;
		}
		nrd = recvfd(fd, &newfd, buf, sizeof buf);
		if(nrd > 0){
			if(nports == aports){
				fprintf(stderr, "out of ports\n");
				continue;
			}
			ports[nports].fd = newfd;
			pthread_create(&ports[nports].thr, NULL, reader, ports+nports);
			__sync_fetch_and_add(&nports, 1);
		}
		close(fd);
	}

	return NULL;
}

int
main(int argc, char *argv[])
{
	char *swtchname;
	int opt;
	int dsock;

	swtchname = NULL;
	while((opt = getopt(argc, argv, "s:")) != -1) {
		switch(opt){
		case 's':
			swtchname = optarg;
			break;
		default:
		caseusage:
			fprintf(stderr, "usage: %s -s path/to/switch-sock\n", argv[0]);
			exit(1);
		}
	}
	if(swtchname == NULL)
		goto caseusage;

	if((dsock = unsocket(SOCK_STREAM, swtchname, NULL)) == -1){
		fprintf(stderr, "could not post %s\n", swtchname);
		goto caseusage;
	}

	if(listen(dsock, 5) == -1){
		fprintf(stderr, "could not listen %s\n", swtchname);
		goto caseusage;
	}

	acceptor(&dsock);
//	pthread_create(&accepthr, NULL, acceptor, &dsock);

	return 0;
}
