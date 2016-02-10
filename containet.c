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

enum {
	MaxPorts = 128,
	Nbuffers = 128,
	Bufsize = 16384,
};

typedef struct Port Port;
typedef struct Queue Queue;
typedef struct Buffer Buffer;

struct Buffer {
	Queue *freeq;
	void *buf;
	int len;
	int cap;
	int nref;
};

struct Queue {
	pthread_cond_t kick;
	pthread_mutex_t lock;
	uint32_t head;
	uint32_t tail;
	Buffer *bufs[2*Nbuffers];
};

struct Port {
	pthread_t recvthr;
	pthread_t xmitthr;
	int fd;
	Queue freeq;
	Queue xmitq;
};

Port ports[128];
int aports = nelem(ports);
int nports;
pthread_t accepthr;

Buffer *
qget(Queue *q)
{
	Buffer *bp;
	uint32_t qtail;

	bp = NULL;
	pthread_mutex_lock(&q->lock);
	while(q->head == q->tail){
		pthread_cond_wait(&q->kick, &q->lock);
	}
	qtail = q->tail;
	bp = q->bufs[qtail];
	q->tail = (qtail + 1) % nelem(q->bufs);
	pthread_mutex_unlock(&q->lock);

	return bp;
}

int
qput(Queue *q, Buffer *bp)
{
	uint32_t qhead;

	pthread_mutex_lock(&q->lock);
	qhead = q->head;
	if((qhead - q->tail) < nelem(q->bufs)-1){
		q->bufs[qhead] = bp;
		q->head = (qhead + 1) % nelem(q->bufs);
		// signal while holding lock, so we don't lose wakeups.
		pthread_cond_signal(&q->kick);
		pthread_mutex_unlock(&q->lock);
		return 0;
	}
	pthread_mutex_unlock(&q->lock);

	return -1;
}

int
bincref(Buffer *bp)
{
	return __sync_fetch_and_add(&bp->nref, 1) + 1;
}

int
bdecref(Buffer *bp)
{
	return __sync_fetch_and_add(&bp->nref, -1) - 1;
}


void *
reader(void *aport)
{
	Port *port = (Port *)aport;
	Buffer *bp;
	int i, nrd, nref;

	for(;;){
		bp = qget(&port->freeq);

		nrd = read(port->fd, bp->buf, bp->cap);
		bp->len = nrd;

		nref = 0;
		for(i = 0; i < nports; i++){
			if(port == (ports+i))
				continue;
			nref = bincref(bp);
			if(qput(&ports[i].xmitq, bp) == -1)
				nref = bdecref(bp);
		}

		// ref is zero after the forward loop. it didn't go anywhere, so drop it.
		if(nref == 0)
			qput(bp->freeq, bp);
	}
	return port;
}

void *
writer(void *aport)
{
	Port *port = (Port *)aport;
	Buffer *bp;
	int nwr, nref;

	for(;;){
		bp = qget(&port->xmitq);
		if(bp->len > 0){
			nwr = write(port->fd, bp->buf, bp->len);
			if(nwr != bp->len){
				fprintf(stderr, "short write on port %ld: %d wanted %d\n", port - ports, nwr, bp->len);
			}
		}
		nref = bdecref(bp);
		if(nref == 0)
			qput(bp->freeq, bp);
	}
	return port;
}


void *
acceptor(void *dsockp)
{
	char buf[32];
	int fd, newfd, dsock = *(int*)dsockp;
	int i, nrd;

	for(;;){
		if((fd = accept(dsock, NULL, NULL)) == -1){
			fprintf(stderr, "accept: %s\n", strerror(errno));
			continue;
		}
		nrd = recvfd(fd, &newfd, buf, sizeof buf-1);
		if(nrd > 0){
			Port *port;
			if(nports == aports){
				fprintf(stderr, "out of ports\n");
				continue;
			}
			port = ports + nports;
			pthread_mutex_init(&port->xmitq.lock, NULL);
			pthread_mutex_init(&port->freeq.lock, NULL);
			port->fd = newfd;
			for(i = 0; i < Nbuffers; i++){
				Buffer *bp;
				bp = malloc(sizeof bp[0]);
				memset(bp, 0, sizeof bp[0]);
				bp->buf = malloc(Bufsize);
				bp->len = 0;
				bp->cap = Bufsize;
				bp->freeq = &port->freeq;
				if(qput(bp->freeq, bp) == -1)
					fprintf(stderr, "acceptor: could not qput to port %ld\n", port - ports);
			}
			pthread_create(&port->recvthr, NULL, reader, port);
			pthread_create(&port->xmitthr, NULL, writer, port);

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
