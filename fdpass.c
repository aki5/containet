#include <string.h>
#include <sys/socket.h>


int
sendfd(int fd, int passfd, char *buf, int len)
{
	struct msghdr msg;
	struct iovec io = {.iov_base = buf, .iov_len = len};
	struct cmsghdr *cmsg;
	char cbuf[CMSG_SPACE(sizeof passfd)];

	memset(&msg, 0, sizeof msg);
	memset(cbuf, 0, sizeof cbuf);
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof cbuf;

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof passfd);
	memcpy(CMSG_DATA(cmsg), &passfd, sizeof passfd);
	msg.msg_controllen = cmsg->cmsg_len;

	return sendmsg(fd, &msg, 0);
}

int
recvfd(int fd, int *passfdp, char *buf, int len)
{
	struct msghdr msg;
	struct iovec io = {.iov_base = buf, .iov_len = len};
	struct cmsghdr *cmsg;
	char cbuf[CMSG_SPACE(sizeof *passfdp)];
	int nrd;

	memset(&msg, 0, sizeof msg);
	memset(cbuf, 0, sizeof cbuf);
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;

	msg.msg_control = cbuf;
	msg.msg_controllen = sizeof cbuf;

	if((nrd = recvmsg(fd, &msg, 0)) == -1)
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	memcpy(passfdp, CMSG_DATA(cmsg), sizeof *passfdp);

	return nrd;
}

/*
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int
main(void)
{
	struct sockaddr_un dst;
	struct sockaddr_un src;
	int fd;

	memset(&src, 0, sizeof src);
	memset(&dst, 0, sizeof dst);
	if((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) == -1){
		fprintf(stderr, "socket: %s\n", strerror(errno));
		goto err_out;
	}

	src.sun_family = AF_UNIX;
	snprintf(src.sun_path, sizeof src.sun_path-1, "/tmp/pktswtch-client-%d.sock", getpid());
	if(bind(fd, (struct sockaddr*)&src, sizeof src) == -1){
		fprintf(stderr, "bind: %s\n", strerror(errno));
		goto err_out;
	}

	dst.sun_family = AF_UNIX;
	strncpy(dst.sun_path, "/tmp/pktswtch.sock", sizeof dst.sun_path-1);

	if(connect(fd, (struct sockaddr*)&dst, sizeof dst) == -1){
		fprintf(stderr, "connect: %s\n", strerror(errno));
		goto err_out;
	}

	if(sendfd(fd, 0, "hello", 5) != 5){
		fprintf(stderr, "sendfd: %s\n", strerror(errno));
		goto err_out;
	}

	unlink(src.sun_path);
	return 0;
err_out:
	unlink(src.sun_path);
	exit(1);
}
*/