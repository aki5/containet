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
typedef struct JsonAst JsonAst;
typedef struct JsonRoot JsonRoot;

struct JsonRoot {
	struct {
		JsonAst *buf;
		int len;
		int cap;
	} ast;
	struct {
		char *buf;
		int len;
		int cap;
	} str;
};

struct JsonAst {
	int type;	// type of json element (JsonObject, JsonArray, JsonNumber, JsonString, JsonSymbol)
	int next;	// offset of next json element in JsonAst array
	int off;	// offset of token
	int len;	// length of token
};

// decided to make all of these in-band, because they can be.
enum {
	JsonArray = '[',
	JsonObject = '{',
	JsonString = '"',
	JsonNumber = '0',
	JsonSymbol = 'a',
};

//int jsonparse(JsonAst *ast, int *astoff, int jscap, char *buf, int *offp, int *lenp);
void jsonsetname(char *filename);

int jsonwalk(JsonRoot *root, int off, char *name);
int jsonparse(JsonRoot *root, char *buf, int len);

char *jsoncstr(JsonRoot *root, int off);
