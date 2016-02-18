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
#include "json.h"

enum {
	MaxPorts = 128,
	Nbuffers = 32,

	// space for maximum ipv4 + then some
	Bufsize = 64*1024,
	Camsize = 8192,

	AgeInterval = 5, // seconds
	MaxAge = 2, // maximum age of a cam entry (# of AgeIntervals)
};

typedef struct Buffer Buffer;
typedef struct Cam Cam;
typedef struct Port Port;
typedef struct Queue Queue;

struct Cam {
	Port *port;
	uint16_t age;
	uint8_t mac[6];
};

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
	char *ifname;
	char *id;
	int fd;
	Queue freeq;
	Queue xmitq;
};


Cam g_cams[Camsize];
Port ports[MaxPorts];

int aports = nelem(ports);
int nports;
pthread_t agethr;

static char *
portname(Port *port)
{
	static __thread char buf[128];
	snprintf(buf, sizeof buf, "%s:%s", port->id, port->ifname);
	return buf;
}

static Buffer *
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

static int
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

static int
bincref(Buffer *bp)
{
	return __sync_fetch_and_add(&bp->nref, 1) + 1;
}

static int
bdecref(Buffer *bp)
{
	return __sync_fetch_and_add(&bp->nref, -1) - 1;
}

static uint32_t
hashmac(uint8_t *buf)
{
	uint32_t a, b, c;

	a = (uint32_t)buf[0] + ((uint32_t)buf[1]<<8);
	b = (uint32_t)buf[2] + ((uint32_t)buf[3]<<8);
	c = (uint32_t)buf[4] + ((uint32_t)buf[5]<<8);

// from lookup3.c, by Bob Jenkins, May 2006, Public Domain.
// http://burtleburtle.net/bob/c/lookup3.c
#define rot32(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
	c ^= b; c -= rot32(b,14);
	a ^= c; a -= rot32(c,11);
	b ^= a; b -= rot32(a,25);
	c ^= b; c -= rot32(b,16);
	a ^= c; a -= rot32(c,4);
	b ^= a; b -= rot32(a,14);
	c ^= b; c -= rot32(b,24);
#undef rot32

	return c;
}

static int
cmpmac(uint8_t *a, uint8_t *b)
{
	int i, rv;
	rv = 0;
	for(i = 0; i < 6; i++)
		rv += a[i] != b[i];
	return rv;
}

static void
copymac(uint8_t *dst, uint8_t *src)
{
	int i;
	for(i = 0; i < 6; i++)
		dst[i] = src[i];
}

static Cam *
camlook(Cam *cams, uint8_t *mac)
{
	Cam *cam;
	uint32_t hash;
	int i;

	hash = hashmac(mac) & (Camsize-1);
	for(i = 1; i <= Camsize; i++){
		cam = cams + hash;
		if(cmpmac(cam->mac, mac) == 0 || cam->port == NULL)
			return cam;
		hash = (hash+i) & (Camsize-1);
	}

	return NULL;
}

#if 0
static void
pktdump(Buffer *bp)
{
	static pthread_mutex_t lock;
	uint8_t *pkt;
	int i;

	pthread_mutex_lock(&lock);
	pkt = bp->buf;
	for(i = 0; i < bp->len; i++){
		if((i & 15) == 0)
			fprintf(stderr, "%4d:", i);
		fprintf(stderr, " %02x", pkt[i]);
		if((i & 15) == 15)
			fprintf(stderr, "\n");
	}
	if((i & 15) != 0)
		fprintf(stderr, "\n");
	pthread_mutex_unlock(&lock);
}
#endif

static void *
agecam(void *aux)
{
	Cam *cam;
	uint32_t i;
	uint16_t age;
	uint8_t zeromac[6] = {0};

	for(;;){
		for(i = 0; i < Camsize; i++){
			cam = g_cams + i;
			age = __sync_fetch_and_add(&cam->age, 1) + 1;
			if(cam->port != NULL && age >= MaxAge){
				fprintf(stderr, "aged port %s\n", portname(cam->port));
				cam->port = NULL;
				copymac(cam->mac, zeromac);
			}
		}
		sleep(AgeInterval);
	}

	return aux;
}

static void *
reader(void *aport)
{
	Buffer *bp;
	Port *port;
	Cam *cam;
	uint8_t *dstmac, *srcmac;
	int i, nrd, nref;

	port = (Port *)aport;
	for(;;){
		bp = qget(&port->freeq);

		nrd = read(port->fd, bp->buf, bp->cap);
		bp->len = nrd;

		dstmac = (uint8_t *)bp->buf + 4;
		srcmac = (uint8_t *)bp->buf + 10;

		nref = 0;
		cam = camlook(g_cams, dstmac);
		if(cam != NULL && cam->port != NULL){
			// port found in cam, forward only there...
			nref = bincref(bp);
			if(qput(&cam->port->xmitq, bp) == -1)
				nref = bdecref(bp);
		} else {
			// broadcast..
			for(i = 0; i < nports; i++){
				if(port == (ports+i))
					continue;
				nref = bincref(bp);
				if(qput(&ports[i].xmitq, bp) == -1)
					nref = bdecref(bp);
			}
		}

		// teach the switch about the source address we just saw
		cam = camlook(g_cams, srcmac);
		if(cam != NULL){
			// always update the port, so if an address moves to a different port
			// the cam will point to that port right away.
			copymac(cam->mac, srcmac);
			cam->age = 0;
			cam->port = port;
		} else {
			fprintf(stderr, "cam presumably full..\n");
		}

		// ref is zero after the forward loop. it didn't go anywhere, so drop it.
		if(nref == 0)
			qput(bp->freeq, bp);
	}
	return port;
}

static void *
writer(void *aport)
{
	Port *port = (Port *)aport;
	Buffer *bp;
	int nwr, nref;

	for(;;){
		bp = qget(&port->xmitq);
		if(bp->len > 0){
			*(uint32_t *)bp->buf = 0;
			nwr = write(port->fd, bp->buf, bp->len);
			if(nwr != bp->len){
				fprintf(stderr, "short write on port %s: %d wanted %d\n", portname(port), nwr, bp->len);
			}
		}
		nref = bdecref(bp);
		if(nref == 0)
			qput(bp->freeq, bp);
	}
	return port;
}


static void *
acceptor(void *dsockp)
{
	JsonRoot jsroot;
	char buf[256];
	int fd, newfd, dsock = *(int*)dsockp;
	int i, nrd;

	memset(&jsroot, 0, sizeof jsroot);
	for(;;){
		if((fd = accept(dsock, NULL, NULL)) == -1){
			fprintf(stderr, "accept: %s\n", strerror(errno));
			continue;
		}
		nrd = recvfd(fd, &newfd, buf, sizeof buf-1);
		if(nrd > 0){
			int addi, remi;

			jsonparse(&jsroot, buf, nrd);

			addi = jsonwalk(&jsroot, 0, "add");
			if(addi != -1 && newfd != -1){
				Port *port;
				char *ifname, *id;
				int ifnamei, idi;

				if(nports == aports){
					fprintf(stderr, "out of ports\n");
					continue;
				}

				ifnamei = jsonwalk(&jsroot, addi, "ifname");
				if(ifnamei == -1){
					fprintf(stderr, "acceptor: add request without ifname\n");
					continue;
				}

				idi = jsonwalk(&jsroot, addi, "id");
				if(idi == -1){
					fprintf(stderr, "acceptor: add request without id\n");
					continue;
				}

				ifname = jsoncstr(&jsroot, ifnamei);
				id = jsoncstr(&jsroot, idi);

				port = ports + nports;
				pthread_mutex_init(&port->xmitq.lock, NULL);
				pthread_mutex_init(&port->freeq.lock, NULL);
				port->ifname = ifname;
				port->id = id;
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
						fprintf(stderr, "acceptor: could not qput to port %s\n", portname(port));
				}
				pthread_create(&port->recvthr, NULL, reader, port);
				pthread_create(&port->xmitthr, NULL, writer, port);
				__sync_fetch_and_add(&nports, 1);
			}

			remi = jsonwalk(&jsroot, 0, "remove");
			if(remi != -1){
			}
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

	pthread_create(&agethr, NULL, agecam, NULL);
	acceptor(&dsock);

	return 0;
}
