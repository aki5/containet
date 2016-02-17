
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>
#include "json.h"
#include "smprintf.h"

typedef struct Chunk Chunk;
struct Chunk {
	char *buf;
	size_t len;
	size_t cap;
};

static size_t
cnkappend(void *data, size_t elsize, size_t numel, void *acnk)
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
	memcpy(cnk->buf + cnk->len, data, len);
	cnk->len += len;
	return len;
}

int
main(int argc, char *argv[])
{
	CURL *curl;
	CURLcode res;
	char *image;
	char *tag;

	if(argc != 2){
		fprintf(stderr, "usage: %s docker-vendor/image-name\n", argv[0]);
		exit(1);
	}

	if(strchr(argv[1], '/') != NULL){
		image = argv[1];
	} else {
		image = smprintf("library/%s", argv[1]);
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
	authurl = smprintf("https://auth.docker.io/token?service=registry.docker.io&scope=repository:%s:pull", argv[1]);
	curl_easy_setopt(curl, CURLOPT_URL, authurl);
	//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cnkappend);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &cnk);
	res = curl_easy_perform(curl);
	if(res != CURLE_OK){
		fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
		exit(1);
	}
	free(authurl);

	/* this really belongs in a library.. and we need a JsonRoot type */
	JsonAst *ast;
	int off, len, jsoff, jscap;
	jscap = 0;
	off = 0;
	len = cnk.len;
	jsonparse(NULL, &jscap, jscap, cnk.buf, &off, &len);

	ast = malloc(jscap * sizeof ast[0]);
	jsoff = 0;
	off = 0;
	len = cnk.len;
	jsonparse(ast, &jsoff, jscap, cnk.buf, &off, &len);

	off = jsonfield(ast, 0, cnk.buf, "token");
	if(off == -1){
		fprintf(stderr, "json: no 'token' field\n");
		exit(1);
	}

	char *token = cnk.buf + ast[off].off + 1;
	token[ast[off].len - 2] = '\0';

	struct curl_slist *hdrlist;
	hdrlist = curl_slist_append(NULL, smprintf("Authorization: Bearer %s", token));
	res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrlist);

	char *manifesturl;
	manifesturl = smprintf("https://registry.hub.docker.com/v2/%s/manifests/%s", argv[1], tag);
	curl_easy_setopt(curl, CURLOPT_URL, manifesturl);
	cnk.len = 0;
	res = curl_easy_perform(curl);
	if(res != CURLE_OK){
		fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
		exit(1);
	}
	free(manifesturl);


	// again, this is pretty stupid..
	jscap = 0;
	off = 0;
	len = cnk.len;
	jsonparse(NULL, &jscap, jscap, cnk.buf, &off, &len);

	ast = realloc(ast, jscap * sizeof ast[0]);
	jsoff = 0;
	off = 0;
	len = cnk.len;
	jsonparse(ast, &jsoff, jscap, cnk.buf, &off, &len);


	off = jsonfield(ast, 0, cnk.buf, "fsLayers");
	if(off == -1){
		fprintf(stderr, "json: no 'fsLayers' field\n");
		exit(1);
	}
	if(ast[off].type != '['){
		fprintf(stderr, "json: 'fsLayers' is '%c', not an array\n", ast[off].type);
		exit(1);
	}
	off++;
	while(ast[off].type != ']'){
		char *blobsum;
		int bi;
		bi = jsonfield(ast, off, cnk.buf, "blobSum");
		if(bi == -1){
			fprintf(stderr, "blobSum not found for layer\n");
			exit(1);
		}
		blobsum = cnk.buf + ast[bi].off + 1;
		blobsum[ast[bi].len - 2] = '\0';

		char *bloburl;
		FILE *fp;
		fp = fopen(blobsum, "wb");
		bloburl = smprintf("https://registry.hub.docker.com/v2/%s/blobs/%s", argv[1], blobsum);
		curl_easy_setopt(curl, CURLOPT_URL, bloburl);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		res = curl_easy_perform(curl);
		if(res != CURLE_OK){
			fprintf(stderr, "curl_easy_perform: %s\n", curl_easy_strerror(res));
			exit(1);
		}
		fclose(fp);
		free(bloburl);

		printf("blobsum: %s\n", blobsum);
		off = ast[off].next;
	}

	//write(1, cnk.buf, cnk.len);

	curl_easy_cleanup(curl);
	curl_slist_free_all(hdrlist);

	return 0;
}
