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
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include "json.h"

int
astdump(JsonAst *ast, int off, char *buf, char *path)
{
	int i, j;
	int pathoff;

	pathoff = strlen(path);
	switch(ast[off].type){
	case '{':
		path[pathoff++] = '.';
		i = off+1;
		for(;;){
			if(ast[i].type == '}')
				break;
			if(ast[i].type != JsonString){
				fprintf(stderr, "astdump: bad type '%c', expecting string for map key", ast[i].type);
				fwrite(buf+ast[i].off, ast[i].len, 1, stderr);
				fprintf(stderr, "\n");
				return -1;
			}
			memcpy(path + pathoff, buf+ast[i].off, ast[i].len);
			path[pathoff+ast[i].len] = '\0';
			i = ast[i].next;
			if(astdump(ast, i, buf, path) == -1)
				return -1;
			i = ast[i].next;

		}
		break;

	case '[':
		j = 0;
		i = off+1;
		for(;;){
			if(ast[i].type == ']')
				break;
			sprintf(path + pathoff, "[%d]", j);
			if(astdump(ast, i, buf, path) == -1)
				return -1;
			i = ast[i].next;
			j++;
		}
		break;

	case '0': // number
	case '"': // string
	case 'a': // symbol
		fprintf(stdout, "%s:", path);
		fwrite(buf+ast[off].off, ast[off].len, 1, stdout);
		fprintf(stdout, "\n");
		break;
	}
	return 0;
}

int
main(int argc, char *argv[])
{
	JsonAst *ast;
	char *buf;
	int astoff, astlen;
	int i, fd;
	int off, len, nrd;

	ast = NULL;
	buf = NULL;

	for(i = 1; i < argc; i++){
		struct stat st;

		if((fd = open(argv[i], O_RDONLY)) == -1){
			fprintf(stderr, "open %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		if(fstat(fd, &st) == -1){
			fprintf(stderr, "stat %s: %s\n", argv[i], strerror(errno));
			close(fd);
			continue;
		}

		buf = realloc(buf, st.st_size);
		nrd = read(fd, buf, st.st_size);
		if(nrd != st.st_size){
			fprintf(stderr, "short read %s: got %d need %ld %s\n", argv[i], nrd, st.st_size, strerror(errno));
			close(fd);
			continue;
		}
		close(fd);


		/* first parse to determine length.. */
		off = 0;
		len = nrd;
		astlen = 0;
		jsonsetname(argv[i]);
		jsonparse(NULL, &astlen, 0, buf, &off, &len);

		/* malloc ast, re-parse to fill it */
		ast = realloc(ast, astlen * sizeof ast[0]);
		off = 0;
		len = nrd;
		astoff = 0;
		jsonsetname(argv[i]);
		jsonparse(ast, &astoff, astlen, buf, &off, &len);


		//char path[512] = {0};
		//astdump(ast, 0, buf, path);
	}
/*
	for(i = 0; i < astoff; i = ast[i].next){
		printf("'%c':", ast[i].type);
		switch(ast[i].type){
		case JsonNumber: case JsonString: case JsonSymbol:
			fwrite(buf+ast[i].off, ast[i].len, 1, stdout);
		}
		printf("\n");
	}
*/
	return 0;
}
