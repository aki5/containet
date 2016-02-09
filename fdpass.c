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

