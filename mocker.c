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

// example use: ./mocker ubuntu:trusty

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <curl/curl.h>
#include "json.h"
#include "smprintf.h"

typedef struct Chunk Chunk;
struct Chunk {
	FILE *fp;
	char *buf;
	size_t len;
	size_t cap;
};

static size_t
cnkbwrite(void *data, size_t elsize, size_t numel, void *acnk)
{
	Chunk *cnk = (Chunk *)acnk;
	size_t len = elsize * numel;

	if(cnk->len + len > cnk->cap){
		cnk->cap += cnk->len + len;
		cnk->buf = realloc(cnk->buf, cnk->cap);
	}
	if(cnk->buf == NULL){
		fprintf(stderr, "out of memory");
		abort();
	}
	memmove(cnk->buf + cnk->len, data, len);
	cnk->len += len;
	return len;
}

static size_t
cnkheader(char *buffer, size_t size, size_t nitems, void *acnk)
{
	char *tag = "Content-Length:";
	Chunk *cnk = (Chunk *)acnk;
	size_t len;

	len = size *nitems;
	if(len >= strlen(tag) && memcmp(buffer, tag, strlen(tag)) == 0){
		cnk->len = 0;
		cnk->cap = (size_t)strtol(buffer + strlen(tag) + 1, NULL, 10);
		//fprintf(stderr, "cnkheader: cap %zd\n", cnk->cap);
	}

	return nitems * size;
}

static size_t
cnkfwrite(void *data, size_t elsize, size_t numel, void *acnk)
{
	Chunk *cnk = (Chunk *)acnk;
	cnk->len += elsize*numel;
	//fprintf(stderr, "%6.2f %%\n", (100.0*cnk->len)/cnk->cap);
	return fwrite(data, elsize, numel, cnk->fp);
}

static void
defrog(char *str)
{
	for(;*str != '\0';str++){
		if(*str >= 'a' && *str <= 'z')
			continue;
		if(*str >= 'A' && *str <= 'Z')
			continue;
		if(*str >= '0' && *str <= '9')
			continue;
		if(*str == '.')
			continue;
		*str = '_';
	}
}

static int xflag;

int
main(int argc, char *argv[])
{
	CURL *curl;
	CURLcode res;
	char *image;
	char *tag;
	int opt;

	while((opt = getopt(argc, argv, "xC:")) != -1) {
		switch(opt){
		case 'x':
			xflag++;
			break;
		case 'C':
			if(chdir(optarg) == -1){
				fprintf(stderr, "chdir '%s': %s\n", optarg, strerror(errno));
				exit(1);
			}
			break;
		default:
		caseusage:
			fprintf(stderr, "usage: %s [-x extract-to] docker-vendor/image-name\n", argv[0]);
			exit(1);
		}
	}

	if(optind >= argc)
		goto caseusage;

	if(strchr(argv[optind], '/') != NULL){
		image = argv[optind];
	} else {
		image = smprintf("library/%s", argv[optind]);
	}

	if((tag = strchr(image, ':')) != NULL){
		*tag = '\0';
		tag++;
	} else {
		tag = "latest";
	}

	curl = curl_easy_init();
	if(curl == NULL){
		fprintf(stderr, "could not init curl\n");
		exit(1);
	}

	Chunk cnk;
	memset(&cnk, 0, sizeof cnk);

	char *authurl;
	authurl = smprintf("https://auth.docker.io/token?service=registry.docker.io&scope=repository:%s:pull", image);
	curl_easy_setopt(curl, CURLOPT_URL, authurl);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	/* coverity[bad_sizeof] */
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (void*)cnkbwrite);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cnk);
	res = curl_easy_perform(curl);
	if(res != CURLE_OK){
		fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
		exit(1);
	}
	free(authurl);

	/* this really belongs in a library.. and we need a JsonRoot type */
	JsonRoot root;
	JsonAst *ast;
	int off;

	memset(&root, 0, sizeof root);
	jsonparse(&root, cnk.buf, cnk.len);
	ast = root.ast.buf;

	off = jsonwalk(&root, 0, "token");
	if(off == -1){
		fprintf(stderr, "json: no 'token' field\n");
		exit(1);
	}

	char *token;
	token = jsoncstr(&root, off);
	if(token == NULL){
		fprintf(stderr, "json: bad value for 'token'\n");
		exit(1);
	}

	struct curl_slist *hdrlist;
	hdrlist = curl_slist_append(NULL, smprintf("Authorization: Bearer %s", token));
	res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrlist);

	char *manifesturl;
	manifesturl = smprintf("https://registry.hub.docker.com/v2/%s/manifests/%s", image, tag);
	curl_easy_setopt(curl, CURLOPT_URL, manifesturl);
	cnk.len = 0;
	res = curl_easy_perform(curl);
	if(res != CURLE_OK){
		fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
		exit(1);
	}

	free(manifesturl);
	free(token);

	//fwrite(cnk.buf, 1, cnk.len, stdout);

	jsonparse(&root, cnk.buf, cnk.len);
	ast = root.ast.buf;

	off = jsonwalk(&root, 0, "fsLayers");
	if(off == -1){
		fprintf(stderr, "json: no 'fsLayers' field\n");
		exit(1);
	}

	if(ast[off].type != '['){
		fprintf(stderr, "json: 'fsLayers' is '%c', not an array\n", ast[off].type);
		exit(1);
	}
	off++;
	int blobno = 0;
	while(ast[off].type != ']'){
		char *blobsum;
		int bi;

		bi = jsonwalk(&root, off, "blobSum");
		if(bi == -1){
			fprintf(stderr, "json: no 'blobSum' field\n");
			exit(1);
		}

		blobsum = jsoncstr(&root, bi);
		if(blobsum == NULL){
			fprintf(stderr, "json: bad value for 'blobSum'\n");
			exit(1);
		}

		Chunk blobcnk;
		char *bloburl;
		FILE *fp;

		memset(&blobcnk, 0, sizeof blobcnk);

		char *blobfile;
		blobfile = smprintf("%02d_%s_%s_%s.tar.gz", blobno, blobsum, image, tag);
		defrog(blobfile);
		printf("%s\n", blobfile);
		fp = fopen(blobfile, "wb");
		free(blobfile);

		bloburl = smprintf("https://registry.hub.docker.com/v2/%s/blobs/%s", image, blobsum);
		blobcnk.fp = fp;
		curl_easy_setopt(curl, CURLOPT_URL, bloburl);
		/* coverity[bad_sizeof] */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, (void*)cnkfwrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &blobcnk);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		/* coverity[bad_sizeof] */
		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, (void*)cnkheader);
		curl_easy_setopt(curl, CURLOPT_HEADERDATA, &blobcnk);

		res = curl_easy_perform(curl);
		if(res != CURLE_OK){
			fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
			exit(1);
		}
		fclose(fp);
		free(bloburl);
		free(blobsum);

		off = ast[off].next;
		blobno++;
	}


	curl_easy_cleanup(curl);
	curl_slist_free_all(hdrlist);

	return 0;
}
