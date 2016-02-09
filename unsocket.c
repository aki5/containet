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
#include <unistd.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int
unsocket(char *srcpath, char *dstpath)
{
	struct sockaddr_un dst;
	struct sockaddr_un src;
	int fd;

	memset(&src, 0, sizeof src);
	memset(&dst, 0, sizeof dst);
	if((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) == -1){
		fprintf(stderr, "unsocket: socket: %s\n", strerror(errno));
		goto err_out;
	}

	src.sun_family = AF_UNIX;
	strncpy(src.sun_path, srcpath, sizeof src.sun_path-1);
	if(bind(fd, (struct sockaddr*)&src, sizeof src) == -1){
		fprintf(stderr, "unsocket: bind %s: %s\n", src.sun_path, strerror(errno));
		goto err_out;
	}

	dst.sun_family = AF_UNIX;
	strncpy(dst.sun_path, dstpath, sizeof dst.sun_path-1);
	if(connect(fd, (struct sockaddr*)&dst, sizeof dst) == -1){
		fprintf(stderr, "unsocket: connect %s: %s\n", dst.sun_path, strerror(errno));
		goto err_out;
	}

	return fd;

err_out:
	if(fd != -1)
		close(fd);
	return -1;
}
