/*
 *      Copyright (c) 2016 Aki Nyrhinen
 *
 *      Permission is hereby granted, free of charge, to any person obtaining a copy
 *      of this software and associated documentation files (the "Software"), to deal
 *      in the Software without restriction, including without limitation the rights
 *      to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *      copies of the Software, and to permit persons to whom the Software is
 *      furnished to do so, subject to the following conditions:
 *
 *      The above copyright notice and this permission notice shall be included in
 *      all copies or substantial portions of the Software.
 *
 *      THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *      IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *      FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *      AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *      LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *      OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *      THE SOFTWARE.
 */
#include "os.h"
#include "file.h"

int
writefile(char *path, void *data, size_t len)
{
	int fd, nwr;
	if((fd = open(path, O_WRONLY)) == -1){
		fprintf(stderr, "open(\"%s\"): %s\n", path, strerror(errno));
		return -1;
	}
	nwr = write(fd, data, len);
	if(close(fd) == -1){
		fprintf(stderr, "close(\"%s\"): %s\n", path, strerror(errno));
		return -1;
	}
	return nwr;
}

int
readfile(char *path, void *data, size_t len)
{
	int fd, nrd;
	if((fd = open(path, O_RDONLY)) == -1){
		fprintf(stderr, "open(\"%s\"): %s\n", path, strerror(errno));
		return -1;
	}
	nrd = read(fd, data, len);
	if(close(fd) == -1){
		fprintf(stderr, "close(\"%s\"): %s\n", path, strerror(errno));
		return -1;
	}
	return nrd;
}
