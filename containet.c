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

#define json(...) #__VA_ARGS__

enum {
	MaxPorts = 1024,
	Nbuffers = 32,

	// space for maximum ipv4 + then some
	Bufsize = 64*1024,
	Camsize = 8192,

	AgeInterval = 10, // seconds
	MaxAge = 2, // maximum age of a cam entry (# of AgeIntervals)
};

enum {
	PortOpen = 0,
	PortClosing = 1,
	PortCloseWait = 2,
	PortClosed = 3,
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
	Port *port;
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
	char *nodeid;
	int fd;
	int state;
	Queue freeq;
	Queue xmitq;
};


static Cam g_cams[Camsize];
static Port ports[MaxPorts];

static pthread_mutex_t portlock;
static int aports = nelem(ports);
static int nports;
static pthread_t agethr;

static char *
portname(Port *port)
{
	static __thread char buf[128];
	snprintf(buf, sizeof buf, "%s-%s", port->nodeid, port->ifname);
	return buf;
}

static Buffer *
qget(Queue *q)
{
	Buffer *bp;
	uint32_t qtail;

	bp = NULL;
	pthread_mutex_lock(&q->lock);
	while(q->port->state == PortOpen && q->head == q->tail)
		pthread_cond_wait(&q->kick, &q->lock);
	if(q->head != q->tail){
		qtail = q->tail;
		bp = q->bufs[qtail];
		q->tail = (qtail + 1) % nelem(q->bufs);
	}
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
	int i;
	uint16_t age;
	uint8_t zeromac[6] = {0};

	for(;;){

		for(i = 0; i < Camsize; i++){
			cam = g_cams + i;
			age = __sync_fetch_and_add(&cam->age, 1) + 1;
			if(cam->port != NULL && (age >= MaxAge || cam->port->state != PortOpen)){
				fprintf(stderr, "%s: aged cam entry\n", portname(cam->port));
				cam->port = NULL;
				copymac(cam->mac, zeromac);
			}
		}
		for(i = 0; i < nports; i++){
			Port *port;
			port = ports + i;
			if(__sync_bool_compare_and_swap(&port->state, PortClosing, PortCloseWait)){
				// this close takes a long time because it tears down a network namespace.
				if(port->fd >= 0)
					close(port->fd);
				port->fd = -1;
				pthread_kill(port->xmitthr, SIGHUP);
				pthread_kill(port->recvthr, SIGHUP);
				pthread_join(port->xmitthr, NULL);
				pthread_join(port->recvthr, NULL);
				fprintf(stderr, "%s: closed fd\n", portname(ports+i));
				__sync_bool_compare_and_swap(&port->state, PortCloseWait, PortClosed);
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
		if(port->state != PortOpen){
			qput(bp->freeq, bp);
			break;
		}
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
	fprintf(stderr, "%s: reader exiting..\n", portname(port));
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
		if(bp == NULL)
			break;
		if(bp->len > 0){
			*(uint32_t *)bp->buf = 0;
			nwr = write(port->fd, bp->buf, bp->len);
			if(nwr != bp->len){
				fprintf(stderr, "%s: short write, got %d wanted %d\n", portname(port), nwr, bp->len);
				break;
			}
		}
		nref = bdecref(bp);
		if(nref == 0)
			qput(bp->freeq, bp);
	}
	fprintf(stderr, "%s: writer exiting\n", portname(port));
	return port;
}

typedef struct Ctrlconn Ctrlconn;
struct Ctrlconn {
	pthread_t thr;
	int fd;
};

static void *acceptor(void *dsockp);

static void *
ctrlhandler(void *actrl)
{
	Ctrlconn *ctrl;
	JsonRoot jsroot;
	char buf[256];
	int fd, newfd;
	int i, nrd;

	ctrl = (Ctrlconn *)actrl;
	fd = ctrl->fd;

	memset(&jsroot, 0, sizeof jsroot);
	for(;;){
		nrd = recvfd(fd, &newfd, buf, sizeof buf-1);
		if(nrd == -1){
			fprintf(stderr, "recvfd: %s\n", strerror(errno));
			break;
		}
		if(nrd == 0)
			break;
		if(nrd > 0){
			int obji;

			jsonparse(&jsroot, buf, nrd);

			obji = jsonwalk(&jsroot, 0, "add-etherfd");
			if(obji != -1 && newfd != -1){
				Port *port;
				char *ifname, *nodeid;
				int ifnamei, nodeidi;

				ifnamei = jsonwalk(&jsroot, obji, "ifname");
				if(ifnamei == -1){
					fprintf(stderr, "acceptor: add request without ifname\n");
					close(newfd);
					goto respond_err;
				}

				nodeidi = jsonwalk(&jsroot, obji, "nodeid");
				if(nodeidi == -1){
					fprintf(stderr, "acceptor: add request without nodeid\n");
					close(newfd);
					goto respond_err;
				}

				ifname = jsoncstr(&jsroot, ifnamei);
				nodeid = jsoncstr(&jsroot, nodeidi);

				pthread_mutex_lock(&portlock);
				for(i = 0; i < nports; i++){
					port = ports + i;
					if(__sync_bool_compare_and_swap(&port->state, PortClosed, PortOpen)){
						free(port->nodeid);
						free(port->ifname);
						port->ifname = ifname;
						port->nodeid = nodeid;
						port->fd = newfd;
						pthread_create(&port->recvthr, NULL, reader, port);
						pthread_create(&port->xmitthr, NULL, writer, port);
						pthread_mutex_unlock(&portlock);
						break;
					}
				}
				if(i < nports)
					goto respond_ok;

				if(nports == aports){
					fprintf(stderr, "out of ports\n");
					close(newfd);
					pthread_mutex_unlock(&portlock);
					free(ifname);
					free(nodeid);
					goto respond_err;
				}

				port = ports + nports;
				memset(port, 0, sizeof port[0]);
				pthread_mutex_init(&port->xmitq.lock, NULL);
				pthread_mutex_init(&port->freeq.lock, NULL);
				port->state = PortOpen;
				port->xmitq.port = port;
				port->freeq.port = port;
				port->ifname = ifname;
				port->nodeid = nodeid;
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
						fprintf(stderr, "%s: acceptor: could not qput\n", portname(port));
				}
				pthread_create(&port->recvthr, NULL, reader, port);
				pthread_create(&port->xmitthr, NULL, writer, port);
				__sync_fetch_and_add(&nports, 1);
				pthread_mutex_unlock(&portlock);
				goto respond_ok;
			}

			obji = jsonwalk(&jsroot, 0, "remove-etherfd");
			if(obji != -1){
				char *nodeid;
				int ncloses, nfound, nodeidi;

				nodeidi = jsonwalk(&jsroot, obji, "nodeid");
				if(nodeidi == -1){
					fprintf(stderr, "acceptor: remove request without nodeid\n");
					goto respond_err;
				}
				nodeid = jsoncstr(&jsroot, nodeidi);
				ncloses = 0;
				nfound = 0;
				for(i = 0; i < nports; i++){
					Port *port = ports + i;
					if(!strcmp(nodeid, port->nodeid)){
						if(__sync_bool_compare_and_swap(&port->state, PortOpen, PortClosing))
							ncloses++;
						nfound++;
					}
				}
				if(nfound == 0)
					fprintf(stderr, "acceptor: remove-etherfd %s: not found\n", nodeid);
				else if(ncloses == 0)
					fprintf(stderr, "acceptor: remove-etherfd %s: already state\n", nodeid);
				free(nodeid);
				goto respond_ok;
			}

			obji = jsonwalk(&jsroot, 0, "add-ctrlsock");
			if(obji != -1){
				char *nodeid;
				int nodeidi;

				if(newfd == -1){
					fprintf(stderr, "acceptor: add-ctrlsock without fd\n");
					goto respond_err;
				}
				nodeidi = jsonwalk(&jsroot, obji, "nodeid");
				if(nodeidi == -1){
					fprintf(stderr, "acceptor: add-ctrlsock request without nodeid\n");
					close(newfd);
					goto respond_err;
				}
				nodeid = jsoncstr(&jsroot, nodeidi);
				fprintf(stderr, "creating acceptor for %s\n", nodeid);

				if(listen(newfd, 5) == -1){
					fprintf(stderr, "%s: listen: %s\n", nodeid, strerror(errno));
					close(newfd);
					goto respond_err;
				}

				Ctrlconn *nctrl;
				nctrl = malloc(sizeof nctrl[0]);
				nctrl->fd = newfd;
				pthread_create(&nctrl->thr, NULL, acceptor, nctrl);
				goto respond_ok;
			}
			char msg[256];
respond_ok:
			snprintf(msg, sizeof msg, json({}));
			write(fd, msg, strlen(msg));
			continue;
respond_err:
			snprintf(msg, sizeof msg, json({"error":"error"}));
			write(fd, msg, strlen(msg));
		}
	}
	close(fd);
	free(ctrl);
	return NULL;
}

static void *
acceptor(void *actrl)
{
	Ctrlconn *ctrl;
	Ctrlconn *nctrl;
	int fd;

	ctrl = (Ctrlconn *)actrl;
	for(;;){
		if((fd = accept(ctrl->fd, NULL, NULL)) == -1){
			fprintf(stderr, "accept: %s\n", strerror(errno));
			continue;
		}
		nctrl = malloc(sizeof nctrl[0]);
		nctrl->fd = fd;
		pthread_create(&nctrl->thr, NULL, ctrlhandler, nctrl);
	}

	close(ctrl->fd);
	free(ctrl);
	return NULL;
}

static void
sigint(int sig)
{
	int oerr = errno;
	fprintf(stderr, "got sig %d\n", sig);
	errno = oerr;
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	char *swtchname;
	int opt;
	int dsock;

	sa.sa_handler = &sigint;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);
	if(sigaction(SIGHUP, &sa, NULL) == -1){
		fprintf(stderr, "%s: could not set sigint handler: %s\n", argv[0], strerror(errno));
		exit(1);
	}

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

	Ctrlconn *nctrl;
	nctrl = malloc(sizeof nctrl[0]);
	nctrl->fd = dsock;
	acceptor((void*)nctrl);

	return 0;
}
